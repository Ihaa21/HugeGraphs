
inline void Init()
{
    DemoState->GridCellBlockBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                    sizeof(grid_cell_block) * 10000); //(DemoState->NumGraphNodes / 512));
    DemoState->GridCellHeaderBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                     sizeof(u32) * DemoState->NumCellsAxis * DemoState->NumCellsAxis);
    DemoState->GridCellBlockCounter = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                     sizeof(u32)*100);

    VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->GridCellBlockBuffer);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->GridCellHeaderBuffer);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->GridCellBlockCounter);
    
    // NOTE: Graph Init Grid Pipeline
    {
        VkDescriptorSetLayout Layouts[] =
            {
                DemoState->GraphDescLayout,
            };
            
        DemoState->GraphInitGridPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                   "shader_graph_init_grid.spv", "main", Layouts, ArrayCount(Layouts));
    }
}
