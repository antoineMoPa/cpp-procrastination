
enable_2_pass_pp();

var mouse = {x:0,y:0};

function render(){
    shader_var("mouse_x",mouse.x);
    shader_var("mouse_y",mouse.y);
}

function on_key(key, x, y){

}

function on_mouse_move(x, y){
    mouse.x = x;
    mouse.y = y;
}
