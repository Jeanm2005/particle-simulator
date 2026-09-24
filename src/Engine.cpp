#include "Engine.hpp"
#include "Orbital.hpp"
#include "Constants.hpp"

#include <iostream>
#include <sstream>
#include <cmath>
#include <algorithm>

namespace qm {

static glm::vec3 heatmap(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const glm::vec3 stops[] = {
        {0.00f, 0.00f, 0.00f},
        {0.40f, 0.00f, 0.70f},
        {0.90f, 0.05f, 0.05f},
        {1.00f, 0.45f, 0.00f},
        {1.00f, 0.95f, 0.15f},
        {1.00f, 1.00f, 1.00f}
    };
    constexpr int N = 6;
    float scaled = t * (N - 1);
    int i = static_cast<int>(scaled);
    int j = std::min(i + 1, N - 1);
    float f = scaled - i;
    return stops[i] + f * (stops[j] - stops[i]);
}

static float intensityAt(const glm::vec3& p, int n, int l, int m, double Z) {
    double r = glm::length(p);
    if (r < 1e-8) return 0.0f;
    double theta = std::acos(std::clamp(static_cast<double>(p.y / r), -1.0, 1.0));
    double R = radialWavefunction(n, l, r, Z);
    double ang = angularPdf(l, m, theta);
    return static_cast<float>(R * R * ang);
}

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, 1024, nullptr, log);
        std::cerr << "Shader error: " << log << "\n";
    }
    return s;
}

static GLuint linkProgram(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

Engine::Engine(int width, int height)
    : width_(width), height_(height)
{
    if (!glfwInit()) { std::cerr << "GLFW init failed\n"; std::exit(1); }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window_ = glfwCreateWindow(width_, height_, "Quantum Atom Visualiser", nullptr, nullptr);
    if (!window_) { std::cerr << "Window failed\n"; glfwTerminate(); std::exit(1); }
    glfwMakeContextCurrent(window_);
    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, keyCallback);
    glfwSetMouseButtonCallback(window_, mouseButtonCallback);
    glfwSetCursorPosCallback(window_, cursorPosCallback);
    glfwSetScrollCallback(window_, scrollCallback);
    glfwSetFramebufferSizeCallback(window_, framebufferSizeCallback);

    if (glewInit() != GLEW_OK) { std::cerr << "GLEW failed\n"; std::exit(1); }

    initGL();
    createPointShaders();
    createRaytraceShaders();
    lastTime_ = glfwGetTime();
}

Engine::~Engine() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (pointShader_) glDeleteProgram(pointShader_);
    if (rtVbo_) glDeleteBuffers(1, &rtVbo_);
    if (rtVao_) glDeleteVertexArrays(1, &rtVao_);
    if (rtShader_) glDeleteProgram(rtShader_);
    if (window_) glfwDestroyWindow(window_);
    glfwTerminate();
}

void Engine::initGL() {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.02f, 0.02f, 0.05f, 1.0f);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);

    float quad[] = {
        -1.f, -1.f,  1.f, -1.f,  1.f, 1.f,
        -1.f, -1.f,  1.f,  1.f, -1.f, 1.f
    };
    glGenVertexArrays(1, &rtVao_);
    glGenBuffers(1, &rtVbo_);
    glBindVertexArray(rtVao_);
    glBindBuffer(GL_ARRAY_BUFFER, rtVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

void Engine::createPointShaders() {
    const char* vs = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aColor;
uniform mat4 uView;
uniform mat4 uProj;
uniform float uPointSize;
out vec3 vColor;
void main() {
    gl_Position = uProj * uView * vec4(aPos, 1.0);
    gl_PointSize = uPointSize;
    vColor = aColor;
})";
    const char* fs = R"(#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main() {
    vec2 c = gl_PointCoord - vec2(0.5);
    float d = length(c);
    if (d > 0.5) discard;
    float alpha = smoothstep(0.5, 0.15, d);
    FragColor = vec4(vColor, alpha * 0.9);
})";
    pointShader_ = linkProgram(compileShader(GL_VERTEX_SHADER, vs),
                               compileShader(GL_FRAGMENT_SHADER, fs));
    uView_      = glGetUniformLocation(pointShader_, "uView");
    uProj_      = glGetUniformLocation(pointShader_, "uProj");
    uPointSize_ = glGetUniformLocation(pointShader_, "uPointSize");
}

