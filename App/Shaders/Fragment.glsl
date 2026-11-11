#version 450 core

in vec2 v_TexCoord;

out vec4 FragColor;

uniform sampler2D u_Texture;

void main()
{
    vec3 color = texture(u_Texture, v_TexCoord).rgb;
    color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
