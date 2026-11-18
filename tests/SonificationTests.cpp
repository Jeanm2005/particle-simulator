#include "Sonification.hpp"
#include "OrbitalModel.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void fails(F fn, const char* field) {
    try { fn(); } catch (const qm::ModelError& error) {
        require(error.code == std::string("invalid-input") && error.field == field, "Unexpected sonification error");
        return;
    }
    throw std::runtime_error("Expected sonification rejection");
}
// Goertzel power of one frequency over a frame window.
double power(const std::vector<std::int16_t>& pcm, std::size_t begin, std::size_t end, double f, int rate) {
    const double w = 2.0 * 3.14159265358979323846 * f / rate, c = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (std::size_t k = begin; k < end; ++k) { const double s0 = pcm[k] + c * s1 - s2; s2 = s1; s1 = s0; }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}
unsigned u32(const std::vector<std::uint8_t>& b, std::size_t i) {
    return b[i] | b[i + 1] << 8 | b[i + 2] << 16 | static_cast<unsigned>(b[i + 3]) << 24;
}
}

int main() {
    try {
        qm::SonificationRequest request;
        const double step3 = 15.0 * 9 / (request.durationSeconds * request.sampleRate - 1);

        // 1s has no radial node; its envelope peaks at the most probable radius 1/Z.
        const auto s1 = qm::sonifyRadialDensity({1, 0, 0}, 1.0, request);
        require(s1.pcm.size() == 132300, "Wrong frame count");
        require(s1.nodeRadiiA0.empty(), "1s reported a radial node");
        const auto top = std::max_element(s1.envelope.begin(), s1.envelope.end()) - s1.envelope.begin();
        require(std::abs(top * s1.scanRadiusA0 / (s1.envelope.size() - 1) - 1.0) < 1e-3, "1s envelope peak not at 1 a0");
        int loudest = 0;
        for (auto v : s1.pcm) loudest = std::max(loudest, std::abs(static_cast<int>(v)));
        require(std::abs(loudest - 26214) <= 2, "Peak is not 0.8 full scale");
        require(s1.pcm.front() == 0 && std::abs(s1.pcm.back()) < 2, "Clip does not start and end silent");

        // Analytic hydrogen nodes: 3s at (9 -/+ 3 sqrt 3)/2, 3p at 6, 4d at 12; all scale as 1/Z.
        const auto s3 = qm::sonifyRadialDensity({3, 0, 0}, 1.0, request);
        require(s3.nodeRadiiA0.size() == 2, "3s should have two silences");
        require(std::abs(s3.nodeRadiiA0[0] - (9 - 3 * std::sqrt(3.0)) / 2) < 2 * step3, "3s inner node misplaced");
        require(std::abs(s3.nodeRadiiA0[1] - (9 + 3 * std::sqrt(3.0)) / 2) < 2 * step3, "3s outer node misplaced");
        const auto p3 = qm::sonifyRadialDensity({3, 1, 1}, 1.0, request);
        require(p3.nodeRadiiA0.size() == 1 && std::abs(p3.nodeRadiiA0[0] - 6.0) < 2 * step3, "3p node misplaced");
        const auto d4 = qm::sonifyRadialDensity({4, 2, 0}, 1.0, request);
        require(d4.nodeRadiiA0.size() == 1 && std::abs(d4.nodeRadiiA0[0] - 12.0) < 0.01, "4d node misplaced");
        for (int n = 1; n <= 7; ++n)
            for (int l = 0; l < n && l <= 3; ++l)
                require(qm::sonifyRadialDensity({n, l, 0}, 1.0, request).nodeRadiiA0.size() == static_cast<std::size_t>(n - l - 1),
                        "Silence count differs from n - l - 1");
        const auto helium = qm::sonifyRadialDensity({3, 0, 0}, 2.0, request);
        for (std::size_t k = 0; k < s3.envelope.size(); ++k)
            require(std::abs(helium.envelope[k] - s3.envelope[k]) < 1e-9, "Charge scaling changed the scan envelope");
        require(std::abs(helium.nodeRadiiA0[0] - s3.nodeRadiiA0[0] / 2) < 1e-12, "Node radius does not scale as 1/Z");

        // The magnetic quantum number does not affect radial density.
        require(qm::sonifyRadialDensity({3, 1, -1}, 1.0, request).pcm == p3.pcm, "m changed the radial scan");
        require(qm::sonifyRadialDensity({3, 0, 0}, 1.0, request).pcm == s3.pcm, "Sonification is not deterministic");

        // Carrier frequency: the base tone dominates a voiced window.
        const std::size_t middle = s1.pcm.size() / 20;
        const double base = power(s1.pcm, middle - 4410, middle + 4410, 220, 44100);
        require(base > 1000 * power(s1.pcm, middle - 4410, middle + 4410, 330, 44100), "Carrier is not the base frequency");

        // WAV container.
        qm::SonificationRequest short48{0.5, 48000, 440};
        const auto clip = qm::sonifyRadialDensity({2, 1, 0}, 1.0, short48);
        const auto wav = qm::encodeWav(clip.pcm, 48000);
        require(wav.size() == 44 + 2 * 24000, "Wrong WAV size");
        require(std::string(wav.begin(), wav.begin() + 4) == "RIFF" && std::string(wav.begin() + 8, wav.begin() + 16) == "WAVEfmt ",
                "Missing RIFF/WAVE tags");
        require(u32(wav, 4) == wav.size() - 8 && u32(wav, 16) == 16 && wav[20] == 1 && wav[22] == 1, "Bad format chunk");
        require(u32(wav, 24) == 48000 && u32(wav, 28) == 96000 && wav[32] == 2 && wav[34] == 16, "Bad PCM parameters");
        require(std::string(wav.begin() + 36, wav.begin() + 40) == "data" && u32(wav, 40) == 48000, "Bad data chunk");
        for (std::size_t k = 0; k < clip.pcm.size(); ++k)
            require(static_cast<std::int16_t>(wav[44 + 2 * k] | wav[45 + 2 * k] << 8) == clip.pcm[k], "PCM bytes differ");

        // Validation.
        auto invalid = request; invalid.durationSeconds = 0.25;
        fails([&] { qm::validateSonification(invalid); }, "sonification.durationSeconds");
        invalid = request; invalid.durationSeconds = std::numeric_limits<double>::quiet_NaN();
        fails([&] { qm::validateSonification(invalid); }, "sonification.durationSeconds");
        invalid = request; invalid.sampleRate = 8000;
        fails([&] { qm::validateSonification(invalid); }, "sonification.sampleRate");
        invalid = request; invalid.baseFrequencyHz = 5000;
        fails([&] { qm::validateSonification(invalid); }, "sonification.baseFrequencyHz");
        std::cout << "Sonification envelope, node, scaling, carrier and WAV checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
