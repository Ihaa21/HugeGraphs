
//
// NOTE: Grid Cell Data
//

struct grid_cell_block
{
    uint NextBlockOffset;
    uint NumElements;
    uint ElementIds[1024];
};

struct grid_cell_header
{
    uint StartGridCellBlock;
};

layout(set = 0, binding = 11) buffer grid_cell_block_array
{
    grid_cell_block GridCellBlocks[];
};

layout(set = 0, binding = 12) buffer grid_cell_header_array
{
    grid_cell_header GridCellHeaders[];
};

layout(set = 0, binding = 13) buffer grid_cell_block_counter
{
    uint GlobalGridCellBlockCounter;
    // TODO: REMOVE DEBUG DATA
    uint DebugCellId;
    uint DebugCellX;
    uint DebugCellY;
};

//=========================================================================================================================================
// NOTE: Graph Init Grid Shader
//=========================================================================================================================================

#if GRAPH_INIT_GRID

shared uint NodeBlockOffset;
shared uint NodeBlock[2048];

shared uint PrevGlobalNodeBlockId;
shared uint CurrGlobalNodeBlockId;

void WriteNodeBlockToMemory(uint LdsOffset)
{
    // NOTE: Get our block id and link the linked list
    if (gl_LocalInvocationIndex == 0)
    {
        CurrGlobalNodeBlockId = atomicAdd(GlobalGridCellBlockCounter, 1);
        GridCellBlocks[CurrGlobalNodeBlockId].NextBlockOffset = PrevGlobalNodeBlockId;
        GridCellBlocks[CurrGlobalNodeBlockId].NumElements = NodeBlockOffset - LdsOffset;
        PrevGlobalNodeBlockId = CurrGlobalNodeBlockId;
    }

    barrier();

    GridCellBlocks[CurrGlobalNodeBlockId].ElementIds[gl_LocalInvocationIndex] = NodeBlock[LdsOffset + gl_LocalInvocationIndex];
}

layout(local_size_x = 1024, local_size_y = 1, local_size_z = 1) in;
void main()
{
    uint CurrCellId = gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x;

    vec2 CellMin = GraphGlobals.CellDim * gl_WorkGroupID.xy;
    vec2 CellMax = GraphGlobals.CellDim * (gl_WorkGroupID.xy + vec2(1));

    // NOTE: Renormalize the cell min/max to be centered in the world
    CellMin -= vec2(GraphGlobals.WorldRadius);
    CellMax -= vec2(GraphGlobals.WorldRadius);
    
    if (gl_LocalInvocationIndex == 0)
    {
        NodeBlockOffset = 0;
        PrevGlobalNodeBlockId = 0xFFFFFFFF;
        CurrGlobalNodeBlockId = 0xFFFFFFFF;
    }

    barrier();
    
    for (uint NodeId = gl_LocalInvocationIndex; NodeId < GraphGlobals.NumNodes; NodeId += 1024)
    {
        vec2 CurrNodePos = NodePositionArray[NodeId];

        bool NodeBlockLess1024 = NodeBlockOffset < 1024;
        if (CellMin.x <= CurrNodePos.x && CellMax.x >= CurrNodePos.x &&
            CellMin.y <= CurrNodePos.y && CellMax.y >= CurrNodePos.y)
        {
            // NOTE: Set the node cell id in struct
            NodeCellIdArray[NodeId] = CurrCellId;
            
            uint OffsetId = atomicAdd(NodeBlockOffset, 1);
            if (OffsetId >= 2048)
            {
                OffsetId -= 2048;
            }

            NodeBlock[OffsetId] = NodeId;
        }

        barrier();
        
        if (NodeBlockLess1024 && NodeBlockOffset >= 1024)
        {
            // NOTE: First 1024 elements are full, we can write to memory
            WriteNodeBlockToMemory(0);
        }
        else if (!NodeBlockLess1024 && NodeBlockOffset >= 2048)
        {
            // NOTE: Second 1024 elements are full, we can write to memory
            WriteNodeBlockToMemory(1024);
        }

        if (gl_LocalInvocationIndex == 0 && NodeBlockOffset >= 2048)
        {
            // NOTE: Wrap the LDS index
            NodeBlockOffset -= 2048;
        }

        barrier();
    }

    if (NodeBlockOffset != 0 && NodeBlockOffset != 1024)
    {
        // NOTE: We have some remaining nodes to write
        if (NodeBlockOffset < 1024)
        {
            WriteNodeBlockToMemory(0);
        }
        else if (NodeBlockOffset >= 1024)
        {
            WriteNodeBlockToMemory(1024);
        }
    }
    
    // NOTE: Write out the grid cell header
    if (gl_LocalInvocationIndex == 0)
    {
        GridCellHeaders[CurrCellId].StartGridCellBlock = PrevGlobalNodeBlockId;
    }
}

