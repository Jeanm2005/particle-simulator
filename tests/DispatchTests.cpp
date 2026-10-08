// A failing CUDA provider exercises runtime fallback even on CPU-only CI.
#include "ComputeBackend.hpp"
#include "QuantumMath.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
int cudaCalls = 0;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}
namespace qm {
bool cudaAvailable() { return true; }
bool cudaSample(const RadialSampler&, const AngularSampler&, std::size_t,
                std::uint64_t, Cloud&) { ++cudaCalls; return false; }
bool cudaAdvance(Cloud&, int, double) { ++cudaCalls; return false; }
}
int main() {
    try {
        qm::RadialSampler radial(1, 0, 1);
        qm::AngularSampler angular(0, 0);
        qm::Cloud cloud{{1, 2, 3}};
        std::mt19937 generator(42);
        auto execution = qm::sampleCloud(radial, angular, 100, generator, cloud);
        require(cudaCalls == 1 && execution.backend == "cpu" &&
                execution.fallbackReason == "CUDA sampling failed" && cloud.size() == 100,
                "Failed CUDA sampling did not fall back");
        auto expected = cloud;
        for (auto& p : expected) qm::advanceProbabilityCurrent(p[0], p[2], 1, 0.1);
        execution = qm::advanceCloud(cloud, 1, 0.1);
        require(cudaCalls == 2 && cloud == expected && execution.backend == "cpu" &&
                execution.fallbackReason == "CUDA current update failed", "Failed CUDA flow did not fall back");
        qm::sampleCloud(radial, angular, 10, generator, cloud, qm::BackendPreference::Cpu);
        qm::advanceCloud(cloud, 1, 0.1, qm::BackendPreference::Cpu);
        require(cudaCalls == 2, "Explicit CPU contacted CUDA");
        cloud = {{1, 2, 3}, {1e-10, 0, 0}, {2, 3, 4}};
        expected = cloud;
        for (auto& p : expected) qm::advanceProbabilityCurrent(p[0], p[2], -1, 0.1);
        qm::advanceCloud(cloud, -1, 0.1);
        require(cloud == expected, "Fallback lost polar-axis guard or negative-m flow");
        cloud = {{1, 2, 3}, {std::numeric_limits<double>::max(), 0, 1}};
        expected = cloud;
        const int previousCalls = cudaCalls;
        try { qm::advanceCloud(cloud, 1, 0.1); throw std::runtime_error("Expected oversized-radius rejection"); }
        catch (const std::invalid_argument&) {}
        require(cloud == expected && cudaCalls == previousCalls, "Flow validation occurred after execution");
        std::cout << "Forced CUDA runtime failure and transactional flow regressions passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
