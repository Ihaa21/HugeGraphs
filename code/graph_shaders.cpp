#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_KHR_shader_subgroup_arithmetic : enable

#include "graph_shaders.h"

GRAPH_DESCRIPTOR_LAYOUT(0)

//=========================================================================================================================================
// NOTE: Graph Move Connections Shader
//=========================================================================================================================================

#if GRAPH_MOVE_CONNECTIONS

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
void main()
{
    uint WorkGroupId = gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x;
    uint CurrNodeId = WorkGroupId * 32 + gl_LocalInvocationIndex;
    if (CurrNodeId < GraphGlobals.NumNodes)
    {
        vec2 CurrNodePos = NodePositionArray[CurrNodeId];
        vec2 CurrNodeForce = vec2(0);
        
        graph_node_edges Edges = NodeEdgeArray[CurrNodeId];
        for (uint EdgeId = Edges.StartConnections; EdgeId < Edges.EndConnections; ++EdgeId)
        {
            uint OtherNodeId = EdgeArray[EdgeId].OtherNodeId;
            float EdgeWeight = EdgeArray[EdgeId].Weight;
            vec2 OtherNodePos = NodePositionArray[OtherNodeId];
            CurrNodeForce += pow(EdgeWeight, GraphGlobals.AttractionWeightPower) * GraphGlobals.AttractionMultiplier * (OtherNodePos - CurrNodePos);
        }

        // NOTE: Apply gravity towards center (0, 0)
        {
            float CurrNodeDegree = NodeDegreeArray[CurrNodeId];
            float DistToCenter = length(CurrNodePos);
            float GravityFactor = CurrNodeDegree * GraphGlobals.GravityMultiplier;
            if (CurrNodePos.x == 0 && CurrNodePos.y == 0)
            {
                GravityFactor = 0.0f;
            }
            if (GraphGlobals.StrongGravityEnabled == 0)
            {
                GravityFactor /= DistToCenter;
            }
            
            vec2 GravityForce = -CurrNodePos * GravityFactor;
            
            CurrNodeForce += GravityForce;
        }
        
        // TODO: MOVE LATER
        NodeCellIdArray[CurrNodeId] = 0xFFFFFFFF;
        NodeForceArray[CurrNodeId] = CurrNodeForce;
    }
}

#endif

//=========================================================================================================================================
// NOTE: Graph Repulsion Shader
//=========================================================================================================================================

#if GRAPH_REPULSION

float RandomFloat(vec2 uv)
{
    // NOTE: https://stackoverflow.com/questions/4200224/random-noise-functions-for-glsl
    return fract(sin(dot(uv,vec2(12.9898,78.233)))*43758.5453123);
}

// NOTE: This is the n^2 solution
layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
void main()
{
    uint WorkGroupId = gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x;
    uint CurrNodeId = WorkGroupId * 32 + gl_LocalInvocationIndex;
    if (CurrNodeId < GraphGlobals.NumNodes)
    {
        float CurrNodeDegree = NodeDegreeArray[CurrNodeId];
        vec2 CurrNodePos = NodePositionArray[CurrNodeId];
        vec2 CurrNodeForce = NodeForceArray[CurrNodeId];
     
        for (uint OtherNodeId = 0; OtherNodeId < GraphGlobals.NumNodes; ++OtherNodeId)
        {
            if (CurrNodeId != OtherNodeId)
            {
                vec2 OtherNodePos = NodePositionArray[OtherNodeId];
                float OtherNodeDegree = NodeDegreeArray[OtherNodeId];

                vec2 DistanceVec = CurrNodePos - OtherNodePos;
                float DistanceSq = DistanceVec.x * DistanceVec.x + DistanceVec.y * DistanceVec.y + GraphGlobals.RepulsionSoftner;
#if 0
                if (DistanceSq < (0.001f * 0.001f))
                {
                    vec2 Input1 = fract(CurrNodePos + 0.9123953f * vec2(CurrNodeId));
                    vec2 Input2 = fract(CurrNodePos + 0.9123953f * vec2(OtherNodeId));
                    vec2 RandomVec = normalize(2.0f * vec2(RandomFloat(Input1), RandomFloat(Input2)) - vec2(1));
                }
#endif
                
                //float RepulsionMultiplier = GraphGlobals.RepulsionMultiplier * (CurrNodeDegree + 1) * (OtherNodeDegree + 1);
                float RepulsionMultiplier = GraphGlobals.RepulsionMultiplier * CurrNodeDegree * OtherNodeDegree;
                vec2 RepulsionForce = RepulsionMultiplier * DistanceVec / DistanceSq;
                CurrNodeForce += RepulsionForce;
            }
        }
        
        NodeForceArray[CurrNodeId] = CurrNodeForce;
    }
}

