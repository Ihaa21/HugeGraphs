#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_KHR_shader_subgroup_arithmetic : enable
#extension GL_KHR_shader_subgroup_vote : enable

/*
  NOTE: Implementation based on https://research.nvidia.com/sites/default/files/pubs/2012-06_Maximizing-Parallelism-in/karras2012hpg_paper.pdf
 */

#include "graph_shaders.h"
#include "radixtree_shaders.h"

RADIX_DESCRIPTOR_LAYOUT(0)
GRAPH_DESCRIPTOR_LAYOUT(1)

//=========================================================================================================================================
// NOTE: Generate Morton Keys Pipeline
//=========================================================================================================================================

#if GENERATE_MORTON_KEYS

uint Morton2dExpandBits(uint Value)
{
    // NOTE: https://stackoverflow.com/questions/30539347/2d-morton-code-encode-decode-64bits
    // NOTE: Expands a 16-bit integer into 32 bits by inserting 1 zeros after each bit.
    uint Result = (Value | (Value << 16)) & 0x0000FFFF;
    Result = (Result | (Result << 8)) & 0x00FF00FF;
    Result = (Result | (Result << 4)) & 0x0F0F0F0F;
    Result = (Result | (Result << 2)) & 0x33333333;
    Result = (Result | (Result << 1)) & 0x55555555;
    return Result;
}

uint Morton2d(vec2 Pos)
{
    vec2 NormalizedPos = (Pos - ElementBounds.Min) / (ElementBounds.Max - ElementBounds.Min);

    // NOTE: Each axis gets 16 bits so we convert x/y into fixed point
    float BitRange = pow(2, 16);
    vec2 FixedPointPos = min(max(NormalizedPos * BitRange, 0.0f), BitRange - 1);

    uint Result = 2 * Morton2dExpandBits(uint(FixedPointPos.x)) + Morton2dExpandBits(uint(FixedPointPos.y));
    return Result;
}

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
void main()
{
    uint WorkGroupId = gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x;
    uint GlobalThreadId = WorkGroupId * 32 + gl_LocalInvocationIndex;

    if (GlobalThreadId < RadixTreeUniforms.NumNodes)
    {
        MortonKeys[GlobalThreadId] = Morton2d(NodePositionArray[GlobalThreadId]);
        ElementReMapping[GlobalThreadId] = GlobalThreadId;
    }
}

#endif

//=========================================================================================================================================
// NOTE: Calc World Bounds Pipeline
//=========================================================================================================================================

#if CALC_WORLD_BOUNDS

shared uint WorkCounterId;
shared uint PassId;
shared uint DoneCounterToWaitOn;
shared uint NumGroupsInPrevPass;
shared uint NumGroupsInPrevPrevPass;
shared uint NumThreadsInGroup;

// TODO: Port math lib to shader code as well?
uint CeilU32(float Val)
{
    uint Result = uint(ceil(Val));
    return Result;
}

#define NUM_THREADS 32

