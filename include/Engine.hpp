#pragma once

#include "Camera.hpp"
#include "Element.hpp"
#include "RadialSampler.hpp"
#include "QuantumMath.hpp"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <vector>
#include <string>
#include <random>

namespace qm {

struct Particle {
    glm::vec3 pos;
    glm::vec3 vel;
    glm::vec3 color;
};

enum class RenderMode {
    PointCloud = 0,
    Raytrace   = 1
};

class Engine {
public:
    Engine(int width = 1280, int height = 720);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void run(const Element& element, int n, int l, int m, bool useSlater);

private:
    void releaseGL() noexcept;
    void initGL();
    void createPointShaders();
    void createRaytraceShaders();
    void generateParticles();
    void uploadParticles();
    void updateParticles(float dt);
    void drawPointCloud();
    void drawRaytrace();
    void drawFrame();
    void processInput(float dt);
    void updateTitle();

    static void keyCallback(GLFWwindow* w, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* w, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* w, double x, double y);
    static void scrollCallback(GLFWwindow* w, double xoff, double yoff);
    static void framebufferSizeCallback(GLFWwindow* w, int width, int height);

    GLFWwindow* window_ = nullptr;
    int width_, height_;

    GLuint vao_ = 0, vbo_ = 0;
    GLuint pointShader_ = 0;
    GLint  uView_ = -1, uProj_ = -1, uPointSize_ = -1;

    GLuint rtVao_ = 0, rtVbo_ = 0;
    GLuint rtShader_ = 0;
    GLint  rtCamPos_ = -1, rtCamFwd_ = -1, rtCamRight_ = -1, rtCamUp_ = -1;
    GLint  rtN_ = -1, rtL_ = -1, rtM_ = -1, rtZ_ = -1, rtAspect_ = -1;

    Camera camera_;
    std::vector<Particle> particles_;
    int particleCount_ = 80000;

    Element element_;
    int n_ = 2, l_ = 1, m_ = 0;
    bool useSlater_ = false;
    double Zeff_ = 1.0;

    RadialSampler  radial_;
    AngularSampler angular_;
    std::mt19937   gen_{42};

    bool needsResample_ = true;
    bool flowEnabled_   = false;
    float flowSpeed_    = 1.0f;
    float pointSize_    = 2.0f;
    RenderMode mode_    = RenderMode::PointCloud;

    double lastTime_ = 0.0;
};

} // namespace qm