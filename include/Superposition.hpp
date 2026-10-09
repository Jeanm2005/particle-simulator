#pragma once
#include "OrbitalModel.hpp"
#include <complex>

namespace qm {
// Two distinct orthonormal eigenstates of the same pure-Z Hamiltonian.
struct SuperpositionRequest {
    OrbitalRequest orbital;
    QuantumNumbers second{2, 1, 0};
    double secondWeight = 0.5;
    double relativePhase = 0.0; // radians, coefficient of the second state
};
struct SuperpositionResult {
    SuperpositionRequest request;
    OrbitalResult orbital;
    double timeAtomicUnits = 0.0;
};
// Complex spherical harmonics use Y as the polar axis and atan2(z,x) as phi.
std::complex<double> orbitalAmplitude(QuantumNumbers, double Z,
                                     const std::array<double, 3>& point);
std::complex<double> superpositionAmplitude(const SuperpositionRequest&, double time,
                                            const std::array<double, 3>& point);
class SuperpositionModel {
public:
    explicit SuperpositionModel(const ElementDatabase& database) : database_(database) {}
    static constexpr int maxN = 4;
    static constexpr int maxSamples = 20000;
    static void validateRequest(const SuperpositionRequest&);
    SuperpositionResult run(const SuperpositionRequest&, double time = 0.0) const;
    void advance(SuperpositionResult&, double dt) const;
private:
    ElementDatabase database_;
};
}