layout(local_size_x = NUM_THREADS, local_size_y = 1, local_size_z = 1) in;
void main()
{
    uint WorkGroupId = gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x;
    if (WorkGroupId >= GraphGlobals.NumThreadGroupsCalcNodeBounds)
    {
        return;
    }

    if (gl_LocalInvocationID.x == 0)
    {
        // TODO: I think a lot of this garbage can be simplified
        WorkCounterId = atomicAdd(GlobalBoundsWorkCounter, 1);
        PassId = 0;
        DoneCounterToWaitOn = 0;
        NumGroupsInPrevPass = RadixTreeUniforms.NumNodes;
        NumGroupsInPrevPrevPass = RadixTreeUniforms.NumNodes;

        uint NumGroupsInPass = CeilU32(float(RadixTreeUniforms.NumNodes) / 32.0f);
        while (!(WorkCounterId < (DoneCounterToWaitOn + NumGroupsInPass)))
        {
            DoneCounterToWaitOn += NumGroupsInPass;
            NumGroupsInPrevPrevPass = NumGroupsInPrevPass;
            NumGroupsInPrevPass = NumGroupsInPass;
            NumGroupsInPass = CeilU32(float(NumGroupsInPass) / 32.0f);
            PassId += 1;
        }

        // NOTE: Calculate how many threads in this thread group need to be active (last thread group in each pass might have too
        // extra threads that do nothing)
        {
            bool LastGroupInPass = (WorkCounterId - DoneCounterToWaitOn) == (NumGroupsInPass - 1);
            NumThreadsInGroup = (LastGroupInPass ?
                                 NUM_THREADS - (NUM_THREADS * CeilU32(float(NumGroupsInPrevPass) / 32.0f) - NumGroupsInPrevPass) :
                                 NUM_THREADS);
        }
    }

    barrier();
    
    // NOTE: Wait for Done Counter to match (only happens if last pass hasn't finished). We have to do this atomically to not read
    // garbage
    // TODO: We can choose a more exact counter to wait on
    while (atomicAdd(GlobalBoundsDoneCounter, 0) < DoneCounterToWaitOn)
    {
    }
    
    bounds BoundsReduction;
    BoundsReduction.Min = vec2(0);
    BoundsReduction.Max = vec2(0);

    // TODO: Move back in
    uint LoadIndex = 0;
    if (gl_LocalInvocationID.x < NumThreadsInGroup)
    {
        if (PassId == 0)
        {
            // NOTE: First set of downsample reads directly from required buffers
            uint NodeId = NUM_THREADS * (WorkCounterId - DoneCounterToWaitOn) + gl_LocalInvocationIndex;
            if (NodeId < RadixTreeUniforms.NumNodes)
            {
                vec2 NodePos = NodePositionArray[NodeId];
                BoundsReduction.Min = NodePos;
                BoundsReduction.Max = NodePos;
            }
        }
        else
        {
            LoadIndex = NUM_THREADS * (WorkCounterId - DoneCounterToWaitOn) + gl_LocalInvocationIndex;
            if ((PassId & 0x1) == 0)
            {
                // NOTE: We have a odd number pass id so we don't start from the beginning of the buffer
                LoadIndex += NumGroupsInPrevPrevPass;
            }
        
            BoundsReduction = GlobalBoundsReductionArray[LoadIndex];
        }
    }
    
    BoundsReduction.Min = subgroupMin(BoundsReduction.Min);
    BoundsReduction.Max = subgroupMax(BoundsReduction.Max);
    barrier();

    if (gl_LocalInvocationID.x == 0)
    {
        if (WorkCounterId != GraphGlobals.NumThreadGroupsCalcNodeBounds - 1)
        {
            // NOTE: Write out the summed values
            uint WriteIndex = WorkCounterId - DoneCounterToWaitOn;
            if ((PassId & 0x1) != 0)
            {
                // NOTE: We have a even number pass id so we have to write offsetted
                WriteIndex += NumGroupsInPrevPass;
            }

            GlobalBoundsReductionArray[WriteIndex] = BoundsReduction;

            memoryBarrier();
            atomicAdd(GlobalBoundsDoneCounter, 1);
        }
        else
        {
            atomicMin(GlobalBoundsWorkCounter, 0);
            atomicMin(GlobalBoundsDoneCounter, 0);
            ElementBounds = BoundsReduction;
        }
    }
}

#endif

//=========================================================================================================================================
// NOTE: Radix Tree Build 
//=========================================================================================================================================

int LengthCommonPrefix(int Id0, int Id1)
{
    int Result = -1;
    if (Id1 >= 0 && Id1 < RadixTreeUniforms.NumNodes)
    {
        uint Key0 = MortonKeys[Id0];
        uint Key1 = MortonKeys[Id1];
        Result = Key0 == Key1 ? 63 - int(findMSB(Id0 ^ Id1)) : 31 - int(findMSB(Key0 ^ Key1));
    }
    return Result;
}

#if RADIX_TREE_BUILD

