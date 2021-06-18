
#include "clearpath_demo.h"

/*

  NOTE:

  - http://www.cs.toronto.edu/~dt/siggraph97-course/cwr87/
  - https://github.com/BogdanCodreanu/ECS-Boids-Murmuration_Unity_2019.1
  - https://github.com/SebLague/Boids
  - https://eater.net/boids
  
 */

inline f32 RandFloat()
{
    f32 Result = f32(rand()) / f32(RAND_MAX);
    return Result;
}

//
// NOTE: ClearPath Sim
//

// TODO: Dumb visualization
inline void DebugDrawPoint(v2 Pos, v4 Color, u32 EntityId)
{
#if DEBUG_DRAW
    if (EntityId == DRAW_ENTITY)
    {
        Assert(DemoState->NumPointVerts < DemoState->MaxNumPointVerts);
        DemoState->PointPos[DemoState->NumPointVerts] = V3(Pos, 0);
        DemoState->PointColors[DemoState->NumPointVerts] = Color;
        DemoState->NumPointVerts++;
    }
#endif
}

inline void DebugDrawPointEntity(v2 Pos, v4 Color, u32 EntityId)
{
    v2 EntityPos = DemoState->PrevEntities[EntityId].Pos;
    DebugDrawPoint(EntityPos + Pos, Color, EntityId);
}

inline void DebugPushLine(v2 Start, v2 End, v4 Color, u32 EntityId)
{
#if DEBUG_DRAW
    if (EntityId == DRAW_ENTITY)
    {
        Assert(DemoState->NumLineVerts < DemoState->MaxNumLineVerts);
        DemoState->LinePos[DemoState->NumLineVerts] = V3(Start, 0);
        DemoState->LineColors[DemoState->NumLineVerts] = Color;
        DemoState->NumLineVerts++;
        
        Assert(DemoState->NumLineVerts < DemoState->MaxNumLineVerts);
        DemoState->LinePos[DemoState->NumLineVerts] = V3(End, 0);
        DemoState->LineColors[DemoState->NumLineVerts] = Color;
        DemoState->NumLineVerts++;
    }
#endif
}

inline void DebugPushLineEntity(v2 Start, v2 End, v4 Color, u32 EntityId)
{
    v2 EntityPos = DemoState->PrevEntities[EntityId].Pos;
    DebugPushLine(Start + EntityPos, End + EntityPos, Color, EntityId);
}

inline void DebugPushTriangle(v2 P0, v2 P1, v2 P2, v4 Color, u32 EntityId)
{
#if DEBUG_DRAW
    if (EntityId == DRAW_ENTITY)
    {
        Assert(DemoState->NumTriangleVerts < DemoState->MaxNumTriangleVerts);
        DemoState->TrianglePos[DemoState->NumTriangleVerts] = V3(P0, 0);
        DemoState->TriangleColors[DemoState->NumTriangleVerts] = Color;
        DemoState->NumTriangleVerts++;
        
        Assert(DemoState->NumTriangleVerts < DemoState->MaxNumTriangleVerts);
        DemoState->TrianglePos[DemoState->NumTriangleVerts] = V3(P1, 0);
        DemoState->TriangleColors[DemoState->NumTriangleVerts] = Color;
        DemoState->NumTriangleVerts++;
        
        Assert(DemoState->NumTriangleVerts < DemoState->MaxNumTriangleVerts);
        DemoState->TrianglePos[DemoState->NumTriangleVerts] = V3(P2, 0);
        DemoState->TriangleColors[DemoState->NumTriangleVerts] = Color;
        DemoState->NumTriangleVerts++;
    }
#endif
}

inline void DebugPushTriangleEntity(v2 P0, v2 P1, v2 P2, v4 Color, u32 EntityId)
{
    v2 EntityPos = DemoState->PrevEntities[EntityId].Pos;
    DebugPushTriangle(EntityPos + P0, EntityPos + P1, EntityPos + P2, Color, EntityId);
}

inline b32 PointOutsideConeUnion(cone_array ConeArray, v2 Pos)
{
    b32 Result = true;

    for (u32 ConeId = 0; ConeId < ConeArray.NumRvos; ++ConeId)
    {
        cone* CurrCone = ConeArray.RvoArray + ConeId;

        f32 Epsilon = 1.0f / 1024.0f;
        v2 ConeSpacePos = NormalizeSafe(Pos - CurrCone->Apex);

        // NOTE: Use rasterization line equation to find out which side we are on
        f32 DeterminantLeft = (ConeSpacePos.y * CurrCone->Left.x) - (ConeSpacePos.x * CurrCone->Left.y);
        f32 DeterminantRight = (ConeSpacePos.y * CurrCone->Right.x) - (ConeSpacePos.x * CurrCone->Right.y);

        if (!((DeterminantLeft < Epsilon) || (DeterminantRight > -Epsilon)))
        {
            Result = false;
            break;
        }
    }
    
    return Result;
}

inline void UpdateCandidate(cone_array ConeArray, candidate* CurrCandidate, v2 TargetVel, v2 PreferredVel, f32 EntitySpeed,
                            u32 EntityId, b32 IsFirstSetup = false)
{
    // NOTE: Only choose vels that are within our max speed radius
    f32 VelDistSq = LengthSquared(TargetVel);
    if (IsFirstSetup || VelDistSq < Square(EntitySpeed))
    {
        DebugDrawPointEntity(TargetVel, V4(0.5f, 1.0f, 0.2f, 1.0f), EntityId);

        // NOTE: Choose vels based on which is closest to preferred vel
        f32 DistToTargetSq = LengthSquared(PreferredVel - TargetVel);
        if (DistToTargetSq < CurrCandidate->DistanceSq && PointOutsideConeUnion(ConeArray, TargetVel))
        {
            CurrCandidate->Vel = TargetVel;
            CurrCandidate->DistanceSq = DistToTargetSq;
        }
    }
}

//
// NOTE: Asset Storage System
//

