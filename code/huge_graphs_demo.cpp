
#include "huge_graphs_demo.h"

/*

  NOTE:

  - https://pygraphviz.github.io/
  - https://networkx.org/documentation/stable/reference/drawing.html#module-networkx.drawing.nx_pylab
  
 */

inline f32 RandFloat()
{
    f32 Result = f32(rand()) / f32(RAND_MAX);
    return Result;
}

//
// NOTE: Asset Storage System
//

inline u32 SceneMeshAdd(render_scene* Scene, VkBuffer VertexBuffer, VkBuffer IndexBuffer, u32 NumIndices)
{
    Assert(Scene->NumRenderMeshes < Scene->MaxNumRenderMeshes);
    
    u32 MeshId = Scene->NumRenderMeshes++;
    render_mesh* Mesh = Scene->RenderMeshes + MeshId;
    Mesh->VertexBuffer = VertexBuffer;
    Mesh->IndexBuffer = IndexBuffer;
    Mesh->NumIndices = NumIndices;

    return MeshId;
}

inline u32 SceneMeshAdd(render_scene* Scene, procedural_mesh Mesh)
{
    u32 Result = SceneMeshAdd(Scene, Mesh.Vertices, Mesh.Indices, Mesh.NumIndices);
    return Result;
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
            InitParams.GpuLocalSize = GigaBytes(1);
            InitParams.DeviceExtensionCount = ArrayCount(DeviceExtensions);
            InitParams.DeviceExtensions = DeviceExtensions;
            VkInit(VulkanLib, hInstance, WindowHandle, &DemoState->Arena, &DemoState->TempArena, InitParams);
        }
    }
    
    // NOTE: Create samplers
    DemoState->PointSampler = VkSamplerCreate(RenderState->Device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, 0.0f);
    DemoState->LinearSampler = VkSamplerCreate(RenderState->Device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, 0.0f);
        
    // NOTE: Init render target entries
    DemoState->SwapChainEntry = RenderTargetSwapChainEntryCreate(RenderState->WindowWidth, RenderState->WindowHeight,
                                                                 RenderState->SwapChainFormat);

    // NOTE: Init scene system
    {
        render_scene* Scene = &DemoState->Scene;

        Scene->Camera = CameraFlatCreate(V3(0, 0, -2), 1.0f, 1.0f, 0.1f, 0.01f, 1000.0f);
        
        Scene->MaxNumRenderMeshes = 1;
        Scene->RenderMeshes = PushArray(&DemoState->Arena, render_mesh, Scene->MaxNumRenderMeshes);
    }

    // NOTE: Init Graph Layout Data
    {
        {
            vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&DemoState->GraphDescLayout);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutEnd(RenderState->Device, &Builder);
        }

        // NOTE: Graph Move Connections Pipeline
        {
            VkDescriptorSetLayout Layouts[] =
                {
                    DemoState->GraphDescLayout,
                };
            
            DemoState->GraphMoveConnectionsPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                              "shader_graph_move_connections.spv", "main", Layouts, ArrayCount(Layouts));
        }

        // NOTE: Graph Nearby Pipeline
        {
            VkDescriptorSetLayout Layouts[] =
                {
                    DemoState->GraphDescLayout,
                };
            
            DemoState->GraphNearbyPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                     "shader_graph_nearby.spv", "main", Layouts, ArrayCount(Layouts));
        }

        // NOTE: Graph Update Nodes Pipeline
        {
            VkDescriptorSetLayout Layouts[] =
                {
                    DemoState->GraphDescLayout,
                };
            
            DemoState->GraphUpdateNodesPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                          "shader_graph_update_nodes.spv", "main", Layouts, ArrayCount(Layouts));
        }

        // NOTE: Graph Gen Edges Pipeline
        {
            VkDescriptorSetLayout Layouts[] =
                {
                    DemoState->GraphDescLayout,
                };
            
            DemoState->GraphGenEdgesPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                       "shader_graph_gen_edges.spv", "main", Layouts, ArrayCount(Layouts));
        }
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
            RenderTargetAddTarget(&Builder, &DemoState->SwapChainEntry, VkClearColorCreate(1, 1, 1, 1));
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
            VkPipelineShaderAdd(&Builder, "shader_circle_vert.spv", "main", VK_SHADER_STAGE_VERTEX_BIT);
            VkPipelineShaderAdd(&Builder, "shader_circle_frag.spv", "main", VK_SHADER_STAGE_FRAGMENT_BIT);
                
            // NOTE: Specify input vertex data format
            VkPipelineVertexBindingBegin(&Builder);
            VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32B32_SFLOAT, sizeof(v3));
            VkPipelineVertexAttributeAddOffset(&Builder, sizeof(v3));
            VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32_SFLOAT, sizeof(v2));
            VkPipelineVertexBindingEnd(&Builder);

            VkPipelineInputAssemblyAdd(&Builder, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE);
            VkPipelineDepthStateAdd(&Builder, VK_FALSE, VK_FALSE, VK_COMPARE_OP_GREATER);
            
            // NOTE: Set the blending state
            VkPipelineColorAttachmentAdd(&Builder, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                         VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);

            VkDescriptorSetLayout DescriptorLayouts[] =
                {
                    DemoState->GraphDescLayout,
                };
            
            DemoState->CirclePipeline = VkPipelineBuilderEnd(&Builder, RenderState->Device, &RenderState->PipelineManager,
                                                             DemoState->RenderTarget.RenderPass, 0, DescriptorLayouts,
                                                             ArrayCount(DescriptorLayouts));
        }
     
        // NOTE: Create Line PSO
        {
            vk_pipeline_builder Builder = VkPipelineBuilderBegin(&DemoState->TempArena);

            // NOTE: Shaders
            VkPipelineShaderAdd(&Builder, "shader_line_vert.spv", "main", VK_SHADER_STAGE_VERTEX_BIT);
            VkPipelineShaderAdd(&Builder, "shader_line_frag.spv", "main", VK_SHADER_STAGE_FRAGMENT_BIT);
                
            // NOTE: Specify input vertex data format
            VkPipelineVertexBindingBegin(&Builder);
            VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32_SFLOAT, sizeof(v2));
            VkPipelineVertexBindingEnd(&Builder);

            VkPipelineVertexBindingBegin(&Builder);
            VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(v4));
            VkPipelineVertexBindingEnd(&Builder);

            VkPipelineInputAssemblyAdd(&Builder, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_FALSE);
            VkPipelineRasterizationSetLineWidth(&Builder, 2.0f);
            VkPipelineDepthStateAdd(&Builder, VK_FALSE, VK_FALSE, VK_COMPARE_OP_GREATER);
            
            // NOTE: Set the blending state
            VkPipelineColorAttachmentAdd(&Builder, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                         VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);

            VkDescriptorSetLayout DescriptorLayouts[] =
                {
                    DemoState->GraphDescLayout,
                };
            
            DemoState->LinePipeline = VkPipelineBuilderEnd(&Builder, RenderState->Device, &RenderState->PipelineManager,
                                                           DemoState->RenderTarget.RenderPass, 0, DescriptorLayouts,
                                                           ArrayCount(DescriptorLayouts));
        }
    }
    
    // NOTE: Upload assets
    vk_commands* Commands = &RenderState->Commands;
    VkCommandsBegin(Commands, RenderState->Device);
    {
        render_scene* Scene = &DemoState->Scene;
                                
        // NOTE: Push meshes
        Scene->CircleMeshId = SceneMeshAdd(Scene, AssetsPushQuad());
        
        // NOTE: Init graph nodes
        {
            // NOTE: Setup default layout params
            DemoState->LayoutAvoidDiffRadius = 2.67f;
            DemoState->LayoutAvoidDiffAccel = 1.82f;
            DemoState->LayoutAvoidSameRadius = 0.5f;
            DemoState->LayoutAvoidSameAccel = 8.98f;
            DemoState->LayoutPullSameRadius = 2.411f;
            DemoState->LayoutPullSameAccel = 0.335f;
            DemoState->LayoutEdgeMinDist = 1.65529f;
            DemoState->LayoutEdgeAccel = 1.472f;

            DemoState->PauseSim = false;
        
            u32 RedNodesStart1 = 0;
            u32 NumRedNodes1 = 800;
            u32 RedNodesStart2 = RedNodesStart1 + NumRedNodes1;
            u32 NumRedNodes2 = 800;
            u32 RedNodesStart3 = RedNodesStart2 + NumRedNodes2;
            u32 NumRedNodes3 = 800;

            DemoState->NumGraphRedNodes = RedNodesStart1 + RedNodesStart2 + RedNodesStart3;

            u32 BlackNodesStart1 = RedNodesStart3 + NumRedNodes3;
            u32 NumBlackNodes1 = 100;
            u32 BlackNodesStart2 = BlackNodesStart1 + NumBlackNodes1;
            u32 NumBlackNodes2 = 100;

            DemoState->NumGraphNodes = NumRedNodes1 + NumRedNodes2 + NumRedNodes3 + NumBlackNodes1 + NumBlackNodes2;
            u32 MaxNumEdges = DemoState->NumGraphNodes * DemoState->NumGraphNodes;

            // NOTE: Allocate buffers
            {
                DemoState->GraphGlobalsBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                               VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                               sizeof(graph_globals));
                DemoState->NodePositionBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                               sizeof(graph_node_pos) * DemoState->NumGraphNodes);
                DemoState->NodeVelocityBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                               sizeof(v2) * DemoState->NumGraphNodes);
                DemoState->NodeEdgeBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                           sizeof(graph_node_edges) * DemoState->NumGraphNodes);
                DemoState->EdgeBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                       sizeof(u32) * MaxNumEdges);
                DemoState->NodeDrawBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                           sizeof(graph_node_draw) * DemoState->NumGraphNodes);
                        
                DemoState->GraphDescriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, DemoState->GraphDescLayout);
                VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, DemoState->GraphGlobalsBuffer);
                VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->NodePositionBuffer);
                VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->NodeVelocityBuffer);
                VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->NodeEdgeBuffer);
                VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->EdgeBuffer);
                VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->NodeDrawBuffer);
            }
        
            // NOTE: Get pointers to GPU memory for our graph
            graph_node_pos* NodePosGpu = VkCommandsPushWriteArray(Commands, DemoState->NodePositionBuffer, graph_node_pos, DemoState->NumGraphNodes,
                                                                  BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                                  BarrierMask(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
            graph_node_edges* NodeEdgeGpu = VkCommandsPushWriteArray(Commands, DemoState->NodeEdgeBuffer, graph_node_edges, DemoState->NumGraphNodes,
                                                                     BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                                     BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
            u32* EdgeGpu = VkCommandsPushWriteArray(Commands, DemoState->EdgeBuffer, u32, MaxNumEdges,
                                                    BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                    BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
            graph_node_draw* NodeDrawGpu = VkCommandsPushWriteArray(Commands, DemoState->NodeDrawBuffer, graph_node_draw, DemoState->NumGraphNodes,
                                                                    BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                                    BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));

            // NOTE: Create Nodes
            {
                for (u32 RedNodeId = RedNodesStart1; RedNodeId < RedNodesStart1 + NumRedNodes1; ++RedNodeId)
                {
                    NodePosGpu[RedNodeId].Pos = V2(0, 0);
                    NodePosGpu[RedNodeId].FamilyId = 0;

                    NodeDrawGpu[RedNodeId].Color = V3(1, 0, 0);
                    NodeDrawGpu[RedNodeId].Scale = 0.2f;
                }

                for (u32 RedNodeId = RedNodesStart2; RedNodeId < RedNodesStart2 + NumRedNodes2; ++RedNodeId)
                {
                    NodePosGpu[RedNodeId].Pos = V2(0, 0);
                    NodePosGpu[RedNodeId].FamilyId = 0;

                    NodeDrawGpu[RedNodeId].Color = V3(1, 0, 0);
                    NodeDrawGpu[RedNodeId].Scale = 0.2f;
                }

                for (u32 RedNodeId = RedNodesStart3; RedNodeId < RedNodesStart3 + NumRedNodes3; ++RedNodeId)
                {
                    NodePosGpu[RedNodeId].Pos = V2(0, 0);
                    NodePosGpu[RedNodeId].FamilyId = 0;

                    NodeDrawGpu[RedNodeId].Color = V3(1, 0, 0);
                    NodeDrawGpu[RedNodeId].Scale = 0.2f;
                }

                for (u32 BlackNodeId = BlackNodesStart1; BlackNodeId < BlackNodesStart1 + NumBlackNodes1; ++BlackNodeId)
                {
                    NodePosGpu[BlackNodeId].Pos = V2(0, 0);
                    NodePosGpu[BlackNodeId].FamilyId = 1;

                    NodeDrawGpu[BlackNodeId].Color = V3(0, 0, 0);
                    NodeDrawGpu[BlackNodeId].Scale = 0.2f;
                }

                for (u32 BlackNodeId = BlackNodesStart2; BlackNodeId < BlackNodesStart2 + NumBlackNodes2; ++BlackNodeId)
                {
                    NodePosGpu[BlackNodeId].Pos = V2(0, 0);
                    NodePosGpu[BlackNodeId].FamilyId = 1;

                    NodeDrawGpu[BlackNodeId].Color = V3(0, 0, 0);
                    NodeDrawGpu[BlackNodeId].Scale = 0.2f;
                }
            }

            // NOTE: Create edges
            {
                {
                    // NOTE: Connect red group 1 to black group 1
                    for (u32 RedNodeId = RedNodesStart1; RedNodeId < RedNodesStart1 + NumRedNodes1; ++RedNodeId)
                    {
                        NodeEdgeGpu[RedNodeId].StartConnections = DemoState->NumGraphEdges;
                        NodeEdgeGpu[RedNodeId].EndConnections = DemoState->NumGraphEdges;

                        for (u32 BlackNodeId = BlackNodesStart1; BlackNodeId < BlackNodesStart1 + NumBlackNodes1; ++BlackNodeId)
                        {
                            Assert(DemoState->NumGraphEdges < MaxNumEdges);
                            EdgeGpu[DemoState->NumGraphEdges++] = BlackNodeId;
                            NodeEdgeGpu[RedNodeId].EndConnections += 1;
                        }
                    }

                    // NOTE: Connect red group 2 to black group 2
                    for (u32 RedNodeId = RedNodesStart2; RedNodeId < RedNodesStart2 + NumRedNodes2; ++RedNodeId)
                    {
                        NodeEdgeGpu[RedNodeId].StartConnections = DemoState->NumGraphEdges;
                        NodeEdgeGpu[RedNodeId].EndConnections = DemoState->NumGraphEdges;

                        for (u32 BlackNodeId = BlackNodesStart2; BlackNodeId < BlackNodesStart2 + NumBlackNodes2; ++BlackNodeId)
                        {
                            Assert(DemoState->NumGraphEdges < MaxNumEdges);
                            EdgeGpu[DemoState->NumGraphEdges++] = BlackNodeId;
                            NodeEdgeGpu[RedNodeId].EndConnections += 1;
                        }
                    }

                    // NOTE: Connect red group 3 to black group 1 and 2
                    for (u32 RedNodeId = RedNodesStart3; RedNodeId < RedNodesStart3 + NumRedNodes3; ++RedNodeId)
                    {
                        NodeEdgeGpu[RedNodeId].StartConnections = DemoState->NumGraphEdges;
                        NodeEdgeGpu[RedNodeId].EndConnections = DemoState->NumGraphEdges;

                        for (u32 BlackNodeId = BlackNodesStart1; BlackNodeId < BlackNodesStart1 + NumBlackNodes1 + NumBlackNodes2; ++BlackNodeId)
                        {
                            Assert(DemoState->NumGraphEdges < MaxNumEdges);
                            EdgeGpu[DemoState->NumGraphEdges++] = BlackNodeId;
                            NodeEdgeGpu[RedNodeId].EndConnections += 1;
                        }
                    }
                }

                // NOTE: Save on memory since nodes are double represented for sim
                {
                    DemoState->NumGraphDrawEdges = DemoState->NumGraphEdges;
                    DemoState->EdgePositionBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                   sizeof(v2) * 2 * DemoState->NumGraphDrawEdges);
                    DemoState->EdgeColorBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                sizeof(v4) * 2 * DemoState->NumGraphDrawEdges);
                    VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->EdgePositionBuffer);
                    VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->EdgeColorBuffer);
                }

                // NOTE: Mirror the connections for black nodes now since GPUs don't support atomic float adds
                {
                    // NOTE: Connect black group 1 to red group 1 and 3
                    for (u32 BlackNodeId = BlackNodesStart1; BlackNodeId < BlackNodesStart1 + NumBlackNodes1; ++BlackNodeId)
                    {
                        NodeEdgeGpu[BlackNodeId].StartConnections = DemoState->NumGraphEdges;
                        NodeEdgeGpu[BlackNodeId].EndConnections = DemoState->NumGraphEdges;

                        for (u32 RedNodeId = RedNodesStart1; RedNodeId < RedNodesStart1 + NumRedNodes1; ++RedNodeId)
                        {
                            Assert(DemoState->NumGraphEdges < MaxNumEdges);
                            EdgeGpu[DemoState->NumGraphEdges++] = RedNodeId;
                            NodeEdgeGpu[BlackNodeId].EndConnections += 1;
                        }

                        for (u32 RedNodeId = RedNodesStart3; RedNodeId < RedNodesStart3 + NumRedNodes3; ++RedNodeId)
                        {
                            Assert(DemoState->NumGraphEdges < MaxNumEdges);
                            EdgeGpu[DemoState->NumGraphEdges++] = RedNodeId;
                            NodeEdgeGpu[BlackNodeId].EndConnections += 1;
                        }
                    }

                    // NOTE: Connect black group 2 to red group 2 and 3
                    for (u32 BlackNodeId = BlackNodesStart2; BlackNodeId < BlackNodesStart2 + NumBlackNodes2; ++BlackNodeId)
                    {
                        NodeEdgeGpu[BlackNodeId].StartConnections = DemoState->NumGraphEdges;
                        NodeEdgeGpu[BlackNodeId].EndConnections = DemoState->NumGraphEdges;

                        for (u32 RedNodeId = RedNodesStart2; RedNodeId < RedNodesStart2 + NumRedNodes2; ++RedNodeId)
                        {
                            Assert(DemoState->NumGraphEdges < MaxNumEdges);
                            EdgeGpu[DemoState->NumGraphEdges++] = RedNodeId;
                            NodeEdgeGpu[BlackNodeId].EndConnections += 1;
                        }

                        for (u32 RedNodeId = RedNodesStart3; RedNodeId < RedNodesStart3 + NumRedNodes3; ++RedNodeId)
                        {
                            Assert(DemoState->NumGraphEdges < MaxNumEdges);
                            EdgeGpu[DemoState->NumGraphEdges++] = RedNodeId;
                            NodeEdgeGpu[BlackNodeId].EndConnections += 1;
                        }
                    }
                }
            
                // NOTE: Populate graph edges
                v2* EdgePosGpu = VkCommandsPushWriteArray(Commands, DemoState->EdgePositionBuffer, v2, 2*DemoState->NumGraphDrawEdges,
                                                          BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                          BarrierMask(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
                v4* EdgeColorGpu = VkCommandsPushWriteArray(Commands, DemoState->EdgeColorBuffer, v4, 2*DemoState->NumGraphDrawEdges,
                                                            BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                            BarrierMask(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));

                for (u32 CurrNodeId = 0; CurrNodeId < BlackNodesStart1; ++CurrNodeId)
                {
                    graph_node_pos CurrNodePos = NodePosGpu[CurrNodeId];
                    graph_node_edges CurrNodeEdges = NodeEdgeGpu[CurrNodeId];

                    for (u32 EdgeId = CurrNodeEdges.StartConnections; EdgeId < CurrNodeEdges.EndConnections; ++EdgeId)
                    {
                        u32 OtherNodeId = EdgeGpu[EdgeId];
                        graph_node_pos OtherNodePos = NodePosGpu[OtherNodeId];

                        EdgePosGpu[2*EdgeId + 0] = CurrNodePos.Pos;
                        EdgePosGpu[2*EdgeId + 1] = OtherNodePos.Pos;

                        EdgeColorGpu[2*EdgeId + 0] = V4(0, 0, 1, 1);
                        EdgeColorGpu[2*EdgeId + 1] = V4(0, 0, 1, 1);
                    }
                }
            }
        }
        
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
            local_global v2 PanelPos = V2(100, 400);
            ui_panel Panel = UiPanelBegin(UiState, &PanelPos, "Huge Graphs Panel");

            {
                UiPanelText(&Panel, "Sim Data:");

                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "PauseSim:");
                UiPanelCheckBox(&Panel, &DemoState->PauseSim);
                UiPanelNextRow(&Panel);            

                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "FrameTime:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 0.03f, &ModifiedFrameTime);
                UiPanelNumberBox(&Panel, 0.0f, 0.03f, &ModifiedFrameTime);
                UiPanelNextRow(&Panel);            

                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "Avoid Diff Radius:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 10.0f, &DemoState->LayoutAvoidDiffRadius);
                UiPanelNumberBox(&Panel, 0.0f, 10.0f, &DemoState->LayoutAvoidDiffRadius);
                UiPanelNextRow(&Panel);            

                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "Avoid Diff Accel:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 10.0f, &DemoState->LayoutAvoidDiffAccel);
                UiPanelNumberBox(&Panel, 0.0f, 10.0f, &DemoState->LayoutAvoidDiffAccel);
                UiPanelNextRow(&Panel);            

                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "Avoid Same Radius:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 10.0f, &DemoState->LayoutAvoidSameRadius);
                UiPanelNumberBox(&Panel, 0.0f, 10.0f, &DemoState->LayoutAvoidSameRadius);
                UiPanelNextRow(&Panel);            

                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "Avoid Same Accel:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 10.0f, &DemoState->LayoutAvoidSameAccel);
                UiPanelNumberBox(&Panel, 0.0f, 10.0f, &DemoState->LayoutAvoidSameAccel);
                UiPanelNextRow(&Panel);            

                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "Pull Same Radius:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 10.0f, &DemoState->LayoutPullSameRadius);
                UiPanelNumberBox(&Panel, 0.0f, 10.0f, &DemoState->LayoutPullSameRadius);
                UiPanelNextRow(&Panel);            

                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "Pull Same Accel:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 10.0f, &DemoState->LayoutPullSameAccel);
                UiPanelNumberBox(&Panel, 0.0f, 10.0f, &DemoState->LayoutPullSameAccel);
                UiPanelNextRow(&Panel);            

                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "Edge Min Dist:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 10.0f, &DemoState->LayoutEdgeMinDist);
                UiPanelNumberBox(&Panel, 0.0f, 10.0f, &DemoState->LayoutEdgeMinDist);
                UiPanelNextRow(&Panel);            

                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "Edge Accel:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 10.0f, &DemoState->LayoutEdgeAccel);
                UiPanelNumberBox(&Panel, 0.0f, 10.0f, &DemoState->LayoutEdgeAccel);
                UiPanelNextRow(&Panel);            
            }

            UiPanelEnd(&Panel);

            UiStateEnd(UiState, &RenderState->DescriptorManager);
        }
        
        // NOTE: Upload scene data
        {
            render_scene* Scene = &DemoState->Scene;
            if (!(DemoState->UiState.MouseTouchingUi || DemoState->UiState.ProcessedInteraction))
            {
                CameraUpdate(&Scene->Camera, CurrInput, PrevInput, FrameTime);
            }

            // NOTE: Populate GPU Buffers
            {
                {
                    CPU_TIMED_BLOCK("Upload scene buffer to  GPU");
                    graph_globals* GpuData = VkCommandsPushWriteStruct(Commands, DemoState->GraphGlobalsBuffer, graph_globals,
                                                                      BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                                      BarrierMask(VK_ACCESS_UNIFORM_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));

                    GpuData->VPTransform = CameraGetVP(&Scene->Camera);
                    GpuData->ViewPort = V2(RenderState->WindowWidth, RenderState->WindowHeight);
                    GpuData->FrameTime = ModifiedFrameTime * (!DemoState->PauseSim);
                    GpuData->NumNodes = DemoState->NumGraphNodes;
                    
                    GpuData->LayoutAvoidDiffRadius = DemoState->LayoutAvoidDiffRadius;
                    GpuData->LayoutAvoidDiffAccel = DemoState->LayoutAvoidDiffAccel;
                    GpuData->LayoutAvoidSameRadius = DemoState->LayoutAvoidSameRadius;
                    GpuData->LayoutAvoidSameAccel = DemoState->LayoutAvoidSameAccel;
                    GpuData->LayoutPullSameRadius = DemoState->LayoutPullSameRadius;
                    GpuData->LayoutPullSameAccel = DemoState->LayoutPullSameAccel;
                    GpuData->LayoutEdgeAccel = DemoState->LayoutEdgeAccel;
                    GpuData->LayoutEdgeMinDist = DemoState->LayoutEdgeMinDist;
                }
            }
            
            VkCommandsTransferFlush(Commands, RenderState->Device);
        }

        // NOTE: Simulate graph layout
        {
            VkDescriptorSet DescriptorSets[] =
                {
                    DemoState->GraphDescriptor,
                };
            u32 DispatchX = CeilU32(f32(DemoState->NumGraphNodes) / 32.0f);

            VkBarrierBufferAdd(Commands, DemoState->NodeVelocityBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkCommandsBarrierFlush(Commands);

            // NOTE: Graph Move Connections
            VkComputeDispatch(Commands, DemoState->GraphMoveConnectionsPipeline, DescriptorSets, ArrayCount(DescriptorSets), DispatchX, 1, 1);

            VkBarrierBufferAdd(Commands, DemoState->NodeVelocityBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkCommandsBarrierFlush(Commands);

            // NOTE: Graph Nearby
            VkComputeDispatch(Commands, DemoState->GraphNearbyPipeline, DescriptorSets, ArrayCount(DescriptorSets), DispatchX, 1, 1);

            VkBarrierBufferAdd(Commands, DemoState->NodeVelocityBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkCommandsBarrierFlush(Commands);

            // NOTE: Graph Update Nodes
            VkComputeDispatch(Commands, DemoState->GraphUpdateNodesPipeline, DescriptorSets, ArrayCount(DescriptorSets), DispatchX, 1, 1);

            VkBarrierBufferAdd(Commands, DemoState->NodePositionBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkCommandsBarrierFlush(Commands);

            // NOTE: Graph Gen Edges
            {
                u32 GraphGenDispatchX = CeilU32(f32(DemoState->NumGraphRedNodes) / 32.0f);
                VkComputeDispatch(Commands, DemoState->GraphGenEdgesPipeline, DescriptorSets, ArrayCount(DescriptorSets), GraphGenDispatchX, 1, 1);
            }
            
            VkBarrierBufferAdd(Commands, DemoState->EdgePositionBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkBarrierBufferAdd(Commands, DemoState->EdgeColorBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkCommandsBarrierFlush(Commands);
        }
        
        // NOTE: Render Scene
        render_scene* Scene = &DemoState->Scene;
        RenderTargetPassBegin(&DemoState->RenderTarget, Commands, RenderTargetRenderPass_SetViewPort | RenderTargetRenderPass_SetScissor);
        {
            CPU_TIMED_BLOCK("Render Lines");
            
            vkCmdBindPipeline(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, DemoState->LinePipeline->Handle);
            {
                VkDescriptorSet DescriptorSets[] =
                    {
                        DemoState->GraphDescriptor,
                    };
                vkCmdBindDescriptorSets(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, DemoState->LinePipeline->Layout, 0,
                                        ArrayCount(DescriptorSets), DescriptorSets, 0, 0);
            }

            VkDeviceSize Offset = 0;
            vkCmdBindVertexBuffers(Commands->Buffer, 0, 1, &DemoState->EdgePositionBuffer, &Offset);
            vkCmdBindVertexBuffers(Commands->Buffer, 1, 1, &DemoState->EdgeColorBuffer, &Offset);

            vkCmdDraw(Commands->Buffer, 2*DemoState->NumGraphDrawEdges, 1, 0, 0);
        }
        
        {
            CPU_TIMED_BLOCK("Render Circle Nodes");
        
            vkCmdBindPipeline(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, DemoState->CirclePipeline->Handle);
            {
                VkDescriptorSet DescriptorSets[] =
                    {
                        DemoState->GraphDescriptor,
                    };
                vkCmdBindDescriptorSets(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, DemoState->CirclePipeline->Layout, 0,
                                        ArrayCount(DescriptorSets), DescriptorSets, 0, 0);
            }
            
            render_mesh* CurrMesh = Scene->RenderMeshes + Scene->CircleMeshId;
            VkDeviceSize Offset = 0;
            vkCmdBindVertexBuffers(Commands->Buffer, 0, 1, &CurrMesh->VertexBuffer, &Offset);
            vkCmdBindIndexBuffer(Commands->Buffer, CurrMesh->IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(Commands->Buffer, CurrMesh->NumIndices, DemoState->NumGraphNodes, 0, 0, 0);
        }
        
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
