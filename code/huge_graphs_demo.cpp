
#include "huge_graphs_demo.h"

#define FFX_CPP
#include "FFX_ParallelSort.h"

/*

  NOTE:

    Based on the below paper:

    - https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0098679
        - https://github.com/bhargavchippada/forceatlas2/blob/master/fa2/fa2util.py

    Optimizations based on this blog post and these references:

    - https://medium.com/rapids-ai/large-graph-visualization-with-rapids-cugraph-590d07edce33
    - https://iss.oden.utexas.edu/Publications/Papers/burtscher11.pdf
        - https://github.com/govertb/GPUGraphLayout

    Papers/blog posts used for various optimizations:

    - (Bitonic Merge Sort) https://poniesandlight.co.uk/reflect/bitonic_merge_sort/
    - (Parallel Sort) https://github.com/GPUOpen-Effects/FidelityFX-ParallelSort
    - (Tree Building) https://research.nvidia.com/sites/default/files/pubs/2012-06_Maximizing-Parallelism-in/karras2012hpg_paper.pdf
        - https://developer.nvidia.com/blog/thinking-parallel-part-ii-tree-traversal-gpu/
        - https://forums.developer.nvidia.com/t/thinking-parallel-part-iii-tree-construction-on-the-gpu/148397
    - https://jheer.github.io/barnes-hut/
        - https://github.com/bhargavchippada/forceatlas2/blob/master/fa2/fa2util.py

    Other Papers that are related:

    - https://www.cse.iitb.ac.in/~rhushabh/publications/octree
    - http://mgarland.org/files/papers/gpubvh.pdf
        
    Graph I'm visualizing:

    - https://medium.com/swlh/watch-six-decade-long-disinformation-operations-unfold-in-six-minutes-5f69a7e75fb3

    Other Graph Visualizers:
    
    - https://pygraphviz.github.io/
    - https://networkx.org/documentation/stable/reference/drawing.html#module-networkx.drawing.nx_pylab

  TODO: Bugs/Perf Optimizations

    - Pipeline cmd lists instead of waiting each time we submit
  
    - Because I use a BVH instead of a quad tree, I get artifacts with more approximate theta values due to probably incorrect size calc
        - Use a quad tree?
        - Check if size calc can be done for the BVH instead
        - Use distance based barnes hutt like the GPUGraphLayout CUDA example

    - Line/node drawing are big bottlenecks due to export order.
        - Draw nodes first and use depth testing. If we get partial overlap, still set the depth. Only if alpha == 0 do we not set depth.
          Give each node increasing depth so we get correct overlap (front to back for better perf)
        - Draw lines after using depth testing.
    - Use CS for drawing lines and atomics instead? Then we don't have any raster order requirements, just do additive blending

    - Tree building optimizations:
        - Bucket nodes that have the same morton key. Makes a more efficient tree.
        - Reorder nodes post sorting to the sorted order. Might make access much faster for traversal. Keep mapping table only for
          attraction calculation
        - Use LDS optimization for tree summarization. For 4m nodes, we spend ~4ms on this, can probably be much lower
        - Investigate why we don't actually hit 100% occupancy when we can hit that occupancy according to RGP

    - Bitonic Merge Sort (probably still always slower than parallel sort)
        - Make it work on non pow2 arrays
        - Reorder LDS stores to avoid bank conflicts. Right now its naive
        - Once SC fixes cache invalidations on barrier, try the 1 pass version
            - Investigate which types of memomry barriers we actually need for this pass at various points
        - We can also probably remove the index1 < ArraySize part of our if for swapping
    
 */

inline f32 RandFloat()
{
    f32 Result = f32(rand()) / f32(RAND_MAX);
    return Result;
}

// TODO: REMOVE
inline void StringMovePastComma(string* CurrChar)
{
    b32 InQuotes = false;
    while (CurrChar->NumChars > 0)
    {
        if (CurrChar->Chars[0] == ',' && !InQuotes)
        {
            break;
        }
        
        if (CurrChar->Chars[0] > 127)
        {
            // NOTE: https://stackoverflow.com/questions/4459571/how-to-recognize-if-a-string-contains-unicode-chars#:~:text=Unicode%20is%20explicitly%20defined%20such,includes%20only%20the%20English%20alphabet.
            // TODO: Small hack to skip unicode chars. I'm assuming theyre 16bit
            AdvanceString(CurrChar, 2u);
        }
        else
        {
            if (CurrChar->Chars[0] == '"')
            {
                InQuotes = !InQuotes;
            }

            AdvanceString(CurrChar, 1u);
        }
    }

    AdvanceString(CurrChar, 1u);
}

//
// NOTE: Graph Creation Functions
// 

