#pragma once

//
// NOTE: File Structs
//

#pragma pack(push, 1)

struct file_header
{
    u64 NumAccounts;
    u64 AccountOffset;

    u64 NumHashtags;
    u64 HashtagOffset;

    u64 NumEdges;
    u64 EdgeOffset;

    u64 NumEdgeDates;
    u64 EdgeDateOffset;
    
    u64 StringBufferSize;
    u64 StringOffset;
};

struct file_account
{
    u32 YearCreated;
    u32 NumFollowers;
    
    u32 NumCharsInName;
    u64 NameOffset;

    u32 NumEdges;
    u64 EdgeOffset;
};

struct file_hashtag
{
    u32 NumCharsInName;
    u64 NameOffset;
    // NOTE: We need to know how many edges we have for allocation when visualizing
    u32 NumEdges;
    u64 EdgeOffset;
};

struct file_edge
{
    u32 OtherId;
    u32 Weight;
    u32 NumDates;
    u64 DateOffset;
};

struct file_edge_date
{
    u16 TweetYear;
    u8 TweetMonth;
    u8 TweetDay;
};

#pragma pack(pop)