/*
  NOTE: This algorithm depends on a specific tree layout. We require that for a node, its left will span keys in the [i, y] range while
        the right child will span [y+1, j]. Left node is at y while right is at y+1 so in memory they are right beside each other. Once we
        find the range of keys spanned by a node, and a split point, we know where the children will be and that doesn't depend on other
        nodes being computed.
  
 */

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
void main()
{
    uint WorkGroupId = gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x;
    int NodeId = int(WorkGroupId * 32 + gl_LocalInvocationIndex);

    if (NodeId < RadixTreeUniforms.NumNodes - 1)
    {
        // NOTE: Find out if we are a left or right child
        int KeyRangeDir = int(sign(LengthCommonPrefix(NodeId, NodeId + 1) - LengthCommonPrefix(NodeId, NodeId - 1)));

        int DistMin = LengthCommonPrefix(NodeId, NodeId - KeyRangeDir);

        // NOTE: Find upper bound on binary search
        int KeyRangeUpperBound = 2;
        while (LengthCommonPrefix(NodeId, NodeId + KeyRangeUpperBound * KeyRangeDir) > DistMin)
        {
            KeyRangeUpperBound = KeyRangeUpperBound * 2;
        }

        // NOTE: Find other end of our range of keys for this node
        int L = 0;
        for (int T = KeyRangeUpperBound / 2; T >= 1; T = T / 2)
        {
            if (LengthCommonPrefix(NodeId, NodeId + (L + T) * KeyRangeDir) > DistMin)
            {
                L = L + T;
            }
        }

        int J = NodeId + L * KeyRangeDir;
        int DistNode = LengthCommonPrefix(NodeId, J);
        
        // NOTE: Find the split position for our keys
        int S = 0;
        for (int T = int(ceil(float(L) * 0.5f)); T > 1; T = int(ceil(float(T) * 0.5f)))
        {
            if (LengthCommonPrefix(NodeId, NodeId + (S + T) * KeyRangeDir) > DistNode)
            {
                S = S + T;
            }
        }

        // NOTE: Do a extra iteration for T = 1
        if (LengthCommonPrefix(NodeId, NodeId + (S + 1) * KeyRangeDir) > DistNode)
        {
            S = S + 1;
        }
        
        int SplitPos = NodeId + S * KeyRangeDir + min(0, KeyRangeDir);
        bool LeftLeaf = min(NodeId, J) == SplitPos;
        bool RightLeaf = max(NodeId, J) == (SplitPos + 1);

        uint LeftChild = SplitPos;
        uint RightChild = SplitPos + 1;

        // NOTE: Output parent pointers
        uint LeftParentIndex = LeftChild + (LeftLeaf ? RadixTreeUniforms.NumNodes - 1 : 0);
        uint RightParentIndex = RightChild + (RightLeaf ? RadixTreeUniforms.NumNodes - 1 : 0);
        RadixNodeParents[LeftParentIndex] = NodeId;
        RadixNodeParents[RightParentIndex] = NodeId;

        // NOTE: Output child pointers
        LeftChild |= LeftLeaf ? (1 << 31) : 0;
        RightChild |= RightLeaf ? (1 << 31) : 0;

        RadixNodeChildren[NodeId] = ivec2(LeftChild, RightChild);
        
#if 0
        if (gl_LocalInvocationIndex == 5)
        {
            RadixNodeChildren[0].x = LengthCommonPrefix(NodeId, NodeId - 1);
            RadixNodeChildren[0].y = LengthCommonPrefix(NodeId, NodeId + 1);
            RadixNodeChildren[1].x = KeyRangeDir;
            RadixNodeChildren[1].y = DistMin;
            RadixNodeChildren[2].x = KeyRangeUpperBound;
            RadixNodeChildren[2].y = L;
            RadixNodeChildren[3].x = J;
            RadixNodeChildren[3].y = DistNode;
            RadixNodeChildren[4].x = SplitPos;
            RadixNodeChildren[4].y = S;
            RadixNodeChildren[5].x = int(LeftChild);
            RadixNodeChildren[5].y = int(RightChild);
            RadixNodeChildren[6].x = int(LeftLeaf);
            RadixNodeChildren[6].y = int(RightLeaf);
        }
#endif
    }
}

#endif

//=========================================================================================================================================
// NOTE: Radix Tree Summarize 
//=========================================================================================================================================

#if RADIX_TREE_SUMMARIZE

