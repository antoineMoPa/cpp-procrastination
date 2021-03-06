#define GLM_FORCE_RADIANS
#define GLM_SWIZZLE

#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <array>
#include <map>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <GL/glew.h>
#include <GL/glut.h>
#include <cstdio>
#include <ctime>
#include <unistd.h>
#include <algorithm>
#include <jsapi.h>

#include "platform.h"
#include "Shader.h"

namespace OglApp{
    Shader * current_shader = nullptr;
    Shader post_process_shader;
    // This frame counter resets at frame resize
    int frame_count = 0;
}

#include "Image.h"

namespace OglApp{
    // The texture that we can post process
    // (and render on the quad)
    using TextureMap = std::map<std::string,Image>;
    TextureMap js_textures;

    /*
      Contains data about an opengl framebuffer
      And the texture it gets rendered to
    */
    class FrameBuffer{
    public:
        Image * rendered_tex;
        GLuint fb_id;
        GLuint depth_buf;

        void delete_ressources(){
            rendered_tex->delete_ressources();
            glDeleteRenderbuffers(1, &depth_buf);
            glDeleteRenderbuffers(1, &fb_id);
        }
        
        void create(int w, int h){
            // Create framebuffer
            glGenFramebuffers(1, &fb_id);
            glGenRenderbuffers(1, &depth_buf);
            glBindFramebuffer(GL_FRAMEBUFFER, fb_id);
            
            rendered_tex = new Image(w,h);
            
            // Poor filtering. Needed !
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            
            glFramebufferTexture2D(
                                   GL_FRAMEBUFFER,
                                   GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D,
                                   rendered_tex->get_id(),0);
            
            GLenum DrawBuffers[1] = {GL_COLOR_ATTACHMENT0};
            glDrawBuffers(1, DrawBuffers);

            glBindRenderbuffer(GL_RENDERBUFFER, depth_buf);
            glRenderbufferStorage(GL_RENDERBUFFER,
                                  GL_DEPTH_COMPONENT, w, h);
            
            glFramebufferRenderbuffer(
                                      GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_buf
                                      );
             
            if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE){
                cerr << "Framebuffer setup error" << endl;
            }
        }
        
        void resize(int w, int h){
            delete_ressources();
            create(w, h);
            rendered_tex->resize(w, h);
        }
    };

    bool enable_2_pass_pp = false;
    const int pass_total = 3;
    FrameBuffer fbs [pass_total + 1];
}

#include "Model.h"
#include "Matrix.h"
#include "Camera.h"

using namespace std;

namespace OglApp{
    // Default app path
    string app_path = "world/";

    using ModelMap = std::map<std::string,Model>;
    ModelMap models;

    using ShaderMap = std::map<std::string,Shader>;
    ShaderMap shaders;

    void compute_matrix()
    {
        glm::mat4 mvp = camera.mat.model_view_matrix();

        // Update MVP in all shaders

        typedef ShaderMap::iterator map_it;
        for( map_it it = shaders.begin();
             it != shaders.end();
             it++ ){
            Shader * shader = &it->second;
            GLuint MatrixID =
                glGetUniformLocation(shader->ProgramID, "MVP");
            glUniformMatrix4fv(MatrixID,1,GL_FALSE,&mvp[0][0]);
        }
    }

    // Window width
    int w = 100;
    // Window height
    int h = 100;
}

#include "js_functions.h"

/*
  For other JS versions: look at
  https://developer.mozilla.org/en-US/docs/Mozilla/Projects/SpiderMonkey/How_to_embed_the_JavaScript_engine#Original_Document_Information
*/
namespace OglApp{
    // The depth buffer
    GLuint depth_buf;
    FrameBuffer fb;
    
    // The data of the render-to-texture quad
    GLuint quad_vertexbuffer;

    JSContext * cx = NULL;
    // global object
    JS::RootedObject * gl;
    JSRuntime *rt;

    int argc;
    char ** argv;

    /* The class of the global object. */
    static JSClass global_class = {
        "global",
        JSCLASS_GLOBAL_FLAGS,
        JS_PropertyStub,
        JS_DeletePropertyStub,
        JS_PropertyStub,
        JS_StrictPropertyStub,
        JS_EnumerateStub,
        JS_ResolveStub,
        JS_ConvertStub
    };

