#pragma once

#define VALIDATION 1

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include "math\math.h"
#include "memory\memory.h"
#include "string\string.h"

#include "preprocess.h"

//
// NOTE: Hashtags
//

/*

  NOTE:

    We have 2 uses for edges. When visualizing layout, we need a weight for each edge, adn we only want one edge for the whole connection.
    In practice though, there are multiple tweets from one account using the same hastag. So how do we store it?

    We store 1 array for each account + metadata.
    We store 1 array for each hashtag + metadata.

    Accounts and hashtags reference edges, which contain a weight and who is connected to who.

    Each edge references a array of dates at which point this edge was actually used.
  
 */

inline u32 HashtagGetOrCreate(string HashtagName)
{
    u32 Result = 0xFFFFFFFF;
    file_header* FileHeader = &GlobalState.FileHeader;

    std::string StbHashtagName = std::string(HashtagName.Chars, HashtagName.NumChars);;
    
    auto HashtagIterator = GlobalState.HashtagMappings.find(StbHashtagName);
    if (HashtagIterator == GlobalState.HashtagMappings.end())
    {
        // NOTE: Not in our map so create a new entry
        Assert(FileHeader->NumHashtags < MAX_NUM_HASHTAGS);
        Result = FileHeader->NumHashtags;

        file_hashtag* Hashtag = (file_hashtag*)FileHeader->HashtagOffset.Ptr + Result;
        Hashtag->NumCharsInName = (u32)HashtagName.NumChars;
        Hashtag->NameOffset.Ptr = HashtagName.Chars;
        GlobalState.StringBufferSize += Hashtag->NumCharsInName;
        
        GlobalState.HashtagMappings.insert(std::make_pair(StbHashtagName, Result));

        FileHeader->NumHashtags += 1;
    }
    else
    {
        Result = HashtagIterator->second;
    }

    return Result;
}

//
// NOTE: Edges
//

inline preproc_edge* EdgeGetOrCreate(u32 AccountId, u32 HashtagId)
{
    preproc_edge* Result = 0;
    account_edge_data* AccountEdgeData = GlobalState.AccountEdgeData + AccountId;

    auto HashtagIterator = AccountEdgeData->HashtagEdgeMapping.find(HashtagId);
    if (HashtagIterator == AccountEdgeData->HashtagEdgeMapping.end())
    {
        Result = PushStruct(&AccountEdgeData->Arena, preproc_edge);
        Result->FileEdge.OtherId = HashtagId;
        Result->Arena = BlockArenaCreate(&GlobalState.TweetBlockArena);

        AccountEdgeData->HashtagEdgeMapping.insert(std::make_pair(HashtagId, Result));
        AccountEdgeData->NumEdges += 1;
        GlobalState.NumEdges += 1;
    }
    else
    {
        Result = HashtagIterator->second;
    }
    
    return Result;
}

//
// NOTE: String Manipulation
//

inline void StringMovePastComma(string* CurrChar)
{
    b32 InQuotes = false;
    while (CurrChar->NumChars > 0)
    {
        if (CurrChar->Chars[0] == ',' && !InQuotes)
        {
            break;
        }
        
        if (CurrChar->Chars[0] > 127)
        {
            // NOTE: https://stackoverflow.com/questions/4459571/how-to-recognize-if-a-string-contains-unicode-chars#:~:text=Unicode%20is%20explicitly%20defined%20such,includes%20only%20the%20English%20alphabet.
            // TODO: Small hack to skip unicode chars. I'm assuming theyre 16bit
            AdvanceString(CurrChar, 2u);
        }
        else
        {
            if (CurrChar->Chars[0] == '"')
            {
                InQuotes = !InQuotes;
            }
        
            AdvanceString(CurrChar, 1u);
        }
    }

    AdvanceString(CurrChar, 1u);
}

