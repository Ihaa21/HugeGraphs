
inline sort_globals SortGlobalsInit()
{
    sort_globals Result = {};
    
    // NOTE: Bitonic Merge Sort Pipelines
    {
        {
            vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&DemoState->SortDescLayout);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutEnd(RenderState->Device, &Builder);
        }

        // NOTE: Local FD
        {
            VkDescriptorSetLayout Layouts[] =
                {
                    DemoState->SortDescLayout,
                };
            
            DemoState->SortLocalFdPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                     "shader_merge_local_fd.spv", "main", Layouts, ArrayCount(Layouts));
        }

        // NOTE: Global Flip
        {
            VkDescriptorSetLayout Layouts[] =
                {
                    DemoState->SortDescLayout,
                };
            
            DemoState->SortGlobalFlipPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                        "shader_merge_global_flip.spv", "main", Layouts, ArrayCount(Layouts));
        }

        // NOTE: Local Disperse
        {
            VkDescriptorSetLayout Layouts[] =
                {
                    DemoState->SortDescLayout,
                };
            
            DemoState->SortLocalDispersePipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                           "shader_merge_local_disperse.spv", "main", Layouts, ArrayCount(Layouts));
        }

        // NOTE: Global Disperse
        {
            VkDescriptorSetLayout Layouts[] =
                {
                    DemoState->SortDescLayout,
                };
            
            DemoState->SortGlobalDispersePipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                            "shader_merge_global_disperse.spv", "main", Layouts, ArrayCount(Layouts));
        }

        {
            vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&DemoState->AtomicSortDescLayout);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutEnd(RenderState->Device, &Builder);
        }

        // NOTE: Atomic Merge Sort
        {
            VkDescriptorSetLayout Layouts[] =
                {
                    DemoState->AtomicSortDescLayout,
                };
            
            DemoState->AtomicSortPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                    "shader_atomic_merge_sort.spv", "main", Layouts, ArrayCount(Layouts));
        }
    }

    // NOTE: Init Sort Data
    {
        u32 NumPasses = 20;
        DemoState->SortArraySize = (u32)Pow(2.0f, (f32)NumPasses);
            
        {
#if 0
            VkDeviceMemory GpuMemory = VkMemoryAllocate(RenderState->Device, RenderState->StagingMemoryId, sizeof(u32)*DemoState->SortArraySize);
            DemoState->SortBuffer = VkBufferCreate(RenderState->Device, GpuMemory,
                                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                   sizeof(u32)*DemoState->SortArraySize);
            VkCheckResult(vkMapMemory(RenderState->Device, GpuMemory, 0, sizeof(u32)*DemoState->SortArraySize, 0, (void**)&DemoState->SortBufferCpu));
#endif

#if 1
            DemoState->SortBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                   sizeof(u32)*DemoState->SortArraySize);
#endif
                
            DemoState->SortBuffer2 = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                    sizeof(u32)*DemoState->SortArraySize);

            u32* GpuData = VkCommandsPushWriteArray(Commands, DemoState->SortBuffer2, u32, DemoState->SortArraySize,
                                                    BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                    BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));

            for (u32 ElementId = 0; ElementId < DemoState->SortArraySize; ++ElementId)
            {
                GpuData[ElementId] = rand(); //DemoState->SortArraySize - ElementId - 1;
            }
        }

        // NOTE: Local Fd Descriptor
        DemoState->SortLocalFdDescriptor = SortDescriptorCreate(Commands, 0, 0, 0);

        // NOTE: Local Disperse Descriptor
        DemoState->SortLocalDisperseDescriptor = SortDescriptorCreate(Commands, 0, 0, 0);

        // NOTE: Global Flip Uniforms
        for (u32 FlipSize = 2048, PassId = 11; FlipSize < DemoState->SortArraySize; PassId += 1, FlipSize = FlipSize * 2)
        {
            DemoState->SortGlobalFlipDescriptors[PassId - 11] = SortDescriptorCreate(Commands, FlipSize, PassId, 0);
        }
            
        // NOTE: Global Disperse Uniforms
        for (u32 FlipSize = 1024, PassId = 10; FlipSize < DemoState->SortArraySize / 2; PassId += 1, FlipSize = FlipSize * 2)
        {
            DemoState->SortGlobalDisperseDescriptors[PassId - 10] = SortDescriptorCreate(Commands, 0, PassId, FlipSize);
        }
    }

    // NOTE: Init Atomic Sort Data
    {
        {
            DemoState->AtomicSortUniformBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                sizeof(atomic_sort_uniform_data));

            atomic_sort_uniform_data* GpuData = VkCommandsPushWriteStruct(Commands, DemoState->AtomicSortUniformBuffer, atomic_sort_uniform_data,
                                                                          BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                                          BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
            
            GpuData->ArraySize = DemoState->SortArraySize;
        }

        {
            // NOTE: Calculate num elements
            u32 NumElements = 1;
            for (u32 FlipSize = 2048, PassId = 11; FlipSize < DemoState->SortArraySize; PassId += 1, FlipSize *= 2)
            {
                NumElements += 1;
                
                for (u32 N = FlipSize / 2, NPassId = PassId - 1; N > 0; NPassId -= 1, N = N / 2)
                {
                    if (N < 1024)
                    {
                        NumElements += 1;
                        break;
                    }
                    else
                    {
                        NumElements += 1;
                    }
                }
            }

            DemoState->AtomicSortPassBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                             sizeof(atomic_pass_params) * NumElements);

            atomic_pass_params* GpuData = VkCommandsPushWriteArray(Commands, DemoState->AtomicSortPassBuffer, atomic_pass_params, NumElements,
                                                                   BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                                   BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));

            // NOTE: Fill in array
            NumElements = 0;
            GpuData[NumElements++] = { PassType_LocalFd, 0, 0, 0 };
                
            for (u32 FlipSize = 2048, PassId = 11; FlipSize < DemoState->SortArraySize; PassId += 1, FlipSize *= 2)
            {
                GpuData[NumElements++] = { PassType_GlobalFlip, FlipSize, PassId, 0 };
                
                for (u32 N = FlipSize / 2, NPassId = PassId - 1; N > 0; NPassId -= 1, N = N / 2)
                {
                    if (N < 1024)
                    {
                        GpuData[NumElements++] = { PassType_LocalDisperse, 0, NPassId, N };
                        break;
                    }
                    else
                    {
                        GpuData[NumElements++] = { PassType_GlobalDisperse, 0, NPassId, N };
                    }
                }
            }
        }

        DemoState->AtomicSortBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                     sizeof(atomic_sort_buffer_data));
            
        DemoState->AtomicSortDescriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, DemoState->AtomicSortDescLayout);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->AtomicSortDescriptor, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, DemoState->AtomicSortUniformBuffer);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->AtomicSortDescriptor, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->SortBuffer);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->AtomicSortDescriptor, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->AtomicSortBuffer);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->AtomicSortDescriptor, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->AtomicSortPassBuffer);
    }
}

