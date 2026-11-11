#version 450 core

layout (location = 0) in vec3 aPos;

out vec2 v_TexCoord;

void main()
{
    v_TexCoord = aPos.xy * 0.5 + 0.5;
    gl_Position = vec4(aPos, 1.0);
}