inline string StringGetAndMovePastString(string* CurrChar)
{
    string Result = {};
    Result.Chars = CurrChar->Chars;

    while (CurrChar->NumChars > 0 && CurrChar->Chars[0] != ',')
    {
        if (CurrChar->Chars[0] > 127)
        {
            // NOTE: https://stackoverflow.com/questions/4459571/how-to-recognize-if-a-string-contains-unicode-chars#:~:text=Unicode%20is%20explicitly%20defined%20such,includes%20only%20the%20English%20alphabet.
            // TODO: Small hack to skip unicode chars. I'm assuming theyre 16bit
            //AdvanceString(CurrChar, 2u);
        }
        else
        {
            //AdvanceString(CurrChar, 1u);
        }

        AdvanceString(CurrChar, 1u);
        
        Result.NumChars += 1;
    }

    AdvanceString(CurrChar, 1u);

    return Result;
}

inline string StringGetPastHashtag(string* CurrChar)
{
    string Result = {};
    Result.Chars = CurrChar->Chars;

    while (CurrChar->NumChars > 0 && CurrChar->Chars[0] != ',' && CurrChar->Chars[0] != ']')
    {
        AdvanceString(CurrChar, 1u);
        Result.NumChars += 1;
    }

    if (CurrChar->Chars[0] != ']')
    {
        AdvanceString(CurrChar, 1u);
    }
    
    return Result;
}