inline void GraphNodeInit(f32 Degree, v3 Color, f32 Scale, v2* NodePos, f32* NodeDegree, graph_node_draw* NodeDraw, b32 BiggerArea = false)
{
    f32 StartSize = 40.0f;
    if (BiggerArea)
    {
        StartSize = 300.0f;
    }
    
    *NodePos = StartSize * V2(2.0f * RandFloat() - 1.0f, 2.0f * RandFloat() - 1.0f);
    //*NodePos = 2.0f * Normalize(V2(2.0f * RandFloat() - 1.0f, 2.0f * RandFloat() - 1.0f));
    *NodeDegree = Degree + 1;
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
                                           sizeof(graph_edge) * NumEdges * 2);
    DemoState->NodeDrawBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                               sizeof(graph_node_draw) * DemoState->NumGraphNodes);
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
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->GlobalMoveBuffer);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->GlobalMoveReductionBuffer);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->GraphDescriptor, 13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->GlobalMoveCounterBuffer);
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
    graph_edge* EdgeGpu = VkCommandsPushWriteArray(Commands, DemoState->EdgeBuffer, graph_edge, MaxNumEdges,
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
                    EdgeGpu[DemoState->NumGraphEdges].OtherNodeId = BlackNodeId;
                    EdgeGpu[DemoState->NumGraphEdges].Weight = 1;
                    DemoState->NumGraphEdges += 1;
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
                    EdgeGpu[DemoState->NumGraphEdges].OtherNodeId = RedNodeId;
                    EdgeGpu[DemoState->NumGraphEdges].Weight = 1;
                    DemoState->NumGraphEdges += 1;
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
                u32 OtherNodeId = EdgeGpu[EdgeId].OtherNodeId;

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
    u32 NumRedNodes1 = 1000;
    u32 RedNodesStart2 = RedNodesStart1 + NumRedNodes1;
    u32 NumRedNodes2 = 1000;
    u32 RedNodesStart3 = RedNodesStart2 + NumRedNodes2;
    u32 NumRedNodes3 = 1000;

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
    graph_edge* EdgeGpu = VkCommandsPushWriteArray(Commands, DemoState->EdgeBuffer, graph_edge, MaxNumEdges,
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
                    EdgeGpu[DemoState->NumGraphEdges].OtherNodeId = BlackNodeId;
                    EdgeGpu[DemoState->NumGraphEdges].Weight = 1;
                    DemoState->NumGraphEdges += 1;
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
                    EdgeGpu[DemoState->NumGraphEdges].OtherNodeId = BlackNodeId;
                    EdgeGpu[DemoState->NumGraphEdges].Weight = 1;
                    DemoState->NumGraphEdges += 1;
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
                    EdgeGpu[DemoState->NumGraphEdges].OtherNodeId = BlackNodeId;
                    EdgeGpu[DemoState->NumGraphEdges].Weight = 1;
                    DemoState->NumGraphEdges += 1;
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
                    EdgeGpu[DemoState->NumGraphEdges].OtherNodeId = RedNodeId;
                    EdgeGpu[DemoState->NumGraphEdges].Weight = 1;
                    DemoState->NumGraphEdges += 1;
                    NodeEdgeGpu[BlackNodeId].EndConnections += 1;
                }

                for (u32 RedNodeId = RedNodesStart3; RedNodeId < RedNodesStart3 + NumRedNodes3; ++RedNodeId)
                {
                    Assert(DemoState->NumGraphEdges < MaxNumEdges);
                    EdgeGpu[DemoState->NumGraphEdges].OtherNodeId = RedNodeId;
                    EdgeGpu[DemoState->NumGraphEdges].Weight = 1;
                    DemoState->NumGraphEdges += 1;
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
                    EdgeGpu[DemoState->NumGraphEdges].OtherNodeId = RedNodeId;
                    EdgeGpu[DemoState->NumGraphEdges].Weight = 1;
                    DemoState->NumGraphEdges += 1;
                    NodeEdgeGpu[BlackNodeId].EndConnections += 1;
                }

                for (u32 RedNodeId = RedNodesStart3; RedNodeId < RedNodesStart3 + NumRedNodes3; ++RedNodeId)
                {
                    Assert(DemoState->NumGraphEdges < MaxNumEdges);
                    EdgeGpu[DemoState->NumGraphEdges].OtherNodeId = RedNodeId;
                    EdgeGpu[DemoState->NumGraphEdges].Weight = 1;
                    DemoState->NumGraphEdges += 1;
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
                u32 OtherNodeId = EdgeGpu[EdgeId].OtherNodeId;

                EdgeIndexGpu[2*EdgeId + 0] = CurrNodeId;
                EdgeIndexGpu[2*EdgeId + 1] = OtherNodeId;

                EdgeColorGpu[EdgeId] = ((0u & 0xFF) << 0) | ((0u & 0xFF) << 8) | ((0xFFu & 0xFF) << 16) | ((0xFFu & 0xFF) << 24);
            }
        }
    }
}

inline void GraphInitTest3(vk_commands* Commands)
{
    u32 NumNodes = 4000000;
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
    graph_edge* EdgeGpu = VkCommandsPushWriteArray(Commands, DemoState->EdgeBuffer, graph_edge, MaxNumEdges,
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
    graph_edge* EdgeGpu = VkCommandsPushWriteArray(Commands, DemoState->EdgeBuffer, graph_edge, FileHeader.NumEdges * 2,
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
        f32 NodeSize = 5.0f;
        for (u32 AccountId = 0; AccountId < FileHeader.NumAccounts; ++AccountId)
        {
            file_account* CurrAccount = FileAccountArray + AccountId;

            u32 NodeId = AccountId;
            //GraphNodeInit(f32(CurrAccount->NumEdges), V3(1, 0, 0), logf(100.0f*f32(CurrAccount->NumFollowers)), NodePosGpu + NodeId, NodeDegreeGpu + NodeId, NodeDrawGpu + NodeId, true);
            GraphNodeInit(f32(CurrAccount->NumFollowers), V3(1, 0, 0), logf(100.0f*f32(CurrAccount->NumFollowers)), NodePosGpu + NodeId, NodeDegreeGpu + NodeId, NodeDrawGpu + NodeId, true);
        }

        for (u32 HashtagId = 0; HashtagId < FileHeader.NumHashtags; ++HashtagId)
        {
            file_hashtag* CurrHashtag = FileHashtagArray + HashtagId;

            u32 NodeId = HashtagId + (u32)FileHeader.NumAccounts;
            //GraphNodeInit(f32(CurrHashtag->NumEdges), V3(0, 0, 0), NodeSize, NodePosGpu + NodeId, NodeDegreeGpu + NodeId, NodeDrawGpu + NodeId);
            GraphNodeInit(1, V3(0, 0, 0), NodeSize, NodePosGpu + NodeId, NodeDegreeGpu + NodeId, NodeDrawGpu + NodeId);
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
                EdgeGpu[DemoState->NumGraphEdges].OtherNodeId = u32(AccountEdges[EdgeId].OtherId + FileHeader.NumAccounts);
                EdgeGpu[DemoState->NumGraphEdges].Weight = f32(AccountEdges[EdgeId].Weight);
                DemoState->NumGraphEdges += 1;
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
                EdgeGpu[DemoState->NumGraphEdges].OtherNodeId = (u32)HashtagEdges[EdgeId].OtherId;
                EdgeGpu[DemoState->NumGraphEdges].Weight = f32(HashtagEdges[EdgeId].Weight);
                DemoState->NumGraphEdges += 1;
            }

            EndTempMem(EdgeTempMem);
        }

        for (u32 CurrNodeId = 0; CurrNodeId < FileHeader.NumAccounts; ++CurrNodeId)
        {
            graph_node_edges CurrNodeEdges = NodeEdgeGpu[CurrNodeId];
                    
            for (u32 EdgeId = CurrNodeEdges.StartConnections; EdgeId < CurrNodeEdges.EndConnections; ++EdgeId)
            {
                u32 OtherNodeId = EdgeGpu[EdgeId].OtherNodeId;

                EdgeIndexGpu[2*EdgeId + 0] = CurrNodeId;
                EdgeIndexGpu[2*EdgeId + 1] = OtherNodeId;

                // NOTE: https://medium.com/swlh/watch-six-decade-long-disinformation-operations-unfold-in-six-minutes-5f69a7e75fb3
                // NOTE: We color the edge based on the account year created
                u32 AccountId = CurrNodeId;
                if (AccountId >= FileHeader.NumAccounts)
                {
                    AccountId = OtherNodeId;
                }

                u32 YearCreated = FileAccountArray[AccountId].YearCreated;
                if (YearCreated <= 2013)
                {
                    EdgeColorGpu[EdgeId] = ((115u & 0xFF) << 0) | ((192u & 0xFF) << 8) | ((0x0u & 0xFF) << 16) | ((0xFFu & 0xFF) << 24);
                }
                else if (YearCreated <= 2015)
                {
                    EdgeColorGpu[EdgeId] = ((0u & 0xFF) << 0) | ((196u & 0xFF) << 8) | ((255u & 0xFF) << 16) | ((0xFFu & 0xFF) << 24);
                }
                else if (YearCreated <= 2017)
                {
                    EdgeColorGpu[EdgeId] = ((223u & 0xFF) << 0) | ((137u & 0xFF) << 8) | ((255u & 0xFF) << 16) | ((0xFFu & 0xFF) << 24);
                }
                else
                {
                    EdgeColorGpu[EdgeId] = ((76u & 0xFF) << 0) | ((70u & 0xFF) << 8) | ((62u & 0xFF) << 16) | ((0xFFu & 0xFF) << 24);
                }
            }
        }
    }

    EndTempMem(TempMem);
}