inline void GpuBitonicMergeSort()
{
    // TODO: REMOVE THIS (check if our array is sorted)
    // TODO: Write API to read back data from GPU
#if 0
    local_global b32 FirstFrame = true;
    if (!FirstFrame)
    {
        for (u32 ElementId = 0; ElementId < DemoState->SortArraySize - 1; ++ElementId)
        {
            Assert(DemoState->SortBufferCpu[ElementId] <= DemoState->SortBufferCpu[ElementId + 1]);
        }
    }
    FirstFrame = false;
#endif

    // TODO: Test Big Sort
#if 0
    {
        VkBufferCopy BufferCopy = {};
        BufferCopy.srcOffset = 0;
        BufferCopy.dstOffset = 0;
        BufferCopy.size = sizeof(u32) * DemoState->SortArraySize;
        vkCmdCopyBuffer(Commands->Buffer, DemoState->SortBuffer2, DemoState->SortBuffer, 1, &BufferCopy);

        VkBarrierBufferAdd(Commands, DemoState->SortBuffer,
                           VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        VkCommandsBarrierFlush(Commands);

#if 0
        // NOTE: Local FD
        {
            u32 DispatchX = CeilU32(f32(DemoState->SortArraySize) / 2048.0f);
                                
            VkDescriptorSet DescriptorSets[] =
                {
                    DemoState->SortLocalFdDescriptor.Descriptor,
                };
            VkComputeDispatch(Commands, DemoState->SortLocalFdPipeline, DescriptorSets, ArrayCount(DescriptorSets), DispatchX, 1, 1);

            VkBarrierBufferAdd(Commands, DemoState->SortBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkCommandsBarrierFlush(Commands);
        }
            
        // NOTE: General Pass
        for (u32 FlipSize = 2048, PassId = 11; FlipSize < DemoState->SortArraySize; PassId += 1, FlipSize *= 2)
        {
            SortGlobalFlip(Commands, PassId);
                
            for (u32 N = FlipSize / 2, NPassId = PassId - 1; N > 0; NPassId -= 1, N = N / 2)
            {
                if (N < 1024)
                {
                    SortLocalDisperse(Commands);
                    break;
                }
                else
                {
                    SortGlobalDisperse(Commands, NPassId);
                }
            }
        }
            
#else
            
        vkCmdFillBuffer(Commands->Buffer, DemoState->AtomicSortBuffer, 0, sizeof(atomic_sort_buffer_data), 0);

        VkBarrierBufferAdd(Commands, DemoState->AtomicSortBuffer,
                           VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        VkCommandsBarrierFlush(Commands);

        u32 DispatchPerPass = CeilU32(f32(DemoState->SortArraySize) / 2048.0f);
        u32 DispatchSize = DispatchPerPass;

        // NOTE: General Pass
        for (u32 FlipSize = 2048, PassId = 11; FlipSize < DemoState->SortArraySize; PassId += 1, FlipSize *= 2)
        {
            DispatchSize += DispatchPerPass;
                
            for (u32 N = FlipSize / 2, NPassId = PassId - 1; N > 0; NPassId -= 1, N = N / 2)
            {
                if (N < 1024)
                {
                    DispatchSize += DispatchPerPass;
                    break;
                }
                else
                {
                    DispatchSize += DispatchPerPass;
                }
            }
        }

        DispatchSize = 4*DispatchPerPass;
            
        u32 DispatchX = DispatchSize;
        u32 DispatchY = 1;
        if (DispatchSize > 16384)
        {
            DispatchX = 1024;
            DispatchY = CeilU32(f32(DispatchSize) / 1024.0f);
        }

        VkDescriptorSet DescriptorSets[] =
            {
                DemoState->AtomicSortDescriptor,
            };
            
        VkComputeDispatch(Commands, DemoState->AtomicSortPipeline, DescriptorSets, ArrayCount(DescriptorSets), DispatchX, DispatchY, 1);

#endif
    }
#endif

}