inline u32 SceneMeshAdd(render_scene* Scene, vk_image Color, vk_image Normal, VkBuffer VertexBuffer, VkBuffer IndexBuffer, u32 NumIndices)
{
    Assert(Scene->NumRenderMeshes < Scene->MaxNumRenderMeshes);
    
    u32 MeshId = Scene->NumRenderMeshes++;
    render_mesh* Mesh = Scene->RenderMeshes + MeshId;
    Mesh->Color = Color;
    Mesh->Normal = Normal;
    Mesh->VertexBuffer = VertexBuffer;
    Mesh->IndexBuffer = IndexBuffer;
    Mesh->NumIndices = NumIndices;
    Mesh->MaterialDescriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, Scene->MaterialDescLayout);
    VkDescriptorImageWrite(&RenderState->DescriptorManager, Mesh->MaterialDescriptor, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           Color.View, DemoState->PointSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VkDescriptorImageWrite(&RenderState->DescriptorManager, Mesh->MaterialDescriptor, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           Normal.View, DemoState->PointSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    return MeshId;
}

inline u32 SceneMeshAdd(render_scene* Scene, vk_image Color, vk_image Normal, procedural_mesh Mesh)
{
    u32 Result = SceneMeshAdd(Scene, Color, Normal, Mesh.Vertices, Mesh.Indices, Mesh.NumIndices);
    return Result;
}

inline void SceneOpaqueInstanceAdd(render_scene* Scene, u32 MeshId, m4 WTransform, v4 Color)
{
    Assert(Scene->NumOpaqueInstances < Scene->MaxNumOpaqueInstances);

    instance_entry* Instance = Scene->OpaqueInstances + Scene->NumOpaqueInstances++;
    Instance->MeshId = MeshId;
    Instance->WVTransform = CameraGetV(&Scene->Camera)*WTransform;
    Instance->WVPTransform = CameraGetP(&Scene->Camera)*Instance->WVTransform;
    Instance->Color = Color;
}

inline void ScenePointLightAdd(render_scene* Scene, v3 Pos, v3 Color, f32 MaxDistance)
{
    Assert(Scene->NumPointLights < Scene->MaxNumPointLights);

    // TODO: Specify strength or a sphere so that we can visualize nicely too?
    point_light* PointLight = Scene->PointLights + Scene->NumPointLights++;
    PointLight->Pos = Pos;
    PointLight->Color = Color;
    PointLight->MaxDistance = MaxDistance;
}

inline void SceneDirectionalLightSet(render_scene* Scene, v3 LightDir, v3 Color, v3 AmbientColor)
{
    Scene->DirectionalLight.Dir = LightDir;
    Scene->DirectionalLight.Color = Color;
    Scene->DirectionalLight.AmbientColor = AmbientColor;
}

//
// NOTE: Demo Code
//

inline void DemoSwapChainChange(u32 Width, u32 Height)
{
    b32 ReCreate = DemoState->RenderTargetArena.Used != 0;
    VkArenaClear(&DemoState->RenderTargetArena);

    // NOTE: Render Target Data
    RenderTargetEntryReCreate(&DemoState->RenderTargetArena, Width, Height, VK_FORMAT_D32_SFLOAT,
                              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT,
                              &DemoState->DepthImage, &DemoState->DepthEntry);
}

inline void DemoAllocGlobals(linear_arena* Arena)
{
    // IMPORTANT: These are always the top of the program memory
    DemoState = PushStruct(Arena, demo_state);
    RenderState = PushStruct(Arena, render_state);
    ProfilerState = PushStruct(Arena, profiler_state);
}

DEMO_INIT(Init)
{
    // NOTE: Init Memory
    {
        linear_arena Arena = LinearArenaCreate(ProgramMemory, ProgramMemorySize);
        DemoAllocGlobals(&Arena);
        *DemoState = {};
        *RenderState = {};
        *ProfilerState = {};
        DemoState->Arena = Arena;
        DemoState->TempArena = LinearSubArena(&DemoState->Arena, MegaBytes(10));
    }

    ProfilerStateCreate(ProfilerFlag_OutputCsv | ProfilerFlag_AutoSetEndOfFrame);

    // NOTE: Init Vulkan
    {
        {
            const char* DeviceExtensions[] =
                {
                    "VK_EXT_shader_viewport_index_layer",
                };
            
            render_init_params InitParams = {};
            InitParams.ValidationEnabled = true;
            InitParams.WindowWidth = WindowWidth;
            InitParams.WindowHeight = WindowHeight;
            InitParams.GpuLocalSize = MegaBytes(10);
            InitParams.DeviceExtensionCount = ArrayCount(DeviceExtensions);
            InitParams.DeviceExtensions = DeviceExtensions;
            VkInit(VulkanLib, hInstance, WindowHandle, &DemoState->Arena, &DemoState->TempArena, InitParams);
        }
    }
    
    // NOTE: Create samplers
    DemoState->PointSampler = VkSamplerCreate(RenderState->Device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, 0.0f);
    DemoState->LinearSampler = VkSamplerCreate(RenderState->Device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, 0.0f);
    DemoState->AnisoSampler = VkSamplerMipMapCreate(RenderState->Device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 16.0f,
                                                    VK_SAMPLER_MIPMAP_MODE_LINEAR, 0, 0, 5);    
        
    // NOTE: Init render target entries
    DemoState->SwapChainEntry = RenderTargetSwapChainEntryCreate(RenderState->WindowWidth, RenderState->WindowHeight,
                                                                 RenderState->SwapChainFormat);

    // NOTE: Init scene system
    {
        render_scene* Scene = &DemoState->Scene;

        Scene->Camera = CameraFpsCreate(V3(0, 0, -2), V3(0, 0, 1), false, 1.0f, 0.05f);
        //CameraSetPersp(&Scene->Camera, f32(RenderState->WindowWidth / RenderState->WindowHeight), 90.0f, 0.01f, 1000.0f);
        f32 OrthoRadius = 1.0f;
        CameraSetOrtho(&Scene->Camera, -OrthoRadius, OrthoRadius, OrthoRadius, -OrthoRadius, 0.01f, 1000.0f);

        Scene->SceneBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                            sizeof(scene_globals));
        
        Scene->MaxNumPointLights = 1000;
        Scene->PointLights = PushArray(&DemoState->Arena, point_light, Scene->MaxNumPointLights);
        Scene->PointLightBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                 sizeof(point_light)*Scene->MaxNumPointLights);
        Scene->PointLightTransforms = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                     sizeof(m4)*Scene->MaxNumPointLights);

        Scene->DirectionalLightBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                       sizeof(directional_light));
        
        Scene->MaxNumRenderMeshes = 1000;
        Scene->RenderMeshes = PushArray(&DemoState->Arena, render_mesh, Scene->MaxNumRenderMeshes);

        Scene->MaxNumOpaqueInstances = 50000;
        Scene->OpaqueInstances = PushArray(&DemoState->Arena, instance_entry, Scene->MaxNumOpaqueInstances);
        Scene->OpaqueInstanceBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                     sizeof(gpu_instance_entry)*Scene->MaxNumOpaqueInstances);

        // NOTE: Create general descriptor set layouts
        {
            {
                vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&Scene->MaterialDescLayout);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
                VkDescriptorLayoutEnd(RenderState->Device, &Builder);
            }

            {
                vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&Scene->SceneDescLayout);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutEnd(RenderState->Device, &Builder);
            }
        }

        // NOTE: Populate descriptors
        Scene->SceneDescriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, Scene->SceneDescLayout);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, Scene->SceneDescriptor, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, Scene->SceneBuffer);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, Scene->SceneDescriptor, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Scene->OpaqueInstanceBuffer);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, Scene->SceneDescriptor, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Scene->PointLightBuffer);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, Scene->SceneDescriptor, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Scene->DirectionalLightBuffer);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, Scene->SceneDescriptor, 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Scene->PointLightTransforms);
    }

    // NOTE: Create render data
    {
        u32 Width = RenderState->WindowWidth;
        u32 Height = RenderState->WindowHeight;
        
        DemoState->RenderTargetArena = VkLinearArenaCreate(RenderState->Device, RenderState->LocalMemoryId, MegaBytes(100));
        DemoSwapChainChange(Width, Height);

        // NOTE: Forward Pass
        {
            render_target_builder Builder = RenderTargetBuilderBegin(&DemoState->Arena, &DemoState->TempArena, Width, Height);
            RenderTargetAddTarget(&Builder, &DemoState->SwapChainEntry, VkClearColorCreate(0, 0, 0, 1));
            RenderTargetAddTarget(&Builder, &DemoState->DepthEntry, VkClearDepthStencilCreate(0, 0));
            
            vk_render_pass_builder RpBuilder = VkRenderPassBuilderBegin(&DemoState->TempArena);
            u32 ColorId = VkRenderPassAttachmentAdd(&RpBuilder, RenderState->SwapChainFormat, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                    VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            u32 DepthId = VkRenderPassAttachmentAdd(&RpBuilder, DemoState->DepthEntry.Format, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                    VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

            VkRenderPassSubPassBegin(&RpBuilder, VK_PIPELINE_BIND_POINT_GRAPHICS);
            VkRenderPassColorRefAdd(&RpBuilder, ColorId, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            VkRenderPassDepthRefAdd(&RpBuilder, DepthId, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            VkRenderPassSubPassEnd(&RpBuilder);

            DemoState->RenderTarget = RenderTargetBuilderEnd(&Builder, VkRenderPassBuilderEnd(&RpBuilder, RenderState->Device));
        }
                
        // NOTE: Create PSO
        {
            vk_pipeline_builder Builder = VkPipelineBuilderBegin(&DemoState->TempArena);

            // NOTE: Shaders
            VkPipelineShaderAdd(&Builder, "shader_forward_vert.spv", "main", VK_SHADER_STAGE_VERTEX_BIT);
            VkPipelineShaderAdd(&Builder, "shader_forward_frag.spv", "main", VK_SHADER_STAGE_FRAGMENT_BIT);
                
            // NOTE: Specify input vertex data format
            VkPipelineVertexBindingBegin(&Builder);
            VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32B32_SFLOAT, sizeof(v3));
            VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32B32_SFLOAT, sizeof(v3));
            VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32_SFLOAT, sizeof(v2));
            VkPipelineVertexBindingEnd(&Builder);

            VkPipelineInputAssemblyAdd(&Builder, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE);
            VkPipelineDepthStateAdd(&Builder, VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER);
            
            // NOTE: Set the blending state
            VkPipelineColorAttachmentAdd(&Builder, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                         VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);

            VkDescriptorSetLayout DescriptorLayouts[] =
                {
                    DemoState->Scene.MaterialDescLayout,
                    DemoState->Scene.SceneDescLayout,
                };
            
            DemoState->RenderPipeline = VkPipelineBuilderEnd(&Builder, RenderState->Device, &RenderState->PipelineManager,
                                                             DemoState->RenderTarget.RenderPass, 0, DescriptorLayouts,
                                                             ArrayCount(DescriptorLayouts));
        }
                
        // NOTE: Create Debug Point PSO
        {
            vk_pipeline_builder Builder = VkPipelineBuilderBegin(&DemoState->TempArena);

            // NOTE: Shaders
            VkPipelineShaderAdd(&Builder, "shader_debug_point_vert.spv", "main", VK_SHADER_STAGE_VERTEX_BIT);
            VkPipelineShaderAdd(&Builder, "shader_debug_frag.spv", "main", VK_SHADER_STAGE_FRAGMENT_BIT);
                
            // NOTE: Specify input vertex data format
            VkPipelineVertexBindingBegin(&Builder);
            VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32B32_SFLOAT, sizeof(v3));
            VkPipelineVertexBindingEnd(&Builder);

            VkPipelineVertexBindingBegin(&Builder);
            VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(v4));
            VkPipelineVertexBindingEnd(&Builder);

            VkPipelineInputAssemblyAdd(&Builder, VK_PRIMITIVE_TOPOLOGY_POINT_LIST, VK_FALSE);
            VkPipelineDepthStateAdd(&Builder, VK_FALSE, VK_FALSE, VK_COMPARE_OP_GREATER);
            
            // NOTE: Set the blending state
            VkPipelineColorAttachmentAdd(&Builder, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                         VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);

            VkDescriptorSetLayout DescriptorLayouts[] =
                {
                    DemoState->Scene.SceneDescLayout,
                };
            
            DemoState->PointPipeline = VkPipelineBuilderEnd(&Builder, RenderState->Device, &RenderState->PipelineManager,
                                                            DemoState->RenderTarget.RenderPass, 0, DescriptorLayouts,
                                                            ArrayCount(DescriptorLayouts));
        }
                
        // NOTE: Create Debug Line PSO
        {
            vk_pipeline_builder Builder = VkPipelineBuilderBegin(&DemoState->TempArena);

            // NOTE: Shaders
            VkPipelineShaderAdd(&Builder, "shader_debug_vert.spv", "main", VK_SHADER_STAGE_VERTEX_BIT);
            VkPipelineShaderAdd(&Builder, "shader_debug_frag.spv", "main", VK_SHADER_STAGE_FRAGMENT_BIT);
                
            // NOTE: Specify input vertex data format
            VkPipelineVertexBindingBegin(&Builder);
            VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32B32_SFLOAT, sizeof(v3));
            VkPipelineVertexBindingEnd(&Builder);

            VkPipelineVertexBindingBegin(&Builder);
            VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(v4));
            VkPipelineVertexBindingEnd(&Builder);

            VkPipelineInputAssemblyAdd(&Builder, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_FALSE);
            VkPipelineDepthStateAdd(&Builder, VK_FALSE, VK_FALSE, VK_COMPARE_OP_GREATER);
            
            // NOTE: Set the blending state
            VkPipelineColorAttachmentAdd(&Builder, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                         VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);

            VkDescriptorSetLayout DescriptorLayouts[] =
                {
                    DemoState->Scene.SceneDescLayout,
                };
            
            DemoState->LinePipeline = VkPipelineBuilderEnd(&Builder, RenderState->Device, &RenderState->PipelineManager,
                                                           DemoState->RenderTarget.RenderPass, 0, DescriptorLayouts,
                                                           ArrayCount(DescriptorLayouts));
        }
                
        // NOTE: Create Debug Triangle PSO
        {
            vk_pipeline_builder Builder = VkPipelineBuilderBegin(&DemoState->TempArena);

            // NOTE: Shaders
            VkPipelineShaderAdd(&Builder, "shader_debug_vert.spv", "main", VK_SHADER_STAGE_VERTEX_BIT);
            VkPipelineShaderAdd(&Builder, "shader_debug_frag.spv", "main", VK_SHADER_STAGE_FRAGMENT_BIT);
                
            // NOTE: Specify input vertex data format
            VkPipelineVertexBindingBegin(&Builder);
            VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32B32_SFLOAT, sizeof(v3));
            VkPipelineVertexBindingEnd(&Builder);

            VkPipelineVertexBindingBegin(&Builder);
            VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(v4));
            VkPipelineVertexBindingEnd(&Builder);

            VkPipelineInputAssemblyAdd(&Builder, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE);
            VkPipelineDepthStateAdd(&Builder, VK_FALSE, VK_FALSE, VK_COMPARE_OP_GREATER);
            
            // NOTE: Set the blending state
            VkPipelineColorAttachmentAdd(&Builder, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                         VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);

            VkDescriptorSetLayout DescriptorLayouts[] =
                {
                    DemoState->Scene.SceneDescLayout,
                };
            
            DemoState->TrianglePipeline = VkPipelineBuilderEnd(&Builder, RenderState->Device, &RenderState->PipelineManager,
                                                               DemoState->RenderTarget.RenderPass, 0, DescriptorLayouts,
                                                               ArrayCount(DescriptorLayouts));
        }
    }
    
    // NOTE: Init Entities
    {
        DemoState->TerrainRadius = 2.0f;
        DemoState->PlatformBlockArena = PlatformBlockArenaCreate(KiloBytes(256), 64);

        DemoState->NumEntities = 10;
        DemoState->CurrEntities = PushArray(&DemoState->Arena, entity, DemoState->NumEntities);
        DemoState->PrevEntities = PushArray(&DemoState->Arena, entity, DemoState->NumEntities);

        ///*
        for (u32 EntityId = 0; EntityId < DemoState->NumEntities; ++EntityId)
        {
            entity* CurrEntity = DemoState->CurrEntities + EntityId;

            f32 Angle = 2.0f * Pi32 * f32(EntityId) / f32(DemoState->NumEntities);
            v2 Radius = 1.3f*V2(Cos(Angle), Sin(Angle));

            CurrEntity->TargetPos = -Radius;
            CurrEntity->Pos = Radius;
            CurrEntity->Speed = 0.2f; //0.1f*RandFloat() + 0.1f;
            CurrEntity->CollisionRadius = 0.11f;
            CurrEntity->Vel = CurrEntity->Speed * Normalize(CurrEntity->TargetPos - CurrEntity->Pos);
        }
        //*/

        /*
        {
            entity* CurrEntity = DemoState->PrevEntities + 0;
            CurrEntity->TargetPos = V2(0, -1);
            CurrEntity->Pos = V2(0, 1);
            CurrEntity->Speed = 0.2f; //0.2f*RandFloat() + 0.1f;
            CurrEntity->CollisionRadius = 0.11f;
            CurrEntity->Vel = CurrEntity->Speed * (1.0f / 60.0f) * Normalize(CurrEntity->TargetPos - CurrEntity->Pos);
        }

        {
            entity* CurrEntity = DemoState->PrevEntities + 1;
            CurrEntity->TargetPos = V2(0, 1);
            CurrEntity->Pos = V2(0, -1);
            CurrEntity->Speed = 0.2f; //0.2f*RandFloat() + 0.1f;
            CurrEntity->CollisionRadius = 0.11f;
            CurrEntity->Vel = CurrEntity->Speed * (1.0f / 60.0f) * Normalize(CurrEntity->TargetPos - CurrEntity->Pos);
        }
        //*/        
    }
    
    // NOTE: Upload assets
    vk_commands* Commands = &RenderState->Commands;
    VkCommandsBegin(Commands, RenderState->Device);
    {
        render_scene* Scene = &DemoState->Scene;
        
        // NOTE: Push textures
        vk_image WhiteTexture = {};
        {
            u32 Texels[] =
            {
                0xFFFFFFFF, 
            };
            u32 Dim = 1;
            
            u32 ImageSize = Dim*Dim*sizeof(u32);
            WhiteTexture = VkImageCreate(RenderState->Device, &RenderState->GpuArena, Dim, Dim, VK_FORMAT_R8G8B8A8_UNORM,
                                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

            u8* GpuMemory = VkCommandsPushWriteImage(Commands, WhiteTexture.Image, Dim, Dim, ImageSize,
                                                     VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                     BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                     BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));

            Copy(Texels, GpuMemory, ImageSize);
        }
                        
        // NOTE: Push meshes
        DemoState->Quad = SceneMeshAdd(Scene, WhiteTexture, WhiteTexture, AssetsPushQuad());
        DemoState->Cube = SceneMeshAdd(Scene, WhiteTexture, WhiteTexture, AssetsPushCube());
        DemoState->Sphere = SceneMeshAdd(Scene, WhiteTexture, WhiteTexture, AssetsPushSphere(64, 64));

        UiStateCreate(RenderState->Device, &DemoState->Arena, &DemoState->TempArena, RenderState->LocalMemoryId,
                      &RenderState->DescriptorManager, &RenderState->PipelineManager, &RenderState->Commands,
                      RenderState->SwapChainFormat, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, &DemoState->UiState);
    }

    VkDescriptorManagerFlush(RenderState->Device, &RenderState->DescriptorManager);
    VkCommandsSubmit(Commands, RenderState->Device, RenderState->GraphicsQueue);
}

