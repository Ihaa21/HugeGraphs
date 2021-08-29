#pragma once

struct grid_data
{
    VkBuffer GridCellBlockBuffer;
    VkBuffer GridCellHeaderBuffer;
    VkBuffer GridCellBlockCounter;

    vk_pipeline* GraphInitGridPipeline;
};
