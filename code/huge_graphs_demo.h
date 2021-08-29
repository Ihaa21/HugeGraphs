#pragma once

#define VALIDATION 1

#include "framework_vulkan\framework_vulkan.h"

// TODO: Do we want this in frameowrk? Cant print debug logs in framework code...
#define WIN32_PROFILING
//#define CPU_PROFILING
//#define X86_PROFILING
#include "profiling\profiling.h"

#include "file_headers.h"

//
// NOTE: Graph Data
//

struct graph_edge
{
    u32 OtherNodeId;
    f32 Weight;
};

struct graph_node_edges
{
    u32 StartConnections;
    u32 EndConnections;
};

struct graph_node_draw
{
    v3 Color;
    f32 Scale;
};

struct grid_cell_block
{
    uint NextBlockOffset;
    uint NumElements;
    uint ElementIds[1024];
};

struct global_move
{
    f32 SpeedEfficiency;
    f32 Speed;
    f32 JitterToleranceConstant;
    f32 MaxJitterTolerance;
};

struct global_move_counters
{
    u32 GlobalMoveWorkCounter;
    u32 GlobalMoveDoneCounter;
};

//
// NOTE: Merge Sort Data
//

struct merge_sort_uniform_data
{
    u32 ArraySize;
    u32 FlipSize;
    u32 PassId;
    u32 N;
    u32 StartIndexOffset;
};

struct merge_sort_descriptor
{
    VkBuffer UniformBuffer;
    VkDescriptorSet Descriptor;
};

struct merge_sort_atomic_uniform_data
{
    u32 ArraySize;
};

struct merge_sort_atomic_buffer_data
{
    u32 WorkCountId;
    u32 DoneCounterId;
};

#define MergeSortPassType_None 0
#define MergeSortPassType_LocalFd 1
#define MergeSortPassType_GlobalFlip 2
#define MergeSortPassType_LocalDisperse 3
#define MergeSortPassType_GlobalDisperse 4

struct merge_sort_atomic_pass_params
{
    u32 PassType;
    u32 FlipSize;
    u32 PassId;
    u32 N;
};

//
// NOTE: Radix Tree Data
//

struct gpu_bounds
{
    v2 Min;
    v2 Max;
};

struct radix_tree_uniform_data
{
    u32 NumNodes;
};

//
// NOTE: Render Data
//

struct circle_entry
{
    m4 WVPTransform;
    v4 Color;
};

#pragma pack(push, 1)
struct line_vertex
{
    v2 Pos;
    v4 Color;
};
#pragma pack(pop)

struct graph_globals
{
    m4 VPTransform;
    v2 ViewPort;
    f32 FrameTime;
    u32 NumNodes;

    // NOTE: Layout Data
    float AttractionMultiplier;
    float AttractionWeightPower;
    float RepulsionMultiplier;
    float RepulsionSoftner;
    float GravityMultiplier;
    b32 StrongGravityEnabled;
    
    // NOTE: Grid Data
    f32 CellDim;
    f32 WorldRadius;
    u32 NumCellsDim;

    // NOTE: Reduction data
    u32 NumThreadGroupsCalcNodeBounds;
    u32 NumThreadGroupsGlobalSpeed;
};

struct render_mesh
{
    VkBuffer VertexBuffer;
    VkBuffer IndexBuffer;
    u32 NumIndices;
};

struct render_scene
{
    // NOTE: General Render Data
    camera Camera;
    
    // NOTE: Scene Meshes
    u32 MaxNumRenderMeshes;
    u32 NumRenderMeshes;
    render_mesh* RenderMeshes;
    u32 CircleMeshId;
    u32 QuadMeshId;
};

#define MAX_THREAD_GROUPS 65535
#define BITONIC_MERGE_SORT 0
#define LINE_PIPELINE_2 0

struct demo_state
{
    platform_block_arena PlatformBlockArena;
    linear_arena Arena;
    linear_arena TempArena;

    // NOTE: Samplers
    VkSampler PointSampler;
    VkSampler LinearSampler;

    // NOTE: Rendering Data
    vk_linear_arena RenderTargetArena;
    render_target_entry SwapChainEntry;
    VkImage DepthImage;
    render_target_entry DepthEntry;
    render_target RenderTarget;

    render_scene Scene;

    ui_state UiState;

    // NOTE: Layout Data
    float AttractionMultiplier;
    float AttractionWeightPower;
    float RepulsionMultiplier;
    float RepulsionSoftner;
    float GravityMultiplier;
    b32 StrongGravityEnabled;
    
    // NOTE: Graph Sim
    b32 PauseSim;
    u32 NumGraphNodes;
    u32 NumGraphRedNodes;
    u32 NumGraphEdges;
    u32 NumGraphDrawEdges;
    u32 NumCellsAxis;
    f32 CellWorldDim;
    f32 WorldRadius;

    //======================================================================
    // NOTE: Graph GPU Data
    //======================================================================
    
