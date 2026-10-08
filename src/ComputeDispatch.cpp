#include "ComputeBackend.hpp"
#include "QuantumMath.hpp"
#include "Constants.hpp"
#include <cmath>
#include <stdexcept>

namespace qm {
namespace {
void validatePreference(BackendPreference preference) {
    if (preference != BackendPreference::Auto && preference != BackendPreference::Cpu)
        throw std::invalid_argument("Unknown compute backend preference");
}
}
ComputeExecution sampleCloud(const RadialSampler& radial, const AngularSampler& angular,
                             std::size_t count, std::mt19937& generator, Cloud& output,
                             BackendPreference preference) {
    validatePreference(preference);
    if (!radial.ready() || !angular.ready() || count == 0)
        throw std::invalid_argument("Sampling requires ready distributions and positive count");
    ComputeExecution execution;
    Cloud points;
    if (preference == BackendPreference::Auto) {
        if (!cudaAvailable()) execution.fallbackReason = "CUDA unavailable";
        else if (cudaSample(radial, angular, count, generator(), points))
            execution.backend = "cuda";
        else execution.fallbackReason = "CUDA sampling failed";
    }
    if (execution.backend == "cpu") {
        points.reserve(count);
        std::uniform_real_distribution<double> phi(0.0, 2.0 * PI);
        for (std::size_t i = 0; i < count; ++i) {
            const double r = radial.sample(generator);
            const double theta = angular.sample(generator);
            const double angle = phi(generator);
            points.push_back({r * std::sin(theta) * std::cos(angle), r * std::cos(theta),
                              r * std::sin(theta) * std::sin(angle)});
        }
    }
    if (points.size() != count) throw std::domain_error("Incorrect sampled cloud size");
    for (const auto& p : points)
        for (double coordinate : p)
            if (!std::isfinite(coordinate)) throw std::domain_error("Nonfinite sampled coordinate");
    output.swap(points);
    return execution;
}

ComputeExecution advanceCloud(Cloud& points, int m, double dt, BackendPreference preference) {
    validatePreference(preference);
    if (!std::isfinite(dt)) throw std::invalid_argument("Current time must be finite");
    // Preflight the same finite-radius/angle constraints as the CPU integrator.
    for (const auto& p : points) {
        for (double coordinate : p)
            if (!std::isfinite(coordinate)) throw std::invalid_argument("Current coordinates must be finite");
        const double radiusSquared = p[0] * p[0] + p[2] * p[2];
        if (!std::isfinite(radiusSquared)) throw std::invalid_argument("Current radius is too large");
        if (m != 0 && radiusSquared >= 1e-16 && dt != 0.0 &&
            !std::isfinite(static_cast<double>(m) * dt / radiusSquared))
            throw std::invalid_argument("Current angle is too large");
    }
    ComputeExecution execution;
    Cloud updated = points;
    if (preference == BackendPreference::Auto) {
        if (!cudaAvailable()) execution.fallbackReason = "CUDA unavailable";
        else if (cudaAdvance(updated, m, dt)) execution.backend = "cuda";
        else execution.fallbackReason = "CUDA current update failed";
    }
    if (execution.backend == "cpu")
        for (auto& p : updated) advanceProbabilityCurrent(p[0], p[2], m, dt);
    for (const auto& p : updated)
        for (double coordinate : p)
            if (!std::isfinite(coordinate)) throw std::domain_error("Nonfinite current coordinate");
    points.swap(updated);
    return execution;
}
}