void Engine::createRaytraceShaders() {
    const char* vs = R"(#version 330 core
layout(location=0) in vec2 aPos;
out vec2 vUV;
void main() {
    vUV = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
})";

    const char* fs = R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform vec3 uCamPos;
uniform vec3 uCamFwd;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform int  uN;
uniform int  uL;
uniform int  uM;
uniform float uZ;
uniform float uAspect;

float laguerre(int k, int alpha, float x) {
    if (k <= 0) return 1.0;
    float Lm2 = 1.0;
    float Lm1 = 1.0 + float(alpha) - x;
    if (k == 1) return Lm1;
    float L = 0.0;
    for (int j = 2; j <= k; ++j) {
        L = ((2.0*float(j) - 1.0 + float(alpha) - x) * Lm1
            - (float(j) - 1.0 + float(alpha)) * Lm2) / float(j);
        Lm2 = Lm1;
        Lm1 = L;
    }
    return L;
}

float associatedLegendre(int l, int m, float x) {
    m = abs(m);
    if (m > l) return 0.0;
    float pmm = 1.0;
    if (m > 0) {
        float somx2 = sqrt(max(0.0, (1.0 - x)*(1.0 + x)));
        float fact = 1.0;
        for (int i = 1; i <= m; ++i) {
            pmm *= -fact * somx2;
            fact += 2.0;
        }
    }
    if (l == m) return pmm;
    float pmmp1 = x * float(2*m + 1) * pmm;
    if (l == m + 1) return pmmp1;
    float pll = 0.0;
    for (int ll = m + 2; ll <= l; ++ll) {
        pll = (float(2*ll - 1)*x*pmmp1 - float(ll + m - 1)*pmm) / float(ll - m);
        pmm = pmmp1;
        pmmp1 = pll;
    }
    return pll;
}

float density(vec3 p) {
    float r = length(p);
    if (r < 1e-4) return 0.0;
    float rho = 2.0 * uZ * r / float(uN);
    int k = uN - uL - 1;
    int alpha = 2 * uL + 1;
    float L = laguerre(k, alpha, rho);
    float R = exp(-rho * 0.5) * pow(rho, float(uL)) * L;
    float radial = R * R;

    float ct = clamp(p.y / r, -1.0, 1.0);
    float Plm = associatedLegendre(uL, uM, ct);
    float angular = Plm * Plm;

    return radial * angular;
}

