#pragma once
#include "RadialSampler.hpp"
#include <array>
#include <cstdint>
#include <vector>
#include <random>
#include <string>
namespace qm {
using Cloud = std::vector<std::array<double, 3>>;
enum class BackendPreference { Auto, Cpu };
struct ComputeExecution {
    std::string backend = "cpu";
    std::string fallbackReason;
};
ComputeExecution sampleCloud(const RadialSampler&, const AngularSampler&,
                             std::size_t count, std::mt19937&, Cloud&,
                             BackendPreference = BackendPreference::Auto);
// Validates before publishing updates; errors leave the cloud unchanged.
ComputeExecution advanceCloud(Cloud&, int m, double dt,
                              BackendPreference = BackendPreference::Auto);
// False means no usable CUDA device or a CUDA error; output is unchanged.
bool cudaAvailable();
bool cudaSample(const RadialSampler&, const AngularSampler&, std::size_t count,
                std::uint64_t seed, Cloud& output);
bool cudaAdvance(Cloud& points, int m, double dt);
}