DEMO_DESTROY(Destroy)
{
    // TODO: Remove if we can verify that this is auto destroyed (check recompiling if it calls the destructor)
    ProfilerStateDestroy();
}

DEMO_SWAPCHAIN_CHANGE(SwapChainChange)
{
    VkCheckResult(vkDeviceWaitIdle(RenderState->Device));
    VkSwapChainReCreate(&DemoState->TempArena, WindowWidth, WindowHeight, RenderState->PresentMode);
    
    DemoState->SwapChainEntry.Width = RenderState->WindowWidth;
    DemoState->SwapChainEntry.Height = RenderState->WindowHeight;
    DemoState->Scene.Camera.PerspAspectRatio = f32(RenderState->WindowWidth / RenderState->WindowHeight);
    DemoSwapChainChange(RenderState->WindowWidth, RenderState->WindowHeight);
}

DEMO_CODE_RELOAD(CodeReload)
{
    linear_arena Arena = LinearArenaCreate(ProgramMemory, ProgramMemorySize);
    // IMPORTANT: We are relying on the memory being the same here since we have the same base ptr with the VirtualAlloc so we just need
    // to patch our global pointers here
    DemoAllocGlobals(&Arena);

    VkGetGlobalFunctionPointers(VulkanLib);
    VkGetInstanceFunctionPointers();
    VkGetDeviceFunctionPointers();
}