void main() {
    vec2 ndc = vUV * 2.0 - 1.0;
    ndc.x *= uAspect;
    vec3 rayDir = normalize(uCamFwd + ndc.x * uCamRight + ndc.y * uCamUp);

    float tMin = 0.0;
    float tMax = length(uCamPos) + 40.0;
    const int STEPS = 96;
    float dt = (tMax - tMin) / float(STEPS);

    vec3 col = vec3(0.02, 0.02, 0.05);
    float transmittance = 1.0;
    float maxD = 0.0;

    for (int i = 0; i < STEPS; ++i) {
        float t = tMin + (float(i) + 0.5) * dt;
        vec3 p = uCamPos + rayDir * t;
        maxD = max(maxD, density(p));
    }
    float invMax = (maxD > 1e-8) ? (1.0 / maxD) : 1.0;

    for (int i = 0; i < STEPS; ++i) {
        float t = tMin + (float(i) + 0.5) * dt;
        vec3 p = uCamPos + rayDir * t;
        float d = density(p) * invMax;
        if (d < 0.01) continue;

        float v = clamp(d * 1.5, 0.0, 1.0);
        vec3 c;
        if (v < 0.25)      c = mix(vec3(0.0), vec3(0.4,0.0,0.7), v/0.25);
        else if (v < 0.5)  c = mix(vec3(0.4,0.0,0.7), vec3(0.9,0.05,0.05), (v-0.25)/0.25);
        else if (v < 0.75) c = mix(vec3(0.9,0.05,0.05), vec3(1.0,0.45,0.0), (v-0.5)/0.25);
        else               c = mix(vec3(1.0,0.45,0.0), vec3(1.0,1.0,0.9), (v-0.75)/0.25);

        float alpha = d * 0.15;
        col += transmittance * alpha * c;
        transmittance *= (1.0 - alpha);
        if (transmittance < 0.02) break;
    }

    FragColor = vec4(col, 1.0);
})";

    rtShader_ = linkProgram(compileShader(GL_VERTEX_SHADER, vs),
                            compileShader(GL_FRAGMENT_SHADER, fs));
    rtCamPos_   = glGetUniformLocation(rtShader_, "uCamPos");
    rtCamFwd_   = glGetUniformLocation(rtShader_, "uCamFwd");
    rtCamRight_ = glGetUniformLocation(rtShader_, "uCamRight");
    rtCamUp_    = glGetUniformLocation(rtShader_, "uCamUp");
    rtN_        = glGetUniformLocation(rtShader_, "uN");
    rtL_        = glGetUniformLocation(rtShader_, "uL");
    rtM_        = glGetUniformLocation(rtShader_, "uM");
    rtZ_        = glGetUniformLocation(rtShader_, "uZ");
    rtAspect_   = glGetUniformLocation(rtShader_, "uAspect");
}

void Engine::generateParticles() {
    Zeff_ = useSlater_ ? slaterZeff(element_.Z, n_, l_)
                       : static_cast<double>(element_.Z);

    radial_.rebuild(n_, l_, Zeff_);
    angular_.rebuild(l_, m_);

    particles_.clear();
    particles_.reserve(particleCount_);

    std::uniform_real_distribution<double> phiDist(0.0, 2.0 * PI);
    float maxI = 0.0f;
    std::vector<float> intensities;
    intensities.reserve(particleCount_);

    for (int i = 0; i < particleCount_; ++i) {
        double r     = radial_.sample(gen_);
        double theta = angular_.sample(gen_);
        double phi   = phiDist(gen_);

        glm::vec3 pos{
            static_cast<float>(r * std::sin(theta) * std::cos(phi)),
            static_cast<float>(r * std::cos(theta)),
            static_cast<float>(r * std::sin(theta) * std::sin(phi))
        };

        double vx, vy, vz;
        probabilityCurrentVelocity(pos.x, pos.y, pos.z, m_, vx, vy, vz);

        float I = intensityAt(pos, n_, l_, m_, Zeff_);
        maxI = std::max(maxI, I);
        intensities.push_back(I);
        particles_.push_back({pos, glm::vec3(vx, vy, vz), {}});
    }

    const float scale = (maxI > 1e-12f) ? (1.0f / maxI) : 1.0f;
    for (size_t i = 0; i < particles_.size(); ++i) {
        float t = std::clamp(intensities[i] * scale * 1.4f, 0.0f, 1.0f);
        particles_[i].color = heatmap(t);
    }

    camera_.radius = static_cast<float>(5.0 * n_ * n_ / std::max(Zeff_, 0.5));
    if (camera_.radius < 5.0f) camera_.radius = 5.0f;

    needsResample_ = false;
    uploadParticles();
    updateTitle();
}

void Engine::uploadParticles() {
    std::vector<float> data;
    data.reserve(particles_.size() * 6);
    for (const auto& p : particles_) {
        data.push_back(p.pos.x); data.push_back(p.pos.y); data.push_back(p.pos.z);
        data.push_back(p.color.r); data.push_back(p.color.g); data.push_back(p.color.b);
    }
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_DYNAMIC_DRAW);
}

