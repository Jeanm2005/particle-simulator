#pragma once

#include <cmath>
#include "Constants.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace qm {

struct Camera {
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    float radius    = 30.0f;
    float azimuth   = 0.6f;
    float elevation = 1.0f;
    float orbitSpeed = 0.005f;
    float zoomSpeed  = 2.0f;

    bool dragging = false;
    bool panning  = false;
    double lastX = 0.0, lastY = 0.0;

    glm::vec3 position() const {
        const float el = std::clamp(elevation, 0.05f, static_cast<float>(qm::PI) - 0.05f);
        return {
            radius * std::sin(el) * std::cos(azimuth),
            radius * std::cos(el),
            radius * std::sin(el) * std::sin(azimuth)
        };
    }

    glm::mat4 viewMatrix() const {
        return glm::lookAt(position(), target, {0.0f, 1.0f, 0.0f});
    }

    glm::mat4 projMatrix(float aspect) const {
        return glm::perspective(glm::radians(45.0f), aspect, 0.1f, 500.0f);
    }

    void onMouseMove(double x, double y) {
        const float dx = static_cast<float>(x - lastX);
        const float dy = static_cast<float>(y - lastY);
        if (dragging) {
            azimuth   += dx * orbitSpeed;
            elevation -= dy * orbitSpeed;
            elevation  = std::clamp(elevation, 0.05f, static_cast<float>(qm::PI) - 0.05f);
        }
        lastX = x;
        lastY = y;
    }

    void onScroll(double yoffset) {
        radius -= static_cast<float>(yoffset) * zoomSpeed;
        if (radius < 1.0f) radius = 1.0f;
        if (radius > 200.0f) radius = 200.0f;
    }
};

} // namespace qm