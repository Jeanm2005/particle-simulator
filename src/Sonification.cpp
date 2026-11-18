#include "Sonification.hpp"
#include "Constants.hpp"
#include "OrbitalModel.hpp"
#include "QuantumMath.hpp"
#include <algorithm>
#include <cmath>

namespace qm {
void validateSonification(const SonificationRequest& request) {
    const double duration = request.durationSeconds;
    if (!std::isfinite(duration) || duration < 0.5 || duration > 10.0)
        throw ModelError("invalid-input", "sonification.durationSeconds", "Duration must be between 0.5 and 10 seconds");
    if (request.sampleRate != 22050 && request.sampleRate != 44100 && request.sampleRate != 48000)
        throw ModelError("invalid-input", "sonification.sampleRate", "Sample rate must be 22050, 44100, or 48000 Hz");
    const double frequency = request.baseFrequencyHz;
    if (!std::isfinite(frequency) || frequency < 55.0 || frequency > 1760.0)
        throw ModelError("invalid-input", "sonification.baseFrequencyHz", "Base frequency must be between 55 and 1760 Hz");
}

SonificationResult sonifyRadialDensity(QuantumNumbers state, double effectiveCharge,
                                       const SonificationRequest& request) {
    validateSonification(request);
    if (state.n < 1 || state.l < 0 || state.l >= state.n)
        throw ModelError("invalid-input", "state", "Invalid orbital for sonification");
    if (!std::isfinite(effectiveCharge) || effectiveCharge <= 0.0)
        throw ModelError("numerical-failure", "", "Effective charge must be positive and finite");

    SonificationResult result;
    result.request = request;
    result.state = state;
    result.effectiveCharge = effectiveCharge;
    result.scanRadiusA0 = 15.0 * state.n * state.n / effectiveCharge;

    const auto frames = static_cast<std::size_t>(std::llround(request.durationSeconds * request.sampleRate));
    const double step = result.scanRadiusA0 / static_cast<double>(frames - 1);
    result.envelope.resize(frames);
    double peak = 0.0;
    for (std::size_t k = 0; k < frames; ++k) {
        const double density = radialPdf(state.n, state.l, step * static_cast<double>(k), effectiveCharge);
        if (!std::isfinite(density)) throw ModelError("numerical-failure", "", "Nonfinite radial density");
        result.envelope[k] = density;
        peak = std::max(peak, density);
    }
    if (!(peak > 0.0)) throw ModelError("numerical-failure", "", "Radial density has no positive values");
    for (auto& value : result.envelope) value /= peak;

    for (std::size_t k = 1; k + 1 < frames; ++k) {
        const auto& a = result.envelope;
        if (a[k] < 0.01 && a[k] <= a[k - 1] && a[k] < a[k + 1])
            result.nodeRadiiA0.push_back(step * static_cast<double>(k));
    }

    result.pcm.resize(frames);
    const double omega = 2.0 * PI * request.baseFrequencyHz / request.sampleRate;
    for (std::size_t k = 0; k < frames; ++k) {
        const double sample = sonificationPeak * result.envelope[k] * std::sin(omega * static_cast<double>(k));
        result.pcm[k] = static_cast<std::int16_t>(std::lround(sample * 32767.0));
    }
    return result;
}

std::complex<double> dipoleMatrixElementY(QuantumNumbers a, QuantumNumbers b, double Z) {
    if (a.m != b.m) return {0.0, 0.0}; // the azimuthal integral vanishes
    // For equal m the integrand does not depend on phi; integrate r and u = cos(theta) at phi = 0.
    // r = s^2 concentrates points near the nucleus; the range covers the outer orbital's tail.
    const int maxN = std::max(a.n, b.n);
    const double sMax = std::sqrt(40.0 * maxN * maxN / Z);
    constexpr int radialSteps = 4000, angularSteps = 96;
    const double ds = sMax / radialSteps, du = 2.0 / angularSteps;
    std::complex<double> sum = 0.0;
    for (int i = 0; i < radialSteps; ++i) {
        const double s = (i + 0.5) * ds, r = s * s, jacobian = 2.0 * s * ds * r * r;
        for (int j = 0; j < angularSteps; ++j) {
            const double u = -1.0 + (j + 0.5) * du;
            const std::array<double, 3> point{r * std::sqrt(1.0 - u * u), r * u, 0.0};
            sum += std::conj(orbitalAmplitude(a, Z, point)) * orbitalAmplitude(b, Z, point) * (r * u) * jacobian * du;
        }
    }
    return 2.0 * PI * sum;
}

namespace {
double energyHartree(int n, double Z) { return -0.5 * Z * Z / (n * static_cast<double>(n)); }
}

double superpositionMeanY(const SuperpositionRequest& request, double time) {
    SuperpositionModel::validateRequest(request);
    const double Z = request.orbital.atomicNumber, w = request.secondWeight;
    const double omega = energyHartree(request.second.n, Z) - energyHartree(request.orbital.state.n, Z);
    const auto d = dipoleMatrixElementY(request.orbital.state, request.second, Z);
    return 2.0 * std::sqrt(w * (1.0 - w)) * std::real(d * std::polar(1.0, request.relativePhase - omega * time));
}

InterferenceSonification sonifyInterference(const SuperpositionRequest& superposition, double startTime,
                                            const SonificationRequest& request) {
    validateSonification(request);
    SuperpositionModel::validateRequest(superposition);
    if (!std::isfinite(startTime)) throw ModelError("invalid-input", "dt", "Model time must be finite");
    const double Z = superposition.orbital.atomicNumber, w = superposition.secondWeight;
    InterferenceSonification result;
    result.request = request;
    result.startTimeAtomicUnits = startTime;
    result.bohrFrequencyAtomicUnits = energyHartree(superposition.second.n, Z) - energyHartree(superposition.orbital.state.n, Z);
    if (result.bohrFrequencyAtomicUnits == 0.0)
        throw ModelError("invalid-input", "superposition.state", "Degenerate states have stationary density; choose a different n");
    result.toneFrequencyHz = request.baseFrequencyHz * std::abs(result.bohrFrequencyAtomicUnits) / interferenceReferenceHartree;
    if (result.toneFrequencyHz < 20.0 || result.toneFrequencyHz > std::min(20000.0, 0.45 * request.sampleRate))
        throw ModelError("invalid-input", "sonification.baseFrequencyHz",
                         "This pair's Bohr frequency maps outside 20 Hz to 20 kHz; adjust the base tone");
    result.dipoleY = dipoleMatrixElementY(superposition.orbital.state, superposition.second, Z);
    result.dipoleAmplitudeA0 = 2.0 * std::sqrt(w * (1.0 - w)) * std::abs(result.dipoleY);
    if (std::abs(result.dipoleY) < 1e-9) result.silentReason = "dipole-forbidden";
    else if (result.dipoleAmplitudeA0 < 1e-12) result.silentReason = "no-coherence";

    const auto frames = static_cast<std::size_t>(std::llround(request.durationSeconds * request.sampleRate));
    result.pcm.assign(frames, 0);
    if (!result.silentReason.empty()) return result;
    // <y>(t)/amplitude = cos(arg d + phase - omega t); one audio period per beat period.
    const double phase0 = std::arg(result.dipoleY) + superposition.relativePhase - result.bohrFrequencyAtomicUnits * startTime;
    const double direction = result.bohrFrequencyAtomicUnits > 0 ? 1.0 : -1.0;
    const double step = 2.0 * PI * result.toneFrequencyHz / request.sampleRate;
    const double fadeFrames = 0.01 * request.sampleRate;
    for (std::size_t k = 0; k < frames; ++k) {
        const double edge = std::min(static_cast<double>(k), static_cast<double>(frames - 1 - k));
        const double fade = edge >= fadeFrames ? 1.0 : 0.5 - 0.5 * std::cos(PI * edge / fadeFrames);
        const double phase = std::remainder(phase0 - direction * step * static_cast<double>(k), 2.0 * PI);
        result.pcm[k] = static_cast<std::int16_t>(std::lround(sonificationPeak * fade * std::cos(phase) * 32767.0));
    }
    return result;
}

std::vector<std::uint8_t> encodeWav(const std::vector<std::int16_t>& pcm, int sampleRate) {
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(pcm.size() * 2);
    std::vector<std::uint8_t> out;
    out.reserve(44 + dataBytes);
    auto text = [&](const char* tag) { out.insert(out.end(), tag, tag + 4); };
    auto u16 = [&](std::uint16_t v) { out.push_back(v & 0xff); out.push_back(v >> 8); };
    auto u32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back((v >> (8 * i)) & 0xff); };
    text("RIFF"); u32(36 + dataBytes); text("WAVE");
    text("fmt "); u32(16); u16(1); u16(1);
    u32(static_cast<std::uint32_t>(sampleRate)); u32(static_cast<std::uint32_t>(sampleRate) * 2);
    u16(2); u16(16);
    text("data"); u32(dataBytes);
    for (const auto sample : pcm) u16(static_cast<std::uint16_t>(sample));
    return out;
}
}
