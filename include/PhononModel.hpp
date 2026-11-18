#pragma once
#include <array>
#include <complex>
#include <string>
#include <vector>

namespace qm {
// One-dimensional diatomic harmonic chain with nearest-neighbour springs and
// periodic boundaries. Reduced units: spring constant K = 1, first mass m1 = 1,
// cell length a = 1, so frequencies are in sqrt(K/m1) and time in sqrt(m1/K).
// Cell n holds atom 1 at x = n and atom 2 at x = n + 1/2. Equations of motion:
//   m1 u''(n,1) = u(n,2) + u(n-1,2) - 2 u(n,1)
//   m2 u''(n,2) = u(n+1,1) + u(n,1) - 2 u(n,2)
// Displacements are longitudinal (along the chain).
enum class PhononBranch { Acoustic, Optical };
struct PhononRequest {
    int contractVersion = 1;
    std::string model = "lattice-phonons";
    int cells = 32;
    double massRatio = 2.0;           // m2 / m1
    PhononBranch branch = PhononBranch::Acoustic;
    int modeIndex = 1;                // k = 2 pi j / N, -N/2 < j <= N/2
    double amplitude = 0.1;           // largest atomic displacement, in cells
};
struct PhononMode {
    double wavenumber = 0.0;          // k in 1/a
    double angularFrequency = 0.0;    // omega in sqrt(K/m1)
    double groupVelocity = 0.0;       // d omega / d k in a sqrt(K/m1)
    std::complex<double> polarization[2]; // mass-weighted eigenvector, unit norm
};
struct PhononResult {
    PhononRequest request;
    PhononMode mode;
    double timeReduced = 0.0;
    double soundSpeed = 0.0;          // long-wavelength acoustic speed
    double acousticMax = 0.0, opticalMin = 0.0, opticalMax = 0.0;
    double displayGain = 0.0;         // Y = displayGain * displacement in the preview
    std::vector<double> displacements; // 2N values, cell-major: u(0,1), u(0,2), u(1,1), ...
    std::vector<std::array<double, 3>> points;
    std::vector<std::array<double, 3>> dispersion; // k, omega acoustic, omega optical
};
class PhononModel {
public:
    static constexpr int minCells = 2, maxCells = 256, dispersionSamples = 129;
    static void validateRequest(const PhononRequest&);
    static double frequency(double k, double massRatio, PhononBranch);
    static PhononMode mode(double k, double massRatio, PhononBranch);
    // Exact traveling-mode displacements and velocities at reduced time t.
    static std::vector<double> displacements(const PhononRequest&, double time);
    static std::vector<double> velocities(const PhononRequest&, double time);
    PhononResult run(const PhononRequest&, double time = 0.0) const;
    void advance(PhononResult&, double dt) const;
};
}