#endif

#if 0
layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
void main()
{
    uint CurrNodeId = gl_GlobalInvocationID.x;
    if (CurrNodeId < GraphGlobals.NumNodes)
    {    
        // NOTE: Every thread processes one graph node, so we may have different cells iterated in one thread group
        // TODO: Try rearranging in memory like for my CPU code
        graph_node_pos CurrNodePos = NodePositionArray[CurrNodeId];
        int CurrCellId = int(CurrNodePos.GridCellId);
        int CellY = CurrCellId / int(GraphGlobals.NumCellsDim);
        int CellX = CurrCellId - CellY * int(GraphGlobals.NumCellsDim);

        if (CurrNodeId == 0)
        {
            DebugCellX = CellX;
            DebugCellY = CellY;
            DebugCellId = CurrCellId;
        }
        
        // NOTE: Calculate how many cells we need to iterate over
        float CheckRadius = max(max(GraphGlobals.LayoutAvoidSameRadius, GraphGlobals.LayoutAvoidDiffRadius), GraphGlobals.LayoutPullSameRadius);
        /// TODO: I Think theres a mistake here
        int CheckCellRadius = int(ceil((CheckRadius + GraphGlobals.CellDim) / GraphGlobals.CellDim));
        if (CheckRadius == 0)
        {
            CheckCellRadius = 0;
        }
     
        // NOTE: Loop through neighboring cells to find nearby elements
        for (int Y = CellY - CheckCellRadius;
             Y <= (CellY + CheckCellRadius) && Y >= 0 && Y < GraphGlobals.NumCellsDim;
             ++Y)
        {
            for (int X = CellX - CheckCellRadius;
                 X <= (CellX + CheckCellRadius) && X >= 0 && X < GraphGlobals.NumCellsDim;
                 ++X)
            {
                int OtherCellId = Y * int(GraphGlobals.NumCellsDim) + X;
                grid_cell_header OtherCellHeader = GridCellHeaders[OtherCellId];

                // NOTE: Loop through all the blocks of elements in this cell
                uint OtherBlockId = OtherCellHeader.StartGridCellBlock;
                while (OtherBlockId != 0xFFFFFFFF)
                {
                    // TODO: Does it make sense to store some of this stuff to LDS to load after? I guess it would fit in L0 cache
                    // NOTE: Update the block header pointer
                    uint OtherNumElements = GridCellBlocks[OtherBlockId].NumElements;

                    // NOTE: Get the elements in the block
                    for (int ElementId = 0; ElementId < OtherNumElements; ++ElementId)
                    {
                        uint OtherNodeId = GridCellBlocks[OtherBlockId].ElementIds[ElementId];

                        if (CurrNodeId != OtherNodeId)
                        {
                            graph_node_pos OtherNodePos = NodePositionArray[OtherNodeId];
                            
                            bool SameFamily = CurrNodePos.FamilyId == OtherNodePos.FamilyId;
                            float AvoidRadius = SameFamily ? GraphGlobals.LayoutAvoidSameRadius : GraphGlobals.LayoutAvoidDiffRadius;
                            float AvoidVel = SameFamily ? GraphGlobals.LayoutAvoidSameAccel : GraphGlobals.LayoutAvoidDiffAccel;

                            vec2 DistanceVec = CurrNodePos.Pos - OtherNodePos.Pos;
                            float Distance = length(DistanceVec);
                            if (Distance < AvoidRadius)
                            {
                                float T = Distance / AvoidRadius;
                                float AvoidVelMag = mix(AvoidVel, 0, T);

                                vec2 Dir = vec2(0);
                                if (Distance == 0)
                                {
                                    vec2 Input1 = fract(CurrNodePos.Pos + 0.9123953f * vec2(CurrNodeId));
                                    vec2 Input2 = fract(CurrNodePos.Pos + 0.9123953f * vec2(OtherNodeId));
                                    Dir = normalize(2.0f * vec2(RandomFloat(Input1), RandomFloat(Input2)) - vec2(1));
                                }
                                else
                                {
                                    Dir = DistanceVec / Distance;
                                }

                                NodeVelocityArray[CurrNodeId] += AvoidVelMag * Dir;
                            }
                            else if (SameFamily && Distance < GraphGlobals.LayoutPullSameRadius)
                            {
                                vec2 NormalVec = DistanceVec / Distance;
                                vec2 Vel = GraphGlobals.LayoutPullSameAccel * NormalVec;
                                NodeVelocityArray[CurrNodeId] -= Vel;
                            }
                        }
                    }
                            
                    OtherBlockId = GridCellBlocks[OtherBlockId].NextBlockOffset;
                }
            }
        }
    }
}
#endif
