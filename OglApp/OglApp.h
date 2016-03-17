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

#include "Shader.h"

namespace OglApp{
    Shader * current_shader = nullptr;
    Shader post_process_shader;
}

#include "Image.h"
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
}

#include "js_functions.h"

/*
  For other JS versions: look at
  https://developer.mozilla.org/en-US/docs/Mozilla/Projects/SpiderMonkey/How_to_embed_the_JavaScript_engine#Original_Document_Information
*/
namespace OglApp{    
    int w = 100;
    int h = 100;
    int i = 0;
    bool has_default_shader = false;
    
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

    
    static void resize(int rhs_w, int rhs_h){
        w = rhs_w;
        h = rhs_h;
        camera.mat.resize(w,h);
    }

    static void stop(){
        JS_DestroyContext(cx);
        JS_DestroyRuntime(rt);
        JS_ShutDown();
    }

    /*
      Render to a texture then render a scene with the texture
      Reference: 
      http://www.opengl-tutorial.org/intermediate-tutorials/tutorial-14-render-to-texture/
     */
    static void render(){
        // Skip post processing for now
        if(false){
            // Create framebuffer
            GLuint fb_id = 0;
            glGenFramebuffers(1, &fb_id);
            glBindFramebuffer(GL_FRAMEBUFFER, fb_id);
            
            GLuint rendered_tex;
            glGenTextures(1, &rendered_tex);
            glBindTexture(GL_TEXTURE_2D, rendered_tex);
            
            // Give an empty image to OpenGL ( the last "0" )
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RGBA,
                w,h,
                0,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                0);
            
            // Poor filtering. Needed !
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            
            // The depth buffer
            
            GLuint depth_buf;
            
            glGenRenderbuffers(1, &depth_buf);
            glBindRenderbuffer(GL_RENDERBUFFER, depth_buf);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, w, h);
            glFramebufferRenderbuffer(
                GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_buf
                );
            
            // Use rendered_tex
            glFramebufferTexture(
                GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0, rendered_tex, 0
                );
            
            // Set the list of draw buffers.
            GLenum draw_buffers[1] = {GL_COLOR_ATTACHMENT0};
            glDrawBuffers(1, draw_buffers);
            
            if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE){
                cerr << "Framebuffer setup error" << endl;
            }
            
            glBindFramebuffer(GL_FRAMEBUFFER, fb_id);
            glViewport(0,0,w,h);
        }
        
        // Actual rendering
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        
        camera.mat.clear_model();        
        
        if(has_default_shader){
            current_shader = &shaders[string("default")];
            current_shader->bind();
        }
        
        JS::RootedValue rval(cx);
        JS::AutoValueVector argv(cx);
        
        JSBool ok = JS_CallFunctionName(
                            cx,
                            *gl,
                            "render",
                            0,
                            argv.begin(),
                            rval.address()
                            );

        // End of the actual rendering
        // Skip post processing for now
        if(false){
            GLuint quad_vertex_array_id;
            glGenVertexArrays(1, &quad_vertex_array_id);
            glBindVertexArray(quad_vertex_array_id);
            
            static const GLfloat quad_vertex_buffer_data[] = {
                -1.0f, -1.0f, 0.0f,
                1.0f, -1.0f, 0.0f,
                -1.0f,  1.0f, 0.0f,
                -1.0f,  1.0f, 0.0f,
                1.0f, -1.0f, 0.0f,
                1.0f,  1.0f, 0.0f,
            };
            
            GLuint quad_vertexbuffer;
            glGenBuffers(1, &quad_vertexbuffer);
            glBindBuffer(GL_ARRAY_BUFFER, quad_vertexbuffer);
            glBufferData(GL_ARRAY_BUFFER,
                         sizeof(quad_vertex_buffer_data),
                         quad_vertex_buffer_data,
                         GL_STATIC_DRAW);
            
            glDrawArrays(GL_QUADS, 0, 4);
            
            // Create and compile our GLSL program from the shaders
            
            post_process_shader.bind();
            
            GLuint pid = post_process_shader.get_id();
            //GLuint tex_id = glGetUniformLocation(pid, "rendered");
            //GLuint time_id = glGetUniformLocation(pid, "time");
            
            glBindFramebuffer(GL_FRAMEBUFFER, 1);
            glViewport(0,0,w,h);
        }
        
        glFlush();
        glutSwapBuffers();
    }
    
    static void keyboard(unsigned char key, int x, int y){
        cout << "key " << key
             << " x: " << x
             << " y: " << y
             << endl;
    }
    
    static void load_default_shaders(){
        // default shaders
        char * vertex_path =
            strdup("vertex.glsl");
        char * frag_path =
            strdup("fragment.glsl");

        Shader s;
        
        if(!s.load(vertex_path,frag_path)){
            cout << "No default vertex & fragment shader found." << endl;
            return;
        }
        s.bind();

        using new_el = ShaderMap::value_type;
        
        shaders.insert(new_el("default",s));
        has_default_shader = true;
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
    
    static void apploop(){
        glutInit(&argc,argv);
        glClearColor(0.0f,0.0f,0.0f,0.0f);
        glutInitDisplayMode(GLUT_DOUBLE|GLUT_RGBA|GLUT_DEPTH);
        glutInitWindowSize(w,h);
        
        glutCreateWindow("Hey");
        
        // http://gamedev.stackexchange.com/questions/22785/
        GLenum err = glewInit();
        if (err != GLEW_OK){
            cout << "GLEW error: " << err << endl;
            cout <<  glewGetErrorString(err) << endl;
        }

        load_default_shaders();
        
        glutKeyboardFunc((*keyboard));

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_BLEND);

        post_process_shader.load(
            (app_path+"post-vertex.glsl").c_str(),
            (app_path+"post-fragment.glsl").c_str());
        
        JS::RootedValue rval(cx);
        run_file((app_path+"main.js").c_str(),&rval);
        
        glutReshapeFunc(resize);
        glutDisplayFunc(render);
        glutIdleFunc(render);
        
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

                static JSFunctionSpec my_functions[] = {
                    JS_FN("plus", jsfn::plus, 2, 0),
                    JS_FN("translate", jsfn::translate, 3, 0),
                    JS_FN("rotate", jsfn::rotate, 4, 0),
                    JS_FN("triangle_strip", jsfn::triangle_strip, 1, 0),
                    JS_FN("color", jsfn::color, 4, 0),
                    JS_FN("render_model", jsfn::render_model, 1, 0),
                    JS_FN("load_shaders", jsfn::load_shaders, 3, 0),
                    JS_FN("bind_shaders", jsfn::bind_shaders, 1, 0),
                    JS_FN("scale", jsfn::scale, 3, 0),
                    JS_FN("divide", jsfn::divide, 2, 0),
                    JS_FN("log", jsfn::log, 1, 0),
                    JS_FN("push_matrix", jsfn::push_matrix, 0, 0),
                    JS_FN("pop_matrix", jsfn::pop_matrix, 0, 0),
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
