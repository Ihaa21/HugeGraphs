
layout(set = 0, binding = 0) uniform sort_uniforms
{
    uint ArraySize;
} SortUniforms;

layout(set = 0, binding = 1) coherent buffer sort_array
{
    uint SortArray[];
};

layout(set = 0, binding = 2) buffer atomic_buffer
{
    uint WorkCountId;
    uint DoneCounterId;
} AtomicBuffer;

#define PassType_None 0
#define PassType_LocalFd 1
#define PassType_GlobalFlip 2
#define PassType_LocalDisperse 3
#define PassType_GlobalDisperse 4

struct pass_params
{
    uint PassType;
    uint FlipSize;
    uint PassId;
    uint N;
};

layout(set = 0, binding = 3) buffer pass_params_buffer
{
    pass_params PassParams[];
};
