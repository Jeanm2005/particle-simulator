#include "ShaderProgram.hpp"
#include "RaytraceShaders.hpp"
#include "RaytraceChecks.hpp"
#include <array>
#include <cmath>
#include <EGL/egl.h>

#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    const bool benchmark = argc == 2 && std::string(argv[1]) == "--benchmark";
    if (argc > 1 && !benchmark) {
        std::cerr << "Usage: shader_tests [--benchmark]\n";
        return 1;
    }
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr)) {
        std::cout << "No offscreen EGL context available\n";
        return 77;
    }
    EGLConfig config;
    EGLint count = 0;
    const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                 EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                                 EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
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
            GLuint orbital = qm::createShaderProgram(qm::raytraceVertex, qm::raytraceFragment);
            glUseProgram(orbital);
            GLuint vao, buffer;
            glGenVertexArrays(1, &vao);
            glBindVertexArray(vao);
            glGenBuffers(1, &buffer);
            glBindBuffer(GL_ARRAY_BUFFER, buffer);
            const float triangle[] = {-1,-1, 3,-1, -1,3};
            glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
            glViewport(0, 0, 1, 1);
            auto scalar = [orbital](const char* name, float value) {
                glUniform1f(glGetUniformLocation(orbital, name), value);
            };
            auto integer = [orbital](const char* name, int value) {
                glUniform1i(glGetUniformLocation(orbital, name), value);
            };
            auto vector = [orbital](const char* name, float x, float y, float z) {
                glUniform3f(glGetUniformLocation(orbital, name), x, y, z);
            };
            integer("uN", 1); integer("uL", 0); integer("uM", 0);
            scalar("uAspect", 1); scalar("uTanHalfFov", 0.41421356237f);
            scalar("uDensityScale", 1);
            vector("uCamFwd", 0,0,-1); vector("uCamRight", 1,0,0); vector("uCamUp", 0,1,0);
            auto render = [&](float x, float distance, float charge, int steps) {
                scalar("uZ", charge); scalar("uRadius", 15 / charge);
                scalar("uLengthScale", 1 / charge); integer("uSteps", steps);
                vector("uCamPos", x / charge, 0, distance / charge);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                std::array<unsigned char, 4> pixel{};
                glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
                if (glGetError() != GL_NO_ERROR) throw std::runtime_error("Orbital render GL error");
                return pixel;
            };
            auto near = render(0, 20, 1, 512);
            auto far = render(0, 40, 1, 512);
            auto charged = render(0, 20, 79, 512);
            auto fine = render(0, 20, 1, 1024);
            auto tail = render(4, 20, 1, 512);
            auto miss = render(16, 20, 1, 512);
            auto inside = render(0, 0, 1, 512);
            for (int c = 0; c < 3; ++c) {
                if (std::abs(int(near[c]) - int(far[c])) > 2 ||
                    std::abs(int(near[c]) - int(charged[c])) > 2 ||
                    std::abs(int(near[c]) - int(fine[c])) > 3)
                    throw std::runtime_error("Orbital render distance/charge/step consistency failed");
            }
            if (near[0] <= tail[0] + 20 || inside[0] <= miss[0] + 20 ||
                std::abs(int(miss[0]) - 5) > 1 || std::abs(int(miss[2]) - 13) > 1)
                throw std::runtime_error("Orbital render bounds/density/inside-camera failed");
            checkOrbitalImages(orbital, benchmark);
            glDeleteBuffers(1, &buffer); glDeleteVertexArrays(1, &vao); glDeleteProgram(orbital);
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
