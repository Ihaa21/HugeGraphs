#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

struct graph_node_pos
{
    vec2 Pos;
    uint FamilyId;
    uint Pad;
};

struct graph_node_edges
{
    uint StartConnections;
    uint EndConnections;
};

struct graph_node_draw
{
    vec3 Color;
    float Scale;
};

layout(set = 0, binding = 0) uniform graph_globals
{
    mat4 VPTransform;
    vec2 ViewPort;
    float FrameTime;
    uint NumNodes;

    // NOTE: Layout Data
    float LayoutAvoidDiffRadius;
    float LayoutAvoidDiffAccel;
    float LayoutAvoidSameRadius;
    float LayoutAvoidSameAccel;
    float LayoutPullSameRadius;
    float LayoutPullSameAccel;
    float LayoutEdgeAccel;
    float LayoutEdgeMinDist;
} GraphGlobals;

layout(set = 0, binding = 1) buffer graph_node_position_array
{
    graph_node_pos NodePositionArray[];
};

layout(set = 0, binding = 2) buffer graph_node_velocity_array
{
    vec2 NodeVelocityArray[];
};

layout(set = 0, binding = 3) buffer graph_node_edge_array
{
    graph_node_edges NodeEdgeArray[];
};

layout(set = 0, binding = 4) buffer graph_edge_array
{
    uint EdgeArray[];
};

//
// NOTE: Draw data
//

layout(set = 0, binding = 5) buffer graph_node_draw_array
{
    graph_node_draw NodeDrawArray[];
};

layout(set = 0, binding = 6) buffer graph_edge_position_array
{
    vec2 DrawEdgePositionArray[];
};

layout(set = 0, binding = 7) buffer graph_edge_color_array
{
    vec4 DrawEdgeColorArray[];
};

//=========================================================================================================================================
// NOTE: Graph Move Connections Shader
//=========================================================================================================================================

#if GRAPH_MOVE_CONNECTIONS

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
void main()
{
    uint CurrNodeId = gl_GlobalInvocationID.x;
    if (CurrNodeId < GraphGlobals.NumNodes)
    {
        vec2 CurrNodePos = NodePositionArray[CurrNodeId].Pos;
        graph_node_edges Edges = NodeEdgeArray[CurrNodeId];
        for (uint EdgeId = Edges.StartConnections; EdgeId < Edges.EndConnections; ++EdgeId)
        {
            uint OtherNodeId = EdgeArray[EdgeId];
            vec2 OtherNodePos = NodePositionArray[OtherNodeId].Pos;

            float Distance = length(CurrNodePos - OtherNodePos);
            if (Distance > GraphGlobals.LayoutEdgeMinDist)
            {
                vec2 NormalVec = (CurrNodePos - OtherNodePos) / Distance;
                vec2 Vel = GraphGlobals.LayoutEdgeAccel * NormalVec;
                NodeVelocityArray[CurrNodeId] -= Vel;
            }
        }
    }
}

#endif

//=========================================================================================================================================
// NOTE: Graph Nearby Shader
//=========================================================================================================================================

#if GRAPH_NEARBY

float RandomFloat(vec2 uv)
{
    // NOTE: https://stackoverflow.com/questions/4200224/random-noise-functions-for-glsl
    return fract(sin(dot(uv,vec2(12.9898,78.233)))*43758.5453123);
}

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
void main()
{
    uint CurrNodeId = gl_GlobalInvocationID.x;
    if (CurrNodeId < GraphGlobals.NumNodes)
    {
        graph_node_pos CurrNodePos = NodePositionArray[CurrNodeId];
        
        // TODO: Do we want to put nodes into a grid or will this be fast enough?
        for (uint OtherNodeId = 0; OtherNodeId < GraphGlobals.NumNodes; ++OtherNodeId)
        {
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
    }
}

#endif

//=========================================================================================================================================
// NOTE: Graph Update Nodes Shader
//=========================================================================================================================================

#if GRAPH_UPDATE_NODES

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
void main()
{
    uint CurrNodeId = gl_GlobalInvocationID.x;
    if (CurrNodeId < GraphGlobals.NumNodes)
    {
        vec2 Vel = NodeVelocityArray[CurrNodeId];
        NodeVelocityArray[CurrNodeId] = vec2(0);
        NodePositionArray[CurrNodeId].Pos += Vel * GraphGlobals.FrameTime * GraphGlobals.FrameTime;
    }
}

#endif

//=========================================================================================================================================
// NOTE: Graph Gen Edges Shader
//=========================================================================================================================================

#if GRAPH_GEN_EDGES

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
void main()
{
    uint CurrNodeId = gl_GlobalInvocationID.x;
    if (CurrNodeId < GraphGlobals.NumNodes)
    {
        vec2 CurrNodePos = NodePositionArray[CurrNodeId].Pos;
        graph_node_edges Edges = NodeEdgeArray[CurrNodeId];
        for (uint EdgeId = Edges.StartConnections; EdgeId < Edges.EndConnections; ++EdgeId)
        {
            uint OtherNodeId = EdgeArray[EdgeId];
            vec2 OtherNodePos = NodePositionArray[OtherNodeId].Pos;

            // NOTE: Write out draw data to vertex buffer
            DrawEdgePositionArray[2*EdgeId + 0] = CurrNodePos;
            DrawEdgePositionArray[2*EdgeId + 1] = OtherNodePos;
        }
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
    graph_node_pos PosEntry = NodePositionArray[gl_InstanceIndex];
    graph_node_draw DrawEntry = NodeDrawArray[gl_InstanceIndex];
    gl_Position = GraphGlobals.VPTransform * vec4(vec3(PosEntry.Pos, 0) + DrawEntry.Scale * InPos, 1);
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
layout(location = 1) in vec4 InColor;

layout(location = 0) out vec4 OutColor;
layout(location = 1) out vec2 OutLineCenter;

void main()
{
    // NOTE: https://vitaliburkov.wordpress.com/2016/09/17/simple-and-fast-high-quality-antialiased-lines-with-opengl/
    vec4 ProjectedPos = GraphGlobals.VPTransform * vec4(InPos, 0, 1);
    
    gl_Position = ProjectedPos;
    OutColor = InColor;
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
    if (DistanceFromCenter > LineWidth)
    {
        Alpha = 0.0f;
    }
    else
    {
        Alpha *= pow((LineWidth - DistanceFromCenter) / LineWidth, BlendFactor);
    }
    
    OutColor = vec4(InColor.rgb * Alpha, Alpha);
}

#endif
