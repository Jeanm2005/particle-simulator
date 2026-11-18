#pragma once
#include "Superposition.hpp"
#include "PhononModel.hpp"
#include <optional>
namespace qm {
// Native worker and browser WebAssembly share this stateful protocol implementation.
class ApiSession {
public:
    explicit ApiSession(const std::string& executablePath = "", int sampleLimit = 1000000);
    std::string execute(const std::string& command);
private:
    ElementDatabase database_;
    OrbitalModel model_;
    SuperpositionModel superposition_;
    std::optional<OrbitalResult> current_;
    std::optional<SuperpositionResult> coherent_;
    PhononModel phonons_;
    std::optional<PhononResult> lattice_;
    double time_ = 0;
    int sampleLimit_;
};
}
