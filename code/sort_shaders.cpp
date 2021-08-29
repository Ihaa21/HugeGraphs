#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_KHR_shader_subgroup_arithmetic : enable

/*
  NOTE: Implementation based on https://poniesandlight.co.uk/reflect/bitonic_merge_sort/
 */

#include "graph_shaders.h"
#include "radixtree_shaders.h"

RADIX_DESCRIPTOR_LAYOUT(0)
GRAPH_DESCRIPTOR_LAYOUT(1)

layout(set = 2, binding = 0) uniform sort_uniforms
{
    uint ArraySize;
    uint FlipSize;
    uint PassId;
    uint N;
} SortUniforms;

void GlobalCompareAndSwap(uint Index1, uint Index2)
{
    if (Index1 < SortUniforms.ArraySize && Index2 < SortUniforms.ArraySize)
    {
        uint MortonKey0 = MortonKeys[Index1];
        uint MortonKey1 = MortonKeys[Index2];
        if (MortonKey0 > MortonKey1)
        {
            MortonKeys[Index1] = MortonKey1;
            MortonKeys[Index2] = MortonKey0;

            uint Temp = ElementReMapping[Index1];
            ElementReMapping[Index1] = ElementReMapping[Index2];
            ElementReMapping[Index2] = Temp;
        }
    }
}

//=========================================================================================================================================
// NOTE: Bitonic Merge Sort Global Flip
//=========================================================================================================================================

#if BITONIC_GLOBAL_FLIP

layout(local_size_x = 1024, local_size_y = 1, local_size_z = 1) in;
void main()
{
    uint ThreadId = uint(gl_GlobalInvocationID.x);

    if (ThreadId < SortUniforms.ArraySize / 2)
    {
        uint DoubleFlip = 2*SortUniforms.FlipSize;
        uint LowerHeightBits = SortUniforms.FlipSize - 1;
        uint FlipId1 = DoubleFlip * (ThreadId >> SortUniforms.PassId) + (ThreadId & LowerHeightBits);
        uint FlipId2 = FlipId1 + DoubleFlip - 2 * (ThreadId & LowerHeightBits) - 1;

        /*
        SortArray[2*ThreadId + 0] = FlipId1;
        SortArray[2*ThreadId + 1] = FlipId2;
        */
        
        GlobalCompareAndSwap(FlipId1, FlipId2);
    }
}

#endif

//=========================================================================================================================================
// NOTE: Bitonic Merge Sort Local Disperse
//=========================================================================================================================================

#if BITONIC_LOCAL_DISPERSE

shared uint SharedArrayValues[2048];
shared uint SharedIndexValues[2048];

void SharedCheckAndSwapValues(uint Id0, uint Id1)
{
    if (Id0 < SortUniforms.ArraySize && Id1 < SortUniforms.ArraySize)
    {
        uint ArrayVal0 = SharedArrayValues[Id0];
        uint ArrayVal1 = SharedArrayValues[Id1];
        if (ArrayVal0 > ArrayVal1)
        {
            SharedArrayValues[Id0] = ArrayVal1;
            SharedArrayValues[Id1] = ArrayVal0;

            uint Temp = SharedIndexValues[Id0];
            SharedIndexValues[Id0] = SharedIndexValues[Id1];
            SharedIndexValues[Id1] = Temp;
        }
    }
}

layout(local_size_x = 1024, local_size_y = 1, local_size_z = 1) in;
void main()
{
    uint GlobalThreadId = uint(gl_GlobalInvocationID.x);

    if (GlobalThreadId < SortUniforms.ArraySize / 2)
    {
        // TODO: We can make this quicker probably with better LDS indexing to avoid bank conflicts
        uint LocalThreadId = uint(gl_LocalInvocationIndex);
        SharedArrayValues[2*LocalThreadId + 0] = MortonKeys[2*GlobalThreadId + 0];
        SharedArrayValues[2*LocalThreadId + 1] = MortonKeys[2*GlobalThreadId + 1];

        SharedIndexValues[2*LocalThreadId + 0] = ElementReMapping[2*GlobalThreadId + 0];
        SharedIndexValues[2*LocalThreadId + 1] = ElementReMapping[2*GlobalThreadId + 1];

        barrier();

        // NOTE: Do Disperse
        for (uint N = 1024, NPassId = 10; N > 0; NPassId -= 1, N = N / 2)
        {
            uint DoubleHeight = N * 2;
            uint LowerBits = N - 1;
            uint DisperseId1 = DoubleHeight * ((LocalThreadId & (~LowerBits)) >> NPassId) + (LocalThreadId & LowerBits);
            uint DisperseId2 = DisperseId1 + N;

            /*
            SharedArrayValues[2*LocalThreadId + 0] = DisperseId1;
            SharedArrayValues[2*LocalThreadId + 1] = DisperseId2;
            */

            SharedCheckAndSwapValues(DisperseId1, DisperseId2);
            barrier();
        }
        
        // TODO: We can make this quicker probably with better LDS indexing to avoid bank conflicts
        MortonKeys[2*GlobalThreadId + 0] = SharedArrayValues[2*LocalThreadId + 0];
        MortonKeys[2*GlobalThreadId + 1] = SharedArrayValues[2*LocalThreadId + 1];

        ElementReMapping[2*GlobalThreadId + 0] = SharedIndexValues[2*LocalThreadId + 0];
        ElementReMapping[2*GlobalThreadId + 1] = SharedIndexValues[2*LocalThreadId + 1];
    }
}

#endif

//=========================================================================================================================================
// NOTE: Bitonic Merge Sort Global Disperse
//=========================================================================================================================================

#if BITONIC_GLOBAL_DISPERSE

