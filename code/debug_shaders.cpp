#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#include "shader_descriptor_layouts.cpp"

SCENE_DESCRIPTOR_LAYOUT(0)

#if VERTEX_SHADER

layout(location = 0) in vec3 InPos;
layout(location = 1) in vec4 InColor;

layout(location = 0) out vec4 OutColor;

void main()
{
    gl_Position = SceneBuffer.VPTransform * vec4(InPos, 1);
    OutColor = InColor;
}

#endif

#if POINT_VERTEX_SHADER

layout(location = 0) in vec3 InPos;
layout(location = 1) in vec4 InColor;

layout(location = 0) out vec4 OutColor;

void main()
{
    gl_Position = SceneBuffer.VPTransform * vec4(InPos, 1);
    gl_PointSize = 5.0f;
    OutColor = InColor;
}

#endif

#if FRAGMENT_SHADER

layout(location = 0) in vec4 InColor;

layout(location = 0) out vec4 OutColor;

void main()
{
    OutColor = vec4(InColor.rgb * InColor.a, InColor.a);
}

#endif
