#pragma once

#define VALIDATION 1

#include "framework_vulkan\framework_vulkan.h"

// TODO: Do we want this in frameowrk? Cant print debug logs in framework code...
#define WIN32_PROFILING
//#define CPU_PROFILING
//#define X86_PROFILING
#include "profiling\profiling.h"

//
// NOTE: Graph Data
//

struct graph_node_pos
{
    v2 Pos;
    u32 FamilyId;
    u32 Pad;
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
    f32 LayoutAvoidDiffRadius;
    f32 LayoutAvoidDiffAccel;
    f32 LayoutAvoidSameRadius;
    f32 LayoutAvoidSameAccel;
    f32 LayoutPullSameRadius;
    f32 LayoutPullSameAccel;
    f32 LayoutEdgeAccel;
    f32 LayoutEdgeMinDist;
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
};

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
    f32 LayoutAvoidDiffRadius;
    f32 LayoutAvoidDiffAccel;
    f32 LayoutAvoidSameRadius;
    f32 LayoutAvoidSameAccel;
    f32 LayoutPullSameRadius;
    f32 LayoutPullSameAccel;
    f32 LayoutEdgeAccel;
    f32 LayoutEdgeMinDist;
    
    // NOTE: Graph Sim
    b32 PauseSim;
    u32 NumGraphNodes;
    u32 NumGraphRedNodes;
    u32 NumGraphEdges;
    u32 NumGraphDrawEdges;
    
    VkDescriptorSetLayout GraphDescLayout;
    VkDescriptorSet GraphDescriptor;
    VkBuffer GraphGlobalsBuffer;
    VkBuffer NodePositionBuffer;
    VkBuffer NodeVelocityBuffer;
    VkBuffer NodeEdgeBuffer;
    VkBuffer EdgeBuffer;
        
    vk_pipeline* GraphMoveConnectionsPipeline;
    vk_pipeline* GraphNearbyPipeline;
    vk_pipeline* GraphUpdateNodesPipeline;
    vk_pipeline* GraphGenEdgesPipeline;
    
    // NOTE: Graph Draw
    VkBuffer NodeDrawBuffer;
    VkBuffer EdgePositionBuffer;
    VkBuffer EdgeColorBuffer;

    vk_pipeline* CirclePipeline;
    vk_pipeline* LinePipeline;
};

global demo_state* DemoState;

inline u32 SceneMeshAdd(render_scene* Scene, vk_image Color, vk_image Normal, VkBuffer VertexBuffer, VkBuffer IndexBuffer, u32 NumIndices);
inline void SceneOpaqueInstanceAdd(render_scene* Scene, u32 MeshId, m4 WTransform, v4 Color);