//
// NOTE: Sort Functions
//

inline merge_sort_descriptor MergeSortDescriptorCreate(vk_commands* Commands, u32 FlipSize, u32 PassId, u32 N)
{
    merge_sort_descriptor Result = {};
    Result.UniformBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                          sizeof(merge_sort_uniform_data));

    merge_sort_uniform_data* GpuData = VkCommandsPushWriteStruct(Commands, Result.UniformBuffer, merge_sort_uniform_data,
                                                                 BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                                 BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));

    GpuData->ArraySize = DemoState->NumGraphNodes;
    GpuData->FlipSize = FlipSize;
    GpuData->PassId = PassId;
    GpuData->N = N;
                
    Result.Descriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, DemoState->MergeSortDescLayout);
    VkDescriptorBufferWrite(&RenderState->DescriptorManager, Result.Descriptor, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, Result.UniformBuffer);
    
    return Result;
}

inline void MergeSortGlobalFlip(vk_commands* Commands, u32 PassId)
{
    u32 DispatchX = CeilU32(f32(DemoState->NumGraphNodes) / 2048.0f);

    VkDescriptorSet DescriptorSets[] =
        {
            DemoState->RadixTreeDescriptor,
            DemoState->GraphDescriptor,
            DemoState->MergeSortGlobalFlipDescriptors[PassId - 11].Descriptor,
        };
    VkComputeDispatch(Commands, DemoState->MergeSortGlobalFlipPipeline, DescriptorSets, ArrayCount(DescriptorSets), DispatchX, 1, 1);

    VkBarrierBufferAdd(Commands, DemoState->RadixMortonKeyBuffer,
                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    VkBarrierBufferAdd(Commands, DemoState->RadixElementReMappingBuffer,
                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    VkCommandsBarrierFlush(Commands);
}

inline void MergeSortLocalDisperse(vk_commands* Commands)
{
    u32 DispatchX = CeilU32(f32(DemoState->NumGraphNodes) / 2048.0f);
    
    VkDescriptorSet DescriptorSets[] =
        {
            DemoState->RadixTreeDescriptor,
            DemoState->GraphDescriptor,
            DemoState->MergeSortLocalDisperseDescriptor.Descriptor,
        };
    VkComputeDispatch(Commands, DemoState->MergeSortLocalDispersePipeline, DescriptorSets, ArrayCount(DescriptorSets), DispatchX, 1, 1);

    VkBarrierBufferAdd(Commands, DemoState->RadixMortonKeyBuffer,
                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    VkBarrierBufferAdd(Commands, DemoState->RadixElementReMappingBuffer,
                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    VkCommandsBarrierFlush(Commands);
}

inline void MergeSortGlobalDisperse(vk_commands* Commands, u32 PassId)
{
    u32 DispatchX = CeilU32(f32(DemoState->NumGraphNodes) / 2048.0f);
    
    VkDescriptorSet DescriptorSets[] =
        {
            DemoState->RadixTreeDescriptor,
            DemoState->GraphDescriptor,
            DemoState->MergeSortGlobalDisperseDescriptors[PassId - 10].Descriptor,
        };
    VkComputeDispatch(Commands, DemoState->MergeSortGlobalDispersePipeline, DescriptorSets, ArrayCount(DescriptorSets), DispatchX, 1, 1);

    VkBarrierBufferAdd(Commands, DemoState->RadixMortonKeyBuffer,
                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    VkBarrierBufferAdd(Commands, DemoState->RadixElementReMappingBuffer,
                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    VkCommandsBarrierFlush(Commands);
}

//
// NOTE: Reduction Helpers
//

inline u32 CalcGlobalSpeedNumThreadGroups()
{
    u32 Result = 1;
    u32 NumGroupsInPass = CeilU32(f32(DemoState->NumGraphNodes) / 32.0f);
    while (NumGroupsInPass > 1)
    {
        Result += NumGroupsInPass;
        NumGroupsInPass = CeilU32(f32(NumGroupsInPass) / 32.0f);
    }

    return Result;
}

inline u32 CalcBoundsNumThreadGroups()
{
    u32 Result = 1;
    u32 NumGroupsInPass = CeilU32(f32(DemoState->NumGraphNodes) / 32.0f);
    while (NumGroupsInPass > 1)
    {
        Result += NumGroupsInPass;
        NumGroupsInPass = CeilU32(f32(NumGroupsInPass) / 32.0f);
    }

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

        Scene->Camera = CameraFlatCreate(V3(0, 0, -1), 60.0f, 10.0f, 4.0f, 0.01f, 1000.0f);
        
        Scene->MaxNumRenderMeshes = 2;
        Scene->RenderMeshes = PushArray(&DemoState->Arena, render_mesh, Scene->MaxNumRenderMeshes);
    }

    // NOTE: Init Graph Layout Data
    {
        // NOTE: Graph Shaders
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

            VkDescriptorSetLayout Layouts[] =
                {
                    DemoState->GraphDescLayout,
                };

            // NOTE: Graph Move Connections Pipeline
            DemoState->GraphMoveConnectionsPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                              "shader_graph_move_connections.spv", "main", Layouts, ArrayCount(Layouts));

            // NOTE: Graph Nearby Pipeline
            DemoState->GraphRepulsionPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                        "shader_graph_repulsion.spv", "main", Layouts, ArrayCount(Layouts));

            // NOTE: Graph Calc Global Speed Pipeline
            DemoState->GraphCalcGlobalSpeedPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                              "shader_graph_calc_global_speed.spv", "main", Layouts, ArrayCount(Layouts));

            // NOTE: Graph Update Nodes Pipeline
            DemoState->GraphUpdateNodesPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                          "shader_graph_update_nodes.spv", "main", Layouts, ArrayCount(Layouts));
        }
        
        // NOTE: Radix Tree Data
        {
            {
                vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&DemoState->RadixTreeDescLayout);
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
                VkDescriptorLayoutEnd(RenderState->Device, &Builder);
            }

            VkDescriptorSetLayout Layouts[] =
                {
                    DemoState->RadixTreeDescLayout,
                    DemoState->GraphDescLayout,
                };

            // NOTE: Generate Morton Keys
            DemoState->GenerateMortonKeysPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                            "shader_generate_morton_keys.spv", "main", Layouts, ArrayCount(Layouts));

            // NOTE: Calc World Bounds
            DemoState->CalcWorldBoundsPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                         "shader_calc_world_bounds.spv", "main", Layouts, ArrayCount(Layouts));

            // NOTE: Radix Tree Build
            DemoState->RadixTreeBuildPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                        "shader_radix_tree_build.spv", "main", Layouts, ArrayCount(Layouts));

            // NOTE: Radix Tree Summarize
            DemoState->RadixTreeSummarizePipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                            "shader_radix_tree_summarize.spv", "main", Layouts, ArrayCount(Layouts));

            // NOTE: Radix Tree Repulsion
            DemoState->RadixTreeRepulsionPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                            "shader_radix_tree_repulsion.spv", "main", Layouts, ArrayCount(Layouts));
        }        

        // NOTE: Bitonic Merge Sort Pipelines
        {
            {
                vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&DemoState->MergeSortDescLayout);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutEnd(RenderState->Device, &Builder);
            }

            VkDescriptorSetLayout Layouts[] =
                {
                    DemoState->RadixTreeDescLayout,
                    DemoState->GraphDescLayout,
                    DemoState->MergeSortDescLayout,
                };

            // NOTE: Local FD
            DemoState->MergeSortLocalFdPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                          "shader_merge_local_fd.spv", "main", Layouts, ArrayCount(Layouts));

            // NOTE: Global Flip
            DemoState->MergeSortGlobalFlipPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                             "shader_merge_global_flip.spv", "main", Layouts, ArrayCount(Layouts));

            // NOTE: Local Disperse
            DemoState->MergeSortLocalDispersePipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                                "shader_merge_local_disperse.spv", "main", Layouts, ArrayCount(Layouts));

            // NOTE: Global Disperse
            DemoState->MergeSortGlobalDispersePipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                                 "shader_merge_global_disperse.spv", "main", Layouts, ArrayCount(Layouts));
        }

        // NOTE: Parallel Sort Pipelines
        {
            {
                vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&DemoState->ParallelSortConstantDescLayout);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutEnd(RenderState->Device, &Builder);
            }
            
            {
                vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&DemoState->ParallelSortInputOutputDescLayout);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutEnd(RenderState->Device, &Builder);
            }
            
            {
                vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&DemoState->ParallelSortScanDescLayout);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutEnd(RenderState->Device, &Builder);
            }
            
            {
                vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&DemoState->ParallelSortScratchDescLayout);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
                VkDescriptorLayoutEnd(RenderState->Device, &Builder);
            }

            VkDescriptorSetLayout Layouts[] =
                {
                    DemoState->ParallelSortConstantDescLayout,
                    DemoState->ParallelSortInputOutputDescLayout,
                    DemoState->ParallelSortScanDescLayout,
                    DemoState->ParallelSortScratchDescLayout,
                };

            DemoState->ParallelSortCountPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                           "shader_parallel_sort_count.spv", "main", Layouts, ArrayCount(Layouts), sizeof(u32));

            DemoState->ParallelSortReducePipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                            "shader_parallel_sort_reduce.spv", "main", Layouts, ArrayCount(Layouts), sizeof(u32));

            DemoState->ParallelSortScanPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                          "shader_parallel_sort_scan.spv", "main", Layouts, ArrayCount(Layouts), sizeof(u32));

            DemoState->ParallelSortScanAddPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                             "shader_parallel_sort_scan_add.spv", "main", Layouts, ArrayCount(Layouts), sizeof(u32));
            
            DemoState->ParallelSortScatterPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                             "shader_parallel_sort_scatter.spv", "main", Layouts, ArrayCount(Layouts), sizeof(u32));
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
#if LINE_PIPELINE_2
            
            vk_pipeline_builder Builder = VkPipelineBuilderBegin(&DemoState->TempArena);

            // NOTE: Shaders
            VkPipelineShaderAdd(&Builder, "shader_line_2_vert.spv", "main", VK_SHADER_STAGE_VERTEX_BIT);
            VkPipelineShaderAdd(&Builder, "shader_line_frag.spv", "main", VK_SHADER_STAGE_FRAGMENT_BIT);
                
            // NOTE: Specify input vertex data format
            VkPipelineVertexBindingBegin(&Builder);
            VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32_SFLOAT, sizeof(v3));
            VkPipelineVertexAttributeAddOffset(&Builder, sizeof(v3) + sizeof(v2));
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
            
            DemoState->LinePipeline = VkPipelineBuilderEnd(&Builder, RenderState->Device, &RenderState->PipelineManager,
                                                           DemoState->RenderTarget.RenderPass, 0, DescriptorLayouts,
                                                           ArrayCount(DescriptorLayouts));
            
