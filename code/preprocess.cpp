#pragma once

#define VALIDATION 1

#define _CRT_SECURE_NO_WARNINGS

// TODO: Hacky rn
#undef internal
#undef global
#undef local_global

#include <string>
#include <unordered_map>
#include <windows.h>

#define internal static
#define global static
#define local_global static

#include "math\math.h"
#include "memory\memory.h"
#include "string\string.h"
#include "file_headers.h"

#include "preprocess.h"

//
// NOTE: File Arena
//

inline file_arena FileArenaCreate(u64 Offset, u64 Size)
{
    file_arena Result = {};
    Result.Start = Offset;
    Result.Size = Size;
    Result.Used = 0;

    return Result;
}

#define FileArenaPushStruct(Arena, Type) FileArenaPushSize(Arena, sizeof(Type))
#define FileArenaPushArray(Arena, Type, Count) FileArenaPushSize(Arena, sizeof(Type)*(Count))
inline u64 FileArenaPushSize(file_arena* Arena, u64 Size)
{
    Assert((Arena->Used + Size) <= Arena->Size);
    
    u64 Result = Arena->Start + Arena->Used;
    Arena->Used += Size;

    return Result;
}

inline file_arena FileSubArena(file_arena* Parent, u64 Size)
{
    file_arena Result = {};
    Result.Start = FileArenaPushSize(Parent, Size);
    Result.Size = Size;
    Result.Used = 0;

    return Result;
}

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
        Result = (u32)FileHeader->NumHashtags;
        
        hashtag* Hashtag = GlobalState.Hashtags + Result;
        Hashtag->Name = HashtagName;
        FileHeader->StringBufferSize += HashtagName.NumChars * sizeof(char);
        
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