DEMO_MAIN_LOOP(MainLoop)
{
    {
        CPU_TIMED_BLOCK("MainLoop");
    
        u32 ImageIndex;
        VkCheckResult(vkAcquireNextImageKHR(RenderState->Device, RenderState->SwapChain, UINT64_MAX, RenderState->ImageAvailableSemaphore,
                                            VK_NULL_HANDLE, &ImageIndex));
        DemoState->SwapChainEntry.View = RenderState->SwapChainViews[ImageIndex];

        vk_commands* Commands = &RenderState->Commands;
        VkCommandsBegin(Commands, RenderState->Device);

        // NOTE: Update pipelines
        VkPipelineUpdateShaders(RenderState->Device, &RenderState->CpuArena, &RenderState->PipelineManager);

        RenderTargetUpdateEntries(&DemoState->TempArena, &DemoState->RenderTarget);
    
        // NOTE: Update Ui State
        local_global f32 ModifiedFrameTime = 1.0f / 60.0f;
        {
            ui_state* UiState = &DemoState->UiState;
        
            ui_frame_input UiCurrInput = {};
            UiCurrInput.MouseDown = CurrInput->MouseDown;
            UiCurrInput.MousePixelPos = V2(CurrInput->MousePixelPos);
            UiCurrInput.MouseScroll = CurrInput->MouseScroll;
            Copy(CurrInput->KeysDown, UiCurrInput.KeysDown, sizeof(UiCurrInput.KeysDown));
            UiStateBegin(UiState, FrameTime, RenderState->WindowWidth, RenderState->WindowHeight, UiCurrInput);
            local_global v2 PanelPos = V2(100, 800);
            ui_panel Panel = UiPanelBegin(UiState, &PanelPos, "ClearPath Panel");

            {
                UiPanelText(&Panel, "Sim Data:");

                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "FrameTime:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 0.03f, &ModifiedFrameTime);
                UiPanelNumberBox(&Panel, 0.0f, 0.03f, &ModifiedFrameTime);
                UiPanelNextRow(&Panel);            
            }

            UiPanelEnd(&Panel);

            UiStateEnd(UiState, &RenderState->DescriptorManager);
        }

        // NOTE: Upload scene data
        {
            render_scene* Scene = &DemoState->Scene;
            Scene->NumOpaqueInstances = 0;
            Scene->NumPointLights = 0;
            if (!(DemoState->UiState.MouseTouchingUi || DemoState->UiState.ProcessedInteraction))
            {
                CameraUpdate(&Scene->Camera, CurrInput, PrevInput);
            }

            // TODO: REMOVE
            local_global u32 FrameId = 0;
            FrameId += 1;
            
            // NOTE: Populate scene
            {
                // NOTE: Add Instances
                {
                    temp_mem TempMem = BeginTempMem(&DemoState->TempArena);
                
                    // NOTE: Terrain
                    SceneOpaqueInstanceAdd(Scene, DemoState->Quad, M4Pos(V3(0.0f, 0.0f, 0.1f)) * M4Scale(V3(2.0f*DemoState->TerrainRadius)), V4(0.7f, 0.4f, 0.4f, 1.0f));

                    // NOTE: Swap prev and current entities
                    {
                        entity* Temp = DemoState->PrevEntities;
                        DemoState->PrevEntities = DemoState->CurrEntities;
                        DemoState->CurrEntities = Temp;
                    }

#if DEBUG_DRAW

                    DemoState->MaxNumPointVerts = 10000;
                    DemoState->NumPointVerts = 0;
                    DemoState->PointPos = PushArray(&DemoState->TempArena, v3, DemoState->MaxNumPointVerts);
                    DemoState->PointColors = PushArray(&DemoState->TempArena, v4, DemoState->MaxNumPointVerts);
                    if (DemoState->PointBuffer == VK_NULL_HANDLE)
                    {
                        DemoState->PointBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                               (sizeof(v3) + sizeof(v4)) * DemoState->MaxNumPointVerts);
                    }

                    DemoState->MaxNumLineVerts = 1000;
                    DemoState->NumLineVerts = 0;
                    DemoState->LinePos = PushArray(&DemoState->TempArena, v3, DemoState->MaxNumLineVerts);
                    DemoState->LineColors = PushArray(&DemoState->TempArena, v4, DemoState->MaxNumLineVerts);
                    if (DemoState->LineBuffer == VK_NULL_HANDLE)
                    {
                        DemoState->LineBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                               (sizeof(v3) + sizeof(v4)) * DemoState->MaxNumLineVerts);
                    }
                    
                    DemoState->MaxNumTriangleVerts = 1000;
                    DemoState->NumTriangleVerts = 0;
                    DemoState->TrianglePos = PushArray(&DemoState->TempArena, v3, DemoState->MaxNumTriangleVerts);
                    DemoState->TriangleColors = PushArray(&DemoState->TempArena, v4, DemoState->MaxNumTriangleVerts);
                    if (DemoState->TriangleBuffer == VK_NULL_HANDLE)
                    {
                        DemoState->TriangleBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                   (sizeof(v3) + sizeof(v4)) * DemoState->MaxNumTriangleVerts);
                    }
                    
#endif

                    // TODO: REMOVE
                    //ModifiedFrameTime = 0.0f;
                    
                    // NOTE: Update entities
                    {
                        CPU_TIMED_BLOCK("Update Entities");

                        // NOTE: Calculate preferred vels
                        for (u32 CurrEntityId = 0; CurrEntityId < DemoState->NumEntities; ++CurrEntityId)
                        {
                            entity* ReadEntity = DemoState->PrevEntities + CurrEntityId;
                            
                            // NOTE: Find preferred vel
                            v2 TargetVec = ReadEntity->TargetPos - ReadEntity->Pos;
                            f32 DistToTarget = Length(TargetVec);
                            if (DistToTarget <= ReadEntity->Speed)
                            {
                                ReadEntity->PreferredVel = TargetVec;
                            }
                            else
                            {
                                ReadEntity->PreferredVel = ReadEntity->Speed * Normalize(TargetVec);
                            }
                        }
                        
                        // NOTE: Loop through each entity and generate VOs/HRVOs
                        for (u32 CurrEntityId = 0; CurrEntityId < DemoState->NumEntities; ++CurrEntityId)
                        {
                            temp_mem EntityTempMem = BeginTempMem(&DemoState->TempArena);

                            entity* ReadEntity = DemoState->PrevEntities + CurrEntityId;
                            entity* WriteEntity = DemoState->CurrEntities + CurrEntityId;

                            DebugPushLine(ReadEntity->Pos, ReadEntity->TargetPos, V4(0, 0, 1, 1), CurrEntityId);
                            
                            // TODO: Add HRVO instead of RVO
                            // NOTE: Cones are stored in world space
                            cone_array ConeArray = {};
                            ConeArray.RvoArray = PushArray(&DemoState->TempArena, cone, Square(DemoState->NumEntities));
                            for (u32 OtherEntityId = 0; OtherEntityId < DemoState->NumEntities; ++OtherEntityId)
                            {
                                if (OtherEntityId == CurrEntityId)
                                {
                                    continue;
                                }

                                entity* OtherEntity = DemoState->PrevEntities + OtherEntityId;

                                f32 CreateConeDist = 2.0f*(ReadEntity->CollisionRadius + OtherEntity->CollisionRadius +
                                                      ReadEntity->Speed + OtherEntity->Speed);
                                f32 ActualDist = LengthSquared(ReadEntity->Pos - OtherEntity->Pos);
                                if (Square(CreateConeDist) < ActualDist)
                                {
                                    //continue;
                                }
                                
                                // TODO: Support HRVO as well as VO for static objects
                                // NOTE: Create RVO
                                cone Cone = {};

                                /*
                                f32 UncertaintyOffset = 0.001f;
                                if (LengthSquared(OtherEntity->Pos - ReadEntity->Pos) > Square(OtherEntity->CollisionRadius + ReadEntity->CollisionRadius)) {
                                    const float angle = std::atan2(OtherEntity->Pos.y - ReadEntity->Pos.y, OtherEntity->Pos.x - ReadEntity->Pos.x);
                                    const float openingAngle = std::asin((OtherEntity->CollisionRadius + ReadEntity->CollisionRadius) / Length(OtherEntity->Pos - ReadEntity->Pos));

                                    Cone.Left = V2(std::cos(angle - openingAngle), std::sin(angle - openingAngle));
                                    Cone.Right = V2(std::cos(angle + openingAngle), std::sin(angle + openingAngle));

                                    const float d = 2.0f * std::sin(openingAngle) * std::cos(openingAngle);

                                    if (Determinant(OtherEntity->Pos - ReadEntity->Pos, ReadEntity->PreferredVel - OtherEntity->PreferredVel) > 0.0f) {
                                        const float s = 0.5f * Determinant(ReadEntity->Vel - OtherEntity->Vel, Cone.Right) / d;

                                        Cone.Apex = OtherEntity->Vel + s * Cone.Left - (UncertaintyOffset * Length(OtherEntity->Pos - ReadEntity->Pos) / (OtherEntity->CollisionRadius + ReadEntity->CollisionRadius)) * Normalize(OtherEntity->Pos - ReadEntity->Pos);
                                    }
                                    else {
                                        const float s = 0.5f * Determinant(ReadEntity->Vel - OtherEntity->Vel, Cone.Left) / d;

                                        Cone.Apex = OtherEntity->Vel + s * Cone.Right - (UncertaintyOffset * Length(OtherEntity->Pos - ReadEntity->Pos) / (OtherEntity->CollisionRadius + ReadEntity->CollisionRadius)) * Normalize(OtherEntity->Pos - ReadEntity->Pos);
                                    }
                                }
                                else {
                                    Cone.Apex = 0.5f * (OtherEntity->Vel + ReadEntity->Vel) - (UncertaintyOffset + 0.5f * (OtherEntity->CollisionRadius + ReadEntity->CollisionRadius - Length(OtherEntity->Pos - ReadEntity->Pos)) / ModifiedFrameTime) * Normalize(OtherEntity->Pos - ReadEntity->Pos);
                                    Cone.Left = Normalize(OtherEntity->Pos - ReadEntity->Pos);
                                    Cone.Right = -Cone.Left;
                                }
                                */
                                
#if 1
#if 1

                                /*
                                  v2 DistVec = OtherEntity->Pos - ReadEntity->Pos;
                                  v2 SideVec = ((ReadEntity->CollisionRadius + OtherEntity->CollisionRadius) *
                                  Normalize(V2(-DistVec.y, DistVec.x)));

                                  // IMPORTANT: Cones are defined in entity space, not world space
                                  Cone.Left = Normalize(DistVec - SideVec);
                                  Cone.Right = Normalize(DistVec + SideVec);
                                //*/

                                ///*
                                f32 RadiusEpsilon = 0.1f;
                                f32 Angle = std::atan2(OtherEntity->Pos.y - ReadEntity->Pos.y, OtherEntity->Pos.x - ReadEntity->Pos.x);
                                f32 OpeningAngle = std::asin((2.0f * RadiusEpsilon + OtherEntity->CollisionRadius + ReadEntity->CollisionRadius) /
                                                             Length(OtherEntity->Pos - ReadEntity->Pos));

                                f32 Test = Length(OtherEntity->Pos - ReadEntity->Pos);
                                
                                Cone.Left = V2(std::cos(Angle - OpeningAngle), std::sin(Angle - OpeningAngle));
                                Cone.Right = V2(std::cos(Angle + OpeningAngle), std::sin(Angle + OpeningAngle));
                                //*/
                                
                                Cone.Apex = 0.5f * (ReadEntity->Vel + OtherEntity->Vel);

                                //DebugPushLine(ReadEntity->Pos, OtherEntity->Pos, V4(0, 0, 1, 1), CurrEntityId);
                                //DebugPushLine(OtherEntity->Pos, ReadEntity->Pos + DistVec + SideVec, V4(0, 0, 1, 1), CurrEntityId);
                                //DebugPushLine(OtherEntity->Pos, ReadEntity->Pos + DistVec - SideVec, V4(0, 0, 1, 1), CurrEntityId);
                                
#else

                                v2 TargetVec = Normalize(OtherEntity->Pos - ReadEntity->Pos);
                                v2 Right = V2(-TargetVec.y, TargetVec.x);
                                Right = Right * (ReadEntity->CollisionRadius + OtherEntity->CollisionRadius);

                                v2 LeftTangent = OtherEntity->Pos - Right;
                                v2 RightTangent = OtherEntity->Pos + Right;
                                
                                Cone.Left = Normalize(LeftTangent - ReadEntity->Pos);
                                Cone.Right = Normalize(RightTangent - ReadEntity->Pos);
                                Cone.Apex = 0.5f * (ReadEntity->Vel + OtherEntity->Vel);
#endif
#endif
                                    
                                f32 ChannelColor = 1.0f / f32(DemoState->NumEntities);
                                v4 Color = V4(ChannelColor, ChannelColor, ChannelColor, 2.0f*ChannelColor);

                                //if (OtherEntityId == 2)
                                {
                                    DebugPushTriangleEntity(Cone.Apex, Cone.Apex + 2.0f*Cone.Left, Cone.Apex + 2.0f*Cone.Right, Color, CurrEntityId);
                                }

                                ConeArray.RvoArray[ConeArray.NumRvos++] = Cone;
                            }

                            // NOTE: Find preferred vel
                            v2 PreferredVel = {}; // NOTE: This is in m/s
                            {
                                v2 TargetVec = ReadEntity->TargetPos - ReadEntity->Pos;
                                f32 DistToTarget = Length(TargetVec);
                                if (DistToTarget <= ReadEntity->Speed)
                                {
                                    PreferredVel = TargetVec;
                                }
                                else
                                {
                                    PreferredVel = ReadEntity->Speed * Normalize(TargetVec);
                                }
                            }
                            
                            // NOTE: Check if we can just use preferred vel
                            candidate Candidate = {};
                            Candidate.DistanceSq = F32_MAX;
                            UpdateCandidate(ConeArray, &Candidate, PreferredVel, PreferredVel, ReadEntity->Speed, CurrEntityId, true);

                            if (Candidate.DistanceSq == F32_MAX)
                            {
                                // NOTE: Project vel onto cones and see if we find a non colliding velocity
                                for (u32 RvoId = 0; RvoId < ConeArray.NumRvos; ++RvoId)
                                {
                                    cone* CurrCone = ConeArray.RvoArray + RvoId;
                                    // NOTE: Project on left side
                                    {
                                        v2 ConeSpaceVel = PreferredVel - CurrCone->Apex;
                                        f32 ProjectedLen = Dot(ConeSpaceVel, CurrCone->Left);

                                        // TODO: The determinant culls velocities which are inside the cone already?
                                        if (ProjectedLen >= 0.0f && Determinant(CurrCone->Left, ConeSpaceVel) > 0.0f)
                                        {
                                            v2 TargetVel = CurrCone->Left * ProjectedLen + CurrCone->Apex;
                                            UpdateCandidate(ConeArray, &Candidate, TargetVel, PreferredVel, ReadEntity->Speed,
                                                            CurrEntityId);
                                        }
                                    }

                                    // NOTE: Project on right side
                                    {
                                        v2 ConeSpaceVel = PreferredVel - CurrCone->Apex;
                                        f32 ProjectedLen = Dot(ConeSpaceVel, CurrCone->Right);

                                        // TODO: The determinant culls velocities which are inside the cone already?
                                        if (ProjectedLen >= 0.0f && Determinant(CurrCone->Right, ConeSpaceVel) > 0.0f)
                                        {
                                            v2 TargetVel = CurrCone->Right * ProjectedLen + CurrCone->Apex;
                                            UpdateCandidate(ConeArray, &Candidate, TargetVel, PreferredVel, ReadEntity->Speed,
                                                            CurrEntityId);
                                        }
                                    }
                                }

                                for (u32 RvoId = 0; RvoId < ConeArray.NumRvos; ++RvoId)
                                {
                                    cone* CurrCone = ConeArray.RvoArray + RvoId;

                                    {
                                        f32 Discriminant = Square(ReadEntity->Speed) - Square(Determinant(CurrCone->Apex, CurrCone->Left));
                                        if (Discriminant > 0.0f)
                                        {
                                            f32 t1 = -Dot(CurrCone->Apex, CurrCone->Left) + SquareRoot(Discriminant);
                                            f32 t2 = -Dot(CurrCone->Apex, CurrCone->Left) - SquareRoot(Discriminant);

                                            if (t1 >= 0.0f)
                                            {
                                                v2 TargetVel = CurrCone->Apex + t1 * CurrCone->Left;
                                                UpdateCandidate(ConeArray, &Candidate, TargetVel, PreferredVel, ReadEntity->Speed, CurrEntityId);
                                            }

                                            if (t2 >= 0.0f)
                                            {
                                                v2 TargetVel = CurrCone->Apex + t2 * CurrCone->Left;
                                                UpdateCandidate(ConeArray, &Candidate, TargetVel, PreferredVel, ReadEntity->Speed, CurrEntityId);
                                            }
                                        }
                                    }

                                    {
                                        f32 Discriminant = Square(ReadEntity->Speed) - Square(Determinant(CurrCone->Apex, CurrCone->Right));
                                        if (Discriminant > 0.0f)
                                        {
                                            f32 t1 = -Dot(CurrCone->Apex, CurrCone->Right) + SquareRoot(Discriminant);
                                            f32 t2 = -Dot(CurrCone->Apex, CurrCone->Right) - SquareRoot(Discriminant);

                                            if (t1 >= 0.0f)
                                            {
                                                v2 TargetVel = CurrCone->Apex + t1 * CurrCone->Right;
                                                UpdateCandidate(ConeArray, &Candidate, TargetVel, PreferredVel, ReadEntity->Speed, CurrEntityId);
                                            }

                                            if (t2 >= 0.0f)
                                            {
                                                v2 TargetVel = CurrCone->Apex + t2 * CurrCone->Right;
                                                UpdateCandidate(ConeArray, &Candidate, TargetVel, PreferredVel, ReadEntity->Speed, CurrEntityId);
                                            }
                                        }
                                    }
                                }
                                
                                // NOTE: Also check if the intersection points between all the cones is a closer velocity
                                for (u32 RvoId = 0; RvoId < ConeArray.NumRvos; ++RvoId)
                                {
                                    for (u32 OtherRvoId = 0; OtherRvoId < ConeArray.NumRvos; ++OtherRvoId)
                                    {
                                        if (RvoId == OtherRvoId)
                                        {
                                            continue;
                                        }
                                        
                                        cone* Cone = ConeArray.RvoArray + RvoId;
                                        cone* OtherCone = ConeArray.RvoArray + OtherRvoId;

                                        f32 d = Determinant(Cone->Left, OtherCone->Left);

                                        if (d != 0.0f)
                                        {
                                            const float s = Determinant(OtherCone->Apex - Cone->Apex, OtherCone->Left) / d;
                                            const float t = Determinant(OtherCone->Apex - Cone->Apex, Cone->Left) / d;

                                            if (s >= 0.0f && t >= 0.0f) {
                                                v2 TargetVel = Cone->Apex + s * Cone->Left;
                                                UpdateCandidate(ConeArray, &Candidate, TargetVel, PreferredVel, ReadEntity->Speed, CurrEntityId);
                                            }
                                        }

                                        d = Determinant(Cone->Right, OtherCone->Left);

                                        if (d != 0.0f) {
                                            const float s = Determinant(OtherCone->Apex - Cone->Apex, OtherCone->Left) / d;
                                            const float t = Determinant(OtherCone->Apex - Cone->Apex, Cone->Right) / d;

                                            if (s >= 0.0f && t >= 0.0f) {
                                                v2 TargetVel = Cone->Apex + s * Cone->Right;
                                                UpdateCandidate(ConeArray, &Candidate, TargetVel, PreferredVel, ReadEntity->Speed, CurrEntityId);
                                            }
                                        }

                                        d = Determinant(Cone->Left, OtherCone->Right);

                                        if (d != 0.0f) {
                                            const float s = Determinant(OtherCone->Apex - Cone->Apex, OtherCone->Right) / d;
                                            const float t = Determinant(OtherCone->Apex - Cone->Apex, Cone->Left) / d;

                                            if (s >= 0.0f && t >= 0.0f) {
                                                v2 TargetVel = Cone->Apex + s * Cone->Left;
                                                UpdateCandidate(ConeArray, &Candidate, TargetVel, PreferredVel, ReadEntity->Speed, CurrEntityId);
                                            }
                                        }

                                        d = Determinant(Cone->Right, OtherCone->Right);

                                        if (d != 0.0f) {
                                            const float s = Determinant(OtherCone->Apex - Cone->Apex, OtherCone->Right) / d;
                                            const float t = Determinant(OtherCone->Apex - Cone->Apex, Cone->Right) / d;

                                            if (s >= 0.0f && t >= 0.0f) {
                                                v2 TargetVel = Cone->Apex + s * Cone->Right;
                                                UpdateCandidate(ConeArray, &Candidate, TargetVel, PreferredVel, ReadEntity->Speed, CurrEntityId);
                                            }
                                        }
                                        
#if 0
                                        
                                        // NOTE: Intersect the lines and generate points to test against
                                        {
                                            v2 TargetVel;
                                            if (RaysIntersect(Cone->Apex, Cone->Left, OtherCone->Apex, OtherCone->Left, &TargetVel))
                                            {
                                                UpdateCandidate(ConeArray, &Candidate, TargetVel, PreferredVel, ReadEntity->Speed,
                                                                CurrEntityId);
                                            }
                                        }

                                        {
                                            v2 TargetVel;
                                            if (RaysIntersect(Cone->Apex, Cone->Right, OtherCone->Apex, OtherCone->Left, &TargetVel))
                                            {
                                                UpdateCandidate(ConeArray, &Candidate, TargetVel, PreferredVel, ReadEntity->Speed,
                                                                CurrEntityId);
                                            }
                                        }                                        

                                        {
                                            v2 TargetVel;
                                            if (RaysIntersect(Cone->Apex, Cone->Left, OtherCone->Apex, OtherCone->Right, &TargetVel))
                                            {
                                                UpdateCandidate(ConeArray, &Candidate, TargetVel, PreferredVel, ReadEntity->Speed,
                                                                CurrEntityId);
                                            }
                                        }                                        

                                        {
                                            v2 TargetVel;
                                            if (RaysIntersect(Cone->Apex, Cone->Right, OtherCone->Apex, OtherCone->Right, &TargetVel))
                                            {
                                                UpdateCandidate(ConeArray, &Candidate, TargetVel, PreferredVel, ReadEntity->Speed,
                                                                CurrEntityId);
                                            }
                                        }
#endif
                                    }
                                }
                            }

                            //Assert(Candidate.DistanceSq != F32_MAX);

                            DebugPushLine(ReadEntity->Pos, ReadEntity->Pos + Candidate.Vel, V4(1, 1, 1, 1), DRAW_ENTITY); //CurrEntityId);

                            // TODO: Adjust later correctly, right now this doesn't look to fix the problem
#if 0
                            f32 MaxAccel = 1.0f;
                            f32 DeltaVel = Length(Candidate.Vel - ReadEntity->Vel);

                            if (DeltaVel < MaxAccel * ModifiedFrameTime)
                            {
                                WriteEntity->Vel = Candidate.Vel;
                            }
                            else
                            {
                                WriteEntity->Vel = (1.0f - (MaxAccel * ModifiedFrameTime / DeltaVel)) * ReadEntity->Vel + (MaxAccel * ModifiedFrameTime / DeltaVel) * Candidate.Vel;
                            }
#endif
                            
                            WriteEntity->Vel = Candidate.Vel;
                            WriteEntity->Pos = ReadEntity->Pos + WriteEntity->Vel * ModifiedFrameTime;
                            
                            // TODO: REMOVE, debugging only
                            if (ModifiedFrameTime == 0.0f)
                            {
                                WriteEntity->Vel = ReadEntity->Vel;
                                WriteEntity->Pos = ReadEntity->Pos;
                            }
                            
                            WriteEntity->TargetPos = ReadEntity->TargetPos;
                            WriteEntity->Speed = ReadEntity->Speed;
                            WriteEntity->CollisionRadius = ReadEntity->CollisionRadius;
                
                            EndTempMem(EntityTempMem);
                        }
                    }

#if DEBUG_DRAW
                    // NOTE: Upload debug points
                    {
                        u8* GpuData = VkCommandsPushWriteArray(Commands, DemoState->PointBuffer, u8, (sizeof(v3) + sizeof(v4)) * DemoState->MaxNumPointVerts,
                                                               BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                               BarrierMask(VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT));

                        u32 Offset = 0;
                        Copy(DemoState->PointPos, GpuData + Offset, sizeof(v3) * DemoState->MaxNumPointVerts);
                        Offset += sizeof(v3) * DemoState->MaxNumPointVerts;
                        Copy(DemoState->PointColors, GpuData + Offset, sizeof(v4) * DemoState->MaxNumPointVerts);
                    }

                    // NOTE: Upload debug lines
                    {
                        u8* GpuData = VkCommandsPushWriteArray(Commands, DemoState->LineBuffer, u8, (sizeof(v3) + sizeof(v4)) * DemoState->MaxNumLineVerts,
                                                               BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                               BarrierMask(VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT));

                        u32 Offset = 0;
                        Copy(DemoState->LinePos, GpuData + Offset, sizeof(v3) * DemoState->MaxNumLineVerts);
                        Offset += sizeof(v3) * DemoState->MaxNumLineVerts;
                        Copy(DemoState->LineColors, GpuData + Offset, sizeof(v4) * DemoState->MaxNumLineVerts);
                    }

                    // NOTE: Upload debug triangles
                    {
                        u8* GpuData = VkCommandsPushWriteArray(Commands, DemoState->TriangleBuffer, u8, (sizeof(v3) + sizeof(v4)) * DemoState->MaxNumTriangleVerts,
                                                               BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                               BarrierMask(VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT));

                        u32 Offset = 0;
                        Copy(DemoState->TrianglePos, GpuData + Offset, sizeof(v3) * DemoState->MaxNumTriangleVerts);
                        Offset += sizeof(v3) * DemoState->MaxNumTriangleVerts;
                        Copy(DemoState->TriangleColors, GpuData + Offset, sizeof(v4) * DemoState->MaxNumTriangleVerts);
                    }
#endif
                    
                    EndTempMem(TempMem);

                    // NOTE: Generate rendering instances
                    {
                        CPU_TIMED_BLOCK("Gen Render Instances");
                        
                        for (u32 EntityId = 0; EntityId < DemoState->NumEntities; ++EntityId)
                        {
                            entity* CurrEntity = DemoState->CurrEntities + EntityId;
                            v2 Position = CurrEntity->Pos;
                            v2 Velocity = CurrEntity->Vel;
                            f32 Angle = atan2(Velocity.y, Velocity.x);
                            f32 Scale = CurrEntity->CollisionRadius;
#if DEBUG_DRAW
                            //Scale *= 0.5f;
#endif
                            m4 Transform = M4Pos(V3(Position, 0)) * M4Rotation(0, 0, Angle) * M4Scale(V3(Scale));
                            v4 Color = V4(0.4f, 0.3f, 0.6f, 1.0f);
                            if (EntityId == DRAW_ENTITY)
                            {
                                Color = V4(1, 0, 1, 1);
                            }
                            
                            SceneOpaqueInstanceAdd(Scene, DemoState->Sphere, Transform, Color);
                        }
                    }
                }

                {
                    CPU_TIMED_BLOCK("Upload instances to GPU");
                    gpu_instance_entry* GpuData = VkCommandsPushWriteArray(Commands, Scene->OpaqueInstanceBuffer, gpu_instance_entry, Scene->NumOpaqueInstances,
                                                                           BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                                           BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT));

                    for (u32 InstanceId = 0; InstanceId < Scene->NumOpaqueInstances; ++InstanceId)
                    {
                        GpuData[InstanceId].WVTransform = Scene->OpaqueInstances[InstanceId].WVTransform;
                        GpuData[InstanceId].WVPTransform = Scene->OpaqueInstances[InstanceId].WVPTransform;
                        GpuData[InstanceId].Color = Scene->OpaqueInstances[InstanceId].Color;
                    }
                }
            
                // NOTE: Add point lights
                ScenePointLightAdd(Scene, V3(0.0f, 0.0f, -1.0f), V3(1.0f, 0.0f, 0.0f), 1);
                ScenePointLightAdd(Scene, V3(-1.0f, 0.0f, 0.0f), V3(1.0f, 1.0f, 0.0f), 1);
                ScenePointLightAdd(Scene, V3(0.0f, 1.0f, 1.0f), V3(1.0f, 0.0f, 1.0f), 1);
                ScenePointLightAdd(Scene, V3(0.0f, -1.0f, 1.0f), V3(0.0f, 1.0f, 1.0f), 1);
                ScenePointLightAdd(Scene, V3(-1.0f, 0.0f, -1.0f), V3(0.0f, 0.0f, 1.0f), 1);
            
                SceneDirectionalLightSet(Scene, Normalize(V3(-1.0f, -1.0f, 0.0f)), 0.3f*V3(1.0f, 1.0f, 1.0f), V3(0.4f, 0.4f, 0.4f));
            }        
        
            // NOTE: Push Point Lights
            {
                point_light* PointLights = VkCommandsPushWriteArray(Commands, Scene->PointLightBuffer, point_light, Scene->NumPointLights,
                                                                    BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                                    BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT));
                m4* Transforms = VkCommandsPushWriteArray(Commands, Scene->PointLightTransforms, m4, Scene->NumPointLights,
                                                          BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                          BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT));

                for (u32 LightId = 0; LightId < Scene->NumPointLights; ++LightId)
                {
                    point_light* CurrLight = Scene->PointLights + LightId;
                    PointLights[LightId] = *CurrLight;
                    // NOTE: Convert to view space
                    PointLights[LightId].Pos = (CameraGetV(&Scene->Camera) * V4(CurrLight->Pos, 1.0f)).xyz;
                    Transforms[LightId] = CameraGetVP(&Scene->Camera) * M4Pos(CurrLight->Pos) * M4Scale(V3(CurrLight->MaxDistance));
                }
            }

            // NOTE: Push Directional Lights
            {
                directional_light* GpuData = VkCommandsPushWriteStruct(Commands, Scene->DirectionalLightBuffer, directional_light,
                                                                       BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                                       BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT));
                Copy(&Scene->DirectionalLight, GpuData, sizeof(directional_light));
            }
        
            {
                scene_globals* Data = VkCommandsPushWriteStruct(Commands, Scene->SceneBuffer, scene_globals,
                                                                BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT),
                                                                BarrierMask(VK_ACCESS_UNIFORM_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
                *Data = {};
                Data->CameraPos = Scene->Camera.Pos;
                Data->NumPointLights = Scene->NumPointLights;
                Data->VPTransform = CameraGetVP(&Scene->Camera);
            }

            VkCommandsTransferFlush(Commands, RenderState->Device);
        }

        // NOTE: Render Scene
        RenderTargetPassBegin(&DemoState->RenderTarget, Commands, RenderTargetRenderPass_SetViewPort | RenderTargetRenderPass_SetScissor);
        {
            CPU_TIMED_BLOCK("Render Forward");
            render_scene* Scene = &DemoState->Scene;
        
            vkCmdBindPipeline(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, DemoState->RenderPipeline->Handle);
            {
                VkDescriptorSet DescriptorSets[] =
                    {
                        Scene->SceneDescriptor,
                    };
                vkCmdBindDescriptorSets(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, DemoState->RenderPipeline->Layout, 1,
                                        ArrayCount(DescriptorSets), DescriptorSets, 0, 0);
            }

            u32 InstanceId = 0;
            for (; InstanceId < Scene->NumOpaqueInstances; )
            {
                instance_entry* CurrInstance = Scene->OpaqueInstances + InstanceId;
                render_mesh* CurrMesh = Scene->RenderMeshes + CurrInstance->MeshId;

                {
                    VkDescriptorSet DescriptorSets[] =
                        {
                            CurrMesh->MaterialDescriptor,
                        };
                    vkCmdBindDescriptorSets(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, DemoState->RenderPipeline->Layout, 0,
                                            ArrayCount(DescriptorSets), DescriptorSets, 0, 0);
                }
            
                VkDeviceSize Offset = 0;
                vkCmdBindVertexBuffers(Commands->Buffer, 0, 1, &CurrMesh->VertexBuffer, &Offset);
                vkCmdBindIndexBuffer(Commands->Buffer, CurrMesh->IndexBuffer, 0, VK_INDEX_TYPE_UINT32);

                // NOTE: Check how many instances share the same mesh
                u32 NextInstanceId = InstanceId + 1;
                while (NextInstanceId < Scene->NumOpaqueInstances)
                {
                    instance_entry* NextInstance = Scene->OpaqueInstances + NextInstanceId;
                    render_mesh* NextMesh = Scene->RenderMeshes + NextInstance->MeshId;

                    if (NextMesh != CurrMesh)
                    {
                        break;
                    }
                    NextInstanceId += 1;
                }
                vkCmdDrawIndexed(Commands->Buffer, CurrMesh->NumIndices, NextInstanceId - InstanceId, 0, 0, InstanceId);

                InstanceId = NextInstanceId;
            }
        }

