#include "ComputeBackend.hpp"
#include "Constants.hpp"
#include <cuda_runtime.h>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace qm {
namespace {
void check(cudaError_t status) {
    if (status != cudaSuccess) throw std::runtime_error(cudaGetErrorString(status));
}
template<class T> struct Buffer {
    T* data = nullptr;
    explicit Buffer(std::size_t n) { check(cudaMalloc(reinterpret_cast<void**>(&data), n * sizeof(T))); }
    ~Buffer() { if (data) cudaFree(data); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
};
// SplitMix64 counter-based random stream: independent of launch scheduling.
__device__ double uniform(unsigned long long& state) {
    unsigned long long z = (state += 0x9e3779b97f4a7c15ULL);
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
        std::size_t an, unsigned long long seed, double pi) {
    const std::size_t i = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (i >= count) return;
    unsigned long long state = seed + 3ULL * i * 0x9e3779b97f4a7c15ULL;
    const double r = inverse(rg, rc, rn, uniform(state));
    const double theta = inverse(ag, ac, an, uniform(state));
    const double phi = 2 * pi * uniform(state);
    out[3*i] = r * sin(theta) * cos(phi);
    out[3*i+1] = r * cos(theta);
    out[3*i+2] = r * sin(theta) * sin(phi);
}
__global__ void flowKernel(double* points, std::size_t count, int m, double dt) {
    const std::size_t i = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (i >= count) return;
    const double x = points[3*i], z = points[3*i+2];
    const double rho2 = x*x + z*z;
    if (m == 0 || rho2 < 1e-16 || dt == 0) return;
    const double angle = static_cast<double>(m) * dt / rho2;
    points[3*i] = x*cos(angle) - z*sin(angle);
    points[3*i+2] = x*sin(angle) + z*cos(angle);
}
void upload(Buffer<double>& buffer, const std::vector<double>& values) {
    check(cudaMemcpy(buffer.data, values.data(), values.size()*sizeof(double), cudaMemcpyHostToDevice));
}
bool validCount(std::size_t n) { return n <= static_cast<std::size_t>(std::numeric_limits<int>::max()); }
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
    try {
        Buffer<double> rg(radial.grid().size()), rc(radial.cdf().size());
        Buffer<double> ag(angular.grid().size()), ac(angular.cdf().size()), points(3*count);
        upload(rg, radial.grid()); upload(rc, radial.cdf());
        upload(ag, angular.grid()); upload(ac, angular.cdf());
        sampleKernel<<<static_cast<unsigned>((count+255)/256),256>>>(points.data, count,
            rg.data, rc.data, radial.grid().size(), ag.data, ac.data, angular.grid().size(), seed, PI);
        check(cudaGetLastError());
        std::vector<double> flat(3*count);
        check(cudaMemcpy(flat.data(), points.data, flat.size()*sizeof(double), cudaMemcpyDeviceToHost));
        Cloud result(count);
        for (std::size_t i=0; i<count; ++i) {
            result[i] = {flat[3*i],flat[3*i+1],flat[3*i+2]};
            for (double value : result[i]) if (!std::isfinite(value)) return false;
        }
        output.swap(result);
        return true;
    } catch (const std::runtime_error&) { return false; }
}
bool cudaAdvance(Cloud& points, int m, double dt) {
    if (!std::isfinite(dt) || !validCount(points.size())) throw std::invalid_argument("Invalid CUDA flow input");
    std::vector<double> flat;
    flat.reserve(3*points.size());
    for (const auto& p : points) {
        for (double v : p) if (!std::isfinite(v)) throw std::invalid_argument("Nonfinite CUDA point");
        const double rho2 = p[0]*p[0]+p[2]*p[2];
        if (!std::isfinite(rho2) || (m != 0 && rho2 >= 1e-16 && !std::isfinite(m*dt/rho2)))
            throw std::invalid_argument("Invalid CUDA flow angle");
        flat.insert(flat.end(), p.begin(), p.end());
    }
    if (!cudaAvailable()) return false;
    if (points.empty()) return true;
    try {
        Buffer<double> device(flat.size()); upload(device,flat);
        flowKernel<<<static_cast<unsigned>((points.size()+255)/256),256>>>(device.data,points.size(),m,dt);
        check(cudaGetLastError());
        check(cudaMemcpy(flat.data(),device.data,flat.size()*sizeof(double),cudaMemcpyDeviceToHost));
        for (double value : flat) if (!std::isfinite(value)) return false;
        for (std::size_t i=0;i<points.size();++i) points[i]={flat[3*i],flat[3*i+1],flat[3*i+2]};
        return true;
    } catch (const std::runtime_error&) { return false; }
}
}