void RadixNodeGetData(int Index, out vec2 Pos, out float Degree)
{
    if ((Index & int(1 << 31)) != 0)
    {
        // NOTE: This is a leaf node
        uint ReMappedId = ElementReMapping[Index & (~int(1 << 31))];
        Pos = NodePositionArray[ReMappedId];
        Degree = NodeDegreeArray[ReMappedId];

        // TODO: REMOVE Debug code
#if 0
        uint FakeId = Index & (~int(1 << 31));
        Pos = vec2(2*FakeId + 0, 2*FakeId + 1);
        Degree = FakeId;
#endif
    }
    else
    {
        // NOTE: This is a internal node
        Pos = RadixTreeParticles[Index].Pos;
        Degree = RadixTreeParticles[Index].Degree;
    }    
}

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
void main()
{
    uint WorkGroupId = gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x;
    uint ThreadId = WorkGroupId * 32 + gl_LocalInvocationIndex;

    if (ThreadId < RadixTreeUniforms.NumNodes)
    {
        uint NodeId = ThreadId + RadixTreeUniforms.NumNodes - 1;

        while (NodeId != 0)
        {
            uint ParentId = RadixNodeParents[NodeId];
            uint AtomicResult = atomicAdd(RadixTreeAtomics[ParentId], 1);
            if (AtomicResult == 1)
            {
                // NOTE: Downsample results
                ivec2 ChildPointers = RadixNodeChildren[ParentId];

                vec2 LeftPos, RightPos;
                float LeftDegree, RightDegree;
                RadixNodeGetData(ChildPointers.x, LeftPos, LeftDegree);
                RadixNodeGetData(ChildPointers.y, RightPos, RightDegree);

                // NOTE: We weight each position based on the degree/mass of the node
                RadixTreeParticles[ParentId].Pos = (LeftDegree * LeftPos +  RightDegree * RightPos) / (LeftDegree + RightDegree);
                RadixTreeParticles[ParentId].Degree = LeftDegree + RightDegree;

                // NOTE: Calculate our node size
                {
                    float Distance0 = length(LeftPos - RadixTreeParticles[ParentId].Pos);
                    float Distance1 = length(RightPos - RadixTreeParticles[ParentId].Pos);
                    RadixTreeParticles[ParentId].Size = 2*max(Distance0, Distance1);
                }
                
                NodeId = ParentId;
            }
            else
            {
                break;
            }
        }
    }
}

#endif

//=========================================================================================================================================
// NOTE: Radix Tree Summarize 
//=========================================================================================================================================

#if RADIX_TREE_REPULSION

#define STACK_SIZE 32

shared int StackPointer;
shared int StackNodes[STACK_SIZE];

// TODO: REMOVE
#define DEBUG_ITERATION 0
#if DEBUG_ITERATION
shared uint DebugNumLeafs;
shared uint DebugNumInternals;
shared uint DebugNumRecursions;

shared uint IterationCount;
shared uint SavedStackPtrs[4];
shared uint SavedIterationCount[4];
shared uint SavedNodeTypes[4];
shared uint EndStackPointers[4];
#endif

void StackPushNodeChildren(uint NodeId)
{
    ivec2 Children = RadixNodeChildren[NodeId];
    // NOTE: Push in reverse order so that we go closer to memory layout order
    StackNodes[StackPointer++] = Children.y;
    StackNodes[StackPointer++] = Children.x;
}

int StackPopNode()
{
    StackPointer -= 1;
    int Result = StackNodes[StackPointer];
    return Result;
}

