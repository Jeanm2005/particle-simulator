#pragma once

#include "QuantumMath.hpp"
#include "RadialSampler.hpp"
#include "Constants.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <chrono>
#include <iostream>
#include <vector>

// Float attachments avoid hiding integration error behind 8-bit quantization.
inline void checkOrbitalImages(GLuint program, bool benchmark) {
    constexpr int width = 49, height = 33;
    GLuint texture, framebuffer;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Float render framebuffer incomplete");
    glViewport(0, 0, width, height);
    glDisable(GL_DITHER);
    auto scalar = [program](const char* name, float value) {
        glUniform1f(glGetUniformLocation(program, name), value);
    };
    auto integer = [program](const char* name, int value) {
        glUniform1i(glGetUniformLocation(program, name), value);
    };
    auto vector = [program](const char* name, float x, float y, float z) {
        glUniform3f(glGetUniformLocation(program, name), x, y, z);
    };
    scalar("uAspect", float(width) / height);
    scalar("uTanHalfFov", std::tan(float(qm::PI) / 8));
    vector("uCamFwd", 0, 0, -1); vector("uCamRight", 1, 0, 0); vector("uCamUp", 0, 1, 0);
    auto image = [&](int steps) {
        integer("uSteps", steps);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        std::vector<float> pixels(width * height * 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data());
        if (glGetError() != GL_NO_ERROR) throw std::runtime_error("Float render GL error");
        for (float value : pixels)
            if (!std::isfinite(value) || value < 0 || value > 1.001f)
                throw std::runtime_error("Nonfinite or out-of-range orbital image");
        return pixels;
    };
    auto difference = [](const std::vector<float>& a, const std::vector<float>& b) {
        double maximum = 0, squared = 0;
        for (size_t i = 0; i < a.size(); ++i) {
            if (i % 4 == 3) continue;
            double error = std::abs(a[i] - b[i]);
            maximum = std::max(maximum, error); squared += error * error;
        }
        return std::pair<double, double>{maximum, std::sqrt(squared / (a.size() / 4 * 3))};
    };
    struct State { int n, l, m; };
    const State states[] = {{1,0,0}, {2,0,0}, {2,1,0}, {3,2,1}, {4,3,3}, {6,0,0}, {7,0,0}, {7,3,0}, {7,3,3}};
    if (benchmark)
        std::cout << "Renderer: " << glGetString(GL_RENDERER) << "\n"
                  << "49x33 RGBA32F, 5 synchronized frames, milliseconds/frame; no vsync or readback in timing\n";
    for (const State state : states) {
        for (int view = 0; view < 2; ++view) {
            const float sine = view == 0 ? 0.0f : 0.6f;
            const float cosine = view == 0 ? 1.0f : 0.8f;
            vector("uCamFwd", 0, -sine, -cosine);
            vector("uCamUp", 0, cosine, -sine);
            const double length = state.n * state.n;
            qm::RadialSampler radial(state.n, state.l, 1);
            qm::AngularSampler angular(state.l, state.m);
            double radialPeak = 0, angularPeak = 0;
            for (double radius : radial.grid()) {
                double rho = 2 * radius / state.n;
                double amplitude = std::exp(-rho / 2) * std::pow(rho, state.l) *
                    qm::associatedLaguerre(state.n-state.l-1, 2*state.l+1, rho);
                radialPeak = std::max(radialPeak, amplitude * amplitude);
            }
            for (double theta : angular.grid()) {
                double amplitude = qm::associatedLegendre(state.l, state.m, std::cos(theta));
                angularPeak = std::max(angularPeak, amplitude * amplitude);
            }
            integer("uN", state.n); integer("uL", state.l); integer("uM", state.m);
            scalar("uZ", 1); scalar("uLengthScale", length);
            scalar("uRadius", radial.rMax()); scalar("uDensityScale", 1 / (radialPeak * angularPeak));
            vector("uCamPos", 0, sine * 5 * length, cosine * 5 * length);
            auto reference = image(4096);
            for (int steps : {256, 512, 1024}) {
                auto pixels = image(steps);
                auto error = difference(pixels, reference);
                std::cout << "n=" << state.n << " l=" << state.l << " m=" << state.m
                          << " view=" << view << " steps=" << steps << " max=" << error.first << " rms=" << error.second;
                if (benchmark) {
                    glFinish();
                    auto start = std::chrono::steady_clock::now();
                    for (int frame = 0; frame < 5; ++frame) {
                        glDrawArrays(GL_TRIANGLES, 0, 3); glFinish();
                    }
                    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count()/5;
                    std::cout << " ms=" << ms;
                }
                std::cout << '\n';
                if (steps == qm::raytraceSteps && (error.first > 0.005 || error.second > 0.0003))
                    throw std::runtime_error("Higher-orbital image convergence failed");
            }
            auto base = image(qm::raytraceSteps);
            scalar("uZ", 79); scalar("uLengthScale", length / 79);
            scalar("uRadius", radial.rMax() / 79); vector("uCamPos", 0, sine * 5 * length / 79, cosine * 5 * length / 79);
            auto scaled = image(qm::raytraceSteps);
            integer("uM", -state.m);
            if (difference(scaled, image(qm::raytraceSteps)).first > 0.0001)
                throw std::runtime_error("Positive/negative m density symmetry failed");
            auto error = difference(base, scaled);
            if (error.first > 0.001) throw std::runtime_error("Higher-orbital charge scaling failed");
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &framebuffer); glDeleteTextures(1, &texture);
}
