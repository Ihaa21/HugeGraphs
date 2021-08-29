#pragma once

struct file_arena
{
    u64 Start;
    u64 Size;
    u64 Used;
};

struct edge
{
    file_edge FileEdge;
    block_arena DateArena;
};

struct account
{
    file_account FileAccount;
    string Name;
    
    block_arena EdgeArena;
    std::unordered_map<u32, edge*> HashtagEdgeMapping;
};

struct hashtag
{
    file_hashtag FileHashtag;
    string Name;

    block_arena EdgeArena;
    std::unordered_map<u32, file_edge*> AccountEdgeMapping;
};

#define MAX_NUM_ACCOUNTS 5000
#define MAX_NUM_HASHTAGS 500000
struct global_state
{
    linear_arena FileArena;
    linear_arena Arena;
    platform_block_arena EdgeBlockArena;
    platform_block_arena DateBlockArena;

    file_header FileHeader;

    // NOTE: Maps account name to its id in AccountEdgeData as well as the file ptr
    std::unordered_map<std::string, u32> AccountMappings;
    account* Accounts;

    // NOTE: Maps hashtag name to its id in file ptr
    std::unordered_map<std::string, u32> HashtagMappings;
    hashtag* Hashtags;
};

global global_state GlobalState;
