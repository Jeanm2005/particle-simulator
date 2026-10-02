#pragma once

#include <GL/glew.h>

namespace qm {
// Requires a current OpenGL context and initialized function pointers.
// Throws with the driver log on compile/link failure and releases partial objects.
GLuint createShaderProgram(const char* vertexSource, const char* fragmentSource);
} // namespace qm