#if DEBUG_DRAW

        render_scene* Scene = &DemoState->Scene;

        // NOTE: Draw triangles
        {
            vkCmdBindPipeline(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, DemoState->TrianglePipeline->Handle);
            {
                VkDescriptorSet DescriptorSets[] =
                    {
                        Scene->SceneDescriptor,
                    };
                vkCmdBindDescriptorSets(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, DemoState->TrianglePipeline->Layout, 0,
                                        ArrayCount(DescriptorSets), DescriptorSets, 0, 0);
            }


            VkDeviceSize Offset = 0;
            vkCmdBindVertexBuffers(Commands->Buffer, 0, 1, &DemoState->TriangleBuffer, &Offset);
            Offset += sizeof(v3) * DemoState->MaxNumTriangleVerts;
            vkCmdBindVertexBuffers(Commands->Buffer, 1, 1, &DemoState->TriangleBuffer, &Offset);

            vkCmdDraw(Commands->Buffer, DemoState->NumTriangleVerts, 1, 0, 0);
        }
                
        // NOTE: Draw points
        {
            vkCmdBindPipeline(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, DemoState->PointPipeline->Handle);
            {
                VkDescriptorSet DescriptorSets[] =
                    {
                        Scene->SceneDescriptor,
                    };
                vkCmdBindDescriptorSets(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, DemoState->PointPipeline->Layout, 0,
                                        ArrayCount(DescriptorSets), DescriptorSets, 0, 0);
            }


            VkDeviceSize Offset = 0;
            vkCmdBindVertexBuffers(Commands->Buffer, 0, 1, &DemoState->PointBuffer, &Offset);
            Offset += sizeof(v3) * DemoState->MaxNumPointVerts;
            vkCmdBindVertexBuffers(Commands->Buffer, 1, 1, &DemoState->PointBuffer, &Offset);

            vkCmdDraw(Commands->Buffer, DemoState->NumPointVerts, 1, 0, 0);
        }

        // NOTE: Draw lines
        {
            vkCmdBindPipeline(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, DemoState->LinePipeline->Handle);
            {
                VkDescriptorSet DescriptorSets[] =
                    {
                        Scene->SceneDescriptor,
                    };
                vkCmdBindDescriptorSets(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, DemoState->LinePipeline->Layout, 0,
                                        ArrayCount(DescriptorSets), DescriptorSets, 0, 0);
            }


            VkDeviceSize Offset = 0;
            vkCmdBindVertexBuffers(Commands->Buffer, 0, 1, &DemoState->LineBuffer, &Offset);
            Offset += sizeof(v3) * DemoState->MaxNumLineVerts;
            vkCmdBindVertexBuffers(Commands->Buffer, 1, 1, &DemoState->LineBuffer, &Offset);

            vkCmdDraw(Commands->Buffer, DemoState->NumLineVerts, 1, 0, 0);
        }