#else
            
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

#endif
        }
    }

    // TODO: Debug radix tree to see if it makes sense
#if 0
    {
        u32 NumElements = 4096;

        // NOTE: Morton Keys Checks
        {
            FILE* FileHandle = fopen("MortonKeysDump.csv", "rb");
            u64 FileSize = 0;

            u32* MortonKeyArray = PushArray(&DemoState->Arena, u32, NumElements);

            // NOTE: Get file sizes
            fseek(FileHandle, 0L, SEEK_END);
            FileSize = ftell(FileHandle);
            fseek(FileHandle, 0L, SEEK_SET);
        
            char* CsvData = PushArray(&DemoState->Arena, char, FileSize);
            fread(CsvData, FileSize, 1, FileHandle);

            fclose(FileHandle);

            string CurrChar = String(CsvData, FileSize);
            AdvanceCharsToNewline(&CurrChar);
            AdvanceString(&CurrChar, 1);

            u32 NodeCount = 0;
            while (CurrChar.NumChars > 0)
            {
                // NOTE: Skip Element Id
                StringMovePastComma(&CurrChar);
                AdvancePastDeadSpaces(&CurrChar);

                ReadUIntAndAdvance(&CurrChar, MortonKeyArray + NodeCount);
                AdvanceString(&CurrChar, 2u);

                NodeCount += 1;
            }

            Assert(NodeCount == NumElements);

            // NOTE: Check we don't have duplicates and sorted
            for (u32 NodeId = 0; NodeId < NumElements - 1; ++NodeId)
            {
                //Assert(MortonKeyArray[NodeId] < MortonKeyArray[NodeId + 1]);

                if (MortonKeyArray[NodeId] == MortonKeyArray[NodeId + 1])
                {
                    DebugPrintLog("MortonKeyEqual: %u, %u\n", NodeId, NodeId + 1);
                }
                if (MortonKeyArray[NodeId] > MortonKeyArray[NodeId + 1])
                {
                    DebugPrintLog("MortonKeyGreater: %u, %u\n", NodeId, NodeId + 1);
                }
            }
        }

        // NOTE: Radix Tree Checks
        {
            FILE* FileHandle = fopen("GraphDump2.csv", "rb");
            u64 FileSize = 0;

            u32* ChildLeftArray = PushArray(&DemoState->Arena, u32, NumElements);
            u32* ChildRightArray = PushArray(&DemoState->Arena, u32, NumElements);

            // NOTE: Get file sizes
            fseek(FileHandle, 0L, SEEK_END);
            FileSize = ftell(FileHandle);
            fseek(FileHandle, 0L, SEEK_SET);
        
            char* CsvData = PushArray(&DemoState->Arena, char, FileSize);
            fread(CsvData, FileSize, 1, FileHandle);

            fclose(FileHandle);

            string CurrChar = String(CsvData, FileSize);
            AdvanceCharsToNewline(&CurrChar);
            AdvanceString(&CurrChar, 1);

            u32 NodeCount = 0;
            while (CurrChar.NumChars > 0)
            {
                // NOTE: Skip Element Id
                StringMovePastComma(&CurrChar);
                AdvancePastDeadSpaces(&CurrChar);

                ReadIntAndAdvance(&CurrChar, (i32*)ChildLeftArray + NodeCount);
                AdvanceString(&CurrChar, 1u);
                AdvancePastDeadSpaces(&CurrChar);
            
                ReadIntAndAdvance(&CurrChar, (i32*)ChildRightArray + NodeCount);
                AdvanceString(&CurrChar, 2u);

                NodeCount += 1;
            }

            Assert(NodeCount == NumElements - 1);

            // NOTE: Make sure all nodes are only referenced once
            u32* InternalRefCount = PushArray(&DemoState->Arena, u32, NumElements - 1);
            u32* LeafRefCount = PushArray(&DemoState->Arena, u32, NumElements);
            for (u32 NodeId = 0; NodeId < NodeCount; ++NodeId)
            {
                u32 LeftChild = ChildLeftArray[NodeId];
                u32 RightChild = ChildRightArray[NodeId];

                if ((LeftChild & (1 << 31)) != 0)
                {
                    u32 LeafId = LeftChild & (~(1 << 31));
                    LeafRefCount[LeafId] += 1;
                }
                else
                {
                    InternalRefCount[LeftChild] += 1;
                }

                if ((RightChild & (1 << 31)) != 0)
                {
                    u32 LeafId = RightChild & (~(1 << 31));
                    LeafRefCount[LeafId] += 1;
                }
                else
                {
                    InternalRefCount[RightChild] += 1;
                }
            }

            for (u32 InternalNodeId = 0; InternalNodeId < NumElements-1; ++InternalNodeId)
            {
                if (InternalNodeId == 0)
                {
                    Assert(InternalRefCount[InternalNodeId] == 0);
                }
                else
                {
                    //Assert(InternalRefCount[InternalNodeId] == 1);
                    if (InternalRefCount[InternalNodeId] != 1)
                    {
                        DebugPrintLog("Radix Over Referenced: %u, %u\n", InternalNodeId, InternalRefCount[InternalNodeId]);
                    }
                }
            }

            for (u32 LeafNodeId = 0; LeafNodeId < NumElements; ++LeafNodeId)
            {
                //Assert(LeafRefCount[LeafNodeId] == 1);
                if (LeafRefCount[LeafNodeId] != 1)
                {
                    DebugPrintLog("Radix Leaf Over Referenced: %u, %u\n", LeafNodeId, LeafRefCount[LeafNodeId]);
                }
            }
        
            // NOTE: Calculate tree height
        }
    }
