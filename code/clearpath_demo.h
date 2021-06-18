#pragma once

#define VALIDATION 1

#include "framework_vulkan\framework_vulkan.h"

#define WIN32_PROFILING
//#define CPU_PROFILING
//#define X86_PROFILING
#include "profiling\profiling.h"

//
// NOTE: Sim Data
//

#define DRAW_ENTITY 4
#define DEBUG_DRAW 1

struct cone
{
    v2 Apex;
    v2 Left;
    v2 Right;
};

struct entity
{
    v2 Pos;
    v2 TargetPos;
    v2 Vel;
    v2 PreferredVel;
    f32 Speed;
    f32 CollisionRadius;
};

struct cone_array
{
    u32 NumRvos;
    cone* RvoArray;
};

struct candidate
{
    f32 DistanceSq;
    v2 Vel;
};

//
// NOTE: Render Data
//

struct directional_light
{
    v3 Color;
    u32 Pad0;
    v3 Dir;
    u32 Pad1;
    v3 AmbientColor;
    u32 Pad2;
};

struct point_light
{
    v3 Color;
    u32 Pad0;
    v3 Pos;
    f32 MaxDistance;
};

struct scene_globals
{
    v3 CameraPos;
    u32 NumPointLights;
    m4 VPTransform;
};

struct instance_entry
{
    u32 MeshId;
    m4 WVTransform;
    m4 WVPTransform;
    v4 Color;
};

struct gpu_instance_entry
{
    m4 WVTransform;
    m4 WVPTransform;
    v4 Color;
};

struct render_mesh
{
    vk_image Color;
    vk_image Normal;
    VkDescriptorSet MaterialDescriptor;
    
    VkBuffer VertexBuffer;
    VkBuffer IndexBuffer;
    u32 NumIndices;
};

struct render_scene
{
    // NOTE: General Render Data
    camera Camera;
    VkDescriptorSetLayout MaterialDescLayout;
    VkDescriptorSetLayout SceneDescLayout;
    VkBuffer SceneBuffer;
    VkDescriptorSet SceneDescriptor;

    // NOTE: Scene Lights
    u32 MaxNumPointLights;
    u32 NumPointLights;
    point_light* PointLights;
    VkBuffer PointLightBuffer;
    VkBuffer PointLightTransforms;
    
    directional_light DirectionalLight;
    VkBuffer DirectionalLightBuffer;

    // NOTE: Scene Meshes
    u32 MaxNumRenderMeshes;
    u32 NumRenderMeshes;
    render_mesh* RenderMeshes;
    
    // NOTE: Opaque Instances
    u32 MaxNumOpaqueInstances;
    u32 NumOpaqueInstances;
    instance_entry* OpaqueInstances;
    VkBuffer OpaqueInstanceBuffer;
};

struct demo_state
{
    platform_block_arena PlatformBlockArena;
    linear_arena Arena;
    linear_arena TempArena;

    // NOTE: Samplers
    VkSampler PointSampler;
    VkSampler LinearSampler;
    VkSampler AnisoSampler;

    // NOTE: Rendering Data
    vk_linear_arena RenderTargetArena;
    render_target_entry SwapChainEntry;
    VkImage DepthImage;
    render_target_entry DepthEntry;
    render_target RenderTarget;
    vk_pipeline* RenderPipeline;

    render_scene Scene;

    ui_state UiState;
    
    // NOTE: Saved model ids
    u32 Quad;
    u32 Cube;
    u32 Sphere;

    // NOTE: Sim Data
    f32 TerrainRadius;
    u32 NumEntities;
    entity* PrevEntities;
    entity* CurrEntities;

    // NOTE: Debug Draw
    vk_pipeline* TrianglePipeline;
    vk_pipeline* LinePipeline;
    vk_pipeline* PointPipeline;
    
#if DEBUG_DRAW
    u32 MaxNumTriangleVerts;
    u32 NumTriangleVerts;
    v3* TrianglePos;
    v4* TriangleColors;
    VkBuffer TriangleBuffer;

    u32 MaxNumLineVerts;
    u32 NumLineVerts;
    v3* LinePos;
    v4* LineColors;
    VkBuffer LineBuffer;

    u32 MaxNumPointVerts;
    u32 NumPointVerts;
    v3* PointPos;
    v4* PointColors;
    VkBuffer PointBuffer;
#endif
};

global demo_state* DemoState;

inline u32 SceneMeshAdd(render_scene* Scene, vk_image Color, vk_image Normal, VkBuffer VertexBuffer, VkBuffer IndexBuffer, u32 NumIndices);
inline void SceneOpaqueInstanceAdd(render_scene* Scene, u32 MeshId, m4 WTransform, v4 Color);
