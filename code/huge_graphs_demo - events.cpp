
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
// NOTE: Graph Creation Functions
// 

inline void GraphNodeInit(f32 Degree, v3 Color, f32 Scale, v2* NodePos, f32* NodeDegree, graph_node_draw* NodeDraw)
{
    *NodePos = 2.0f * Normalize(V2(2.0f * RandFloat() - 1.0f, 2.0f * RandFloat() - 1.0f));
    //*NodePos = V2(0);
    *NodeDegree = Degree;
    NodeDraw->Color = Color;
    NodeDraw->Scale = Scale;
}

inline void GraphCreateBuffers(u32 NumNodes, u32 NumEdges)
{
    DemoState->GraphGlobalsBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                   sizeof(graph_globals));
    DemoState->NodePosBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                              sizeof(v2) * DemoState->NumGraphNodes);
    DemoState->NodeDegreeBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                 sizeof(f32) * DemoState->NumGraphNodes);
    DemoState->NodeCellIdBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                 sizeof(u32) * DemoState->NumGraphNodes);
    DemoState->NodeForceBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                sizeof(v2) * DemoState->NumGraphNodes);
    DemoState->NodePrevForceBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                    sizeof(v2) * DemoState->NumGraphNodes);
    DemoState->NodeEdgeBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                               sizeof(graph_node_edges) * DemoState->NumGraphNodes);
    DemoState->EdgeBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                           sizeof(u32) * NumEdges * 2);
    DemoState->NodeDrawBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                               sizeof(graph_node_draw) * DemoState->NumGraphNodes);
    DemoState->GridCellBlockBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                    sizeof(grid_cell_block) * 10000); //(DemoState->NumGraphNodes / 512));
    DemoState->GridCellHeaderBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                     sizeof(u32) * DemoState->NumCellsAxis * DemoState->NumCellsAxis);
    DemoState->GridCellBlockCounter = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                     sizeof(u32)*100);
    DemoState->GlobalMoveBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                 sizeof(global_move));
    DemoState->GlobalMoveReductionBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                          (NumNodes + CeilU32(f32(NumNodes) / 32.0f)) * sizeof(v2));
    DemoState->GlobalMoveCounterBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                        sizeof(global_move_counters));
                
    DemoState->GraphDescriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, DemoState->GraphDescLayout);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, DemoState->GraphGlobalsBuffer);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->NodePosBuffer);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->NodeDegreeBuffer);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->NodeCellIdBuffer);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->NodeForceBuffer);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->NodePrevForceBuffer);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->NodeEdgeBuffer);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->EdgeBuffer);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->NodeDrawBuffer);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->GridCellBlockBuffer);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->GridCellHeaderBuffer);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->GridCellBlockCounter);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->GlobalMoveBuffer);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->GlobalMoveReductionBuffer);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->GlobalMoveCounterBuffer);
}