void Engine::updateParticles(float dt) {
    if (!flowEnabled_ || m_ == 0) return;

    const float speed = flowSpeed_ * 0.4f;
    for (auto& p : particles_) {
        double vx, vy, vz;
        probabilityCurrentVelocity(p.pos.x, p.pos.y, p.pos.z, m_, vx, vy, vz);
        p.vel = glm::vec3(vx, vy, vz);
        p.pos += p.vel * (speed * dt);

        float r = glm::length(p.pos);
        float rTarget = static_cast<float>(n_ * n_ / std::max(Zeff_, 0.5));
        if (r > 1e-4f && r > rTarget * 3.0f) {
            p.pos *= (rTarget * 2.5f) / r;
        }
    }
    uploadParticles();
}

void Engine::drawPointCloud() {
    glUseProgram(pointShader_);
    float aspect = static_cast<float>(width_) / std::max(height_, 1);
    glm::mat4 view = camera_.viewMatrix();
    glm::mat4 proj = camera_.projMatrix(aspect);
    glUniformMatrix4fv(uView_, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(uProj_, 1, GL_FALSE, glm::value_ptr(proj));
    glUniform1f(uPointSize_, pointSize_);
    glBindVertexArray(vao_);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(particles_.size()));
    glBindVertexArray(0);
}

void Engine::drawRaytrace() {
    glDisable(GL_DEPTH_TEST);
    glUseProgram(rtShader_);

    glm::vec3 pos = camera_.position();
    glm::vec3 fwd = glm::normalize(camera_.target - pos);
    glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    glm::vec3 up = glm::cross(right, fwd);
    float aspect = static_cast<float>(width_) / std::max(height_, 1);

    glUniform3fv(rtCamPos_, 1, glm::value_ptr(pos));
    glUniform3fv(rtCamFwd_, 1, glm::value_ptr(fwd));
    glUniform3fv(rtCamRight_, 1, glm::value_ptr(right));
    glUniform3fv(rtCamUp_, 1, glm::value_ptr(up));
    glUniform1i(rtN_, n_);
    glUniform1i(rtL_, l_);
    glUniform1i(rtM_, m_);
    glUniform1f(rtZ_, static_cast<float>(Zeff_));
    glUniform1f(rtAspect_, aspect);

    glBindVertexArray(rtVao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

void Engine::drawFrame() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (mode_ == RenderMode::Raytrace)
        drawRaytrace();
    else
        drawPointCloud();
}

void Engine::updateTitle() {
    std::ostringstream ss;
    ss << "Quantum Atom | " << element_.symbol
       << " n=" << n_ << " l=" << l_ << " m=" << m_
       << " Zeff=" << Zeff_
       << " E=" << hydrogenicEnergy(n_, Zeff_) << " eV"
       << " | " << (mode_ == RenderMode::Raytrace ? "RAYTRACE" : "POINTS")
       << (flowEnabled_ ? " +FLOW" : "")
       << " | N/B n  L/K l  M/J m  P flow  T mode  R resample";
    glfwSetWindowTitle(window_, ss.str().c_str());
}

void Engine::processInput(float) {
    if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window_, true);
}