layout(local_size_x = 1024, local_size_y = 1, local_size_z = 1) in;
void main()
{
    uint ThreadId = uint(gl_GlobalInvocationID.x);

    if (ThreadId < SortUniforms.ArraySize / 2)
    {
        uint DoubleHeight = SortUniforms.N * 2;
        uint LowerBits = SortUniforms.N - 1;
        uint DisperseId1 = DoubleHeight * ((ThreadId & (~LowerBits)) >> SortUniforms.PassId) + (ThreadId & LowerBits);
        uint DisperseId2 = DisperseId1 + SortUniforms.N;

        GlobalCompareAndSwap(DisperseId1, DisperseId2);
    }
}

#endif

//=========================================================================================================================================
// NOTE: Bitonic Merge Sort Local Flip Disperse
//=========================================================================================================================================

#if BITONIC_LOCAL_FD

uint NextPow2(uint Val)
{
    // NOTE: https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
    uint Result = Val;
    Result--;
    Result |= Result >> 1;
    Result |= Result >> 2;
    Result |= Result >> 4;
    Result |= Result >> 8;
    Result |= Result >> 16;
    Result++;

    return Result / 2;
}

uint Morton2dExpandBits(uint Value)
{
    // NOTE: https://stackoverflow.com/questions/30539347/2d-morton-code-encode-decode-64bits
    // NOTE: Expands a 16-bit integer into 32 bits by inserting 1 zeros after each bit.
    uint Result = (Value | (Value << 16)) & 0x0000FFFF;
    Result = (Result | (Result << 8)) & 0x00FF00FF;
    Result = (Result | (Result << 4)) & 0x0F0F0F0F;
    Result = (Result | (Result << 2)) & 0x33333333;
    Result = (Result | (Result << 1)) & 0x55555555;
    return Result;
}

uint Morton2d(vec2 Pos)
{
    vec2 NormalizedPos = (Pos - ElementBounds.Min) / (ElementBounds.Max - ElementBounds.Min);

    // NOTE: Each axis gets 16 bits so we convert x/y into fixed point
    float BitRange = pow(2, 16);
    vec2 FixedPointPos = min(max(NormalizedPos * BitRange, 0.0f), BitRange - 1);

    uint Result = 2 * Morton2dExpandBits(uint(FixedPointPos.x)) + Morton2dExpandBits(uint(FixedPointPos.y));
    return Result;
}

shared uint SharedArrayValues[2048];
shared uint SharedIndexValues[2048];

void SharedCheckAndSwapValues(uint Id0, uint Id1)
{
    if (Id0 < SortUniforms.ArraySize && Id1 < SortUniforms.ArraySize)
    {
        uint ArrayVal0 = SharedArrayValues[Id0];
        uint ArrayVal1 = SharedArrayValues[Id1];
        if (ArrayVal0 > ArrayVal1)
        {
            SharedArrayValues[Id0] = ArrayVal1;
            SharedArrayValues[Id1] = ArrayVal0;

            uint Temp = SharedIndexValues[Id0];
            SharedIndexValues[Id0] = SharedIndexValues[Id1];
            SharedIndexValues[Id1] = Temp;
        }
    }
}

layout(local_size_x = 1024, local_size_y = 1, local_size_z = 1) in;
void main()
{
    uint GlobalThreadId = uint(gl_GlobalInvocationID.x);

    if (GlobalThreadId < SortUniforms.ArraySize / 2)
    {
        // TODO: We can make this quicker probably with better LDS indexing to avoid bank conflicts
        uint LocalThreadId = uint(gl_LocalInvocationIndex);
        SharedArrayValues[2*LocalThreadId + 0] = Morton2d(NodePositionArray[2*GlobalThreadId + 0]);
        SharedArrayValues[2*LocalThreadId + 1] = Morton2d(NodePositionArray[2*GlobalThreadId + 1]);

        SharedIndexValues[2*LocalThreadId + 0] = 2*GlobalThreadId + 0;
        SharedIndexValues[2*LocalThreadId + 1] = 2*GlobalThreadId + 1;

        uint MinArraySize = min(NextPow2(SortUniforms.ArraySize), 2048);
        
        for (uint FlipSize = 1, PassId = 0; FlipSize < MinArraySize; PassId += 1, FlipSize *= 2)
        {
            barrier();

            // NOTE: Do Flip
            {
                uint LowerHeightBits = FlipSize - 1;
                uint FlipId1 = 2*FlipSize * (LocalThreadId >> PassId) + (LocalThreadId & LowerHeightBits);
                uint FlipId2 = FlipId1 + 2*FlipSize - 2 * (LocalThreadId & LowerHeightBits) - 1;

                SharedCheckAndSwapValues(FlipId1, FlipId2);
            }
        
            barrier();

            // NOTE: Do Disperse
            for (uint N = FlipSize / 2, NPassId = PassId - 1; N > 0; NPassId -= 1, N = N / 2)
            {
                uint DoubleHeight = N * 2;
                uint LowerBits = N - 1;
                uint DisperseId1 = DoubleHeight * ((LocalThreadId & (~LowerBits)) >> NPassId) + (LocalThreadId & LowerBits);
                uint DisperseId2 = DisperseId1 + N;

                SharedCheckAndSwapValues(DisperseId1, DisperseId2);
                barrier();
            }
        }

        // TODO: We can make this quicker probably with better LDS indexing to avoid bank conflicts
        MortonKeys[2*GlobalThreadId + 0] = SharedArrayValues[2*LocalThreadId + 0];
        MortonKeys[2*GlobalThreadId + 1] = SharedArrayValues[2*LocalThreadId + 1];

        ElementReMapping[2*GlobalThreadId + 0] = SharedIndexValues[2*LocalThreadId + 0];
        ElementReMapping[2*GlobalThreadId + 1] = SharedIndexValues[2*LocalThreadId + 1];
    }
}

#endif
