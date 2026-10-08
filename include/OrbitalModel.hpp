#pragma once
#include "Simulation.hpp"
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace qm {
enum class ScreeningMode { PureZ, SlaterNeutral };
struct OrbitalRequest {
    int contractVersion = 1;
    std::string model = "hydrogenic-orbital";
    int atomicNumber = 1;
    QuantumNumbers state{1, 0, 0};
    ScreeningMode screening = ScreeningMode::PureZ;
    std::int64_t sampleCount = 50000;
    std::uint32_t seed = 42;
    BackendPreference backend = BackendPreference::Auto;
};
struct ModelCapabilities {
    int contractVersion = 1;
    int modelVersion = 1;
    std::string model = "hydrogenic-orbital";
    int maxN = 7;
    int maxL = 3;
    std::int64_t maxSamples = 1000000;
    std::string coordinateUnit = "a0";
    std::string energyUnit = "eV";
    std::string timeUnit = "atomic-unit";
};
class ModelError : public std::runtime_error {
public:
    ModelError(std::string code, std::string field, const std::string& message)
        : std::runtime_error(message), code(std::move(code)), field(std::move(field)) {}
    const std::string code;
    const std::string field;
};
struct OrbitalResult {
    OrbitalRequest request;
    int modelVersion = 1;
    Element element;
    bool metadataAvailable = false;
    std::size_t actualSampleCount = 0;
    SimulationResult observables;
};
class OrbitalModel {
public:
    explicit OrbitalModel(const ElementDatabase& database) : database_(database) {}
    static ModelCapabilities capabilities() { return {}; }
    OrbitalResult run(const OrbitalRequest&) const;
    // Time is in atomic units; the last operation's backend is returned separately.
    ComputeExecution advance(OrbitalResult&, double dt) const;
private:
    ElementDatabase database_;
};
}
