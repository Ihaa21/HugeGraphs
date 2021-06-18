#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

struct circle_entry
{
    mat4 WVPTransform;
    vec4 Color;
};

layout(set = 0, binding = 0) buffer instance_buffer
{
    circle_entry CircleEntries[];
};

//=========================================================================================================================================
// NOTE: Vertex Shader
//=========================================================================================================================================

#if VERTEX_SHADER

layout(location = 0) in vec3 InPos;
layout(location = 1) in vec2 InUv;

layout(location = 0) out vec4 OutColor;
layout(location = 1) out vec2 OutUv;

void main()
{
    circle_entry Entry = CircleEntries[gl_InstanceIndex];
    gl_Position = Entry.WVPTransform * vec4(InPos, 1);
    OutColor = Entry.Color;
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
    
    // NOTE: Premul alpha
    OutColor = vec4(InColor.rgb * Alpha, Alpha);
}

#endif