    /**
       Window resize callback
     */
    static void resize(int rhs_w, int rhs_h){
        w = rhs_w;
        h = rhs_h;
        camera.mat.resize(w,h);
        
        for(int i = 0; i < pass_total + 1; i++){
            fbs[i].resize(w, h);
        }
        frame_count = 0;
    }

    /**
       Things to do when we kill JS
     */
    static void stop(){
        JS_DestroyContext(cx);
        JS_DestroyRuntime(rt);
        JS_ShutDown();
    }

    void main_render(){
        glViewport(0,0,w,h);
        
        // Actual rendering
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

        // Reset camera transforms
        camera.mat.clear_model();
        
        current_shader = &shaders[string("default")];
        current_shader->bind();
        fbs[0].rendered_tex->bind(3,"rendered_tex");
        
        // return value and empty arg
        JS::RootedValue rval(cx);
        JS::AutoValueVector argv(cx);

        // Call javascript "render" function
        // defined in the app's main.js
        JSBool ok = JS_CallFunctionName(
            cx,
            *gl,
            "render",
            0,
            argv.begin(),
            rval.address()
            );

        glFlush();        
    }

    void post_process_render(int pass){
        glViewport(0,0,w,h);

        // TODO document all these defined values in some place
        // for new users
        
        // Add aspect ratio
        GLuint loc = post_process_shader
            .get_uniform_location("ratio");
        
        // Bind it
        glUniform1f(loc,(float)w/(float)h);

        // Add screen dimensions
        // Width
        loc = post_process_shader
            .get_uniform_location("screen_w");
        
        glUniform1i(loc,w);

        // Height
        loc = post_process_shader
            .get_uniform_location("screen_h");
        
        glUniform1i(loc,h);

        
        // Add timestamp
        loc = post_process_shader
            .get_uniform_location("time");
        
        // Bind timestamp to variable
        glUniform1i(loc,get_timestamp());

        // Add render pass number
        loc = post_process_shader
            .get_uniform_location("pass");
        
        // Bind pass number
        glUniform1i(loc,pass);

        // Add frame count
        loc = post_process_shader
            .get_uniform_location("frame_count");

        glUniform1i(loc,frame_count);

        if(pass < 1){
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        } else {
            glClear(GL_DEPTH_BUFFER_BIT);
        }
        
        // Render the plane
        glEnableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, quad_vertexbuffer);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,(void*)0);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(0);

        glFlush();
    }

    void two_pass_pp(){
        int pass;
        GLuint pps = post_process_shader.get_id();
        int last_fb = 0;
        int curr_fb;
        
        post_process_shader.bind();
        
        int limit = pass_total;

        /*
          Render each pass
          Bind texture of last pass to pass_0/pass_1/etc.
          pass_0 is the main rendered scene
         */
        for(pass = 1; pass <= limit; pass++){
            curr_fb = pass;
            
            string num = "0";
            num[0] += pass - 1;
            string pass_name("pass_");
            pass_name += num;

            // bind last texture
            fbs[last_fb]
                .rendered_tex->bind(pps,3,"last_pass");

            fbs[last_fb]
                .rendered_tex->bind(pps,3 + pass,pass_name.c_str());

            if(pass != pass_total){
                // Render texture on a plane
                glBindFramebuffer( GL_FRAMEBUFFER,
                                   fbs[curr_fb].fb_id );
            } else {
                // Last pass: render to screen
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }
            
            post_process_render(pass);
            last_fb = curr_fb;
        }
    }
    
    static void render(){
        // Prepare to render to a texture
        glBindFramebuffer(GL_FRAMEBUFFER, fbs[0].fb_id);
        main_render();

        if(enable_2_pass_pp == true){
            two_pass_pp();
        } else {
            GLuint pps = post_process_shader.get_id();
            fbs[0]
                .rendered_tex->bind(pps,0,"renderedTexture");

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            post_process_shader.bind();
            post_process_render(0);
        }

        glutSwapBuffers();
        frame_count++;
    }

    /**
       Manages up/down event
       Was made instead of writing almost the same code 
       in key_up and key_down.
     */
    static void key_event( unsigned char key,
                           int x, int y,
                           bool is_down ){
        // return value and empty arg
        JS::RootedValue rval(cx);
        JS::AutoValueVector argv(cx);

        argv.resize(3);
        
        JS::Value js_key;
        JS::Value js_x;
        JS::Value js_y;

        string key_s = " ";
        key_s[0] = key;
        
        js_key.setString(JS_NewStringCopyN(cx,key_s.c_str(),1));

        js_x.setNumber((float)x/(float)w);
        js_y.setNumber(1.0 - (float)y/(float)h);
        
        argv[0] = js_key;
        argv[1] = js_x;
        argv[2] = js_y;

        const char * fct_name = is_down?
            "on_key_down": "on_key_up";
        
        JSBool ok = JS_CallFunctionName(
            cx,
            *gl,
            fct_name,
            3,
            argv.begin(),
            rval.address()
            );

    }

    /**
       Fired when a key is pressed
     */
    static void key_down(unsigned char key, int x, int y){
        key_event(key, x, y, true);
    }

    /**
       Fired when a key is released
     */
    static void key_up(unsigned char key, int x, int y){
        key_event(key, x, y, false);
    }
    
    static void mouse_func(int button, int state, int x, int y){
        // TODO: create a class containing mouse state
        // and update it here
    }

    static void mouse_motion(int x, int y){
        // return value and empty arg
        JS::RootedValue rval(cx);
        JS::AutoValueVector argv(cx);
        
        argv.resize(2);
        
        JS::Value js_x;
        JS::Value js_y;

        js_x.setNumber((float)x/(float)w);
        js_y.setNumber(1.0 - (float)y/(float)h);
        
        argv[0] = js_x;
        argv[1] = js_y;

        const char * fct_name = "on_mouse_move";
        
        // Call javascript "render" function
        // defined in the app's main.js
        JSBool ok = JS_CallFunctionName(
            cx,
            *gl,
            fct_name,
            2,
            argv.begin(),
            rval.address()
            );
    }
    
    static void load_default_shaders(){
        // default shaders
        char * vertex_path =
            strdup("./vertex.glsl");
        char * frag_path =
            strdup("./fragment.glsl");

        Shader s;
        cout << frag_path;
        cout << vertex_path;
        if(!s.load(vertex_path,frag_path)){
            cout << "No default vertex & fragment shader found." << endl;
            exit(0);
            return;
        }
        s.bind();

        using new_el = ShaderMap::value_type;

        shaders.insert(new_el("default",s));
    }

    static bool run_file(const char * filename, JS::RootedValue * rval){
        int lineno = 0;

        ifstream file;

        file.open(filename);

        // Take the content of the file
        // and put it in a string
        stringstream strStream;
        strStream << file.rdbuf();
        string str = strStream.str();
        const char * script = str.c_str();

        bool ok = JS_EvaluateScript
            (
             cx,
             *gl,
             script,
             strlen(script),
             filename,
             lineno,
             rval->address()
             );

        return ok;
    }

    /**
       Creates the plane that will be used to render everything on
     */
    static void create_render_quad(){
        GLuint quad_vertex_array_id;
        // Create a quad
        glGenVertexArrays(1, &quad_vertex_array_id);
        glBindVertexArray(quad_vertex_array_id);

        static const GLfloat quad_vertex_buffer_data[] = {
            -1.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 0.0f,
            -1.0f,  -1.0f, 0.0f,
            1.0f,  -1.0f, 0.0f
        };

        // Put the data in buffers
        glGenBuffers(1, &quad_vertexbuffer);
        glBindBuffer(GL_ARRAY_BUFFER, quad_vertexbuffer);
        glBufferData(GL_ARRAY_BUFFER,
                     sizeof(quad_vertex_buffer_data),
                     quad_vertex_buffer_data,
                     GL_STATIC_DRAW);
    }

    static void apploop(){
        glutInit(&argc,argv);
        glClearColor(0.0f,0.0f,0.0f,0.0f);
        glutInitDisplayMode(GLUT_DOUBLE|GLUT_RGBA|GLUT_DEPTH);
        glutInitWindowSize(w,h);

        glutCreateWindow("ogl-js");

        // http://gamedev.stackexchange.com/questions/22785/
        GLenum err = glewInit();
        if (err != GLEW_OK){
            cout << "GLEW error: " << err << endl;
            cout <<  glewGetErrorString(err) << endl;
        }

        load_default_shaders();

        // Listen to the keyboard
        glutKeyboardFunc((*key_down));
        // Listen to the keyboard
        glutKeyboardUpFunc((*key_up));
        
        // Listen to mouse move
        glutMotionFunc((*mouse_motion));
        glutPassiveMotionFunc((*mouse_motion));
        
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        // Alpha mixing setup
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_BLEND);

        // Load post processing shaders
        post_process_shader.load(
            (app_path+"post-vertex.glsl").c_str(),
            (app_path+"post-fragment.glsl").c_str());

        // Setup javascript global context
        // Load main.js containing console.log()
        JS::RootedValue rval(cx);
        run_file((app_path+"main.js").c_str(),&rval);

        // Assign callbacks
        glutReshapeFunc(resize);
        glutDisplayFunc(render);
        glutIdleFunc(render);

        // Create the plane for render-to-texture
        create_render_quad();
        
        // Init buffers for render-to-texture
        // and post process
        for(int i = 0; i < pass_total + 1; i++){
            fbs[i].create(w, h);
        }
        
        // The app becomes alive here
        glutMainLoop();
    }

    static void dispatchError(
                              JSContext* cx,
                              const char* message,
                              JSErrorReport* report) {
        cout << "Javascript error at line "
             << report->lineno << " column "
             << report->column << endl
             << message << endl
             << "'" << report->linebuf << "'" << endl;
    }

    static int initJavascript(void (*after_run_callback)(void)){
        rt = JS_NewRuntime(8L * 1024 * 1024, JS_USE_HELPER_THREADS);
        if (!rt)
            return 1;

        cx = JS_NewContext(rt, 8192);

        JS_SetErrorReporter(cx, &OglApp::dispatchError);

        if (!cx)
            return 1;

        JS::RootedValue rval(cx);

        {
            // Scope for our various stack objects
            // (JSAutoRequest, RootedObject), so they all go
            // out of scope before we JS_DestroyContext.
            JSAutoRequest ar(cx);
            // In practice, you would want to exit this any
            // time you're spinning the event loop

            JS::RootedObject global(cx, JS_NewGlobalObject(cx, &global_class, nullptr));

            gl = &global;

            if (!global)
                return 1;

            // Scope for JSAutoCompartment
            {
                JSAutoCompartment ac(cx, global);
                JS_InitStandardClasses(cx, global);
                /*
                  function name in js,
                  c++ function,
                  argument count,
                  flags
                 */
                static JSFunctionSpec my_functions[] = {
                    JS_FN("plus", jsfn::plus, 2, 0),
                    JS_FN("translate", jsfn::translate, 3, 0),
                    JS_FN("rotate", jsfn::rotate, 4, 0),
                    JS_FN("triangle_strip", jsfn::triangle_strip, 1, 0),
                    JS_FN("color", jsfn::color, 4, 0),
                    JS_FN("render_model", jsfn::render_model, 1, 0),
                    JS_FN("load_shaders", jsfn::load_shaders, 3, 0),
                    JS_FN("bind_shaders", jsfn::bind_shaders, 1, 0),
                    JS_FN("create_triangle_array", jsfn::create_triangle_array, 4, 0),
                    JS_FN("render_triangle_array", jsfn::render_triangle_array, 1, 0),
                    JS_FN("scale", jsfn::scale, 3, 0),
                    JS_FN("divide", jsfn::divide, 2, 0),
                    JS_FN("log", jsfn::log, 1, 0),
                    JS_FN("push_matrix", jsfn::push_matrix, 0, 0),
                    JS_FN("pop_matrix", jsfn::pop_matrix, 0, 0),
                    JS_FN("load_texture",jsfn::load_texture,2,0),
                    JS_FN("window_width",jsfn::window_width,3,0),
                    JS_FN("window_height",jsfn::window_height,3,0),
                    JS_FN("enable_2_pass_pp",jsfn::enable_2_pass_pp,0,0),
                    JS_FN("shader_var",jsfn::shader_var,2,0),
                    JS_FS_END
                };
                
                bool ok = JS_DefineFunctions(cx, global, my_functions);
                if(!ok){
                    cout << "Function definition error" << endl;
                    return 1;
                }

                run_file("jslib/main.js",&rval);

                // Now we can call functions from
                // the script
                after_run_callback();

                if(!ok){
                    return 1;
                }
            }
            if(rval.isString()){
                JSString *str = rval.toString();
                char * str2 = JS_EncodeString(cx, str);
                printf("%s\n", str2);
            }
        }

        stop();
    }

    static void start(int _argc, char ** _argv){
        argc = _argc;
        argv = _argv;
        if(argc >= 2){
            app_path = argv[1];
            if(app_path[app_path.size()-1] != '/'){
                app_path += '/';
            }
        }
        initJavascript(apploop);
    }
}
