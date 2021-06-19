#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

struct circle_entry
{
    mat4 WVPTransform;
    vec4 Color;
};

layout(set = 0, binding = 0) uniform scene_buffer
{
    mat4 VPTransform;
    vec2 ViewPort;
} SceneBuffer;

layout(set = 0, binding = 1) buffer circle_entry_buffer
{
    circle_entry CircleEntries[];
};

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
    vec4 ProjectedPos = SceneBuffer.VPTransform * vec4(InPos, 0, 1);
    
    gl_Position = ProjectedPos;
    OutColor = InColor;
    OutLineCenter = 0.5 * (ProjectedPos.xy + vec2(1)) * SceneBuffer.ViewPort;
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