void Engine::keyCallback(GLFWwindow* w, int key, int, int action, int) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    auto* self = static_cast<Engine*>(glfwGetWindowUserPointer(w));
    if (!self) return;

    bool changed = false;
    switch (key) {
        case GLFW_KEY_N:
            if (self->n_ < 7) { ++self->n_; changed = true; }
            if (self->l_ >= self->n_) self->l_ = self->n_ - 1;
            if (std::abs(self->m_) > self->l_) self->m_ = 0;
            break;
        case GLFW_KEY_B:
            if (self->n_ > 1) { --self->n_; changed = true; }
            if (self->l_ >= self->n_) self->l_ = self->n_ - 1;
            if (std::abs(self->m_) > self->l_) self->m_ = 0;
            break;
        case GLFW_KEY_L:
            if (self->l_ < self->n_ - 1) { ++self->l_; changed = true; }
            if (std::abs(self->m_) > self->l_) self->m_ = 0;
            break;
        case GLFW_KEY_K:
            if (self->l_ > 0) { --self->l_; changed = true; }
            if (std::abs(self->m_) > self->l_) self->m_ = 0;
            break;
        case GLFW_KEY_M:
            if (self->m_ < self->l_) { ++self->m_; changed = true; }
            break;
        case GLFW_KEY_J:
            if (self->m_ > -self->l_) { --self->m_; changed = true; }
            break;
        case GLFW_KEY_EQUAL:
        case GLFW_KEY_KP_ADD:
            self->particleCount_ = std::min(300000, self->particleCount_ + 20000);
            changed = true;
            break;
        case GLFW_KEY_MINUS:
        case GLFW_KEY_KP_SUBTRACT:
            self->particleCount_ = std::max(5000, self->particleCount_ - 20000);
            changed = true;
            break;
        case GLFW_KEY_LEFT_BRACKET:
            self->pointSize_ = std::max(1.0f, self->pointSize_ - 0.5f);
            break;
        case GLFW_KEY_RIGHT_BRACKET:
            self->pointSize_ = std::min(12.0f, self->pointSize_ + 0.5f);
            break;
        case GLFW_KEY_R:
            changed = true;
            break;
        case GLFW_KEY_P:
            self->flowEnabled_ = !self->flowEnabled_;
            self->updateTitle();
            break;
        case GLFW_KEY_T:
            self->mode_ = (self->mode_ == RenderMode::PointCloud)
                              ? RenderMode::Raytrace
                              : RenderMode::PointCloud;
            self->updateTitle();
            break;
        case GLFW_KEY_COMMA:
            self->flowSpeed_ = std::max(0.1f, self->flowSpeed_ - 0.2f);
            break;
        case GLFW_KEY_PERIOD:
            self->flowSpeed_ = std::min(5.0f, self->flowSpeed_ + 0.2f);
            break;
        default:
            break;
    }
    if (changed) self->needsResample_ = true;
}

void Engine::mouseButtonCallback(GLFWwindow* w, int button, int action, int) {
    auto* self = static_cast<Engine*>(glfwGetWindowUserPointer(w));
    if (!self) return;
    if (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS) {
            self->camera_.dragging = true;
            glfwGetCursorPos(w, &self->camera_.lastX, &self->camera_.lastY);
        } else if (action == GLFW_RELEASE) {
            self->camera_.dragging = false;
        }
    }
}

void Engine::cursorPosCallback(GLFWwindow* w, double x, double y) {
    auto* self = static_cast<Engine*>(glfwGetWindowUserPointer(w));
    if (self) self->camera_.onMouseMove(x, y);
}

void Engine::scrollCallback(GLFWwindow* w, double, double yoff) {
    auto* self = static_cast<Engine*>(glfwGetWindowUserPointer(w));
    if (self) self->camera_.onScroll(yoff);
}

void Engine::framebufferSizeCallback(GLFWwindow* w, int width, int height) {
    auto* self = static_cast<Engine*>(glfwGetWindowUserPointer(w));
    if (!self) return;
    self->width_ = width;
    self->height_ = height;
    glViewport(0, 0, width, height);
}

void Engine::run(const Element& element, int n, int l, int m, bool useSlater) {
    element_ = element;
    n_ = n; l_ = l; m_ = m;
    useSlater_ = useSlater;
    needsResample_ = true;

    std::cout << "\n=== Quantum Atom Visualiser ===\n"
              << "  Mouse drag     orbit\n"
              << "  Scroll         zoom\n"
              << "  N/B  L/K  M/J  quantum numbers\n"
              << "  P              toggle probability-current flow\n"
              << "  T              toggle POINTS / RAYTRACE\n"
              << "  , / .          flow speed down / up\n"
              << "  +/-            particle count\n"
              << "  R              resample\n"
              << "  Esc            quit\n\n";

    while (!glfwWindowShouldClose(window_)) {
        double now = glfwGetTime();
        float dt = static_cast<float>(now - lastTime_);
        lastTime_ = now;

        processInput(dt);
        if (needsResample_)
            generateParticles();
        if (mode_ == RenderMode::PointCloud)
            updateParticles(dt);
        drawFrame();
        glfwSwapBuffers(window_);
        glfwPollEvents();
    }
}

} // namespace qm