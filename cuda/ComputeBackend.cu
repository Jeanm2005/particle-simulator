#include "ComputeBackend.hpp"
#include "Constants.hpp"
#include <cuda_runtime.h>
#include <cmath>
#include <limits>
#include <mutex>
#include <stdexcept>
namespace qm {
namespace {
static_assert(sizeof(std::array<double, 3>) == 3 * sizeof(double), "Cloud points must be contiguous triples");
void check(cudaError_t status) {
    if (status != cudaSuccess) throw std::runtime_error(cudaGetErrorString(status));
}
// Grow-only device allocation reused across calls; avoids cudaMalloc/cudaFree per frame.
template<class T> struct Buffer {
    T* data = nullptr;
    std::size_t capacity = 0;
    Buffer() = default;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    ~Buffer() { if (data) cudaFree(data); }
    void reserve(std::size_t n) {
        if (n <= capacity) return;
        if (data) { cudaFree(data); data = nullptr; capacity = 0; }
        check(cudaMalloc(reinterpret_cast<void**>(&data), n * sizeof(T)));
        capacity = n;
    }
};
// Persistent device state. One mutex serializes use of the shared buffers.
struct DeviceState {
    std::mutex lock;
    Buffer<double> radialGrid, radialCdf, angularGrid, angularCdf;
    Buffer<double> input, output;
    Buffer<int> flag;
};
DeviceState& state() { static DeviceState device; return device; }

// SplitMix64 counter-based random stream: independent of launch scheduling.
__device__ double uniform(unsigned long long& s) {
    unsigned long long z = (s += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    z ^= z >> 31;
    return static_cast<double>(z >> 11) * 0x1.0p-53;
}
__device__ double inverse(const double* grid, const double* cdf, std::size_t n, double u) {
    std::size_t lo = 0, hi = n;
    while (lo < hi) {
        const auto mid = lo + (hi - lo) / 2;
        if (cdf[mid] < u) lo = mid + 1; else hi = mid;
    }
    return grid[lo < n ? lo : n - 1];
}
__global__ void sampleKernel(double* out, std::size_t count, const double* rg,
        const double* rc, std::size_t rn, const double* ag, const double* ac,
        std::size_t an, unsigned long long seed, double pi, int* nonfinite) {
    const std::size_t i = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (i >= count) return;
    unsigned long long s = seed + 3ULL * i * 0x9e3779b97f4a7c15ULL;
    const double r = inverse(rg, rc, rn, uniform(s));
    const double theta = inverse(ag, ac, an, uniform(s));
    const double phi = 2 * pi * uniform(s);
    const double x = r * sin(theta) * cos(phi), y = r * cos(theta), z = r * sin(theta) * sin(phi);
    if (!isfinite(x) || !isfinite(y) || !isfinite(z)) atomicExch(nonfinite, 1);
    out[3*i] = x; out[3*i+1] = y; out[3*i+2] = z;
}
// flag bit 1: invalid input (matches host preflight); bit 2: nonfinite output.
__global__ void flowKernel(const double* in, double* out, std::size_t count, int m, double dt, int* flag) {
    const std::size_t i = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (i >= count) return;
    const double x = in[3*i], y = in[3*i+1], z = in[3*i+2];
    const double rho2 = x*x + z*z;
    double nx = x, nz = z;
    if (!isfinite(x) || !isfinite(y) || !isfinite(z) || !isfinite(rho2)) { atomicOr(flag, 1); }
    else if (m != 0 && rho2 >= 1e-16 && dt != 0) {
        const double angle = static_cast<double>(m) * dt / rho2;
        if (!isfinite(angle)) atomicOr(flag, 1);
        else { nx = x*cos(angle) - z*sin(angle); nz = x*sin(angle) + z*cos(angle); }
    }
    if (!isfinite(nx) || !isfinite(nz)) atomicOr(flag, 2);
    out[3*i] = nx; out[3*i+1] = y; out[3*i+2] = nz;
}
void upload(Buffer<double>& buffer, const std::vector<double>& values) {
    buffer.reserve(values.size());
    check(cudaMemcpy(buffer.data, values.data(), values.size()*sizeof(double), cudaMemcpyHostToDevice));
}
bool validCount(std::size_t n) { return n <= static_cast<std::size_t>(std::numeric_limits<int>::max()) / 3; }
unsigned blocks(std::size_t n) { return static_cast<unsigned>((n + 255) / 256); }
}
bool cudaAvailable() {
    int count = 0;
    return cudaGetDeviceCount(&count) == cudaSuccess && count > 0;
}
bool cudaSample(const RadialSampler& radial, const AngularSampler& angular,
                std::size_t count, std::uint64_t seed, Cloud& output) {
    if (!radial.ready() || !angular.ready()) throw std::logic_error("Invalid CUDA sampling distribution");
    if (!validCount(count)) throw std::invalid_argument("CUDA sample count too large");
    if (!cudaAvailable()) return false;
    if (count == 0) { output.clear(); return true; }
    auto& device = state();
    std::lock_guard<std::mutex> guard(device.lock);
    try {
        upload(device.radialGrid, radial.grid()); upload(device.radialCdf, radial.cdf());
        upload(device.angularGrid, angular.grid()); upload(device.angularCdf, angular.cdf());
        device.output.reserve(3 * count); device.flag.reserve(1);
        check(cudaMemset(device.flag.data, 0, sizeof(int)));
        sampleKernel<<<blocks(count), 256>>>(device.output.data, count, device.radialGrid.data, device.radialCdf.data,
            radial.grid().size(), device.angularGrid.data, device.angularCdf.data, angular.grid().size(), seed, PI,
            device.flag.data);
        check(cudaGetLastError());
        int nonfinite = 0;
        check(cudaMemcpy(&nonfinite, device.flag.data, sizeof(int), cudaMemcpyDeviceToHost));
        if (nonfinite) return false;
        Cloud result(count);
        check(cudaMemcpy(result.data(), device.output.data, 3 * count * sizeof(double), cudaMemcpyDeviceToHost));
        output.swap(result);
        return true;
    } catch (const std::runtime_error&) { return false; }
}
// Transactional: on false or an exception the host cloud is unchanged.
bool cudaAdvance(Cloud& points, int m, double dt) {
    if (!std::isfinite(dt) || !validCount(points.size())) throw std::invalid_argument("Invalid CUDA flow input");
    if (!cudaAvailable()) return false;
    if (points.empty()) return true;
    const std::size_t values = 3 * points.size(), bytes = values * sizeof(double);
    auto& device = state();
    std::lock_guard<std::mutex> guard(device.lock);
    int flag = 0;
    try {
        device.input.reserve(values); device.output.reserve(values); device.flag.reserve(1);
        check(cudaMemcpy(device.input.data, points.data(), bytes, cudaMemcpyHostToDevice));
        check(cudaMemset(device.flag.data, 0, sizeof(int)));
        flowKernel<<<blocks(points.size()), 256>>>(device.input.data, device.output.data, points.size(), m, dt, device.flag.data);
        check(cudaGetLastError());
        check(cudaMemcpy(&flag, device.flag.data, sizeof(int), cudaMemcpyDeviceToHost));
    } catch (const std::runtime_error&) { return false; }
    if (flag & 1) throw std::invalid_argument("Invalid CUDA flow input or angle");
    if (flag & 2) return false;
    if (cudaMemcpy(points.data(), device.output.data, bytes, cudaMemcpyDeviceToHost) == cudaSuccess) return true;
    // A failed copy may have written part of the cloud; restore the untouched input.
    if (cudaMemcpy(points.data(), device.input.data, bytes, cudaMemcpyDeviceToHost) == cudaSuccess) return false;
    throw std::runtime_error("CUDA flow download failed and the cloud could not be restored");
}
}
