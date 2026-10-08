#include "OrbitalModel.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class Operation>
void fails(Operation operation, const std::string& code, const std::string& field) {
    try { operation(); }
    catch (const qm::ModelError& error) {
        require(error.code == code && error.field == field, "Wrong model error code/field");
        return;
    }
    throw std::runtime_error("Expected model error");
}
double radius(const std::array<double, 3>& point) {
    return std::hypot(std::hypot(point[0], point[1]), point[2]);
}
}
int main() {
    try {
        qm::ElementDatabase database;
        qm::OrbitalModel model(database);
        const auto caps = model.capabilities();
        require(caps.maxSamples == 1000000 && caps.coordinateUnit == "a0" && caps.energyUnit == "eV",
                "Missing capabilities/units");
        qm::OrbitalRequest request;
        request.backend = qm::BackendPreference::Cpu;
        auto hydrogen = model.run(request);
        require(hydrogen.metadataAvailable && hydrogen.element.symbol == "H", "Wrong element identity");
        require(hydrogen.request.contractVersion == 1 && hydrogen.modelVersion == 1 &&
                hydrogen.actualSampleCount == 50000 && hydrogen.observables.Zeff == 1,
                "Missing result metadata");
        require(std::abs(hydrogen.observables.meanRadius_a0 - 1.5) < 0.025, "Hydrogen radius mismatch");
        require(std::abs(hydrogen.observables.energy_eV + 13.6) < 0.01, "Hydrogen energy mismatch");
        require(hydrogen.observables.execution.backend == "cpu" &&
                hydrogen.observables.execution.fallbackReason.empty(), "Explicit CPU invoked fallback");
        require(model.run(request).observables.points == hydrogen.observables.points, "CPU seed not repeatable");
        request.seed = 123;
        require(model.run(request).observables.points != hydrogen.observables.points, "Seed ignored");
        const auto stationary = hydrogen.observables.points;
        model.advance(hydrogen, 0.1);
        require(hydrogen.observables.points == stationary, "m=0 state moved");
        request.atomicNumber = 79;
        request.state = {6, 0, 0};
        request.screening = qm::ScreeningMode::SlaterNeutral;
        const auto gold = model.run(request);
        require(std::abs(gold.observables.Zeff - 3.7) < 1e-12, "Gold screening mismatch");
        require(std::abs(gold.observables.meanRadius_a0 - 54.0 / 3.7) < 0.3, "Gold radius mismatch");
        auto boundary = request;
        boundary.state = {7, 3, -3}; boundary.screening = qm::ScreeningMode::PureZ;
        boundary.sampleCount = 256; boundary.seed = std::numeric_limits<std::uint32_t>::max();
        require(model.run(boundary).actualSampleCount == 256, "Supported state/seed boundary rejected");

        auto invalid = request;
        invalid.contractVersion = 2;
        fails([&]{model.run(invalid);}, "unsupported-version", "contractVersion");
        invalid = request; invalid.model = "lattice-dynamics";
        fails([&]{model.run(invalid);}, "unsupported-model", "model");
        invalid = request; invalid.atomicNumber = 0;
        fails([&]{model.run(invalid);}, "invalid-input", "element.atomicNumber");
        for (int n : {0, 8}) {
            invalid = request; invalid.state.n = n;
            fails([&]{model.run(invalid);}, "invalid-input", "state.n");
        }
        for (int l : {-1, 4, 6}) {
            invalid = request; invalid.state.l = l;
            fails([&]{model.run(invalid);}, "invalid-input", "state.l");
        }
        invalid = request; invalid.state = {1, 1, 0};
        fails([&]{model.run(invalid);}, "invalid-input", "state.l");
        for (int m : {std::numeric_limits<int>::min(), 1}) {
            invalid = request; invalid.state.m = m;
            fails([&]{model.run(invalid);}, "invalid-input", "state.m");
        }
        invalid = request; invalid.screening = static_cast<qm::ScreeningMode>(99);
        fails([&]{model.run(invalid);}, "invalid-input", "screening");
        invalid = request; invalid.backend = static_cast<qm::BackendPreference>(99);
        fails([&]{model.run(invalid);}, "invalid-input", "backend");
        invalid = request; invalid.sampleCount = 0;
        fails([&]{model.run(invalid);}, "invalid-input", "sampleCount");
        invalid.sampleCount = -1;
        fails([&]{model.run(invalid);}, "invalid-input", "sampleCount");
        invalid.sampleCount = std::numeric_limits<std::int64_t>::max();
        fails([&]{model.run(invalid);}, "resource-limit", "sampleCount");
        invalid = request; invalid.atomicNumber = 1; invalid.state = {2, 1, 0};
        fails([&]{model.run(invalid);}, "screening-unavailable", "screening");
        invalid = request; invalid.atomicNumber = 120;
        fails([&]{model.run(invalid);}, "screening-unavailable", "screening");
        invalid.screening = qm::ScreeningMode::PureZ;
        const auto unknown = model.run(invalid);
        require(!unknown.metadataAvailable && unknown.element.Z == 120 && unknown.observables.Zeff == 120,
                "Unknown pure Z metadata mismatch");

        request = {}; request.state = {2, 1, -1}; request.backend = qm::BackendPreference::Cpu;
        auto flowing = model.run(request);
        const auto original = flowing.observables.points;
        const auto execution = model.advance(flowing, 0.1);
        require(execution.backend == "cpu" && execution.fallbackReason.empty(), "Flow backend mismatch");
        bool moved = false;
        for (std::size_t i = 0; i < original.size(); ++i) {
            const auto& after = flowing.observables.points[i];
            require(original[i][1] == after[1], "Flow changed Y");
            require(std::abs(radius(original[i]) - radius(after)) < 1e-12, "Flow changed radius");
            moved = moved || original[i] != after;
        }
        require(moved, "Flow did not move samples");
        const auto beforeFailure = flowing.observables.points;
        fails([&]{model.advance(flowing, std::numeric_limits<double>::infinity());}, "invalid-input", "dt");
        require(beforeFailure == flowing.observables.points, "Invalid time changed cloud");
        flowing.observables.points.back()[1] = std::numeric_limits<double>::infinity();
        const auto badCloud = flowing.observables.points;
        fails([&]{model.advance(flowing, 0.1);}, "invalid-input", "current");
        require(badCloud == flowing.observables.points, "Invalid cloud partially advanced");
        flowing.observables.points.pop_back();
        fails([&]{model.advance(flowing, 0.1);}, "invalid-input", "result");
        request.backend = qm::BackendPreference::Auto;
        const auto automatic = model.run(request);
        require(automatic.observables.execution.backend == "cuda" ||
                !automatic.observables.execution.fallbackReason.empty(), "Missing automatic execution metadata");
        if (!qm::cudaAvailable()) require(automatic.observables.execution.backend == "cpu", "Fallback did not use CPU");
        std::cout << "Model validation, screening, seed, and current regressions passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
