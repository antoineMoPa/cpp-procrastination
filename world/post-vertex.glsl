#version 300 es
layout(location = 0) in vec3 v_pos;

out vec2 UV;

void main(){
    gl_Position = vec4(v_pos,1.0);
    UV = v_pos.xy;
}