#endif
    
    // NOTE: Upload assets
    vk_commands* Commands = &RenderState->Commands;
    VkCommandsBegin(Commands, RenderState->Device);
    {
        render_scene* Scene = &DemoState->Scene;
                                
        // NOTE: Push meshes
        Scene->CircleMeshId = SceneMeshAdd(Scene, AssetsPushQuad());
        Scene->QuadMeshId = SceneMeshAdd(Scene, AssetsPushQuad());
        
        // NOTE: Init graph nodes
        {
            // NOTE: Setup default layout params
            DemoState->AttractionMultiplier = 1.0f;
            DemoState->AttractionWeightPower = 1.0f;
            DemoState->RepulsionMultiplier = 1.0f;
            DemoState->RepulsionSoftner = 0.05f * 0.05f;
            DemoState->GravityMultiplier = 1.0f;
            DemoState->StrongGravityEnabled = true;

            DemoState->PauseSim = false;
            //GraphInitTest3(Commands);
            GraphInitFromFile(Commands);

            global_move* GlobalMoveGpu = VkCommandsPushWriteStruct(Commands, DemoState->GlobalMoveBuffer, global_move,
                                                                   BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                                   BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
            *GlobalMoveGpu = {};
            GlobalMoveGpu->Speed = 1.0f;
            GlobalMoveGpu->SpeedEfficiency = 1.0f;
            GlobalMoveGpu->JitterToleranceConstant = 1.0f;
            GlobalMoveGpu->MaxJitterTolerance = 10.0f;
        }

        // NOTE: Radix Tree Data
        {
            {
                DemoState->RadixTreeUniformBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                   sizeof(radix_tree_uniform_data));

                radix_tree_uniform_data* GpuData = VkCommandsPushWriteStruct(Commands, DemoState->RadixTreeUniformBuffer, radix_tree_uniform_data,
                                                                             BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                                             BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
            
                GpuData->NumNodes = DemoState->NumGraphNodes;
            }

            DemoState->RadixMortonKeyBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                             sizeof(u32) * DemoState->NumGraphNodes);
            DemoState->RadixElementReMappingBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                    sizeof(u32) * DemoState->NumGraphNodes);
            DemoState->RadixTreeChildrenBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                sizeof(u32) * 2 * (DemoState->NumGraphNodes - 1));
            DemoState->RadixTreeParentBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                              sizeof(u32) * (2 * DemoState->NumGraphNodes - 1));
            DemoState->RadixTreeParticleBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                sizeof(v4) * (DemoState->NumGraphNodes - 1));
            DemoState->RadixTreeAtomicsBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                               sizeof(u32) * (DemoState->NumGraphNodes - 1));

            DemoState->GlobalBoundsReductionBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                    sizeof(gpu_bounds) * (DemoState->NumGraphNodes + CeilU32(f32(DemoState->NumGraphNodes) / 32.0f)));
            DemoState->GlobalBoundsCounterBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                  sizeof(u32) * 2);
            DemoState->ElementBoundsBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                            sizeof(gpu_bounds));

            DemoState->RadixTreeDescriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, DemoState->RadixTreeDescLayout);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->RadixTreeDescriptor, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, DemoState->RadixTreeUniformBuffer);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->RadixTreeDescriptor, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->RadixMortonKeyBuffer);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->RadixTreeDescriptor, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->RadixElementReMappingBuffer);

            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->RadixTreeDescriptor, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->RadixTreeChildrenBuffer);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->RadixTreeDescriptor, 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->RadixTreeParentBuffer);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->RadixTreeDescriptor, 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->RadixTreeParticleBuffer);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->RadixTreeDescriptor, 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->RadixTreeAtomicsBuffer);

            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->RadixTreeDescriptor, 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->GlobalBoundsReductionBuffer);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->RadixTreeDescriptor, 8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->GlobalBoundsCounterBuffer);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->RadixTreeDescriptor, 9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->ElementBoundsBuffer);
        }

        // NOTE: Init Sort Data
        {
#if BITONIC_MERGE_SORT
            u32 NumPasses = 20;

            // NOTE: Local Fd Descriptor
            DemoState->MergeSortLocalFdDescriptor = MergeSortDescriptorCreate(Commands, 0, 0, 0);

            // NOTE: Local Disperse Descriptor
            DemoState->MergeSortLocalDisperseDescriptor = MergeSortDescriptorCreate(Commands, 0, 0, 0);

            // NOTE: Global Flip Uniforms
            for (u32 FlipSize = 2048, PassId = 11; FlipSize < DemoState->NumGraphNodes; PassId += 1, FlipSize = FlipSize * 2)
            {
                DemoState->MergeSortGlobalFlipDescriptors[PassId - 11] = MergeSortDescriptorCreate(Commands, FlipSize, PassId, 0);
            }
            
            // NOTE: Global Disperse Uniforms
            for (u32 FlipSize = 1024, PassId = 10; FlipSize < DemoState->NumGraphNodes / 2; PassId += 1, FlipSize = FlipSize * 2)
            {
                DemoState->MergeSortGlobalDisperseDescriptors[PassId - 10] = MergeSortDescriptorCreate(Commands, 0, PassId, FlipSize);
            }
#else

            u32 ScratchBufferSize = 0;
            u32 ReduceScratchBufferSize = 0;
            FFX_ParallelSort_CalculateScratchResourceSize(DemoState->NumGraphNodes, ScratchBufferSize, ReduceScratchBufferSize);

            {
                DemoState->ParallelSortUniformBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                      sizeof(FFX_ParallelSortCB));

                FFX_ParallelSortCB* GpuData = VkCommandsPushWriteStruct(Commands, DemoState->ParallelSortUniformBuffer, FFX_ParallelSortCB,
                                                                        BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                                        BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));

                FFX_ParallelSort_SetConstantAndDispatchData(DemoState->NumGraphNodes, PARALLEL_SORT_MAX_THREAD_GROUPS, *GpuData, DemoState->ParallelSortNumThreadGroups, DemoState->ParallelSortNumReducedThreadGroups);
            }

            DemoState->ParallelSortMortonBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                 sizeof(u32) * DemoState->NumGraphNodes);

            DemoState->ParallelSortPayloadBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                  sizeof(u32) * DemoState->NumGraphNodes);

            DemoState->ParallelSortScratchBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                  ScratchBufferSize);

            DemoState->ParallelSortReducedScratchBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                         ReduceScratchBufferSize);

            DemoState->ParallelSortConstantDescriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, DemoState->ParallelSortConstantDescLayout);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->ParallelSortConstantDescriptor, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, DemoState->ParallelSortUniformBuffer);

            DemoState->ParallelSortInputOutputDescriptor[0] = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, DemoState->ParallelSortInputOutputDescLayout);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->ParallelSortInputOutputDescriptor[0], 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->RadixMortonKeyBuffer);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->ParallelSortInputOutputDescriptor[0], 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->ParallelSortMortonBuffer);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->ParallelSortInputOutputDescriptor[0], 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->RadixElementReMappingBuffer);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->ParallelSortInputOutputDescriptor[0], 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->ParallelSortPayloadBuffer);

            DemoState->ParallelSortInputOutputDescriptor[1] = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, DemoState->ParallelSortInputOutputDescLayout);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->ParallelSortInputOutputDescriptor[1], 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->ParallelSortMortonBuffer);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->ParallelSortInputOutputDescriptor[1], 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->RadixMortonKeyBuffer);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->ParallelSortInputOutputDescriptor[1], 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->ParallelSortPayloadBuffer);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->ParallelSortInputOutputDescriptor[1], 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->RadixElementReMappingBuffer);

            DemoState->ParallelSortScanDescriptor[0] = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, DemoState->ParallelSortScanDescLayout);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->ParallelSortScanDescriptor[0], 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->ParallelSortReducedScratchBuffer);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->ParallelSortScanDescriptor[0], 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->ParallelSortReducedScratchBuffer);

            DemoState->ParallelSortScanDescriptor[1] = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, DemoState->ParallelSortScanDescLayout);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->ParallelSortScanDescriptor[1], 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->ParallelSortScratchBuffer);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->ParallelSortScanDescriptor[1], 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->ParallelSortScratchBuffer);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->ParallelSortScanDescriptor[1], 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->ParallelSortReducedScratchBuffer);

            DemoState->ParallelSortScratchDescriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, DemoState->ParallelSortScratchDescLayout);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->ParallelSortScratchDescriptor, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->ParallelSortScratchBuffer);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->ParallelSortScratchDescriptor, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->ParallelSortReducedScratchBuffer);
            