inline edge* EdgeGetOrCreate(u32 AccountId, u32 HashtagId)
{
    Assert(AccountId < GlobalState.FileHeader.NumAccounts);
    Assert(HashtagId < GlobalState.FileHeader.NumHashtags);
    
    edge* Result = 0;
    file_header* FileHeader = &GlobalState.FileHeader;
    account* Account = GlobalState.Accounts + AccountId;

    if (AccountId == 2000 && HashtagId == 38840)
    {
        int i = 0;
    }
    
    auto HashtagIterator = Account->HashtagEdgeMapping.find(HashtagId);
    if (HashtagIterator == Account->HashtagEdgeMapping.end())
    {
        // NOTE: Create a edge for the account
        {
            Result = PushStruct(&Account->EdgeArena, edge);
            Result->FileEdge.OtherId = HashtagId;
            Result->DateArena = BlockArenaCreate(&GlobalState.DateBlockArena);

            Account->HashtagEdgeMapping.insert(std::make_pair(HashtagId, Result));
            Account->FileAccount.NumEdges += 1;
            FileHeader->NumEdges += 1;
        }
        
        // NOTE: Create a edge for the hashtag
        {
            hashtag* Hashtag = GlobalState.Hashtags + HashtagId;
            file_edge* HashtagEdge = PushStruct(&Hashtag->EdgeArena, file_edge);
            *HashtagEdge = {};
            HashtagEdge->OtherId = AccountId;
            HashtagEdge->Weight = 1;

            Hashtag->AccountEdgeMapping.insert(std::make_pair(AccountId, HashtagEdge));
            Hashtag->FileHashtag.NumEdges += 1;
        }
    }
    else
    {
        Result = HashtagIterator->second;

        // NOTE: Update weight on hashtag edge
        {
            hashtag* Hashtag = GlobalState.Hashtags + HashtagId;

            auto EdgeIterator = Hashtag->AccountEdgeMapping.find(AccountId);
            if (HashtagIterator == Account->HashtagEdgeMapping.end())
            {
                InvalidCodePath;
            }
            else
            {
                EdgeIterator->second->Weight += 1;
            }
        }
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
    GlobalState.Arena = LinearArenaCreate(MemoryAllocate(MegaBytes(100)), MegaBytes(100));
    GlobalState.FileArena = LinearArenaCreate(MemoryAllocate(GigaBytes(6)), GigaBytes(6));
    file_header* FileHeader = &GlobalState.FileHeader;

    GlobalState.EdgeBlockArena = PlatformBlockArenaCreate(MegaBytes(4), 16);
    GlobalState.DateBlockArena = PlatformBlockArenaCreate(MegaBytes(4), 1024);

    GlobalState.AccountMappings = std::unordered_map<std::string, u32>();
    GlobalState.AccountMappings.reserve(MAX_NUM_ACCOUNTS);

    GlobalState.HashtagMappings = std::unordered_map<std::string, u32>();
    GlobalState.HashtagMappings.reserve(MAX_NUM_HASHTAGS);

    GlobalState.Accounts = PushArray(&GlobalState.Arena, account, MAX_NUM_ACCOUNTS);
    for (u32 AccountId = 0; AccountId < MAX_NUM_ACCOUNTS; ++AccountId)
    {
        account* Account = GlobalState.Accounts + AccountId;
        // NOTE: Can't zero mem with C++ classes, another mess
        Account->FileAccount = {};
        Account->Name = {};
        Account->EdgeArena = BlockArenaCreate(&GlobalState.EdgeBlockArena, sizeof(edge));

        // NOTE: C++ actaully is a mess
        std::unordered_map<u32, edge*>* Mem = new (&Account->HashtagEdgeMapping) std::unordered_map<u32, edge*>();
    }

    GlobalState.Hashtags = PushArray(&GlobalState.Arena, hashtag, MAX_NUM_HASHTAGS);
    for (u32 HashtagId = 0; HashtagId < MAX_NUM_HASHTAGS; ++HashtagId)
    {
        hashtag* Hashtag = GlobalState.Hashtags + HashtagId;
        // NOTE: Can't zero mem with C++ classes, another mess
        Hashtag->FileHashtag = {};
        Hashtag->Name = {};
        Hashtag->EdgeArena = BlockArenaCreate(&GlobalState.EdgeBlockArena, sizeof(file_edge));

        // NOTE: C++ actaully is a mess
        std::unordered_map<u32, file_edge*>* Mem = new (&Hashtag->AccountEdgeMapping) std::unordered_map<u32, file_edge*>();
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
        
        account* CurrAccount = GlobalState.Accounts;
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
            CurrAccount->Name = StringGetAndMovePastString(&CurrChar);
            FileHeader->StringBufferSize += CurrAccount->Name.NumChars;

            // TODO: All of this because I dont wanna write a parser for > 64bit hex values D:
            std::string AccountStrName = std::string(CurrAccount->Name.Chars, CurrAccount->Name.NumChars);
            GlobalState.AccountMappings.insert(std::make_pair(AccountStrName, (u32)FileHeader->NumAccounts));

            // NOTE: Skip to follower count
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);
            StringMovePastComma(&CurrChar);

            ReadUIntAndAdvance(&CurrChar, &CurrAccount->FileAccount.NumFollowers);
            AdvanceString(&CurrChar, 1u);

            // NOTE: Skip to account creation date
            StringMovePastComma(&CurrChar);
            
            ReadUIntAndAdvance(&CurrChar, &CurrAccount->FileAccount.YearCreated);
            AdvanceCharsToNewline(&CurrChar);
            AdvanceString(&CurrChar, 1u);

            CurrAccount += 1;
            FileHeader->NumAccounts += 1;
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
                    edge* Edge = EdgeGetOrCreate(AccountId, HashtagId);
                    Edge->FileEdge.Weight += 1;

                    file_edge_date* DateInfo = PushStruct(&Edge->DateArena, file_edge_date);
                    
                    DateInfo->TweetYear = (u16)Year;
                    DateInfo->TweetMonth = (u8)Month;
                    DateInfo->TweetDay = (u8)Day;
                    Edge->FileEdge.NumDates += 1;
                    FileHeader->NumEdgeDates += 1;
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

        u64 FileTotalSize = (sizeof(file_header) +
                             sizeof(file_account) * FileHeader->NumAccounts +
                             sizeof(file_hashtag) * FileHeader->NumHashtags +
                             sizeof(file_edge) * 2 * FileHeader->NumEdges +
                             sizeof(file_edge_date) * FileHeader->NumEdgeDates +
                             sizeof(char) * FileHeader->StringBufferSize);
        file_arena FileArena = FileArenaCreate(0, FileTotalSize);
        FileArenaPushStruct(&FileArena, file_header);
        
        FileHeader->AccountOffset = FileArenaPushArray(&FileArena, file_account, FileHeader->NumAccounts);
        FileHeader->HashtagOffset = FileArenaPushArray(&FileArena, file_hashtag, FileHeader->NumHashtags);

        // NOTE: Count for accoutns and hashtag edges
        file_arena EdgeArena = FileSubArena(&FileArena, sizeof(file_edge) * 2 * FileHeader->NumEdges);
        FileHeader->EdgeOffset = EdgeArena.Start;
        file_arena EdgeDateArena = FileSubArena(&FileArena, sizeof(file_edge_date) * FileHeader->NumEdgeDates);
        FileHeader->EdgeDateOffset = EdgeDateArena.Start;
        file_arena StringArena = FileSubArena(&FileArena, sizeof(char) * FileHeader->StringBufferSize);
        FileHeader->StringOffset = StringArena.Start;

        // NOTE: Update account file data/pointers
        for (u32 AccountId = 0; AccountId < FileHeader->NumAccounts; ++AccountId)
        {
            account* Account = GlobalState.Accounts + AccountId;

            // NOTE: Allocate name
            Account->FileAccount.NameOffset = FileArenaPushArray(&StringArena, char, Account->Name.NumChars);

            // NOTE: Allocate edges
            Account->FileAccount.EdgeOffset = FileArenaPushArray(&EdgeArena, file_edge, Account->FileAccount.NumEdges);
                
            u32 GlobalEdgeId = 0;
            for (block* CurrBlock = Account->EdgeArena.Next; CurrBlock; CurrBlock = CurrBlock->Next)
            {
                edge* EdgeArray = BlockGetData(CurrBlock, edge);
                u32 NumEdgesInBlock = Min(Account->FileAccount.NumEdges - GlobalEdgeId, u32(Account->EdgeArena.BlockSpace / sizeof(edge)));

                for (u32 EdgeId = 0; EdgeId < NumEdgesInBlock; ++EdgeId)
                {
                    edge* CurrEdge = EdgeArray + EdgeId;

                    // NOTE: Allocate dates
                    CurrEdge->FileEdge.DateOffset = FileArenaPushArray(&EdgeDateArena, file_edge_date, CurrEdge->FileEdge.NumDates);
                }
            }
        }

        // NOTE: Update hashtag file data/pointers
        for (u32 HashtagId = 0; HashtagId < FileHeader->NumHashtags; ++HashtagId)
        {
            hashtag* Hashtag = GlobalState.Hashtags + HashtagId;

            // NOTE: Allocate name
            Hashtag->FileHashtag.NameOffset = FileArenaPushArray(&StringArena, char, Hashtag->Name.NumChars);

            // NOTE: Allocate edges (they only are there for weight and whos connected)
            Hashtag->FileHashtag.EdgeOffset = FileArenaPushArray(&EdgeArena, file_edge, Hashtag->FileHashtag.NumEdges);
        }

        Assert(FileArena.Used == FileArena.Size);
        Assert(EdgeArena.Used == EdgeArena.Size);
        Assert(EdgeDateArena.Used == EdgeDateArena.Size);
        Assert(StringArena.Used == StringArena.Size);

        // NOTE: Write file header
        fwrite(FileHeader, sizeof(file_header), 1, OutFile);

        // NOTE: Write account array
        for (u32 AccountId = 0; AccountId < FileHeader->NumAccounts; ++AccountId)
        {
            account* Account = GlobalState.Accounts + AccountId;
            fwrite(&Account->FileAccount, sizeof(file_account), 1, OutFile);
        }

        // NOTE: Write hashtag array
        for (u32 HashtagId = 0; HashtagId < FileHeader->NumHashtags; ++HashtagId)
        {
            hashtag* Hashtag = GlobalState.Hashtags + HashtagId;
            fwrite(&Hashtag->FileHashtag, sizeof(file_hashtag), 1, OutFile);
        }
        
        // NOTE: Write edges
        {
            for (u32 AccountId = 0; AccountId < FileHeader->NumAccounts; ++AccountId)
            {
                account* Account = GlobalState.Accounts + AccountId;

                u32 GlobalEdgeId = 0;
                for (block* CurrBlock = Account->EdgeArena.Next; CurrBlock; CurrBlock = CurrBlock->Next)
                {
                    edge* EdgeArray = BlockGetData(CurrBlock, edge);
                    u32 NumEdgesInBlock = Min(Account->FileAccount.NumEdges - GlobalEdgeId, u32(Account->EdgeArena.BlockSpace / sizeof(edge)));

                    for (u32 EdgeId = 0; EdgeId < NumEdgesInBlock; ++EdgeId)
                    {
                        edge* CurrEdge = EdgeArray + EdgeId;

                        fwrite(&CurrEdge->FileEdge, sizeof(file_edge), 1, OutFile);
                    }
                }
            }

            for (u32 HashtagId = 0; HashtagId < FileHeader->NumHashtags; ++HashtagId)
            {
                hashtag* Hashtag = GlobalState.Hashtags + HashtagId;

                u32 GlobalEdgeId = 0;
                for (block* CurrBlock = Hashtag->EdgeArena.Next; CurrBlock; CurrBlock = CurrBlock->Next)
                {
                    file_edge* EdgeArray = BlockGetData(CurrBlock, file_edge);
                    u32 NumEdgesInBlock = Min(Hashtag->FileHashtag.NumEdges - GlobalEdgeId, u32(Hashtag->EdgeArena.BlockSpace / sizeof(edge)));
                    fwrite(EdgeArray, sizeof(file_edge) * NumEdgesInBlock, 1, OutFile);
                }
            }
        }

        // NOTE: Write edge dates
        for (u32 AccountId = 0; AccountId < FileHeader->NumAccounts; ++AccountId)
        {
            account* Account = GlobalState.Accounts + AccountId;

            u32 GlobalEdgeId = 0;
            for (block* CurrBlock = Account->EdgeArena.Next; CurrBlock; CurrBlock = CurrBlock->Next)
            {
                edge* EdgeArray = BlockGetData(CurrBlock, edge);
                u32 NumEdgesInBlock = Min(Account->FileAccount.NumEdges - GlobalEdgeId, u32(Account->EdgeArena.BlockSpace / sizeof(edge)));

                for (u32 EdgeId = 0; EdgeId < NumEdgesInBlock; ++EdgeId)
                {
                    edge* CurrEdge = EdgeArray + EdgeId;
                    
                    u32 GlobalDateId = 0;
                    for (block* CurrBlock2 = CurrEdge->DateArena.Next; CurrBlock2; CurrBlock2 = CurrBlock2->Next)
                    {
                        file_edge_date* DateArray = BlockGetData(CurrBlock2, file_edge_date);
                        u32 NumDatesInBlock = Min(CurrEdge->FileEdge.NumDates - GlobalDateId, u32(CurrEdge->DateArena.BlockSpace / sizeof(file_edge_date)));

                        fwrite(DateArray, NumDatesInBlock, sizeof(file_edge_date), OutFile);
                        GlobalDateId += NumDatesInBlock;
                    }
                }
            }
        }

        // NOTE: Write strings
        {
            for (u32 AccountId = 0; AccountId < FileHeader->NumAccounts; ++AccountId)
            {
                account* Account = GlobalState.Accounts + AccountId;
                fwrite(&Account->Name.Chars, sizeof(char) * Account->Name.NumChars, 1, OutFile);
            }

            for (u32 HashtagId = 0; HashtagId < FileHeader->NumHashtags; ++HashtagId)
            {
                hashtag* Hashtag = GlobalState.Hashtags + HashtagId;
                fwrite(&Hashtag->Name.Chars, sizeof(char) * Hashtag->Name.NumChars, 1, OutFile);
            }
        }
        
        fclose(OutFile);
    }
    
    return 1;
}
