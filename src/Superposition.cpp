#include "Superposition.hpp"
#include "QuantumMath.hpp"
#include "Constants.hpp"
#include <algorithm>
#include <cmath>
#include <new>
#include <random>

namespace qm {
namespace {
void validateTime(double time, int Z) {
    if (!std::isfinite(time) || std::abs(time) * static_cast<double>(Z) * Z > 1e6)
        throw ModelError("invalid-input", "dt", "Time must be finite with |time| Z^2 <= 1000000 atomic units");
}
std::complex<double> evolveCoefficient(double magnitude, double phase, int n, double Z, double time) {
    const double energyHartree = -0.5 * Z * Z / (n * static_cast<double>(n));
    return std::polar(magnitude, std::remainder(phase - energyHartree * time, 2.0 * PI));
}
}
std::complex<double> orbitalAmplitude(QuantumNumbers q, double Z, const std::array<double, 3>& point) {
    if (q.n < 1 || q.n > 7 || q.l < 0 || q.l >= q.n || q.l > 3 || q.m < -q.l || q.m > q.l || !std::isfinite(Z) || Z <= 0)
        throw std::invalid_argument("Invalid complex orbital state");
    for (double v : point) if (!std::isfinite(v)) throw std::invalid_argument("Nonfinite orbital coordinate");
    const double r = std::hypot(std::hypot(point[0], point[1]), point[2]);
    if (!std::isfinite(r)) throw std::invalid_argument("Orbital radius is too large");
    if (r == 0 && q.l != 0) return {0, 0};
    const double cosine = r == 0 ? 1.0 : std::clamp(point[1] / r, -1.0, 1.0);
    const double phi = std::atan2(point[2], point[0]);
    const int absM = std::abs(q.m);
    const double amplitude = radialWavefunction(q.n, q.l, r, Z) *
        sphericalHarmonicNorm(q.l, absM) * associatedLegendre(q.l, absM, cosine);
    const auto positive = amplitude * std::polar(1.0, absM * phi);
    return q.m < 0 ? (absM % 2 ? -1.0 : 1.0) * std::conj(positive) : positive;
}
std::complex<double> superpositionAmplitude(const SuperpositionRequest& request, double time,
                                            const std::array<double, 3>& point) {
    SuperpositionModel::validateRequest(request);
    validateTime(time, request.orbital.atomicNumber);
    const double Z = request.orbital.atomicNumber;
    return evolveCoefficient(std::sqrt(1.0 - request.secondWeight), 0, request.orbital.state.n, Z, time) *
               orbitalAmplitude(request.orbital.state, Z, point) +
           evolveCoefficient(std::sqrt(request.secondWeight), request.relativePhase, request.second.n, Z, time) *
               orbitalAmplitude(request.second, Z, point);
}
void SuperpositionModel::validateRequest(const SuperpositionRequest& request) {
    if (request.orbital.model != "orbital-superposition")
        throw ModelError("unsupported-model", "model", "Expected orbital-superposition");
    auto primary = request.orbital;
    primary.model = "hydrogenic-orbital";
    OrbitalModel::validateRequest(primary);
    primary.state = request.second;
    try { OrbitalModel::validateRequest(primary); }
    catch (const ModelError& error) { throw ModelError(error.code, "superposition." + error.field, error.what()); }
    if (request.orbital.state.n > maxN)
        throw ModelError("invalid-input", "state.n", "Superpositions currently support n <= 4");
    if (request.second.n > maxN)
        throw ModelError("invalid-input", "superposition.state.n", "Superpositions currently support n <= 4");
    if (request.orbital.screening != ScreeningMode::PureZ)
        throw ModelError("invalid-input", "screening", "Superpositions require pure-z for one shared Hamiltonian");
    if (request.orbital.sampleCount > maxSamples)
        throw ModelError("resource-limit", "sampleCount", "Superposition sample count exceeds 20000");
    const auto a = request.orbital.state, b = request.second;
    if (a.n == b.n && a.l == b.l && a.m == b.m)
        throw ModelError("invalid-input", "superposition.state", "Choose two distinct orthogonal orbitals");
    if (!std::isfinite(request.secondWeight) || request.secondWeight < 0 || request.secondWeight > 1)
        throw ModelError("invalid-input", "superposition.weight", "Second-state probability weight must be between 0 and 1");
    if (!std::isfinite(request.relativePhase) || std::abs(request.relativePhase) > 1e6)
        throw ModelError("invalid-input", "superposition.phase", "Relative phase must be finite, with magnitude <= 1000000 radians");
}
SuperpositionResult SuperpositionModel::run(const SuperpositionRequest& request, double time) const {
    validateRequest(request); validateTime(time, request.orbital.atomicNumber);
    try {
        const auto q = request.orbital.state, b = request.second;
        const double Z = request.orbital.atomicNumber, weight = request.secondWeight;
        RadialSampler radialA(q.n, q.l, Z), radialB(b.n, b.l, Z);
        AngularSampler angularA(q.l, q.m), angularB(b.l, b.m);
        const auto coefficientA = evolveCoefficient(std::sqrt(1 - weight), 0, q.n, Z, time);
        const auto coefficientB = evolveCoefficient(std::sqrt(weight), request.relativePhase, b.n, Z, time);
        SuperpositionResult result;
        result.request = request; result.timeAtomicUnits = time;
        auto& output = result.orbital;
        output.request = request.orbital;
        const auto element = database_.findByZ(request.orbital.atomicNumber);
        output.metadataAvailable = element.has_value();
        output.element = element.value_or(Element{request.orbital.atomicNumber, "Z" + std::to_string(request.orbital.atomicNumber), "Unknown element", "", 0});
        auto& data = output.observables;
        data.qn = q; data.Zeff = Z;
        data.energy_eV = (1 - weight) * hydrogenicEnergy(q.n, Z) + weight * hydrogenicEnergy(b.n, Z);
        data.execution = {"cpu", request.orbital.backend == BackendPreference::Auto ? "Superposition sampling currently uses CPU" : ""};
        const auto count = static_cast<std::size_t>(request.orbital.sampleCount);
        data.points.reserve(count);
        std::mt19937 generator(request.orbital.seed);
        std::uniform_real_distribution<double> uniform(0, 1), phi(0, 2 * PI);
        double sumRadius = 0;
        // Cauchy-Schwarz: |cA psiA + cB psiB|^2 <= 2 * mixture density.
        for (std::size_t attempt = 0; data.points.size() < count && attempt < 128 * count; ++attempt) {
            const bool chooseB = uniform(generator) < weight;
            const double r = chooseB ? radialB.sample(generator) : radialA.sample(generator);
            const double theta = chooseB ? angularB.sample(generator) : angularA.sample(generator);
            const double angle = phi(generator);
            const std::array<double, 3> point{r * std::sin(theta) * std::cos(angle), r * std::cos(theta), r * std::sin(theta) * std::sin(angle)};
            const auto psiA = orbitalAmplitude(q, Z, point), psiB = orbitalAmplitude(b, Z, point);
            const double mixture = (1 - weight) * std::norm(psiA) + weight * std::norm(psiB);
            const double density = std::norm(coefficientA * psiA + coefficientB * psiB);
            if (!std::isfinite(mixture) || !std::isfinite(density)) throw std::domain_error("Nonfinite superposition density");
            if (mixture <= 0) continue;
            const double ratio = density / (2 * mixture);
            if (ratio > 1 + 1e-12) throw std::domain_error("Superposition rejection bound exceeded");
            if (uniform(generator) < std::min(1.0, ratio)) { data.points.push_back(point); sumRadius += r; }
        }
        if (data.points.size() != count) throw std::domain_error("Superposition sampling attempt limit exceeded");
        output.actualSampleCount = count;
        data.meanRadius_a0 = sumRadius / count;
        if (!std::isfinite(data.meanRadius_a0) || !std::isfinite(data.energy_eV)) throw std::domain_error("Nonfinite superposition statistics");
        return result;
    } catch (const std::bad_alloc&) { throw ModelError("resource-limit", "sampleCount", "Insufficient memory for superposition"); }
      catch (const ModelError&) { throw; }
      catch (const std::exception& error) { throw ModelError("numerical-failure", "", error.what()); }
}
void SuperpositionModel::advance(SuperpositionResult& result, double dt) const {
    validateTime(dt, result.request.orbital.atomicNumber);
    auto next = run(result.request, result.timeAtomicUnits + dt);
    result = std::move(next); // Rejection samples are density snapshots, not particle trajectories.
}
}
