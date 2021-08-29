#pragma once

struct sort_globals
{
    // NOTE: Sort Data
    u32 SortArraySize;
    VkDescriptorSetLayout SortDescLayout;
    sort_descriptor SortLocalFdDescriptor;
    sort_descriptor SortLocalDisperseDescriptor;
    sort_descriptor SortGlobalFlipDescriptors[10];
    sort_descriptor SortGlobalDisperseDescriptors[10];
    VkBuffer SortBuffer;
    VkBuffer SortBuffer2;
    u32* SortBufferCpu;

    vk_pipeline* SortLocalFdPipeline;
    vk_pipeline* SortGlobalFlipPipeline;
    vk_pipeline* SortLocalDispersePipeline;
    vk_pipeline* SortGlobalDispersePipeline;

    // NOTE: Atomic Sort Data
    VkBuffer AtomicSortUniformBuffer;
    VkBuffer AtomicSortBuffer;
    VkBuffer AtomicSortPassBuffer;
    VkDescriptorSet AtomicSortDescriptor;
    VkDescriptorSetLayout AtomicSortDescLayout;
    vk_pipeline* AtomicSortPipeline;
};
