var step = 0;

render = function(){
    ++step;
    
    translate(-50,-3,-50.0);
    scale(100,100,100);

    for(var i = 0; i < 30; i++){
        translate(0,0.0001,0);
        translate(
            Math.sin(i) * 0.00008,
            0,
            Math.sin(i) * 0.00008
        );
        plane();
    }
};

function plane(){
    var P1 = [0,0,1];
    var P2 = [1,0,0];
    var P3 = [1,0,1];
    var P4 = [0,0,0];

    strips = [];
    
    strips.push(
        P1.concat(P2,P3,P4,P1)
    );

    // Draw faces
    for(var j in strips){
        var strip = strips[j];
        triangle_strip(
            strip
        );
    }
}