#endif

//=========================================================================================================================================
// NOTE: Graph Calculate Global Speed Shader
//=========================================================================================================================================

#if GRAPH_CALC_GLOBAL_SPEED

#if 1
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
/*

  NOTE: First n threads will load 1 value, and do parallel add using wave ops to get 32 wide swing/traction sum.
        They then write it to buffer and exit.
        Next n/32 threads will scoop those values, do the same reduction and exit. We continue until 1 thread writes out the final
        values.
  
 */

    uint WorkGroupId = gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x;
    if (WorkGroupId >= GraphGlobals.NumThreadGroupsGlobalSpeed)
    {
        return;
    }
    
    if (gl_LocalInvocationID.x == 0)
    {
        // TODO: I think a lot of this garbage can be simplified
        WorkCounterId = atomicAdd(GlobalMoveWorkCounter, 1);
        PassId = 0;
        DoneCounterToWaitOn = 0;
        NumGroupsInPrevPass = GraphGlobals.NumNodes;
        NumGroupsInPrevPrevPass = GraphGlobals.NumNodes;

        uint NumGroupsInPass = CeilU32(float(GraphGlobals.NumNodes) / 32.0f);
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
    while (atomicAdd(GlobalMoveDoneCounter, 0) < DoneCounterToWaitOn)
    {
    }
    
    // NOTE: x = Swing, y = Traction
    global_move_reduction MoveReduction;
    MoveReduction.Swing = 0;
    MoveReduction.Traction = 0;

    // TODO: Move back in
    uint LoadIndex = 0;
    if (gl_LocalInvocationID.x < NumThreadsInGroup)
    {
        if (PassId == 0)
        {
            // NOTE: First set of downsample reads directly from required buffers
            uint NodeId = NUM_THREADS * (WorkCounterId - DoneCounterToWaitOn) + gl_LocalInvocationIndex;
            if (NodeId < GraphGlobals.NumNodes)
            {
                vec2 PrevForce = NodePrevForceArray[NodeId];
                vec2 CurrForce = NodeForceArray[NodeId];
                MoveReduction.Swing += (1.0f + NodeDegreeArray[NodeId]) * length(CurrForce - PrevForce);
                MoveReduction.Traction += 0.5f * length(CurrForce + PrevForce);
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
        
            MoveReduction = GlobalMoveReductionArray[LoadIndex];
        }
    }
    
    MoveReduction.Swing = subgroupAdd(MoveReduction.Swing);
    MoveReduction.Traction = subgroupAdd(MoveReduction.Traction);
    barrier();

    if (gl_LocalInvocationID.x == 0)
    {
        if (WorkCounterId != GraphGlobals.NumThreadGroupsGlobalSpeed - 1)
        {
            // NOTE: Write out the summed values
            uint WriteIndex = WorkCounterId - DoneCounterToWaitOn;
            if ((PassId & 0x1) != 0)
            {
                // NOTE: We have a even number pass id so we have to write offsetted
                WriteIndex += NumGroupsInPrevPass;
            }

            GlobalMoveReductionArray[WriteIndex] = MoveReduction;

            memoryBarrier();
            atomicAdd(GlobalMoveDoneCounter, 1);
        }
        else
        {
            // NOTE: We are the last job so calculate global speed
            // NOTE: Remove bad behavior at 0
            MoveReduction.Swing += 0.001f;
            MoveReduction.Traction += 0.001f;

            // NOTE: Calculate jitter tolerance
            float EstimatedOptimalJitterTolerance = 0.05f * sqrt(GraphGlobals.NumNodes);
            float MinJitterTolerance = sqrt(EstimatedOptimalJitterTolerance);
            float JitterTolerance = max(MinJitterTolerance,
                                        min(GlobalMove.MaxJitterTolerance, EstimatedOptimalJitterTolerance * MoveReduction.Traction / float(GraphGlobals.NumNodes * GraphGlobals.NumNodes)));
            JitterTolerance *= GlobalMove.JitterToleranceConstant;

            // NOTE: Protect against erratic behavior
            float MinSpeedEfficiency = 0.05f;
            if (MoveReduction.Swing / MoveReduction.Traction > 2.0f)
            {
                if (GlobalMove.SpeedEfficiency > MinSpeedEfficiency)
                {
                    GlobalMove.SpeedEfficiency *= 0.5f;
                }
                JitterTolerance = max(JitterTolerance, GlobalMove.JitterToleranceConstant);
            }

            float TargetSpeed = JitterTolerance * GlobalMove.SpeedEfficiency * MoveReduction.Traction / MoveReduction.Swing;

            if (MoveReduction.Swing > JitterTolerance * MoveReduction.Traction)
            {
                if (GlobalMove.SpeedEfficiency > MinSpeedEfficiency)
                {
                    GlobalMove.SpeedEfficiency *= 0.7f;
                }
            }
            else if (GlobalMove.Speed < 1000.0f)
            {
                GlobalMove.SpeedEfficiency *= 1.3f;
            }

            // NOTE: Prevent speed from rising to quickly
            float MaxRise = 0.5f;
            GlobalMove.Speed += min(TargetSpeed - GlobalMove.Speed, MaxRise * GlobalMove.Speed);
        }
    }
}
#endif

#if 0
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
void main()
{
    // TODO: Super inefficient now but it iz what it iz

    /* NOTE: References:
       - https://github.com/govertb/GPUGraphLayout/blob/master/src/RPFA2Kernels.cu
       - https://github.com/bhargavchippada/forceatlas2/blob/master/fa2/fa2util.py
       - https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0098679
    */

    float GlobalSwing = 0.001f;
    float GlobalTraction = 0.001f;
    
    for (uint NodeId = 0; NodeId < GraphGlobals.NumNodes; ++NodeId)
    {
        vec2 PrevForce = NodePrevForceArray[NodeId];
        vec2 CurrForce = NodeForceArray[NodeId];
        GlobalSwing += (1.0f + NodeDegreeArray[NodeId]) * length(CurrForce - PrevForce);
        GlobalTraction += 0.5f * length(CurrForce + PrevForce);
    }

    GlobalMove.Swing = GlobalSwing;
    GlobalMove.Traction = GlobalTraction;
    
    // NOTE: Calculate jitter tolerance
    float EstimatedOptimalJitterTolerance = 0.05f * sqrt(GraphGlobals.NumNodes);
    float MinJitterTolerance = sqrt(EstimatedOptimalJitterTolerance);
    float JitterTolerance = max(MinJitterTolerance,
                                min(GlobalMove.MaxJitterTolerance, EstimatedOptimalJitterTolerance * GlobalTraction / float(GraphGlobals.NumNodes * GraphGlobals.NumNodes)));
    JitterTolerance *= GlobalMove.JitterToleranceConstant;

    // NOTE: Protect against erratic behavior
    float MinSpeedEfficiency = 0.05f;
    if (GlobalSwing / GlobalTraction > 2.0f)
    {
        if (GlobalMove.SpeedEfficiency > MinSpeedEfficiency)
        {
            GlobalMove.SpeedEfficiency *= 0.5f;
        }
        JitterTolerance = max(JitterTolerance, GlobalMove.JitterToleranceConstant);
    }

    float TargetSpeed = JitterTolerance * GlobalMove.SpeedEfficiency * GlobalTraction / GlobalSwing;

    if (GlobalSwing > JitterTolerance * GlobalTraction)
    {
        if (GlobalMove.SpeedEfficiency > MinSpeedEfficiency)
        {
            GlobalMove.SpeedEfficiency *= 0.7f;
        }
    }
    else if (GlobalMove.Speed < 1000.0f)
    {
        GlobalMove.SpeedEfficiency *= 1.3f;
    }

    // NOTE: Prevent speed from rising to quickly
    float MaxRise = 0.5f;
    GlobalMove.Speed += min(TargetSpeed - GlobalMove.Speed, MaxRise * GlobalMove.Speed);
}
#endif

#endif

//=========================================================================================================================================
// NOTE: Graph Update Nodes Shader
//=========================================================================================================================================

#if GRAPH_UPDATE_NODES

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
void main()
{
    uint WorkGroupId = gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x;
    uint CurrNodeId = WorkGroupId * 32 + gl_LocalInvocationIndex;
    if (CurrNodeId < GraphGlobals.NumNodes)
    {
        // NOTE: The degree of a node is its mass
        vec2 CurrNodePos = NodePositionArray[CurrNodeId];
        float CurrNodeDegree = NodeDegreeArray[CurrNodeId];        
        vec2 CurrNodeForce = NodeForceArray[CurrNodeId];
        vec2 PrevNodeForce = NodePrevForceArray[CurrNodeId];

        //float Swing = (1.0f + CurrNodeDegree) * length(CurrNodeForce - PrevNodeForce);
        float Swing = CurrNodeDegree * length(CurrNodeForce - PrevNodeForce);
        float NodeSpeed = GlobalMove.Speed / (1 + GlobalMove.Speed * Swing);

        // NOTE: Update position
        vec2 NewPos = CurrNodePos + NodeSpeed * CurrNodeForce;
        //NewPos = max(min(NewPos, GraphGlobals.WorldRadius), -GraphGlobals.WorldRadius);

        // NOTE: Write out to required buffers
        NodePositionArray[CurrNodeId] = NewPos;
        NodePrevForceArray[CurrNodeId] = CurrNodeForce;
    }
}

#endif

//=========================================================================================================================================
// NOTE: Circle Vertex Shader
//=========================================================================================================================================

#if CIRCLE_VERTEX_SHADER

layout(location = 0) in vec3 InPos;
layout(location = 1) in vec2 InUv;

layout(location = 0) out vec4 OutColor;
layout(location = 1) out vec2 OutUv;

void main()
{
    vec2 PosEntry = NodePositionArray[gl_InstanceIndex];
    graph_node_draw DrawEntry = NodeDrawArray[gl_InstanceIndex];
    gl_Position = GraphGlobals.VPTransform * vec4(vec3(PosEntry, 0) + DrawEntry.Scale * InPos, 1);
    OutColor = vec4(DrawEntry.Color, 1);
    
    OutUv = InUv;
}

#endif

//=========================================================================================================================================
// NOTE: Circle Fragment Shader
//=========================================================================================================================================

#if CIRCLE_FRAGMENT_SHADER

layout(location = 0) in vec4 InColor;
layout(location = 1) in vec2 InUv;

layout(location = 0) out vec4 OutColor;

void main()
{
    // NOTE: https://rubendv.be/posts/fwidth/
    vec2 CenterUv = vec2(0.5f);
    vec2 DistanceVec = InUv - CenterUv;
    // NOTE: x^2 + y^2 <= r^2
    float Radius = 0.5;
    float Distance = sqrt(DistanceVec.x * DistanceVec.x + DistanceVec.y * DistanceVec.y);
    float ResDistDelta = fwidth(Distance);
    float Alpha = 1.0f - smoothstep(Radius - ResDistDelta, Radius, Distance);

    vec3 Color = InColor.rgb;
    vec3 AddedColor = vec3(0.8*smoothstep(0, 0.5, 1 - (Radius - Distance) / 0.05));
    Color.rgb = clamp(Color.rgb + AddedColor, 0, 1);
    
    // NOTE: Premul alpha
    OutColor = vec4(Color * Alpha, Alpha);
}

#endif

//=========================================================================================================================================
// NOTE: Line Vertex Shader
//=========================================================================================================================================

#if LINE_VERTEX_SHADER

layout(location = 0) in vec2 InPos;

layout(location = 0) out vec4 OutColor;
layout(location = 1) out vec2 OutLineCenter;

void main()
{
    // NOTE: https://vitaliburkov.wordpress.com/2016/09/17/simple-and-fast-high-quality-antialiased-lines-with-opengl/
    uint EdgeId = gl_VertexIndex / 2;
    vec4 ProjectedPos = GraphGlobals.VPTransform * vec4(InPos, 0, 1);
    uint Color = DrawEdgeColorArray[EdgeId];
    
    gl_Position = ProjectedPos;

    // TODO: Used set alpha value
    OutColor = vec4(float((Color >> 0) & 0xFF) / 255.0f,
                    float((Color >> 8) & 0xFF) / 255.0f,
                    float((Color >> 16) & 0xFF) / 255.0f,
                    0.1f);
    OutLineCenter = 0.5 * (ProjectedPos.xy + vec2(1)) * GraphGlobals.ViewPort;
}

#endif

//=========================================================================================================================================
// NOTE: Line Fragment Shader
//=========================================================================================================================================

#if LINE_FRAGMENT_SHADER

layout(location = 0) in vec4 InColor;
layout(location = 1) in vec2 InLineCenter;

layout(location = 0) out vec4 OutColor;

void main()
{
    // TODO: This doesn't appear to work well with zooming camera?
    // NOTE: https://vitaliburkov.wordpress.com/2016/09/17/simple-and-fast-high-quality-antialiased-lines-with-opengl/
    float LineWidth = 1.5f;
    float BlendFactor = 1.5f;

    float Alpha = InColor.a;
    float DistanceFromCenter = length(InLineCenter - gl_FragCoord.xy);
    Alpha = DistanceFromCenter > LineWidth ? 0.0f : Alpha * pow((LineWidth - DistanceFromCenter) / LineWidth, BlendFactor);
    
    OutColor = vec4(InColor.rgb * Alpha, Alpha);
}

#endif

//=========================================================================================================================================
// NOTE: Line 2 Vertex Shader
//=========================================================================================================================================

#if LINE_2_VERTEX_SHADER

layout(location = 0) in vec3 InPos;

layout(location = 0) out vec4 OutColor;
layout(location = 1) out vec2 OutLineCenter;

void main()
{
    // NOTE: https://twitter.com/m_schuetz/status/1423275869825503232/photo/2
    uint LineId = gl_InstanceIndex;
    uint VertexIndex = gl_VertexIndex;
    vec2 Start = NodePositionArray[DrawEdgeIndexArray[2*LineId + 0]];
    vec2 End = NodePositionArray[DrawEdgeIndexArray[2*LineId + 1]];

    vec2 Position;
    if (InPos.x == -0.5f)
    {
        Position = Start;
    }
    else
    {
        Position = End;
    }

    // NOTE: Find direction of our offset
    float LineWidth = 2.0f;
    vec2 LineDir = normalize(End - Start);
    vec2 Thickness = LineWidth * vec2(-LineDir.y, LineDir.x);
    vec2 Offset;
    if (InPos.y == 0.5f)
    {
        Offset = Thickness;
    }
    else
    {
        Offset = -Thickness;
    }
    Offset /= GraphGlobals.ViewPort;

    vec4 ProjectedPos = GraphGlobals.VPTransform * vec4(Position, 0, 1);
    // NOTE: Adjust the projected pos based on our offset
    ProjectedPos.xy += Offset * ProjectedPos.w;
    gl_Position = ProjectedPos;
    
    uint Color = DrawEdgeColorArray[LineId];
    // TODO: Used set alpha value
    OutColor = vec4(float((Color >> 0) & 0xFF) / 255.0f,
                    float((Color >> 8) & 0xFF) / 255.0f,
                    float((Color >> 16) & 0xFF) / 255.0f,
                    0.005f);
    OutLineCenter = 0.5 * (ProjectedPos.xy + vec2(1)) * GraphGlobals.ViewPort;
}

#endif
