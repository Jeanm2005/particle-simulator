#include "PhononModel.hpp"
#include "Constants.hpp"
#include "OrbitalModel.hpp"
#include <algorithm>
#include <cmath>

namespace qm {
namespace {
// omega^2 = s -/+ sqrt(R), s = 1/m1 + 1/m2, R = s^2 - 4 sin^2(k/2) / (m1 m2)
double discriminant(double k, double m2) {
    const double s = 1.0 + 1.0 / m2, half = std::sin(0.5 * k);
    return std::max(0.0, s * s - 4.0 * half * half / m2);
}
void validateTime(double time) {
    if (!std::isfinite(time) || std::abs(time) > 1e6)
        throw ModelError("invalid-input", "dt", "Time must be finite with magnitude <= 1000000 reduced units");
}
// Amplitude scale so the largest |u| equals the requested amplitude.
double scale(const PhononRequest& request, const PhononMode& mode) {
    const double largest = std::max(std::abs(mode.polarization[0]), std::abs(mode.polarization[1]) / std::sqrt(request.massRatio));
    return request.amplitude / largest;
}
template<class F> std::vector<double> field(const PhononRequest& request, double time, F value) {
    PhononModel::validateRequest(request);
    validateTime(time);
    const int N = request.cells;
    const double k = 2.0 * PI * request.modeIndex / N;
    const auto m = PhononModel::mode(k, request.massRatio, request.branch);
    const double A = scale(request, m);
    const double weights[2] = {1.0, 1.0 / std::sqrt(request.massRatio)};
    std::vector<double> out(2 * static_cast<std::size_t>(N));
    for (int n = 0; n < N; ++n)
        for (int s = 0; s < 2; ++s) {
            const auto wave = A * weights[s] * m.polarization[s] * std::polar(1.0, k * n - m.angularFrequency * time);
            out[2 * n + s] = value(wave, m.angularFrequency);
        }
    return out;
}
}

void PhononModel::validateRequest(const PhononRequest& r) {
    if (r.contractVersion != 1)
        throw ModelError("unsupported-version", "contractVersion", "Supported contract version is 1");
    if (r.model != "lattice-phonons")
        throw ModelError("unsupported-model", "model", "Expected lattice-phonons");
    if (r.cells < minCells || r.cells > maxCells)
        throw ModelError("invalid-input", "lattice.cells", "Cells must be between 2 and 256");
    if (!std::isfinite(r.massRatio) || r.massRatio < 0.1 || r.massRatio > 10.0)
        throw ModelError("invalid-input", "lattice.massRatio", "Mass ratio m2/m1 must be between 0.1 and 10");
    if (r.branch != PhononBranch::Acoustic && r.branch != PhononBranch::Optical)
        throw ModelError("invalid-input", "lattice.branch", "Branch must be acoustic or optical");
    if (2 * r.modeIndex <= -r.cells || 2 * r.modeIndex > r.cells)
        throw ModelError("invalid-input", "lattice.modeIndex", "Mode index j must satisfy -N/2 < j <= N/2");
    if (!std::isfinite(r.amplitude) || r.amplitude <= 0.0 || r.amplitude > 0.25)
        throw ModelError("invalid-input", "lattice.amplitude", "Amplitude must be in (0, 0.25] cells");
}

double PhononModel::frequency(double k, double m2, PhononBranch branch) {
    const double s = 1.0 + 1.0 / m2, root = std::sqrt(discriminant(k, m2)), half = std::sin(0.5 * k);
    // Acoustic: s - root rewritten as (s^2 - R) / (s + root) to avoid cancellation as k -> 0.
    return std::sqrt(branch == PhononBranch::Acoustic ? 4.0 * half * half / (m2 * (s + root)) : s + root);
}

PhononMode PhononModel::mode(double k, double m2, PhononBranch branch) {
    PhononMode mode;
    mode.wavenumber = k;
    mode.angularFrequency = frequency(k, m2, branch);
    const double lambda = mode.angularFrequency * mode.angularFrequency;
    // Mass-weighted dynamical matrix D = [[2, -c], [-conj(c), 2/m2]], c = (1 + e^{-ik}) / sqrt(m2).
    const auto c = (1.0 + std::polar(1.0, -k)) / std::sqrt(m2);
    std::complex<double> e1, e2;
    if (std::abs(c) < 1e-12) {
        // Zone edge: sublattices decouple with omega^2 = 2 (atom 1) and 2/m2 (atom 2).
        // The heavier sublattice carries the lower, acoustic frequency.
        const bool atom2Lower = m2 > 1.0;
        const bool useAtom2 = branch == PhononBranch::Acoustic ? atom2Lower : !atom2Lower;
        e1 = useAtom2 ? 0.0 : 1.0;
        e2 = useAtom2 ? 1.0 : 0.0;
    } else { e1 = c; e2 = 2.0 - lambda; } // first row of (D - lambda) e = 0
    const double norm = std::sqrt(std::norm(e1) + std::norm(e2));
    mode.polarization[0] = e1 / norm;
    mode.polarization[1] = e2 / norm;
    // Group velocity d omega/dk = (d omega^2/dk) / (2 omega); one-sided differences where singular.
    const double R = discriminant(k, m2);
    if (mode.angularFrequency > 1e-9 && R > 1e-12) {
        const double derivative = std::sin(k) / (m2 * std::sqrt(R));
        mode.groupVelocity = (branch == PhononBranch::Acoustic ? derivative : -derivative) / (2.0 * mode.angularFrequency);
    } else {
        const double h = 1e-7;
        mode.groupVelocity = mode.angularFrequency <= 1e-9
            ? (frequency(k + h, m2, branch) - mode.angularFrequency) / h
            : (mode.angularFrequency - frequency(k - h, m2, branch)) / h;
    }
    return mode;
}

std::vector<double> PhononModel::displacements(const PhononRequest& request, double time) {
    return field(request, time, [](std::complex<double> wave, double) { return wave.real(); });
}
std::vector<double> PhononModel::velocities(const PhononRequest& request, double time) {
    return field(request, time, [](std::complex<double> wave, double omega) {
        return (std::complex<double>(0.0, -omega) * wave).real();
    });
}

PhononResult PhononModel::run(const PhononRequest& request, double time) const {
    validateRequest(request);
    validateTime(time);
    PhononResult result;
    result.request = request;
    result.timeReduced = time;
    const int N = request.cells;
    const double m2 = request.massRatio;
    result.mode = mode(2.0 * PI * request.modeIndex / N, m2, request.branch);
    result.soundSpeed = std::sqrt(1.0 / (2.0 * (1.0 + m2)));
    result.acousticMax = frequency(PI, m2, PhononBranch::Acoustic);
    result.opticalMin = frequency(PI, m2, PhononBranch::Optical);
    result.opticalMax = frequency(0.0, m2, PhononBranch::Optical);
    result.displacements = displacements(request, time);
    result.displayGain = 0.1 * N / request.amplitude;
    result.points.reserve(result.displacements.size());
    for (int n = 0; n < N; ++n)
        for (int s = 0; s < 2; ++s) {
            const double u = result.displacements[2 * n + s];
            result.points.push_back({n + 0.5 * s - 0.5 * N + 0.25 + u, result.displayGain * u, 0.0});
        }
    result.dispersion.reserve(dispersionSamples);
    for (int i = 0; i < dispersionSamples; ++i) {
        const double k = -PI + 2.0 * PI * i / (dispersionSamples - 1);
        result.dispersion.push_back({k, frequency(k, m2, PhononBranch::Acoustic), frequency(k, m2, PhononBranch::Optical)});
    }
    for (const auto& p : result.points)
        for (double v : p) if (!std::isfinite(v)) throw ModelError("numerical-failure", "", "Nonfinite lattice displacement");
    return result;
}

void PhononModel::advance(PhononResult& result, double dt) const {
    validateTime(dt);
    validateTime(result.timeReduced + dt);
    result = run(result.request, result.timeReduced + dt);
}
}
