#include "ShaderProgram.hpp"
#include <EGL/egl.h>

#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr)) {
        std::cout << "No offscreen EGL context available\n";
        return 77;
    }
    EGLConfig config;
    EGLint count = 0;
    const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                 EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
    if (!eglBindAPI(EGL_OPENGL_API) ||
        !eglChooseConfig(display, attributes, &config, 1, &count) || count == 0) {
        eglTerminate(display);
        return 77;
    }
    const EGLint contextAttributes[] = {EGL_CONTEXT_MAJOR_VERSION, 3,
                                        EGL_CONTEXT_MINOR_VERSION, 3, EGL_NONE};
    const EGLint surfaceAttributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
    EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttributes);
    if (context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(display, surface, surface, context)) {
        if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
        if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
        eglTerminate(display);
        return 77;
    }
    // GLEW may report no GLX display under EGL even after loading the GL pointers.
    glewExperimental = GL_TRUE;
    glewInit();
    int result = 0;
    if (!glCreateShader || !glCreateProgram || !glGetProgramiv) {
        result = 77;
    } else {
        try {
            const char* vertex = "#version 330 core\nvoid main(){gl_Position=vec4(0.0);}";
            const char* fragment = "#version 330 core\nout vec4 color; void main(){color=vec4(1.0);}";
            GLuint program = qm::createShaderProgram(vertex, fragment);
            glDeleteProgram(program);
            auto rejects = [](const char* vs, const char* fs, const char* expected) {
                try {
                    GLuint unexpected = qm::createShaderProgram(vs, fs);
                    glDeleteProgram(unexpected);
                } catch (const std::runtime_error& error) {
                    if (std::string(error.what()).find(expected) != std::string::npos) return;
                    throw;
                }
                throw std::runtime_error("Invalid shader program was accepted");
            };
            rejects("#version 330 core\ninvalid", fragment, "Shader compilation failed:");
            rejects(vertex, "#version 330 core\ninvalid", "Shader compilation failed:");
            rejects("#version 330 core\nout vec3 varyingColor; void main(){varyingColor=vec3(1.0);gl_Position=vec4(0.0);}",
                    "#version 330 core\nin vec4 varyingColor; out vec4 color; void main(){color=varyingColor;}",
                    "Shader linking failed:");
            std::cout << "Offscreen shader regressions passed\n";
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            result = 1;
        }
    }
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(display, surface);
    eglDestroyContext(display, context);
    eglTerminate(display);
    return result;
}
