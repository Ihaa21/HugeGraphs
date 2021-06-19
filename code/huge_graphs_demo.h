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

struct graph_node
{
    v3 Color;
    f32 Scale;
    v2 Pos;
    v2 Vel;
    v2 Accel;
    u32 StartEdges;
    u32 EndEdges;
    u32 FamilyId;
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

struct scene_buffer
{
    m4 VPTransform;
    v2 ViewPort;
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
    VkDescriptorSetLayout SceneDescLayout;
    VkDescriptorSet SceneDescriptor;
    VkBuffer SceneBuffer;
    
    // NOTE: Scene Meshes
    u32 MaxNumRenderMeshes;
    u32 NumRenderMeshes;
    render_mesh* RenderMeshes;
    
    // NOTE: Circle Entries
    u32 CircleMeshId;
    u32 MaxNumCircles;
    u32 NumCircles;
    circle_entry* CircleEntries;
    VkBuffer CircleEntryBuffer;

    // NOTE: Line Entries
    u32 MaxNumLines;
    u32 NumLines;
    line_vertex* LinePoints;
    VkBuffer LineVertexBuffer;
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

    // NOTE: Graph Layout Params
    f32 LayoutAvoidDiffRadius;
    f32 LayoutAvoidDiffAccel;
    f32 LayoutAvoidSameRadius;
    f32 LayoutAvoidSameAccel;
    f32 LayoutPullSameRadius;
    f32 LayoutPullSameAccel;
    f32 LayoutEdgeAccel;
    f32 LayoutEdgeMinDist;

    b32 PauseSim;
    
    // NOTE: Graph Data
    u32 NumGraphNodes;
    graph_node* GraphNodes;

    u32 MaxNumGraphEdges;
    u32 NumGraphEdges;
    u32* GraphEdges;
    
    // NOTE: Graph Draw
    vk_pipeline* CirclePipeline;
    vk_pipeline* LinePipeline;
};

global demo_state* DemoState;

inline u32 SceneMeshAdd(render_scene* Scene, vk_image Color, vk_image Normal, VkBuffer VertexBuffer, VkBuffer IndexBuffer, u32 NumIndices);
inline void SceneOpaqueInstanceAdd(render_scene* Scene, u32 MeshId, m4 WTransform, v4 Color);
