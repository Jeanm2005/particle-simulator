#include "ComputeBackend.hpp"
namespace qm {
bool cudaAvailable() { return false; }
bool cudaSample(const RadialSampler&, const AngularSampler&, std::size_t,
                std::uint64_t, Cloud&) { return false; }
bool cudaAdvance(Cloud&, int, double) { return false; }
}
