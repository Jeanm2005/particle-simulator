#pragma once
#include "RadialSampler.hpp"
#include <array>
#include <cstdint>
#include <vector>
namespace qm {
using Cloud = std::vector<std::array<double, 3>>;
// False means no usable CUDA device or a CUDA error; output is unchanged.
bool cudaAvailable();
bool cudaSample(const RadialSampler&, const AngularSampler&, std::size_t count,
                std::uint64_t seed, Cloud& output);
bool cudaAdvance(Cloud& points, int m, double dt);
}
