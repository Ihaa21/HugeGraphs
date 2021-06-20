#pragma once

//
// NOTE: File Structs
//

#pragma pack(push, 1)

union file_ptr_union
{
    u64 Offset;
    void* Ptr;
    char* CharPtr;
};

struct file_header
{
    u32 NumAccounts;
    file_ptr_union AccountOffset;

    u32 NumHashtags;
    file_ptr_union HashtagOffset;

    u32 NumEdges;
    file_ptr_union EdgeOffset;

    file_ptr_union EdgeDateOffset;
    
    file_ptr_union StringOffset;
};

struct file_account
{
    u32 YearCreated;
    u32 NumFollowers;
    
    u32 NumCharsInName;
    file_ptr_union NameOffset;
};

struct file_hashtag
{
    u32 NumCharsInName;
    file_ptr_union NameOffset;
    // NOTE: We need to know how many edges we have for allocation when visualizing
    u32 NumEdges;
};

struct file_edge
{
    u32 OtherId;
    u32 Weight;
    u32 NumDates;
    file_ptr_union DateOffset;
};

struct file_edge_date_info
{
    u16 TweetYear;
    u8 TweetMonth;
    u8 TweetDay;
};

#pragma pack(pop)

struct preproc_edge
{
    file_edge FileEdge;
    block_arena Arena;
};

#include <unordered_map>

struct account_edge_data
{
    u32 NumEdges;
    block_arena Arena;
    std::unordered_map<u32, preproc_edge*> HashtagEdgeMapping;
};

#define MAX_NUM_ACCOUNTS 5000
#define MAX_NUM_HASHTAGS 500000
struct global_state
{
    linear_arena FileArena;
    linear_arena Arena;
    platform_block_arena EdgeBlockArena;
    platform_block_arena TweetBlockArena;

    u64 NumEdges;
    u64 StringBufferSize;
    u64 NumEdgeDates;
    file_header FileHeader;

    // NOTE: Maps account name to its id in AccountEdgeData as well as the file ptr
    std::unordered_map<std::string, u32> AccountMappings;
    account_edge_data* AccountEdgeData;

    // NOTE: Maps hashtag name to its id in file ptr
    std::unordered_map<std::string, u32> HashtagMappings;
};

global global_state GlobalState;
