#pragma once
#include "Orbital.hpp"
#include <cstdint>
#include <vector>

namespace qm {
// Sonification maps model data to audio. An isolated orbital emits no audible sound.
//
// Mapping "radial-density-scan" (version 1):
//   frame k of N scans r_k = scanRadius * k / (N - 1), scanRadius = 15 n^2 / Zeff a0
//   (the same extent used by the radial sampler);
//   envelope a_k = P(r_k) / max_j P(r_j), with P(r) = r^2 |R_nl(r)|^2;
//   sample s_k = peak * a_k * sin(2 pi f t_k), t_k = k / sampleRate, peak = 0.8 full scale.
// Radial nodes therefore become silences; the tone's pitch is a fixed display choice.
struct SonificationRequest {
    double durationSeconds = 3.0;
    int sampleRate = 44100;
    double baseFrequencyHz = 220.0;
};
struct SonificationResult {
    SonificationRequest request;
    QuantumNumbers state{1, 0, 0};
    double effectiveCharge = 1.0;
    double scanRadiusA0 = 0.0;
    std::vector<double> envelope;      // a_k in [0, 1]
    std::vector<std::int16_t> pcm;     // mono, 16-bit signed
    std::vector<double> nodeRadiiA0;   // interior envelope minima below 1% of peak
};
constexpr const char* sonificationMapping = "radial-density-scan";
constexpr int sonificationMappingVersion = 1;
constexpr double sonificationPeak = 0.8;

// Throws ModelError with fields under "sonification.".
void validateSonification(const SonificationRequest&);
SonificationResult sonifyRadialDensity(QuantumNumbers state, double effectiveCharge,
                                       const SonificationRequest&);
// RIFF/WAVE, PCM format 1, mono, 16 bits per sample, little-endian.
std::vector<std::uint8_t> encodeWav(const std::vector<std::int16_t>& pcm, int sampleRate);
}
