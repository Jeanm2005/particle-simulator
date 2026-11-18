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