#endif
        
        RenderTargetPassEnd(Commands);        
        UiStateRender(&DemoState->UiState, RenderState->Device, Commands, DemoState->SwapChainEntry.View);

        VkCommandsEnd(Commands, RenderState->Device);
    
        // NOTE: Render to our window surface
        // NOTE: Tell queue where we render to surface to wait
        VkPipelineStageFlags WaitDstMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo SubmitInfo = {};
        SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        SubmitInfo.waitSemaphoreCount = 1;
        SubmitInfo.pWaitSemaphores = &RenderState->ImageAvailableSemaphore;
        SubmitInfo.pWaitDstStageMask = &WaitDstMask;
        SubmitInfo.commandBufferCount = 1;
        SubmitInfo.pCommandBuffers = &Commands->Buffer;
        SubmitInfo.signalSemaphoreCount = 1;
        SubmitInfo.pSignalSemaphores = &RenderState->FinishedRenderingSemaphore;
        VkCheckResult(vkQueueSubmit(RenderState->GraphicsQueue, 1, &SubmitInfo, Commands->Fence));
    
        VkPresentInfoKHR PresentInfo = {};
        PresentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        PresentInfo.waitSemaphoreCount = 1;
        PresentInfo.pWaitSemaphores = &RenderState->FinishedRenderingSemaphore;
        PresentInfo.swapchainCount = 1;
        PresentInfo.pSwapchains = &RenderState->SwapChain;
        PresentInfo.pImageIndices = &ImageIndex;
        VkResult Result = vkQueuePresentKHR(RenderState->PresentQueue, &PresentInfo);

        switch (Result)
        {
            case VK_SUCCESS:
            {
            } break;

            case VK_ERROR_OUT_OF_DATE_KHR:
            case VK_SUBOPTIMAL_KHR:
            {
                // NOTE: Window size changed
                InvalidCodePath;
            } break;

            default:
            {
                InvalidCodePath;
            } break;
        }
    }

    ProfilerProcessData();
    ProfilerPrintTimeStamps();
}