vec2 NodeCalculateRepulsion(vec2 DistanceVec, float DistanceSq, float NodeDegree, float OtherNodeDegree)
{
    float RepulsionMultiplier = GraphGlobals.RepulsionMultiplier * NodeDegree * OtherNodeDegree;
    vec2 Result = RepulsionMultiplier * DistanceVec / DistanceSq;

    return Result;
}

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
void main()
{
    uint WorkGroupId = gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x;
    uint ThreadId = WorkGroupId * 32 + gl_LocalInvocationIndex;

    if (ThreadId < RadixTreeUniforms.NumNodes)
    {
        uint GraphNodeId = ElementReMapping[ThreadId];
        float GraphNodeDegree = NodeDegreeArray[GraphNodeId];
        vec2 GraphNodePos = NodePositionArray[GraphNodeId];
        vec2 GraphNodeForce = NodeForceArray[GraphNodeId];

        // TODO: REMOVE
#if DEBUG_ITERATION
        DebugNumLeafs = 0;
        DebugNumInternals = 0;
        DebugNumRecursions = 0;
        IterationCount = 0;
#endif
        
        // NOTE: Push root onto the stack
        StackPointer = 0;
        StackPushNodeChildren(0);
        
        while (StackPointer > 0)
        {
#if DEBUG_ITERATION
            if (IterationCount < 4)
            {
                SavedStackPtrs[IterationCount] = StackPointer;
                SavedIterationCount[IterationCount] = IterationCount;
            }
#endif
            
            int TreeNodeId = StackPopNode();
            
            int TreeLeafNodeId = TreeNodeId & (~int(1 << 31));
            bool IsNodeLeaf = TreeNodeId != TreeLeafNodeId;

            if (IsNodeLeaf)
            {
                // NOTE: We have a leaf so calculate repulsion
                uint OtherGraphNodeId = ElementReMapping[TreeLeafNodeId];
                if (GraphNodeId != OtherGraphNodeId)
                {
                    vec2 OtherNodePos = NodePositionArray[OtherGraphNodeId];
                    float OtherNodeDegree = NodeDegreeArray[OtherGraphNodeId];

                    vec2 DistanceVec = GraphNodePos - OtherNodePos;
                    float DistanceSq = DistanceVec.x * DistanceVec.x + DistanceVec.y * DistanceVec.y + GraphGlobals.RepulsionSoftner;

                    GraphNodeForce += NodeCalculateRepulsion(DistanceVec, DistanceSq, GraphNodeDegree, OtherNodeDegree);

#if DEBUG_ITERATION
                    if (IterationCount < 4)
                    {
                        SavedNodeTypes[IterationCount] = 10;
                    }
            
                    DebugNumLeafs++;
#endif
                }
            }
            else
            {
                // NOTE: We have a internal node, check if we take avg node data or traverse
                radix_tree_particle NodeParticle = RadixTreeParticles[TreeNodeId];
                vec2 DistanceVec = GraphNodePos - NodeParticle.Pos;
                float DistanceSq = DistanceVec.x * DistanceVec.x + DistanceVec.y * DistanceVec.y + GraphGlobals.RepulsionSoftner;
                float Theta = 0.1f;

                if (subgroupAll(Theta * sqrt(DistanceSq) > NodeParticle.Size) || StackPointer >= (STACK_SIZE - 2))
                {
                    // NOTE: We either are far enough away or we don't have enough room on the stack for more nodes so
                    // take average data and quit traversing this sub tree
                    GraphNodeForce += NodeCalculateRepulsion(DistanceVec, DistanceSq, GraphNodeDegree, NodeParticle.Degree);

#if DEBUG_ITERATION
                    DebugNumInternals++;

                    if (IterationCount < 4)
                    {
                        SavedNodeTypes[IterationCount] = 20;
                    }
#endif
                }
                else
                {
#if DEBUG_ITERATION
                    if (IterationCount < 4)
                    {
                        SavedNodeTypes[IterationCount] = 30;
                    }
                    DebugNumRecursions++;
#endif
                    
                    StackPushNodeChildren(TreeNodeId);
                }
            }

#if DEBUG_ITERATION
            if (IterationCount < 4)
            {
                EndStackPointers[IterationCount] = StackPointer;
            }
            
            IterationCount += 1;
#endif
        }

        NodeForceArray[GraphNodeId] = GraphNodeForce;

#if DEBUG_ITERATION
        // TODO: DEBUG REMOVE
        if (gl_LocalInvocationIndex == 0)
        {
            uint CurrOffset = 0;
            NodeForceArray[CurrOffset++] = vec2(DebugNumLeafs, DebugNumInternals);
            NodeForceArray[CurrOffset++] = vec2(DebugNumRecursions, IterationCount);
            for (uint I = 0; I < 4; ++I)
            {
                NodeForceArray[CurrOffset++] = vec2(SavedStackPtrs[I], SavedIterationCount[I]);
                NodeForceArray[CurrOffset++] = vec2(SavedNodeTypes[I], EndStackPointers[I]);
            }
        }
#endif
    }
}

#endif
