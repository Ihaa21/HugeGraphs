
#define FFX_HLSL
#define kRS_ValueCopy 1
#include "FFX_ParallelSort.h"

struct RootConstantData {
    uint CShiftBit;
};

[[vk::push_constant]] RootConstantData rootConstData;                                               // Store the shift bit directly in the root signature

[[vk::binding(0, 0)]] ConstantBuffer<FFX_ParallelSortCB>    CBuffer     : register(b0);                 // Constant buffer

[[vk::binding(0, 1)]] RWStructuredBuffer<uint>  SrcBuffer       : register(u0, space0);                 // The unsorted keys or scan data
[[vk::binding(1, 1)]] RWStructuredBuffer<uint>  DstBuffer       : register(u0, space1);                 // The sorted keys or prefixed data
[[vk::binding(2, 1)]] RWStructuredBuffer<uint>  SrcPayload      : register(u0, space2);                 // The payload data
[[vk::binding(3, 1)]] RWStructuredBuffer<uint>  DstPayload      : register(u0, space3);                 // the sorted payload data

[[vk::binding(0, 2)]] RWStructuredBuffer<uint>  ScanSrc         : register(u0, space4);                 // Source for Scan Data
[[vk::binding(1, 2)]] RWStructuredBuffer<uint>  ScanDst         : register(u0, space5);                 // Destination for Scan Data
[[vk::binding(2, 2)]] RWStructuredBuffer<uint>  ScanScratch     : register(u0, space6);                 // Scratch data for Scan

[[vk::binding(0, 3)]] RWStructuredBuffer<uint>  SumTable        : register(u0, space7);                 // The sum table we will write sums to
[[vk::binding(1, 3)]] RWStructuredBuffer<uint>  ReduceTable     : register(u0, space8);                 // The reduced sum table we will write sums to

//=========================================================================================================================================
// NOTE: Parallel Sort Count
//=========================================================================================================================================

#if PARALLEL_SORT_COUNT

[numthreads(FFX_PARALLELSORT_THREADGROUP_SIZE, 1, 1)]
void main(uint localID : SV_GroupThreadID, uint2 InGroupID : SV_GroupID)
{
    uint32_t CalculatedGroupId = InGroupID.y * 64 + InGroupID.x;
    if (CalculatedGroupId >= CBuffer.NumThreadGroups)
    {
        return;
    }
    
    // Call the uint version of the count part of the algorithm
    FFX_ParallelSort_Count_uint(localID, CalculatedGroupId, CBuffer, rootConstData.CShiftBit, SrcBuffer, SumTable);
}

#endif

//=========================================================================================================================================
// NOTE: Parallel Sort Reduce
//=========================================================================================================================================

#if PARALLEL_SORT_REDUCE

[numthreads(FFX_PARALLELSORT_THREADGROUP_SIZE, 1, 1)]
void main(uint localID : SV_GroupThreadID, uint2 InGroupID : SV_GroupID)
{
    uint32_t CalculatedGroupId = InGroupID.y * 64 + InGroupID.x;
    if (CalculatedGroupId >= CBuffer.NumScanValues)
    {
        return;
    }
    
    // Call the reduce part of the algorithm
    FFX_ParallelSort_ReduceCount(localID, CalculatedGroupId, CBuffer,  SumTable, ReduceTable);
}

#endif

//=========================================================================================================================================
// NOTE: Parallel Sort Scan
//=========================================================================================================================================

#if PARALLEL_SORT_SCAN

[numthreads(FFX_PARALLELSORT_THREADGROUP_SIZE, 1, 1)]
void main(uint localID : SV_GroupThreadID, uint2 InGroupID : SV_GroupID)
{
    uint32_t CalculatedGroupId = InGroupID.y * 64 + InGroupID.x;
    uint BaseIndex = FFX_PARALLELSORT_ELEMENTS_PER_THREAD * FFX_PARALLELSORT_THREADGROUP_SIZE * CalculatedGroupId;
    FFX_ParallelSort_ScanPrefix(CBuffer.NumScanValues, localID, CalculatedGroupId, 0, BaseIndex, false,
                                CBuffer, ScanSrc, ScanDst, ScanScratch);
}

#endif

//=========================================================================================================================================
// NOTE: Parallel Sort Scan Add
//=========================================================================================================================================

#if PARALLEL_SORT_SCAN_ADD

[numthreads(FFX_PARALLELSORT_THREADGROUP_SIZE, 1, 1)]
void main(uint localID : SV_GroupThreadID, uint2 InGroupID : SV_GroupID)
{
    uint32_t CalculatedGroupId = InGroupID.y * 64 + InGroupID.x;
    if (CalculatedGroupId >= CBuffer.NumScanValues)
    {
        return;
    }

    // When doing adds, we need to access data differently because reduce 
    // has a more specialized access pattern to match optimized count
    // Access needs to be done similarly to reduce
    // Figure out what bin data we are reducing
    uint BinID = CalculatedGroupId / CBuffer.NumReduceThreadgroupPerBin;
    uint BinOffset = BinID * CBuffer.NumThreadGroups;

    // Get the base index for this thread group
    uint BaseIndex = (CalculatedGroupId % CBuffer.NumReduceThreadgroupPerBin) * FFX_PARALLELSORT_ELEMENTS_PER_THREAD * FFX_PARALLELSORT_THREADGROUP_SIZE;

    FFX_ParallelSort_ScanPrefix(CBuffer.NumThreadGroups, localID, CalculatedGroupId, BinOffset, BaseIndex, true,
                                CBuffer, ScanSrc, ScanDst, ScanScratch);
}

#endif

//=========================================================================================================================================
// NOTE: Parallel Sort Scatter
//=========================================================================================================================================

#if PARALLEL_SORT_SCATTER

[numthreads(FFX_PARALLELSORT_THREADGROUP_SIZE, 1, 1)]
void main(uint localID : SV_GroupThreadID, uint2 InGroupID : SV_GroupID)
{
    uint32_t CalculatedGroupId = InGroupID.y * 64 + InGroupID.x;
    if (CalculatedGroupId >= CBuffer.NumThreadGroups)
    {
        return;
    }

    FFX_ParallelSort_Scatter_uint(localID, CalculatedGroupId, CBuffer, rootConstData.CShiftBit, SrcBuffer, DstBuffer, SumTable, SrcPayload, DstPayload);
}

#endif