#endif
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
                UiPanelText(&Panel, "Attraction Multiplier:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 100.0f, &DemoState->AttractionMultiplier);
                UiPanelNumberBox(&Panel, 0.0f, 100.0f, &DemoState->AttractionMultiplier);
                UiPanelNextRow(&Panel);            

                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "Attraction Weight Power:");
                UiPanelHorizontalSlider(&Panel, 0.0f, 10.0f, &DemoState->AttractionWeightPower);
                UiPanelNumberBox(&Panel, 0.0f, 10.0f, &DemoState->AttractionWeightPower);
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

                UiPanelNextRowIndent(&Panel);
                UiPanelText(&Panel, "Strong Gravity Enabled:");
                UiPanelCheckBox(&Panel, &DemoState->StrongGravityEnabled);
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
                    GpuData->AttractionWeightPower = DemoState->AttractionWeightPower;
                    GpuData->RepulsionMultiplier = DemoState->RepulsionMultiplier;
                    GpuData->RepulsionSoftner = DemoState->RepulsionSoftner;
                    GpuData->GravityMultiplier = DemoState->GravityMultiplier;
                    GpuData->StrongGravityEnabled = DemoState->StrongGravityEnabled;

                    GpuData->CellDim = DemoState->CellWorldDim;
                    GpuData->WorldRadius = DemoState->WorldRadius;
                    GpuData->NumCellsDim = DemoState->NumCellsAxis;

                    GpuData->NumThreadGroupsCalcNodeBounds = CalcGlobalSpeedNumThreadGroups();
                    GpuData->NumThreadGroupsGlobalSpeed = CalcBoundsNumThreadGroups();
                }
            }
            
            VkCommandsTransferFlush(Commands, RenderState->Device);
        }
        
        // NOTE: Simulate graph layout
        {
            u32 GraphDispatchX = DispatchSize(DemoState->NumGraphNodes, 32);
            u32 GraphDispatchY = 1;

            if (GraphDispatchX > MAX_THREAD_GROUPS)
            {
                GraphDispatchX = 64;
                GraphDispatchY = DispatchSize(DemoState->NumGraphNodes, 32 * GraphDispatchX);
            }
            
            // TODO: Remove the clears here (reduction we should be able to never clear, counter we can clear in a cs pass)
            vkCmdFillBuffer(Commands->Buffer, DemoState->GlobalMoveReductionBuffer, 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(Commands->Buffer, DemoState->GlobalMoveCounterBuffer, 0, VK_WHOLE_SIZE, 0);
            
            VkDescriptorSet GraphSimSets[] =
                {
                    DemoState->GraphDescriptor,
                };
            
            VkBarrierBufferAdd(Commands, DemoState->NodeForceBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkCommandsBarrierFlush(Commands);
            
            // NOTE: Graph Attraction
            VkComputeDispatch(Commands, DemoState->GraphMoveConnectionsPipeline, GraphSimSets, ArrayCount(GraphSimSets), GraphDispatchX, GraphDispatchY, 1);

            VkBarrierBufferAdd(Commands, DemoState->NodeForceBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkBarrierBufferAdd(Commands, DemoState->NodeCellIdBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkCommandsBarrierFlush(Commands);
            
            // NOTE: Graph Repulsion
#if 1
            {
                VkComputeDispatch(Commands, DemoState->GraphRepulsionPipeline, GraphSimSets, ArrayCount(GraphSimSets), GraphDispatchX, GraphDispatchY, 1);

                VkBarrierBufferAdd(Commands, DemoState->NodeForceBuffer,
                                   VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                VkCommandsBarrierFlush(Commands);
            }
#else

            // TODO: There is a bug still with sometimes everything collapsing?
            VkDescriptorSet RadixDescriptorSets[] =
                {
                    DemoState->RadixTreeDescriptor,
                    DemoState->GraphDescriptor,
                };

            // NOTE: Graph Calc Node Bounds
            {
                u32 BoundsDispatchX = CalcBoundsNumThreadGroups();
                u32 BoundsDispatchY = 1;
                if (BoundsDispatchX > MAX_THREAD_GROUPS)
                {
                    u32 NumThreadGroups = BoundsDispatchX;
                    BoundsDispatchX = 64;
                    BoundsDispatchY = DispatchSize(NumThreadGroups, BoundsDispatchX);
                }
                
                VkComputeDispatch(Commands, DemoState->CalcWorldBoundsPipeline, RadixDescriptorSets, ArrayCount(RadixDescriptorSets), BoundsDispatchX, BoundsDispatchY, 1);
            }

            VkBarrierBufferAdd(Commands, DemoState->ElementBoundsBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkCommandsBarrierFlush(Commands);

            // NOTE: Generate and Sort Morton Keys
            {
#if BITONIC_MERGE_SORT
                // NOTE: Local FD
                {
                    u32 DispatchX = CeilU32(f32(DemoState->NumGraphNodes) / 2048.0f);
                                
                    VkDescriptorSet DescriptorSets[] =
                        {
                            DemoState->RadixTreeDescriptor,
                            DemoState->GraphDescriptor,
                            DemoState->MergeSortLocalFdDescriptor.Descriptor,
                        };
                    VkComputeDispatch(Commands, DemoState->MergeSortLocalFdPipeline, DescriptorSets, ArrayCount(DescriptorSets), DispatchX, 1, 1);

                    VkBarrierBufferAdd(Commands, DemoState->RadixMortonKeyBuffer,
                                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                    VkBarrierBufferAdd(Commands, DemoState->RadixElementReMappingBuffer,
                                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                    VkCommandsBarrierFlush(Commands);
                }
            
                // NOTE: General Pass
                u32 NumNodesNextPow2 = NextPow2(DemoState->NumGraphNodes) / 2;
                for (u32 FlipSize = 2048, PassId = 11; FlipSize < NumNodesNextPow2; PassId += 1, FlipSize *= 2)
                {
                    MergeSortGlobalFlip(Commands, PassId);
                
                    for (u32 N = FlipSize / 2, NPassId = PassId - 1; N > 0; NPassId -= 1, N = N / 2)
                    {
                        if (N < 1024)
                        {
                            MergeSortLocalDisperse(Commands);
                            break;
                        }
                        else
                        {
                            MergeSortGlobalDisperse(Commands, NPassId);
                        }
                    }
                }
#else
                // NOTE: Generate Morton Keys
                {
                    VkDescriptorSet DescriptorSets[] =
                    {
                        DemoState->RadixTreeDescriptor,
                    };
                    
                    vk_pipeline* Pipeline = DemoState->GenerateMortonKeysPipeline;
                    VkComputeDispatch(Commands, Pipeline, DescriptorSets, ArrayCount(DescriptorSets), GraphDispatchX, GraphDispatchY, 1);

                    VkBarrierBufferAdd(Commands, DemoState->RadixMortonKeyBuffer,
                                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                    VkCommandsBarrierFlush(Commands);
                }
     
                // NOTE: Parallel Sort
                VkBuffer SrcMortonBuffer = DemoState->RadixMortonKeyBuffer;
                VkBuffer DstMortonBuffer = DemoState->ParallelSortMortonBuffer;
                VkBuffer SrcPayloadBuffer = DemoState->RadixElementReMappingBuffer;
                VkBuffer DstPayloadBuffer = DemoState->ParallelSortPayloadBuffer;

                u32 DispatchX = DemoState->ParallelSortNumThreadGroups;
                u32 DispatchY = 1;

                if (DispatchX > MAX_THREAD_GROUPS)
                {
                    DispatchX = 64;
                    DispatchY = DispatchSize(DemoState->ParallelSortNumThreadGroups, DispatchX);
                }

                u32 ReducedDispatchX = DemoState->ParallelSortNumReducedThreadGroups;
                u32 ReducedDispatchY = 1;

                if (ReducedDispatchX > MAX_THREAD_GROUPS)
                {
                    ReducedDispatchX = 64;
                    ReducedDispatchY = DispatchSize(DemoState->ParallelSortNumReducedThreadGroups, ReducedDispatchX);
                }
                
                b32 InputSet = 0;                
                for (u32 Shift = 0; Shift < 32u; Shift += FFX_PARALLELSORT_SORT_BITS_PER_PASS)
                {
                    VkDescriptorSet SharedDescriptorSets0[] =
                    {
                        DemoState->ParallelSortConstantDescriptor,
                        DemoState->ParallelSortInputOutputDescriptor[InputSet],
                        DemoState->ParallelSortScanDescriptor[0],
                        DemoState->ParallelSortScratchDescriptor,
                    };

                    VkDescriptorSet SharedDescriptorSets1[] =
                    {
                        DemoState->ParallelSortConstantDescriptor,
                        DemoState->ParallelSortInputOutputDescriptor[InputSet],
                        DemoState->ParallelSortScanDescriptor[1],
                        DemoState->ParallelSortScratchDescriptor,
                    };

                    // NOTE: Sort Count
                    {
                        vk_pipeline* Pipeline = DemoState->ParallelSortCountPipeline;
                        vkCmdPushConstants(Commands->Buffer, Pipeline->Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &Shift);
                        VkComputeDispatch(Commands, Pipeline, SharedDescriptorSets0, ArrayCount(SharedDescriptorSets0),
                                          DispatchX, DispatchY, 1);
                    }
                    
                    VkBarrierBufferAdd(Commands, DemoState->ParallelSortScratchBuffer,
                                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                    VkCommandsBarrierFlush(Commands);
            
                    // NOTE: Sort Reduce
                    {
                        vk_pipeline* Pipeline = DemoState->ParallelSortReducePipeline;
                        vkCmdPushConstants(Commands->Buffer, Pipeline->Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &Shift);
                        VkComputeDispatch(Commands, Pipeline, SharedDescriptorSets0, ArrayCount(SharedDescriptorSets0),
                                          ReducedDispatchX, ReducedDispatchY, 1);
                    }

                    VkBarrierBufferAdd(Commands, DemoState->ParallelSortReducedScratchBuffer,
                                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                    VkCommandsBarrierFlush(Commands);

                    // NOTE: Sort Scan
                    {
                        // NOTE: First do scan prefix of reduced values
                        {
                            vk_pipeline* Pipeline = DemoState->ParallelSortScanPipeline;
                            vkCmdPushConstants(Commands->Buffer, Pipeline->Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &Shift);
                            // NOTE: Need to account for bigger reduced histogram scan
                            Assert(DemoState->ParallelSortNumReducedThreadGroups < FFX_PARALLELSORT_ELEMENTS_PER_THREAD * FFX_PARALLELSORT_THREADGROUP_SIZE);
                            VkComputeDispatch(Commands, Pipeline, SharedDescriptorSets0, ArrayCount(SharedDescriptorSets0),
                                              1, 1, 1);
                        }

                        VkBarrierBufferAdd(Commands, DemoState->ParallelSortReducedScratchBuffer,
                                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                        VkCommandsBarrierFlush(Commands);
                
                        // NOTE: Next do scan prefix on the histogram with partial sums that we just did
                        {
                            vk_pipeline* Pipeline = DemoState->ParallelSortScanAddPipeline;
                            vkCmdPushConstants(Commands->Buffer, Pipeline->Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &Shift);
                            VkComputeDispatch(Commands, Pipeline, SharedDescriptorSets1, ArrayCount(SharedDescriptorSets1),
                                              ReducedDispatchX, ReducedDispatchY, 1);
                        }
                    }

                    VkBarrierBufferAdd(Commands, DemoState->ParallelSortScratchBuffer,
                                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                    VkCommandsBarrierFlush(Commands);
            
                    // NOTE: Sort Scatter
                    {
                        vk_pipeline* Pipeline = DemoState->ParallelSortScatterPipeline;
                        vkCmdPushConstants(Commands->Buffer, Pipeline->Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &Shift);
                        VkComputeDispatch(Commands, Pipeline, SharedDescriptorSets1, ArrayCount(SharedDescriptorSets1),
                                          DispatchX, DispatchY, 1);
                    }
                    
                    VkBarrierBufferAdd(Commands, DstMortonBuffer,
                                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                    VkBarrierBufferAdd(Commands, DstPayloadBuffer,
                                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                    VkCommandsBarrierFlush(Commands);
            
                    // NOTE: Swap read/write sources
                    std::swap(SrcMortonBuffer, DstMortonBuffer);
                    std::swap(SrcPayloadBuffer, DstPayloadBuffer);
                    InputSet = !InputSet;
                }
#endif
            }

            // NOTE: Build Radix Tree
            VkComputeDispatch(Commands, DemoState->RadixTreeBuildPipeline, RadixDescriptorSets, ArrayCount(RadixDescriptorSets), GraphDispatchX, GraphDispatchY, 1);

            // NOTE: Clear Radix Tree Atomics
            vkCmdFillBuffer(Commands->Buffer, DemoState->RadixTreeAtomicsBuffer, 0, sizeof(u32) * (DemoState->NumGraphNodes - 1), 0);
            
            VkBarrierBufferAdd(Commands, DemoState->RadixTreeChildrenBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkBarrierBufferAdd(Commands, DemoState->RadixTreeParentBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkCommandsBarrierFlush(Commands);

            // NOTE: Summarize Radix Tree
            VkComputeDispatch(Commands, DemoState->RadixTreeSummarizePipeline, RadixDescriptorSets, ArrayCount(RadixDescriptorSets), GraphDispatchX, GraphDispatchY, 1);
            
            VkBarrierBufferAdd(Commands, DemoState->RadixTreeParticleBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkCommandsBarrierFlush(Commands);

            // NOTE: Calculate Repulsion
            VkComputeDispatch(Commands, DemoState->RadixTreeRepulsionPipeline, RadixDescriptorSets, ArrayCount(RadixDescriptorSets), GraphDispatchX, GraphDispatchY, 1);
            
            VkBarrierBufferAdd(Commands, DemoState->NodeForceBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkCommandsBarrierFlush(Commands);
            
#endif

            // NOTE: Graph Calc Global Speed
            {
                u32 GlobalSpeedDispatchX = CalcGlobalSpeedNumThreadGroups();
                u32 GlobalSpeedDispatchY = 1;
                if (GlobalSpeedDispatchX > MAX_THREAD_GROUPS)
                {
                    u32 NumThreadGroups = GlobalSpeedDispatchX;
                    GlobalSpeedDispatchX = 64;
                    GlobalSpeedDispatchY = DispatchSize(NumThreadGroups, GlobalSpeedDispatchX);
                }
                
                VkComputeDispatch(Commands, DemoState->GraphCalcGlobalSpeedPipeline, GraphSimSets, ArrayCount(GraphSimSets), GlobalSpeedDispatchX, GlobalSpeedDispatchY, 1);
            }
            
            VkBarrierBufferAdd(Commands, DemoState->GlobalMoveBuffer,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); //VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            VkCommandsBarrierFlush(Commands);

            // NOTE: Graph Update Nodes
            VkComputeDispatch(Commands, DemoState->GraphUpdateNodesPipeline, GraphSimSets, ArrayCount(GraphSimSets), GraphDispatchX, GraphDispatchY, 1);
            
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

#if LINE_PIPELINE_2

            vkCmdBindPipeline(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, DemoState->LinePipeline->Handle);
            {
                VkDescriptorSet DescriptorSets[] =
                    {
                        DemoState->GraphDescriptor,
                    };
                vkCmdBindDescriptorSets(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, DemoState->LinePipeline->Layout, 0,
                                        ArrayCount(DescriptorSets), DescriptorSets, 0, 0);
            }

            render_mesh* CurrMesh = Scene->RenderMeshes + Scene->QuadMeshId;
            VkDeviceSize Offset = 0;
            vkCmdBindVertexBuffers(Commands->Buffer, 0, 1, &CurrMesh->VertexBuffer, &Offset);
            vkCmdBindIndexBuffer(Commands->Buffer, CurrMesh->IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(Commands->Buffer, 6, 2*DemoState->NumGraphDrawEdges, 0, 0, 0);
            
#else

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
            vkCmdDrawIndexed(Commands->Buffer, 2*DemoState->NumGraphDrawEdges, 1, 0, 0, 0);
#endif
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
