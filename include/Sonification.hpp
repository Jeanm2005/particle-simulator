#pragma once
#include "Superposition.hpp"
#include <complex>
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
// Mapping "interference-dipole" (version 1) for two-state superpositions:
//   <y>(t) = 2 sqrt(w (1 - w)) Re(d e^{i(phase - omega t)}), d = <1|y|2>, omega = E2 - E1 (hartree).
//   The tone frequency is baseFrequency * |omega| / 0.375 hartree, so hydrogen 1s+2p
//   sounds at the base frequency and other pairs keep their Bohr-frequency ratios.
//   One audio period spans one beat period of model time, starting at the current time;
//   s_k = peak * fade_k * <y>(t_k) / max|<y>|, with 10 ms raised-cosine fades.
//   Pairs without a Y dipole (selection rules) or without coherence produce silence.
struct InterferenceSonification {
    SonificationRequest request;
    double startTimeAtomicUnits = 0.0;
    double bohrFrequencyAtomicUnits = 0.0; // E2 - E1 in hartree
    double toneFrequencyHz = 0.0;
    std::complex<double> dipoleY;          // <first|y|second> in a0
    double dipoleAmplitudeA0 = 0.0;        // amplitude of the oscillating <y>
    std::string silentReason;              // empty, "dipole-forbidden" or "no-coherence"
    std::vector<std::int16_t> pcm;
};
constexpr const char* interferenceMapping = "interference-dipole";
constexpr double interferenceReferenceHartree = 0.375;

// <a|y|b> for complex hydrogenic orbitals of charge Z, by quadrature (Y is the polar axis).
std::complex<double> dipoleMatrixElementY(QuantumNumbers a, QuantumNumbers b, double Z);
// Exact expectation <y>(t) of the superposition in a0.
double superpositionMeanY(const SuperpositionRequest&, double time);
InterferenceSonification sonifyInterference(const SuperpositionRequest&, double startTime,
                                            const SonificationRequest&);
// RIFF/WAVE, PCM format 1, mono, 16 bits per sample, little-endian.
std::vector<std::uint8_t> encodeWav(const std::vector<std::int16_t>& pcm, int sampleRate);
}