    VkDescriptorSetLayout GraphDescLayout;
    VkDescriptorSet GraphDescriptor;
    VkBuffer GraphGlobalsBuffer;
    VkBuffer NodePosBuffer;
    VkBuffer NodeDegreeBuffer;
    VkBuffer NodeCellIdBuffer;
    VkBuffer NodeForceBuffer;
    VkBuffer NodePrevForceBuffer;
    VkBuffer NodeEdgeBuffer;
    VkBuffer EdgeBuffer;

    VkBuffer GlobalMoveBuffer;
    VkBuffer GlobalMoveReductionBuffer;
    VkBuffer GlobalMoveCounterBuffer;
        
    vk_pipeline* GraphMoveConnectionsPipeline;
    vk_pipeline* GraphCalcGlobalSpeedPipeline;
    vk_pipeline* GraphUpdateNodesPipeline;

    // NOTE: Regular n^2 repulsion
    vk_pipeline* GraphRepulsionPipeline;

    //======================================================================
    // NOTE: Radix Repulsion GPU Data
    //======================================================================

    VkBuffer GlobalBoundsReductionBuffer;
    VkBuffer GlobalBoundsCounterBuffer;
    VkBuffer ElementBoundsBuffer;
    
    vk_pipeline* CalcWorldBoundsPipeline;
    
    // NOTE: Radix Tree Data
    VkBuffer RadixTreeUniformBuffer;
    VkBuffer RadixMortonKeyBuffer;
    VkBuffer RadixElementReMappingBuffer;
    VkBuffer RadixTreeChildrenBuffer;
    VkBuffer RadixTreeParentBuffer;
    VkBuffer RadixTreeParticleBuffer;
    VkBuffer RadixTreeAtomicsBuffer;
    VkDescriptorSet RadixTreeDescriptor;
    VkDescriptorSetLayout RadixTreeDescLayout;

    vk_pipeline* GenerateMortonKeysPipeline;
    vk_pipeline* RadixTreeBuildPipeline;
    vk_pipeline* RadixTreeSummarizePipeline;
    vk_pipeline* RadixTreeRepulsionPipeline;
    
    // NOTE: Merge Sort Data
    VkDescriptorSetLayout MergeSortDescLayout;
    merge_sort_descriptor MergeSortLocalFdDescriptor;
    merge_sort_descriptor MergeSortLocalDisperseDescriptor;
    merge_sort_descriptor MergeSortGlobalFlipDescriptors[10];
    merge_sort_descriptor MergeSortGlobalDisperseDescriptors[10];

    vk_pipeline* MergeSortLocalFdPipeline;
    vk_pipeline* MergeSortGlobalFlipPipeline;
    vk_pipeline* MergeSortLocalDispersePipeline;
    vk_pipeline* MergeSortGlobalDispersePipeline;

    // NOTE: Parallel Sort Data
#define PARALLEL_SORT_MAX_THREAD_GROUPS 1000000
    u32 ParallelSortNumThreadGroups;
    u32 ParallelSortNumReducedThreadGroups;

    VkDescriptorSet ParallelSortConstantDescriptor;
    VkDescriptorSet ParallelSortInputOutputDescriptor[2];
    VkDescriptorSet ParallelSortScanDescriptor[2];
    VkDescriptorSet ParallelSortScratchDescriptor;
    VkBuffer ParallelSortUniformBuffer;
    VkBuffer ParallelSortMortonBuffer;
    VkBuffer ParallelSortPayloadBuffer;
    VkBuffer ParallelSortScratchBuffer;
    VkBuffer ParallelSortReducedScratchBuffer;

    VkDescriptorSetLayout ParallelSortConstantDescLayout;
    VkDescriptorSetLayout ParallelSortInputOutputDescLayout;
    VkDescriptorSetLayout ParallelSortScanDescLayout;
    VkDescriptorSetLayout ParallelSortScratchDescLayout;
    vk_pipeline* ParallelSortCountPipeline;
    vk_pipeline* ParallelSortReducePipeline;
    vk_pipeline* ParallelSortScanPipeline;
    vk_pipeline* ParallelSortScanAddPipeline;
    vk_pipeline* ParallelSortScatterPipeline;
    
    //======================================================================
    // NOTE: Graph Draw Data
    //======================================================================
    
    VkBuffer NodeDrawBuffer;
    VkBuffer EdgeIndexBuffer;
    VkBuffer EdgeColorBuffer;

    vk_pipeline* CirclePipeline;
    vk_pipeline* LinePipeline;
};

global demo_state* DemoState;

inline u32 SceneMeshAdd(render_scene* Scene, vk_image Color, vk_image Normal, VkBuffer VertexBuffer, VkBuffer IndexBuffer, u32 NumIndices);
inline void SceneOpaqueInstanceAdd(render_scene* Scene, u32 MeshId, m4 WTransform, v4 Color);
