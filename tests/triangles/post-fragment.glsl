#version 300 es

in highp vec2 UV;
out highp vec4 color;
uniform sampler2D last_pass;
uniform sampler2D pass_0;
uniform sampler2D pass_1;
uniform sampler2D pass_2;
uniform int time;
uniform int pass;
uniform int screen_w;
uniform int screen_h;
uniform int frame_count;
uniform int wall;
uniform int reset;
uniform int pause;
uniform int shading;
uniform int source_type;
uniform highp float ratio;
uniform highp float damp;
uniform highp float radius;
uniform highp float mouse_x;
uniform highp float mouse_y;
highp vec4 rand_var;

void main(){
    // We'll use this later
    highp vec4 last = texture(last_pass,UV);

    // Height, speed and resistance is stored in a texture
    // red = x = height
    // green = y = speed
    // etc.
    // Since we can't store a negative value,
    // values are given a 0.5 offset at storage
    // (Also: we cannot store values > 1.0)
    highp vec4 data = texture(pass_2,UV);
    highp float height = data.x - 0.5;
    highp float speed = data.y - 0.5;
    highp float resistance = data.z;

    // Ratio used to find distance between mouse and
    // current position
    highp vec2 uv_ratio = vec2(1.0,1.0 / ratio);

    // Compute mouse distance
    highp float mouse_dist =
        distance(UV * uv_ratio,
                 vec2(mouse_x,mouse_y) * uv_ratio);

    highp float pixel_width = 1.0 / float(screen_w);
    highp float pixel_heigth = 1.0 / float(screen_h);
    
    // Create these values to find neighboring cells
    highp vec2 x_offset = vec2(pixel_width,0.00);
    highp vec2 y_offset = vec2(0.00,pixel_heigth);
    
    if(frame_count == 0 || reset == 1){
        if(distance(UV,vec2(0.5,0.95)) < float(pixel_width)){
            color = vec4(1.0,1.0,1.0,1.0);
        } else {
            color = vec4(0.0,0.0,0.0,1.0);
        }
    } else if(pass == 1) {
        int active_line = frame_count % screen_h;
        int this_line = int(
                            (1.0-UV.y) *
                            float(screen_h));

        last = texture(pass_2,UV);

        if(last.x > 0.5){
            color = last;
            return;
        }
        
        if(pause == 1){
            color = last;
            return;
        }
            
        if(!(this_line == active_line)){
            color = last;
            return;
        }

        bool cell_1 = texture(pass_2,
                              UV + y_offset - x_offset).x > 0.5;
        bool cell_2 = texture(pass_2,
                              UV + y_offset).x > 0.5;
        bool cell_3 = texture(pass_2,
                              UV + y_offset + x_offset).x > 0.5;
        int num = 0;
        
        if(cell_3){
            num += 4;
        }
        if(cell_2){
            num += 2;
        }
        if(cell_1){
            num += 1;
        }
        
        bool next = false;
        int rule = 110;
        
        switch(num){
        case 0:
            next = (rule & 1) != 0;
            break;
        case 1:
            next = (rule & 2) != 0;
            break;
        case 2:
            next = (rule & 4) != 0;
            break;
        case 3:
            next = (rule & 8) != 0;
            break;
        case 4:
            next = (rule & 16) != 0;
            break;
        case 5:
            next = (rule & 32) != 0;
            break;
        case 6:
            next = (rule & 64) != 0;
            break;
        default:
            next = (rule & 128) != 0;
            break;
        }
        
        if(next){
            color.rgb = vec3(1.0);
        } else {
            color = last;
        }
        
        color.a = 1.0;
    } else if(pass == 2){
        // We do nothing here
        color = last;
    } else if(pass == 3){
        color = last;
        color.a = 1.0;
    }
}