int main(int argc, char** argv)
{
    // NOTE: Files for the below come from https://transparency.twitter.com/en/reports/information-operations.html
    // In this case its 2018 IRA dataset
    
    GlobalState.Arena = LinearArenaCreate(MemoryAllocate(MegaBytes(10)), MegaBytes(100));
    GlobalState.FileArena = LinearArenaCreate(MemoryAllocate(GigaBytes(6)), GigaBytes(6));
    file_header* FileHeader = &GlobalState.FileHeader;

    GlobalState.AccountMappings = std::unordered_map<std::string, u32>();
    GlobalState.AccountMappings.reserve(MAX_NUM_ACCOUNTS);

    GlobalState.HashtagMappings = std::unordered_map<std::string, u32>();
    GlobalState.HashtagMappings.reserve(MAX_NUM_HASHTAGS);

    // NOTE: Init file header data
    {
        FileHeader->AccountOffset.Ptr = PushArray(&GlobalState.Arena, file_account, MAX_NUM_ACCOUNTS);
        FileHeader->HashtagOffset.Ptr = PushArray(&GlobalState.Arena, file_hashtag, MAX_NUM_HASHTAGS);
    }
    
    // NOTE: Get account info
    {
        FILE* FileHandle = fopen("ira_users_csv_hashed.csv", "rb");
        u64 FileSize = 0;

        // NOTE: Get file sizes
        fseek(FileHandle, 0L, SEEK_END);
        FileSize = ftell(FileHandle);
        fseek(FileHandle, 0L, SEEK_SET);
        
        char* CsvData = PushArray(&GlobalState.FileArena, char, FileSize);
        fread(CsvData, FileSize, 1, FileHandle);

        fclose(FileHandle);
        
        file_account* CurrAccount = (file_account*)FileHeader->AccountOffset.Ptr;
        string CurrChar = String(CsvData, FileSize);

        // NOTE: Skip titles
        AdvanceCharsToNewline(&CurrChar);
        AdvanceString(&CurrChar, 1);
        
        while (CurrChar.NumChars > 0)
        {
            Assert(FileHeader->NumAccounts < MAX_NUM_ACCOUNTS);
            
            /*
              NOTE: Column Data
                  userid    user_display_name   user_screen_name    user_reported_location  user_profile_description    user_profile_url
                  follower_count    following_count account_creation_date   account_language

             */

            // NOTE: Skip user id
            StringMovePastComma(&CurrChar);
            string AccountName = StringGetAndMovePastString(&CurrChar);

            CurrAccount->NumCharsInName = u32(AccountName.NumChars);
            CurrAccount->NameOffset.Ptr = AccountName.Chars;
            GlobalState.StringBufferSize += CurrAccount->NumCharsInName;

            // TODO: All of this because I dont wanna write a parser for > 64bit hex values D:
            std::string AccountStrName = std::string(AccountName.Chars, AccountName.NumChars);
            GlobalState.AccountMappings.insert(std::make_pair(AccountStrName, FileHeader->NumAccounts));

            // NOTE: Skip to follower count
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);

            ReadUIntAndAdvance(&CurrChar, &CurrAccount->NumFollowers);
            AdvanceString(&CurrChar, 1u);

            // NOTE: Skip to account creation date
            StringMovePastComma(&CurrChar);
            
            ReadUIntAndAdvance(&CurrChar, &CurrAccount->YearCreated);
            AdvanceCharsToNewline(&CurrChar);
            AdvanceString(&CurrChar, 1u);

            CurrAccount += 1;
            FileHeader->NumAccounts += 1;
        }
    }

    // NOTE: Setup account tweet block arenas
    {
        GlobalState.EdgeBlockArena = PlatformBlockArenaCreate(MegaBytes(4), 16);
        GlobalState.TweetBlockArena = PlatformBlockArenaCreate(MegaBytes(4), 1024);
        GlobalState.AccountEdgeData = PushArray(&GlobalState.Arena, account_edge_data, FileHeader->NumAccounts);
        
        for (u32 AccountId = 0; AccountId < FileHeader->NumAccounts; ++AccountId)
        {
            account_edge_data* Account = GlobalState.AccountEdgeData + AccountId;
            Account->Arena = BlockArenaCreate(&GlobalState.EdgeBlockArena, sizeof(file_edge));

            // NOTE: C++ actaully is a mess
            std::unordered_map<u32, preproc_edge*>* Mem = new (&Account->HashtagEdgeMapping) std::unordered_map<u32, preproc_edge*>();
        }
    }

    // NOTE: Get tweet info
    {
        // NOTE: Windows API for this one because its a large file
        HANDLE FileHandle = CreateFileA("ira_tweets_csv_hashed.csv", GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
        if(FileHandle == INVALID_HANDLE_VALUE)
        {
            InvalidCodePath;
        }
    
        LARGE_INTEGER FileSize = {};
        if(!GetFileSizeEx(FileHandle, &FileSize))
        {
            InvalidCodePath;
        }

        char* CsvData = PushArray(&GlobalState.FileArena, char, FileSize.QuadPart);

        // TODO: Some day write sample code that processes files async so I can overlap loading and processing. This is so slow...
        // https://docs.microsoft.com/en-us/previous-versions/ms810613(v=msdn.10)?redirectedfrom=MSDN
        u64 FileSizeLeft = FileSize.QuadPart;
        while (FileSizeLeft > 0)
        {
            u64 ReadRequest = Min(FileSizeLeft, (u64)U32_MAX);
            u32 ReadRequest32 = u32(ReadRequest);
            DWORD BytesRead;
            if(!(ReadFile(FileHandle, CsvData + FileSize.QuadPart - FileSizeLeft, ReadRequest32, &BytesRead, 0) && (ReadRequest32 == BytesRead)))
            {
                InvalidCodePath;
            }

            FileSizeLeft -= ReadRequest;
        }
        
        CloseHandle(FileHandle);

        file_account* CurrAccount = (file_account*)FileHeader->AccountOffset.Ptr;
        string CurrChar = String(CsvData, FileSize.QuadPart);

        // NOTE: Skip titles
        AdvanceCharsToNewline(&CurrChar);
        AdvanceString(&CurrChar, 1);

        u32 DebugCount = 0;
        while (CurrChar.NumChars > 0)
        {
            DebugCount += 1;
            
            /*

              NOTE: Columms
          
              tweetid   userid  user_display_name   user_screen_name    user_reported_location  user_profile_description
              user_profile_url  follower_count  following_count account_creation_date   account_language    tweet_language  tweet_text
              tweet_time    tweet_client_name   in_reply_to_tweetid in_reply_to_userid  quoted_tweet_tweetid    is_retweet  retweet_userid
              retweet_tweetid   latitude    longitude   quote_count reply_count like_count  retweet_count   hashtags    urls
              user_mentions poll_choices
          
            */

            /*

              NOTE: We run into a tweet, we find out if the hashtag has already been created (create one otherwise). We need a mapping
                    between ids for account and hashtag, and where we actually store them hence the unordered maps.
              
             */

            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);
            // NOTE: Accounts here have extra quotes so we remove them
            AdvanceString(&CurrChar, 1u);
            string AccountName = StringGetAndMovePastString(&CurrChar);
            AccountName.NumChars -= 1;

            std::string AccountStrName = std::string(AccountName.Chars, AccountName.NumChars);
            auto AccountIdIterator = GlobalState.AccountMappings.find(AccountStrName);

            if (AccountIdIterator == GlobalState.AccountMappings.end())
            {
                InvalidCodePath;
            }

            u32 AccountId = AccountIdIterator->second;

            // NOTE: Skip to tweet time
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);

            // NOTE: Example date format "2017-06-22 16:03"
            AdvanceString(&CurrChar, 1u);
            u32 Year = 0;
            u32 Month = 0;
            u32 Day = 0;
            ReadUIntAndAdvance(&CurrChar, &Year);
            AdvanceString(&CurrChar, 1u);
            ReadUIntAndAdvance(&CurrChar, &Month);
            AdvanceString(&CurrChar, 1u);
            ReadUIntAndAdvance(&CurrChar, &Day);
            // NOTE: Walk to next end quote
            while (CurrChar.NumChars > 0 && CurrChar.Chars[0] != '"')
            {
                AdvanceString(&CurrChar, 1u);
            }
            // NOTE: Get past quote and comma
            AdvanceString(&CurrChar, 2u);

            // NOTE: Skip to hashtags
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);

            // NOTE: Create a edge for every hashtag used. If there are none, skip for now
            AdvanceString(&CurrChar, 1u);
            if (CurrChar.Chars[0] != '"')
            {
                // NOTE: The format here is [_hashtag1_,_hashtag2_,...]

                // NOTE: Get past square brackets
                AdvanceString(&CurrChar, 1u);

                while (CurrChar.Chars[0] != ']')
                {
                    AdvancePastDeadSpaces(&CurrChar);
                    string Hashtag = StringGetPastHashtag(&CurrChar);
                    AdvancePastDeadSpaces(&CurrChar);

                    if (CurrChar.Chars[0] == ',')
                    {
                        AdvanceString(&CurrChar, 1u);
                    }

                    u32 HashtagId = HashtagGetOrCreate(Hashtag);
                    preproc_edge* Edge = EdgeGetOrCreate(AccountId, HashtagId);
                    Edge->FileEdge.Weight += 1;

                    file_edge_date_info* DateInfo = PushStruct(&Edge->Arena, file_edge_date_info);
                    DateInfo->TweetYear = (u16)Year;
                    DateInfo->TweetMonth = (u8)Month;
                    DateInfo->TweetDay = (u8)Day;
                    GlobalState.NumEdgeDates += 1;
                }
                
                // NOTE: Get past square brackets
                AdvanceString(&CurrChar, 2u);
            }
            
            AdvanceCharsToNewline(&CurrChar);
            AdvanceString(&CurrChar, 1u);
        }
    }
    
    // NOTE: Write all our data to a binary file for rendering/visualizing
    {
        FILE* OutFile = fopen("preprocessed.bin", "wb");

        u64 GlobalFileOffset = sizeof(file_header);

        u64 AccountOffset = GlobalFileOffset;
        GlobalFileOffset += sizeof(file_account) * FileHeader->NumAccounts;

        u64 HashtagOffset = GlobalFileOffset;
        GlobalFileOffset += sizeof(file_hashtag) * FileHeader->NumHashtags;

        u64 EdgeOffset = GlobalFileOffset;
        GlobalFileOffset += sizeof(file_edge) * FileHeader->NumEdges;

        u64 EdgeDateOffset = GlobalFileOffset;
        GlobalFileOffset += sizeof(file_edge_date_info) * GlobalState.NumEdgeDates;

        u64 StringOffset = GlobalFileOffset;
        GlobalFileOffset += sizeof(char) * GlobalState.StringBufferSize;

        // NOTE: Write strings and patch pointers
        {
            u64 SubOffset = StringOffset;
            fseek(OutFile, SubOffset, SEEK_SET);

            for (u32 AccountId = 0; AccountId < FileHeader->NumAccounts; ++AccountId)
            {
                file_account* CurrAccount = (file_account*)FileHeader->AccountOffset.Ptr + AccountId;

                fwrite(CurrAccount->NameOffset.Ptr, CurrAccount->NumCharsInName, sizeof(char), OutFile);
                CurrAccount->NameOffset.Offset = SubOffset;
                SubOffset += CurrAccount->NumCharsInName * sizeof(char);
            }

            for (u32 HashtagId = 0; HashtagId < FileHeader->NumHashtags; ++HashtagId)
            {
                file_hashtag* CurrHashtag = (file_hashtag*)FileHeader->HashtagOffset.Ptr + HashtagId;

                fwrite(CurrHashtag->NameOffset.Ptr, CurrHashtag->NumCharsInName, sizeof(char), OutFile);
                CurrHashtag->NameOffset.Offset = SubOffset;
                SubOffset += CurrHashtag->NumCharsInName * sizeof(char);
            }            
        }

        // NOTE: Write edge dates
        {
            u64 SubOffset = EdgeDateOffset;
            fseek(OutFile, SubOffset, SEEK_SET);

            for (u32 AccountId = 0; AccountId < FileHeader->NumAccounts; ++AccountId)
            {
                account_edge_data* EdgeData = GlobalState.AccountEdgeData + AccountId;

                u32 GlobalEdgeId = 0;
                for (block* CurrBlock = EdgeData->Arena.Next; CurrBlock; CurrBlock = CurrBlock->Next)
                {
                    preproc_edge* EdgeArray = BlockGetData(CurrBlock, preproc_edge);
                    u32 NumEdgesInBlock = Min(EdgeData->NumEdges - GlobalEdgeId, u32(EdgeData->Arena.BlockSpace / sizeof(preproc_edge)));

                    for (u32 EdgeId = 0; EdgeId < NumEdgesInBlock; ++EdgeId)
                    {
                        preproc_edge* CurrEdge = EdgeArray + EdgeId;
                        CurrEdge->FileEdge.DateOffset.Offset = SubOffset;
                    
                        u32 GlobalDateId = 0;
                        for (block* CurrBlock2 = CurrEdge->Arena.Next; CurrBlock; CurrBlock = CurrBlock->Next)
                        {
                            file_edge_date_info* DateArray = BlockGetData(CurrBlock2, file_edge_date_info);
                            u32 NumDatesInBlock = Min(CurrEdge->FileEdge.NumDates - GlobalDateId, u32(CurrEdge->Arena.BlockSpace / sizeof(file_edge_date_info)));

                            fwrite(DateArray, NumDatesInBlock, sizeof(file_edge_date_info), OutFile);
                            SubOffset += NumDatesInBlock * sizeof(file_edge_date_info);
                            GlobalDateId += NumDatesInBlock;
                        }
                    }
                }
            }
        }
        
        // NOTE: Write edges
        {
            fseek(FileHandle, EdgeOffset, SEEK_SET);

            for (u32 AccountId = 0; AccountId < FileHeader->NumAccounts; ++AccountId)
            {
                account_edge_data* EdgeData = GlobalState.AccountEdgeData + AccountId;
                EdgeData->FileEdge.DateOffset.Offset = SubOffset;
            }
        }

        // NOTE: Write account array
        {
        }

        // NOTE: Write hashtag array
        {
        }

        
        
        fclose(OutFile);
    }
    
    return 1;
}
