#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_KHR_shader_subgroup_arithmetic : enable

/*
  NOTE: Implementation based on https://poniesandlight.co.uk/reflect/bitonic_merge_sort/
 */

#include "sort_shaders_atomic.h"

void GlobalCompareAndSwap(uint Index1, uint Index2)
{
    if (SortArray[Index1] > SortArray[Index2])
    {
        uint Temp = SortArray[Index1];
        SortArray[Index1] = SortArray[Index2];
        SortArray[Index2] = Temp;
    }
}

void IncrementDoneCounter()
{
    if (gl_LocalInvocationID.x == 0)
    {
        atomicAdd(AtomicBuffer.DoneCounterId, 1);
    }
}

shared uint SharedWorkGroupId;
shared uint DoneCounterToWaitOn;

shared uint SharedPassType;
shared uint SharedPassId;
shared uint SharedFlipSize;
shared uint SharedN;

shared uint SharedArrayValues[2048];

layout(local_size_x = 1024, local_size_y = 1, local_size_z = 1) in;
void main()
{
    if (gl_LocalInvocationID.x == 0)
    {
        SharedWorkGroupId = atomicAdd(AtomicBuffer.WorkCountId, 1);
        uint NumGroupsPerPass = uint(ceil(float(SortUniforms.ArraySize) / 2048.0f));

        // NOTE: Local FD
#if 0
        uint CurrWorkGroupId = 0;
        SharedPassType = PassType_None;

        CurrWorkGroupId += NumGroupsPerPass;
        if (SharedWorkGroupId < CurrWorkGroupId)
        {
            SharedPassType = PassType_LocalFd;
        }

        // NOTE: General Pass
        for (uint FlipSize = 2048, PassId = 11;
             FlipSize < SortUniforms.ArraySize && SharedPassType == PassType_None;
             PassId += 1, FlipSize *= 2)
        {
            CurrWorkGroupId += NumGroupsPerPass;
            if (SharedWorkGroupId < CurrWorkGroupId)
            {
                SharedPassType = PassType_GlobalFlip;
                SharedFlipSize = FlipSize;
                SharedPassId = PassId;
                break;
            }

            for (uint N = FlipSize / 2, NPassId = PassId - 1; N > 0; NPassId -= 1, N = N / 2)
            {
                if (N < 1024)
                {
                    CurrWorkGroupId += NumGroupsPerPass;
                    if (SharedWorkGroupId < CurrWorkGroupId)
                    {
                        SharedPassType = PassType_LocalDisperse;
                        SharedN = N;
                        SharedPassId = NPassId;
                        break;
                    }
                    break;
                }
                else
                {
                    CurrWorkGroupId += NumGroupsPerPass;
                    if (SharedWorkGroupId < CurrWorkGroupId)
                    {
                        SharedPassType = PassType_GlobalDisperse;
                        SharedN = N;
                        SharedPassId = NPassId;
                        break;
                    }
                }
            }
        }
#endif

        uint ElementId = SharedWorkGroupId / NumGroupsPerPass;
        SharedPassType = PassParams[ElementId].PassType;
        SharedFlipSize = PassParams[ElementId].FlipSize;
        SharedPassId = PassParams[ElementId].PassId;
        SharedN = PassParams[ElementId].N;
        
        //DoneCounterToWaitOn = CurrWorkGroupId - NumGroupsPerPass;
        DoneCounterToWaitOn = ElementId * NumGroupsPerPass;
    }

    //barrier();
    memoryBarrierShared();

    // TODO: For passes that are global, we can make them finish executing independently by having each 32/64 threads write atomically
    // after they finish so we don't need memory barriers in that case. Idk if that is super helpful since I guess our wait time is more on
    // the entire pass to finish, not some threads but might be worth a try
    
#if 1
    // NOTE: Wait for prev pass to finish
    // TODO: We can choose a more accurate counter to wait on
    while (atomicAdd(AtomicBuffer.DoneCounterId, 0) < DoneCounterToWaitOn)
    {
    }
    
    uint GlobalThreadId = (SharedWorkGroupId - DoneCounterToWaitOn) * 1024 + uint(gl_LocalInvocationID.x);
    switch (SharedPassType)
    {
        case PassType_LocalFd:
        {
            if (GlobalThreadId < SortUniforms.ArraySize / 2)
            {
                uint LocalThreadId = uint(gl_LocalInvocationIndex);
                SharedArrayValues[2*LocalThreadId + 0] = SortArray[2*GlobalThreadId + 0];
                SharedArrayValues[2*LocalThreadId + 1] = SortArray[2*GlobalThreadId + 1];

                uint MinArraySize = min(SortUniforms.ArraySize, 2048);
        
                for (uint FlipSize = 1, PassId = 0; FlipSize < MinArraySize; PassId += 1, FlipSize *= 2)
                {
                    barrier();

                    // NOTE: Do Flip
                    {
                        uint LowerHeightBits = FlipSize - 1;
                        uint FlipId1 = 2*FlipSize * (LocalThreadId >> PassId) + (LocalThreadId & LowerHeightBits);
                        uint FlipId2 = FlipId1 + 2*FlipSize - 2 * (LocalThreadId & LowerHeightBits) - 1;

                        if (SharedArrayValues[FlipId1] > SharedArrayValues[FlipId2])
                        {
                            uint Temp = SharedArrayValues[FlipId1];
                            SharedArrayValues[FlipId1] = SharedArrayValues[FlipId2];
                            SharedArrayValues[FlipId2] = Temp;
                        }
                    }
        
                    barrier();

                    // NOTE: Do Disperse
                    for (uint N = FlipSize / 2, NPassId = PassId - 1; N > 0; NPassId -= 1, N = N / 2)
                    {
                        uint DoubleHeight = N * 2;
                        uint LowerBits = N - 1;
                        uint DisperseId1 = DoubleHeight * ((LocalThreadId & (~LowerBits)) >> NPassId) + (LocalThreadId & LowerBits);
                        uint DisperseId2 = DisperseId1 + N;
            
                        if (SharedArrayValues[DisperseId1] > SharedArrayValues[DisperseId2])
                        {
                            uint Temp = SharedArrayValues[DisperseId1];
                            SharedArrayValues[DisperseId1] = SharedArrayValues[DisperseId2];
                            SharedArrayValues[DisperseId2] = Temp;
                        }

                        barrier();
                    }
                }

                SortArray[2*GlobalThreadId + 0] = SharedArrayValues[2*LocalThreadId + 0];
                SortArray[2*GlobalThreadId + 1] = SharedArrayValues[2*LocalThreadId + 1];

                //memoryBarrier();
                IncrementDoneCounter();
            }

        } break;

        case PassType_GlobalFlip:
        {
            if (GlobalThreadId < SortUniforms.ArraySize / 2)
            {
                uint DoubleFlip = 2*SharedFlipSize;
                uint LowerHeightBits = SharedFlipSize - 1;
                uint FlipId1 = DoubleFlip * (GlobalThreadId >> SharedPassId) + (GlobalThreadId & LowerHeightBits);
                uint FlipId2 = FlipId1 + DoubleFlip - 2 * (GlobalThreadId & LowerHeightBits) - 1;
        
                GlobalCompareAndSwap(FlipId1, FlipId2);
                
                //memoryBarrier();
                IncrementDoneCounter();
            }
        } break;

        case PassType_LocalDisperse:
        {
            if (GlobalThreadId < SortUniforms.ArraySize / 2)
            {
                uint LocalThreadId = uint(gl_LocalInvocationIndex);
                SharedArrayValues[2*LocalThreadId + 0] = SortArray[2*GlobalThreadId + 0];
                SharedArrayValues[2*LocalThreadId + 1] = SortArray[2*GlobalThreadId + 1];

                barrier();

                // NOTE: Do Disperse
                for (uint N = 1024, NPassId = 10; N > 0; NPassId -= 1, N = N / 2)
                {
                    uint DoubleHeight = N * 2;
                    uint LowerBits = N - 1;
                    uint DisperseId1 = DoubleHeight * ((LocalThreadId & (~LowerBits)) >> NPassId) + (LocalThreadId & LowerBits);
                    uint DisperseId2 = DisperseId1 + N;
            
                    if (SharedArrayValues[DisperseId1] > SharedArrayValues[DisperseId2])
                    {
                        uint Temp = SharedArrayValues[DisperseId1];
                        SharedArrayValues[DisperseId1] = SharedArrayValues[DisperseId2];
                        SharedArrayValues[DisperseId2] = Temp;
                    }

                    barrier();
                }
        
                SortArray[2*GlobalThreadId + 0] = SharedArrayValues[2*LocalThreadId + 0];
                SortArray[2*GlobalThreadId + 1] = SharedArrayValues[2*LocalThreadId + 1];

                //memoryBarrier();
                IncrementDoneCounter();
            }
        } break;

        case PassType_GlobalDisperse:
        {
            if (GlobalThreadId < SortUniforms.ArraySize / 2)
            {
                uint DoubleHeight = SharedN * 2;
                uint LowerBits = SharedN - 1;
                uint DisperseId1 = DoubleHeight * ((GlobalThreadId & (~LowerBits)) >> SharedPassId) + (GlobalThreadId & LowerBits);
                uint DisperseId2 = DisperseId1 + SharedN;

                GlobalCompareAndSwap(DisperseId1, DisperseId2);

                //memoryBarrier();
                IncrementDoneCounter();
            }
        } break;
    }
#endif
}
