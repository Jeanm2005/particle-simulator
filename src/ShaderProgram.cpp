#include "ShaderProgram.hpp"

#include <stdexcept>
#include <string>

namespace qm {

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetShaderInfoLog(s, 1024, nullptr, log);
        glDeleteShader(s);
        throw std::runtime_error(std::string("Shader compilation failed: ") + log);
    }
    return s;
}

GLuint createShaderProgram(const char* vertexSource, const char* fragmentSource) {
    GLuint vertex = 0, fragment = 0, program = 0;
    try {
        vertex = compileShader(GL_VERTEX_SHADER, vertexSource);
        fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
        program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        GLint ok = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024] = {};
            glGetProgramInfoLog(program, 1024, nullptr, log);
            throw std::runtime_error(std::string("Shader linking failed: ") + log);
        }
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        return program;
    } catch (...) {
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        if (program) glDeleteProgram(program);
        throw;
    }
}

} // namespace qm
