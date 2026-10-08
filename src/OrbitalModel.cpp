#include "OrbitalModel.hpp"
#include "QuantumMath.hpp"
#include <cmath>
#include <new>

namespace qm {
namespace {
void validate(const OrbitalRequest& request) {
    if (request.contractVersion != 1)
        throw ModelError("unsupported-version", "contractVersion", "Supported contract version is 1");
    if (request.model != "hydrogenic-orbital")
        throw ModelError("unsupported-model", "model", "Only hydrogenic-orbital is available");
    if (request.atomicNumber <= 0)
        throw ModelError("invalid-input", "element.atomicNumber", "Atomic number must be positive");
    const auto q = request.state;
    if (q.n < 1 || q.n > 7)
        throw ModelError("invalid-input", "state.n", "n must be between 1 and 7");
    if (q.l < 0 || q.l > 3 || q.l >= q.n)
        throw ModelError("invalid-input", "state.l", "l must be between 0 and min(3, n-1)");
    if (q.m < -q.l || q.m > q.l)
        throw ModelError("invalid-input", "state.m", "m must be between -l and l");
    if (request.screening != ScreeningMode::PureZ && request.screening != ScreeningMode::SlaterNeutral)
        throw ModelError("invalid-input", "screening", "Unknown screening mode");
    if (request.backend != BackendPreference::Auto && request.backend != BackendPreference::Cpu)
        throw ModelError("invalid-input", "backend", "Unknown backend preference");
    if (request.sampleCount <= 0)
        throw ModelError("invalid-input", "sampleCount", "Sample count must be positive");
    if (request.sampleCount > OrbitalModel::capabilities().maxSamples)
        throw ModelError("resource-limit", "sampleCount", "Sample count exceeds 1000000");
}
}
OrbitalResult OrbitalModel::run(const OrbitalRequest& request) const {
    validate(request);
    const auto element = database_.findByZ(request.atomicNumber);
    OrbitalResult result;
    result.request = request;
    result.metadataAvailable = element.has_value();
    result.element = element.value_or(Element{request.atomicNumber, "Z" + std::to_string(request.atomicNumber),
                                             "Unknown element", "", 0.0});
    if (request.screening == ScreeningMode::SlaterNeutral) {
        try {
            slaterZeff(request.atomicNumber, request.state.n, request.state.l, result.element.config);
        } catch (const std::exception& error) {
            throw ModelError("screening-unavailable", "screening", std::string(error.what()) + "; select pure-z instead");
        }
    }
    try {
        Simulation simulation(result.element, request.state.n, request.state.l, request.state.m,
                              request.screening == ScreeningMode::SlaterNeutral,
                              static_cast<int>(request.sampleCount), request.seed, request.backend);
        result.observables = simulation.run();
        result.actualSampleCount = result.observables.points.size();
    } catch (const std::bad_alloc&) {
        throw ModelError("resource-limit", "sampleCount", "Insufficient memory for cloud");
    } catch (const std::exception& error) {
        throw ModelError("numerical-failure", "", error.what());
    }
    return result;
}

ComputeExecution OrbitalModel::advance(OrbitalResult& result, double dt) const {
    validate(result.request);
    if (!std::isfinite(dt)) throw ModelError("invalid-input", "dt", "Time must be finite");
    if (result.actualSampleCount != result.observables.points.size() ||
        result.actualSampleCount != static_cast<std::size_t>(result.request.sampleCount) ||
        result.observables.qn.n != result.request.state.n ||
        result.observables.qn.l != result.request.state.l ||
        result.observables.qn.m != result.request.state.m)
        throw ModelError("invalid-input", "result", "Result state or count does not match its request");
    try {
        return advanceCloud(result.observables.points, result.request.state.m, dt, result.request.backend);
    } catch (const std::bad_alloc&) {
        throw ModelError("resource-limit", "sampleCount", "Insufficient memory for current update");
    } catch (const std::invalid_argument& error) {
        throw ModelError("invalid-input", "current", error.what());
    } catch (const std::exception& error) {
        throw ModelError("numerical-failure", "", error.what());
    }
}
}