inline void GraphInitTest1(vk_commands* Commands)
{
    u32 RedNodesStart = 0;
    u32 NumRedNodes = 1;
    DemoState->NumGraphRedNodes = NumRedNodes;

    u32 BlackNodesStart = RedNodesStart + NumRedNodes;
    u32 NumBlackNodes = 1;

    DemoState->NumGraphNodes = NumRedNodes + NumBlackNodes;
    u32 MaxNumEdges = DemoState->NumGraphNodes * DemoState->NumGraphNodes;

    DemoState->NumCellsAxis = 128;
    DemoState->WorldRadius = 3.5f;
    DemoState->CellWorldDim = (2.0f * DemoState->WorldRadius) / f32(DemoState->NumCellsAxis);

    GraphCreateBuffers(DemoState->NumGraphNodes, MaxNumEdges);
            
    // NOTE: Get pointers to GPU memory for our graph
    v2* NodePosGpu = VkCommandsPushWriteArray(Commands, DemoState->NodePosBuffer, v2, DemoState->NumGraphNodes,
                                              BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                              BarrierMask(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
    f32* NodeDegreeGpu = VkCommandsPushWriteArray(Commands, DemoState->NodeDegreeBuffer, f32, DemoState->NumGraphNodes,
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
        for (u32 RedNodeId = RedNodesStart; RedNodeId < RedNodesStart + NumRedNodes; ++RedNodeId)
        {
            GraphNodeInit(1.0f, V3(1, 0, 0), 0.2f, NodePosGpu + RedNodeId, NodeDegreeGpu + RedNodeId,
                          NodeDrawGpu + RedNodeId);
        }

        for (u32 BlackNodeId = BlackNodesStart; BlackNodeId < BlackNodesStart + NumBlackNodes; ++BlackNodeId)
        {
            GraphNodeInit(1.0f, V3(0, 0, 0), 0.2f, NodePosGpu + BlackNodeId, NodeDegreeGpu + BlackNodeId,
                          NodeDrawGpu + BlackNodeId);
        }
    }

    // NOTE: Create edges
    {
        {
            // NOTE: Connect red group to black group
            for (u32 RedNodeId = RedNodesStart; RedNodeId < RedNodesStart + NumRedNodes; ++RedNodeId)
            {
                NodeEdgeGpu[RedNodeId].StartConnections = DemoState->NumGraphEdges;
                NodeEdgeGpu[RedNodeId].EndConnections = DemoState->NumGraphEdges;

                for (u32 BlackNodeId = BlackNodesStart; BlackNodeId < BlackNodesStart + NumBlackNodes; ++BlackNodeId)
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
            DemoState->EdgeIndexBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                        sizeof(u32) * 2 * DemoState->NumGraphDrawEdges);
            DemoState->EdgeColorBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                        sizeof(u32) * DemoState->NumGraphDrawEdges);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->EdgeIndexBuffer);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->EdgeColorBuffer);
        }

        // NOTE: Mirror the connections for black nodes now since GPUs don't support atomic float adds
        {
            // NOTE: Connect black group to red group
            for (u32 BlackNodeId = BlackNodesStart; BlackNodeId < BlackNodesStart + NumBlackNodes; ++BlackNodeId)
            {
                NodeEdgeGpu[BlackNodeId].StartConnections = DemoState->NumGraphEdges;
                NodeEdgeGpu[BlackNodeId].EndConnections = DemoState->NumGraphEdges;

                for (u32 RedNodeId = RedNodesStart; RedNodeId < RedNodesStart + NumRedNodes; ++RedNodeId)
                {
                    Assert(DemoState->NumGraphEdges < MaxNumEdges);
                    EdgeGpu[DemoState->NumGraphEdges++] = RedNodeId;
                    NodeEdgeGpu[BlackNodeId].EndConnections += 1;
                }
            }
        }
            
        // NOTE: Populate graph edges
        u32* EdgeIndexGpu = VkCommandsPushWriteArray(Commands, DemoState->EdgeIndexBuffer, u32, 2*DemoState->NumGraphDrawEdges,
                                                     BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                     BarrierMask(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
        u32* EdgeColorGpu = VkCommandsPushWriteArray(Commands, DemoState->EdgeColorBuffer, u32, DemoState->NumGraphDrawEdges,
                                                     BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                     BarrierMask(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));

        for (u32 CurrNodeId = 0; CurrNodeId < BlackNodesStart; ++CurrNodeId)
        {
            graph_node_edges CurrNodeEdges = NodeEdgeGpu[CurrNodeId];

            for (u32 EdgeId = CurrNodeEdges.StartConnections; EdgeId < CurrNodeEdges.EndConnections; ++EdgeId)
            {
                u32 OtherNodeId = EdgeGpu[EdgeId];

                EdgeIndexGpu[2*EdgeId + 0] = CurrNodeId;
                EdgeIndexGpu[2*EdgeId + 1] = OtherNodeId;

                EdgeColorGpu[EdgeId] = ((0u & 0xFF) << 0) | ((0u & 0xFF) << 8) | ((0xFFu & 0xFF) << 16) | ((0xFFu & 0xFF) << 24);
            }
        }
    }
}

inline void GraphInitTest2(vk_commands* Commands)
{
    u32 RedNodesStart1 = 0;
    u32 NumRedNodes1 = 5000;
    u32 RedNodesStart2 = RedNodesStart1 + NumRedNodes1;
    u32 NumRedNodes2 = 5000;
    u32 RedNodesStart3 = RedNodesStart2 + NumRedNodes2;
    u32 NumRedNodes3 = 5000;

    DemoState->NumGraphRedNodes = RedNodesStart1 + RedNodesStart2 + RedNodesStart3;

    u32 BlackNodesStart1 = RedNodesStart3 + NumRedNodes3;
    u32 NumBlackNodes1 = 500;
    u32 BlackNodesStart2 = BlackNodesStart1 + NumBlackNodes1;
    u32 NumBlackNodes2 = 500;

    DemoState->NumGraphNodes = NumRedNodes1 + NumRedNodes2 + NumRedNodes3 + NumBlackNodes1 + NumBlackNodes2;
    u32 MaxNumEdges = DemoState->NumGraphNodes * DemoState->NumGraphNodes;

    DemoState->NumCellsAxis = 128;
    DemoState->WorldRadius = 3.5f;
    DemoState->CellWorldDim = (2.0f * DemoState->WorldRadius) / f32(DemoState->NumCellsAxis);

    GraphCreateBuffers(DemoState->NumGraphNodes, MaxNumEdges);
            
    // NOTE: Get pointers to GPU memory for our graph
    v2* NodePosGpu = VkCommandsPushWriteArray(Commands, DemoState->NodePosBuffer, v2, DemoState->NumGraphNodes,
                                              BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                              BarrierMask(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
    f32* NodeDegreeGpu = VkCommandsPushWriteArray(Commands, DemoState->NodeDegreeBuffer, f32, DemoState->NumGraphNodes,
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
        f32 NodeSize = 5.0f;
        for (u32 RedNodeId = RedNodesStart1; RedNodeId < RedNodesStart1 + NumRedNodes1; ++RedNodeId)
        {
            GraphNodeInit(f32(NumBlackNodes1), V3(1, 0, 0), NodeSize, NodePosGpu + RedNodeId, NodeDegreeGpu + RedNodeId,
                          NodeDrawGpu + RedNodeId);
        }

        for (u32 RedNodeId = RedNodesStart2; RedNodeId < RedNodesStart2 + NumRedNodes2; ++RedNodeId)
        {
            GraphNodeInit(f32(NumBlackNodes2), V3(1, 0, 0), NodeSize, NodePosGpu + RedNodeId, NodeDegreeGpu + RedNodeId,
                          NodeDrawGpu + RedNodeId);
        }

        for (u32 RedNodeId = RedNodesStart3; RedNodeId < RedNodesStart3 + NumRedNodes3; ++RedNodeId)
        {
            GraphNodeInit(f32(NumBlackNodes1 + NumBlackNodes2), V3(1, 0, 0), NodeSize, NodePosGpu + RedNodeId, NodeDegreeGpu + RedNodeId,
                          NodeDrawGpu + RedNodeId);
        }

        for (u32 BlackNodeId = BlackNodesStart1; BlackNodeId < BlackNodesStart1 + NumBlackNodes1; ++BlackNodeId)
        {
            GraphNodeInit(f32(NumRedNodes1 + NumRedNodes3), V3(0, 0, 0), NodeSize, NodePosGpu + BlackNodeId, NodeDegreeGpu + BlackNodeId,
                          NodeDrawGpu + BlackNodeId);
        }

        for (u32 BlackNodeId = BlackNodesStart2; BlackNodeId < BlackNodesStart2 + NumBlackNodes2; ++BlackNodeId)
        {
            GraphNodeInit(f32(NumRedNodes2 + NumRedNodes3), V3(0, 0, 0), NodeSize, NodePosGpu + BlackNodeId, NodeDegreeGpu + BlackNodeId,
                          NodeDrawGpu + BlackNodeId);
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
            DemoState->EdgeIndexBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                        sizeof(u32) * 2 * DemoState->NumGraphDrawEdges);
            DemoState->EdgeColorBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                        sizeof(u32) * DemoState->NumGraphDrawEdges);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->EdgeIndexBuffer);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->EdgeColorBuffer);
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
        u32* EdgeIndexGpu = VkCommandsPushWriteArray(Commands, DemoState->EdgeIndexBuffer, u32, 2*DemoState->NumGraphDrawEdges,
                                                     BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                     BarrierMask(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
        u32* EdgeColorGpu = VkCommandsPushWriteArray(Commands, DemoState->EdgeColorBuffer, u32, DemoState->NumGraphDrawEdges,
                                                     BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                     BarrierMask(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));

        for (u32 CurrNodeId = 0; CurrNodeId < BlackNodesStart1; ++CurrNodeId)
        {
            graph_node_edges CurrNodeEdges = NodeEdgeGpu[CurrNodeId];

            for (u32 EdgeId = CurrNodeEdges.StartConnections; EdgeId < CurrNodeEdges.EndConnections; ++EdgeId)
            {
                u32 OtherNodeId = EdgeGpu[EdgeId];

                EdgeIndexGpu[2*EdgeId + 0] = CurrNodeId;
                EdgeIndexGpu[2*EdgeId + 1] = OtherNodeId;

                EdgeColorGpu[EdgeId] = ((0u & 0xFF) << 0) | ((0u & 0xFF) << 8) | ((0xFFu & 0xFF) << 16) | ((0xFFu & 0xFF) << 24);
            }
        }
    }
}

inline void GraphInitTest3(vk_commands* Commands)
{
    u32 NumNodes = 30000;
    DemoState->NumGraphRedNodes = NumNodes;
    DemoState->NumGraphNodes = NumNodes;

    u32 MaxNumEdges = 1;
    
    DemoState->NumCellsAxis = 128;
    DemoState->WorldRadius = 3.5f;
    DemoState->CellWorldDim = (2.0f * DemoState->WorldRadius) / f32(DemoState->NumCellsAxis);

    GraphCreateBuffers(DemoState->NumGraphNodes, 1);
            
    // NOTE: Get pointers to GPU memory for our graph
    v2* NodePosGpu = VkCommandsPushWriteArray(Commands, DemoState->NodePosBuffer, v2, DemoState->NumGraphNodes,
                                              BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                              BarrierMask(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
    f32* NodeDegreeGpu = VkCommandsPushWriteArray(Commands, DemoState->NodeDegreeBuffer, f32, DemoState->NumGraphNodes,
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
    f32 NodeSize = 5.0f;
    for (u32 NodeId = 0; NodeId < NumNodes; ++NodeId)
    {
        GraphNodeInit(0, V3(1, 0, 0), NodeSize, NodePosGpu + NodeId, NodeDegreeGpu + NodeId, NodeDrawGpu + NodeId);
    }
        
    // NOTE: Save on memory since nodes are double represented for sim
    {
        DemoState->NumGraphDrawEdges = 1;
        DemoState->EdgeIndexBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                    sizeof(u32) * 2 * DemoState->NumGraphDrawEdges);
        DemoState->EdgeColorBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                    sizeof(u32) * DemoState->NumGraphDrawEdges);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->EdgeIndexBuffer);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->EdgeColorBuffer);
    }
}

inline void GraphInitFromFile(vk_commands* Commands)
{
    temp_mem TempMem = BeginTempMem(&DemoState->TempArena);
    FILE* GraphFile = fopen("preprocessed.bin", "rb");

    file_header FileHeader = {};
    fread(&FileHeader, sizeof(file_header), 1, GraphFile);
            
    DemoState->NumGraphRedNodes = u32(FileHeader.NumAccounts);
    DemoState->NumGraphNodes = u32(FileHeader.NumAccounts + FileHeader.NumHashtags);

    DemoState->NumCellsAxis = 128;
    DemoState->WorldRadius = 1.5f;
    DemoState->CellWorldDim = (2.0f * DemoState->WorldRadius) / f32(DemoState->NumCellsAxis);

    GraphCreateBuffers(DemoState->NumGraphNodes, (u32)FileHeader.NumEdges * 2);
        
    // NOTE: Get pointers to GPU memory for our graph
    v2* NodePosGpu = VkCommandsPushWriteArray(Commands, DemoState->NodePosBuffer, v2, DemoState->NumGraphNodes,
                                              BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                              BarrierMask(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
    f32* NodeDegreeGpu = VkCommandsPushWriteArray(Commands, DemoState->NodeDegreeBuffer, f32, DemoState->NumGraphNodes,
                                              BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                              BarrierMask(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
    graph_node_edges* NodeEdgeGpu = VkCommandsPushWriteArray(Commands, DemoState->NodeEdgeBuffer, graph_node_edges, DemoState->NumGraphNodes,
                                                             BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                             BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
    u32* EdgeGpu = VkCommandsPushWriteArray(Commands, DemoState->EdgeBuffer, u32, FileHeader.NumEdges * 2,
                                            BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                            BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
    graph_node_draw* NodeDrawGpu = VkCommandsPushWriteArray(Commands, DemoState->NodeDrawBuffer, graph_node_draw, DemoState->NumGraphNodes,
                                                            BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                            BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));

    file_account* FileAccountArray = PushArray(&DemoState->TempArena, file_account, FileHeader.NumAccounts);
    fseek(GraphFile, (u32)FileHeader.AccountOffset, SEEK_SET);
    fread(FileAccountArray, sizeof(file_account) * FileHeader.NumAccounts, 1, GraphFile);

    file_hashtag* FileHashtagArray = PushArray(&DemoState->TempArena, file_hashtag, FileHeader.NumHashtags);
    fseek(GraphFile, (u32)FileHeader.HashtagOffset, SEEK_SET);
    fread(FileHashtagArray, sizeof(file_hashtag) * FileHeader.NumHashtags, 1, GraphFile);
            
    // NOTE: Create Nodes
    {                
        for (u32 AccountId = 0; AccountId < FileHeader.NumAccounts; ++AccountId)
        {
            file_account* CurrAccount = FileAccountArray + AccountId;

            u32 NodeId = AccountId;
            // TODO: Set degree correctly
            // TODO: Set size based on follower count
            GraphNodeInit(0, V3(0, 0, 0), 0.005f, NodePosGpu + NodeId, NodeDegreeGpu + NodeId, NodeDrawGpu + NodeId);
        }

        for (u32 HashtagId = 0; HashtagId < FileHeader.NumHashtags; ++HashtagId)
        {
            file_hashtag* CurrHashtag = FileHashtagArray + HashtagId;

            u32 NodeId = HashtagId + (u32)FileHeader.NumAccounts;
            GraphNodeInit(0, V3(0, 0, 0), 0.005f, NodePosGpu + NodeId, NodeDegreeGpu + NodeId, NodeDrawGpu + NodeId);
        }
    }

    // NOTE: Create edges
    {
        for (u32 AccountId = 0; AccountId < FileHeader.NumAccounts; ++AccountId)
        {
            file_account* CurrAccount = FileAccountArray + AccountId;

            u32 NodeId = AccountId;
            NodeEdgeGpu[AccountId].StartConnections = DemoState->NumGraphEdges;
            NodeEdgeGpu[AccountId].EndConnections = DemoState->NumGraphEdges + CurrAccount->NumEdges;

            temp_mem EdgeTempMem = BeginTempMem(&DemoState->TempArena);

            file_edge* AccountEdges = PushArray(&DemoState->TempArena, file_edge, CurrAccount->NumEdges);
            fseek(GraphFile, (u32)CurrAccount->EdgeOffset, SEEK_SET);
            fread(AccountEdges, sizeof(file_edge) * CurrAccount->NumEdges, 1, GraphFile);
                    
            for (u32 EdgeId = 0; EdgeId < CurrAccount->NumEdges; ++EdgeId)
            {
                Assert(DemoState->NumGraphEdges < FileHeader.NumEdges);
                EdgeGpu[DemoState->NumGraphEdges++] = u32(AccountEdges[EdgeId].OtherId + FileHeader.NumAccounts);
            }

            EndTempMem(EdgeTempMem);
        }

        DemoState->NumGraphDrawEdges = DemoState->NumGraphEdges;
        DemoState->EdgeIndexBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                    sizeof(u32) * 2 * DemoState->NumGraphDrawEdges);
        DemoState->EdgeColorBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                    sizeof(u32) * DemoState->NumGraphDrawEdges);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->EdgeIndexBuffer);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->EdgeColorBuffer);

        u32* EdgeIndexGpu = VkCommandsPushWriteArray(Commands, DemoState->EdgeIndexBuffer, u32, 2*DemoState->NumGraphDrawEdges,
                                                     BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                     BarrierMask(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
        u32* EdgeColorGpu = VkCommandsPushWriteArray(Commands, DemoState->EdgeColorBuffer, u32, DemoState->NumGraphDrawEdges,
                                                     BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                     BarrierMask(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
                
        for (u32 HashtagId = 0; HashtagId < FileHeader.NumHashtags; ++HashtagId)
        {
            file_hashtag* CurrHashtag = FileHashtagArray + HashtagId;

            u32 NodeId = u32(FileHeader.NumAccounts + HashtagId);
            NodeEdgeGpu[NodeId].StartConnections = DemoState->NumGraphEdges;
            NodeEdgeGpu[NodeId].EndConnections = DemoState->NumGraphEdges + CurrHashtag->NumEdges;

            temp_mem EdgeTempMem = BeginTempMem(&DemoState->TempArena);

            file_edge* HashtagEdges = PushArray(&DemoState->TempArena, file_edge, CurrHashtag->NumEdges);
            fseek(GraphFile, (u32)CurrHashtag->EdgeOffset, SEEK_SET);
            fread(HashtagEdges, sizeof(file_edge) * CurrHashtag->NumEdges, 1, GraphFile);
                    
            for (u32 EdgeId = 0; EdgeId < CurrHashtag->NumEdges; ++EdgeId)
            {
                Assert(DemoState->NumGraphEdges < 2*FileHeader.NumEdges);
                EdgeGpu[DemoState->NumGraphEdges++] = (u32)HashtagEdges[EdgeId].OtherId;
            }

            EndTempMem(EdgeTempMem);
        }

        for (u32 CurrNodeId = 0; CurrNodeId < FileHeader.NumAccounts; ++CurrNodeId)
        {
            graph_node_edges CurrNodeEdges = NodeEdgeGpu[CurrNodeId];
                    
            for (u32 EdgeId = CurrNodeEdges.StartConnections; EdgeId < CurrNodeEdges.EndConnections; ++EdgeId)
            {
                u32 OtherNodeId = EdgeGpu[EdgeId];

                EdgeIndexGpu[2*EdgeId + 0] = CurrNodeId;
                EdgeIndexGpu[2*EdgeId + 1] = OtherNodeId;

                EdgeColorGpu[EdgeId] = ((0u & 0xFF) << 0) | ((0u & 0xFF) << 8) | ((0xFFu & 0xFF) << 16) | ((0xFFu & 0xFF) << 24);
            }
        }
    }

    EndTempMem(TempMem);
}

//
// NOTE: Sort Functions
//

inline sort_descriptor SortDescriptorCreate(vk_commands* Commands, u32 FlipSize, u32 PassId, u32 N)
{
    sort_descriptor Result = {};
    for (u32 Index = 0; Index < ArrayCount(Result.UniformBuffer); ++Index)
    {
        Result.UniformBuffer[Index] = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                     sizeof(sort_uniform_data));

        sort_uniform_data* GpuData = VkCommandsPushWriteStruct(Commands, Result.UniformBuffer[Index], sort_uniform_data,
                                                               BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                               BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));

        GpuData->ArraySize = DemoState->SortArraySize;
        GpuData->FlipSize = FlipSize;
        GpuData->PassId = PassId;
        GpuData->N = N;
        GpuData->StartIndexOffset = Index * CeilU32(f32(DemoState->SortArraySize) / 2048.0f) / 2;
                
        Result.Descriptor[Index] = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, DemoState->SortDescLayout);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, Result.Descriptor[Index], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, Result.UniformBuffer[Index]);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, Result.Descriptor[Index], 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->SortBuffer);
    }
    
    return Result;
}

inline void SortGlobalFlip(vk_commands* Commands, u32 PassId)
{
    u32 DispatchX = CeilU32(f32(DemoState->SortArraySize) / 2048.0f) / 2;

    for (u32 EventIndex = 0; EventIndex < ArrayCount(DemoState->SortEvents); ++EventIndex)
    {
        // TODO: Create a API
        VkBufferMemoryBarrier Barrier = {};
        Barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        Barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        Barrier.buffer = DemoState->SortBuffer;
        Barrier.offset = EventIndex * DispatchX * 2048;
        Barrier.size = DispatchX * 2048;
        
        vkCmdWaitEvents(Commands->Buffer, 1, DemoState->SortEvents + EventIndex, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, 1, &Barrier, 0, 0);
        vkCmdResetEvent(Commands->Buffer, DemoState->SortEvents[EventIndex], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    
        VkDescriptorSet DescriptorSets[] =
            {
                DemoState->SortGlobalFlipDescriptors[PassId - 11].Descriptor[EventIndex],
            };
        VkComputeDispatch(Commands, DemoState->SortGlobalFlipPipeline, DescriptorSets, ArrayCount(DescriptorSets), DispatchX, 1, 1);

        vkCmdSetEvent(Commands->Buffer, DemoState->SortEvents[EventIndex], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

#if 0    
    VkBarrierBufferAdd(Commands, DemoState->SortBuffer,
                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    VkCommandsBarrierFlush(Commands);
#endif
}

inline void SortLocalDisperse(vk_commands* Commands)
{
    u32 DispatchX = CeilU32(f32(DemoState->SortArraySize) / 2048.0f) / 2;
    
    for (u32 EventIndex = 0; EventIndex < ArrayCount(DemoState->SortEvents); ++EventIndex)
    {
        // TODO: Create a API
        VkBufferMemoryBarrier Barrier = {};
        Barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        Barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        Barrier.buffer = DemoState->SortBuffer;
        Barrier.offset = EventIndex * DispatchX * 2048;
        Barrier.size = DispatchX * 2048;
        
        vkCmdWaitEvents(Commands->Buffer, 1, DemoState->SortEvents + EventIndex, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, 1, &Barrier, 0, 0);
        vkCmdResetEvent(Commands->Buffer, DemoState->SortEvents[EventIndex], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        VkDescriptorSet DescriptorSets[] =
            {
                DemoState->SortLocalDisperseDescriptor.Descriptor[EventIndex],
            };
        VkComputeDispatch(Commands, DemoState->SortLocalDispersePipeline, DescriptorSets, ArrayCount(DescriptorSets), DispatchX, 1, 1);

        vkCmdSetEvent(Commands->Buffer, DemoState->SortEvents[EventIndex], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

#if 0
    VkBarrierBufferAdd(Commands, DemoState->SortBuffer,
                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    VkCommandsBarrierFlush(Commands);
#endif
}

inline void SortGlobalDisperse(vk_commands* Commands, u32 PassId)
{
    u32 DispatchX = CeilU32(f32(DemoState->SortArraySize) / 2048.0f) / 2;
        
    for (u32 EventIndex = 0; EventIndex < ArrayCount(DemoState->SortEvents); ++EventIndex)
    {
        // TODO: Create a API
        VkBufferMemoryBarrier Barrier = {};
        Barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        Barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        Barrier.buffer = DemoState->SortBuffer;
        Barrier.offset = EventIndex * DispatchX * 2048;
        Barrier.size = DispatchX * 2048;
        
        vkCmdWaitEvents(Commands->Buffer, 1, DemoState->SortEvents + EventIndex, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, 1, &Barrier, 0, 0);
        vkCmdResetEvent(Commands->Buffer, DemoState->SortEvents[EventIndex], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    
        VkDescriptorSet DescriptorSets[] =
            {
                DemoState->SortGlobalDisperseDescriptors[PassId - 10].Descriptor[EventIndex],
            };
        VkComputeDispatch(Commands, DemoState->SortGlobalDispersePipeline, DescriptorSets, ArrayCount(DescriptorSets), DispatchX, 1, 1);

        vkCmdSetEvent(Commands->Buffer, DemoState->SortEvents[EventIndex], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

#if 0
    VkBarrierBufferAdd(Commands, DemoState->SortBuffer,
                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    VkCommandsBarrierFlush(Commands);
#endif
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

        Scene->Camera = CameraFlatCreate(V3(0, 0, -1), 60.0f, 10.0f, 4.0f, 0.01f, 1000.0f);
        
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
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
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

        // NOTE: Graph Init Grid Pipeline
        {
            VkDescriptorSetLayout Layouts[] =
                {
                    DemoState->GraphDescLayout,
                };
            
            DemoState->GraphInitGridPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                       "shader_graph_init_grid.spv", "main", Layouts, ArrayCount(Layouts));
        }

        // NOTE: Graph Nearby Pipeline
        {
            VkDescriptorSetLayout Layouts[] =
                {
                    DemoState->GraphDescLayout,
                };
            
            DemoState->GraphRepulsionPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                        "shader_graph_repulsion.spv", "main", Layouts, ArrayCount(Layouts));
        }

        // NOTE: Graph Calc Global Speed Pipeline
        {
            VkDescriptorSetLayout Layouts[] =
                {
                    DemoState->GraphDescLayout,
                };
            
            DemoState->GraphCalcGlobalSpeedPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                              "shader_graph_calc_global_speed.spv", "main", Layouts, ArrayCount(Layouts));
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

        {
            vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&DemoState->SortDescLayout);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutEnd(RenderState->Device, &Builder);
        }

        // NOTE: Bitonic Merge Sort Pipelines
        {
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
            DemoState->AttractionMultiplier = 1.0f;
            DemoState->RepulsionMultiplier = 1.0f;
            DemoState->RepulsionSoftner = 0.05f * 0.05f;
            DemoState->GravityMultiplier = 1.0f;

            DemoState->PauseSim = false;
            GraphInitTest3(Commands);

            global_move* GlobalMoveGpu = VkCommandsPushWriteStruct(Commands, DemoState->GlobalMoveBuffer, global_move,
                                                                   BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                                   BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
            *GlobalMoveGpu = {};
            GlobalMoveGpu->Speed = 1.0f;
            GlobalMoveGpu->SpeedEfficiency = 1.0f;
            GlobalMoveGpu->JitterToleranceConstant = 1.0f;
            GlobalMoveGpu->MaxJitterTolerance = 10.0f;
        }

        // TODO: This is for testing
        // NOTE: Init Sort Data
        {
            u32 NumPasses = 20;
            DemoState->SortArraySize = (u32)Pow(2.0f, (f32)NumPasses);

            for (u32 EventId = 0; EventId < ArrayCount(DemoState->SortEvents); ++EventId)
            {
                VkEventCreateInfo CreateInfo = {};
                CreateInfo.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;
                //CreateInfo.flags = VK_EVENT_CREATE_DEVICE_ONLY_BIT_KHR;
                
                VkCheckResult(vkCreateEvent(RenderState->Device, &CreateInfo, 0, DemoState->SortEvents + EventId));
            }
            
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
                UiPanelText(&Panel, "Attraction Multiplier:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 10.0f, &DemoState->AttractionMultiplier);
                UiPanelNumberBox(&Panel, 0.0f, 10.0f, &DemoState->AttractionMultiplier);
                UiPanelNextRow(&Panel);            

                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "Repulsion Multiplier:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 10.0f, &DemoState->RepulsionMultiplier);
                UiPanelNumberBox(&Panel, 0.0f, 10.0f, &DemoState->RepulsionMultiplier);
                UiPanelNextRow(&Panel);            

                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "Repulsion Softner:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 0.5f, &DemoState->RepulsionSoftner);
                UiPanelNumberBox(&Panel, 0.0f, 0.5f, &DemoState->RepulsionSoftner);
                UiPanelNextRow(&Panel);            

                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "Gravity Multiplier:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 10.0f, &DemoState->GravityMultiplier);
                UiPanelNumberBox(&Panel, 0.0f, 10.0f, &DemoState->GravityMultiplier);
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

                    GpuData->AttractionMultiplier = DemoState->AttractionMultiplier;
                    GpuData->RepulsionMultiplier = DemoState->RepulsionMultiplier;
                    GpuData->RepulsionSoftner = DemoState->RepulsionSoftner;
                    GpuData->GravityMultiplier = DemoState->GravityMultiplier;

                    GpuData->CellDim = DemoState->CellWorldDim;
                    GpuData->WorldRadius = DemoState->WorldRadius;
                    GpuData->NumCellsDim = DemoState->NumCellsAxis;

                }
            }
            
            VkCommandsTransferFlush(Commands, RenderState->Device);
        }

        // TODO: Test Big Sort
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
     
            // NOTE: Local FD
            {
                u32 DispatchX = CeilU32(f32(DemoState->SortArraySize) / 2048.0f) / 2;
                        
                for (u32 EventIndex = 0; EventIndex < ArrayCount(DemoState->SortEvents); ++EventIndex)
                {
                    // TODO: Create a API
                    VkBufferMemoryBarrier Barrier = {};
                    Barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                    Barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                    Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                    Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    Barrier.buffer = DemoState->SortBuffer;
                    Barrier.offset = EventIndex * DispatchX * 2048;
                    Barrier.size = DispatchX * 2048;
                
                    VkDescriptorSet DescriptorSets[] =
                        {
                            DemoState->SortLocalFdDescriptor.Descriptor[EventIndex],
                        };
                    VkComputeDispatch(Commands, DemoState->SortLocalFdPipeline, DescriptorSets, ArrayCount(DescriptorSets), DispatchX, 1, 1);

                    vkCmdSetEvent(Commands->Buffer, DemoState->SortEvents[EventIndex], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                }

#if 0
                VkBarrierBufferAdd(Commands, DemoState->SortBuffer,
                                   VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                VkCommandsBarrierFlush(Commands);
#endif
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

            vkCmdResetEvent(Commands->Buffer, DemoState->SortEvents[0], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            vkCmdResetEvent(Commands->Buffer, DemoState->SortEvents[1], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }
        
        // NOTE: Simulate graph layout
        {
            // TODO: Remove the clears here (reduction we should be able to never clear, counter we can clear in a cs pass)
            vkCmdFillBuffer(Commands->Buffer, DemoState->GlobalMoveReductionBuffer, 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(Commands->Buffer, DemoState->GlobalMoveCounterBuffer, 0, VK_WHOLE_SIZE, 0);
            
            VkDescriptorSet DescriptorSets[] =
                {
                    DemoState->GraphDescriptor,
                };
            u32 DispatchX = CeilU32(f32(DemoState->NumGraphNodes) / 32.0f);

            VkBarrierBufferAdd(Commands, DemoState->NodeForceBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkCommandsBarrierFlush(Commands);
            
            // NOTE: Graph Attraction
            VkComputeDispatch(Commands, DemoState->GraphMoveConnectionsPipeline, DescriptorSets, ArrayCount(DescriptorSets), DispatchX, 1, 1);

            VkBarrierBufferAdd(Commands, DemoState->NodeForceBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkBarrierBufferAdd(Commands, DemoState->NodeCellIdBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkCommandsBarrierFlush(Commands);

            // NOTE: Graph Init Graph Connections
            //VkComputeDispatch(Commands, DemoState->GraphInitGridPipeline, DescriptorSets, ArrayCount(DescriptorSets), DemoState->NumCellsAxis, DemoState->NumCellsAxis, 1);

            VkBarrierBufferAdd(Commands, DemoState->NodeForceBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkBarrierBufferAdd(Commands, DemoState->NodeCellIdBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkBarrierBufferAdd(Commands, DemoState->GridCellBlockBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkBarrierBufferAdd(Commands, DemoState->GridCellHeaderBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkBarrierBufferAdd(Commands, DemoState->GridCellBlockCounter,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkCommandsBarrierFlush(Commands);

            // NOTE: Graph Repulsion
            VkComputeDispatch(Commands, DemoState->GraphRepulsionPipeline, DescriptorSets, ArrayCount(DescriptorSets), DispatchX, 1, 1);

            VkBarrierBufferAdd(Commands, DemoState->NodeForceBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkCommandsBarrierFlush(Commands);

            // NOTE: Graph Calc Global Speed
            {
                u32 GlobalSpeedDispatchX = 1;
                u32 NumGroupsInPass = CeilU32(f32(DemoState->NumGraphNodes) / 32.0f);
                while (NumGroupsInPass > 1)
                {
                    GlobalSpeedDispatchX += NumGroupsInPass;
                    NumGroupsInPass = CeilU32(f32(NumGroupsInPass) / 32.0f);
                }

                // TODO: REMOVE
                //GlobalSpeedDispatchX = 1;
                
                VkComputeDispatch(Commands, DemoState->GraphCalcGlobalSpeedPipeline, DescriptorSets, ArrayCount(DescriptorSets), GlobalSpeedDispatchX, 1, 1);
            }
            
            VkBarrierBufferAdd(Commands, DemoState->GlobalMoveBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkCommandsBarrierFlush(Commands);
            
            // NOTE: Graph Update Nodes
            VkComputeDispatch(Commands, DemoState->GraphUpdateNodesPipeline, DescriptorSets, ArrayCount(DescriptorSets), DispatchX, 1, 1);

            VkBarrierBufferAdd(Commands, DemoState->NodePosBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkBarrierBufferAdd(Commands, DemoState->EdgeIndexBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
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
            vkCmdBindVertexBuffers(Commands->Buffer, 0, 1, &DemoState->NodePosBuffer, &Offset);
            vkCmdBindIndexBuffer(Commands->Buffer, DemoState->EdgeIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

            //vkCmdDrawIndexed(Commands->Buffer, 2*DemoState->NumGraphDrawEdges, 1, 0, 0, 0);
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

//=========================================================================================================================================
// NOTE: Old code for testing
//=========================================================================================================================================

#if 0
        
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
#endif
