#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

struct layout_inputs
{
    float FrameTime;
    uint NumNodes;
};

struct graph_node
{
    vec2 Pos;
    vec2 Vel;
    uint StartConnections;
    uint EndConnections;
    uint Pad[2];
};

//=========================================================================================================================================
// NOTE: Graph Node Layout Shader
//=========================================================================================================================================

#if GRAPH_NODE_LAYOUT

void main()
{
    
}

#endif
