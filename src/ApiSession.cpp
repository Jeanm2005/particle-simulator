#include "ApiSession.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace {
constexpr std::size_t previewLimit = 20000;
void string(std::ostream& out, const std::string& value) {
    out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 0x20) out << "\\u00" << "0123456789abcdef"[c >> 4] << "0123456789abcdef"[c & 15];
        else out << c;
    }
    out << '"';
}
std::string token(std::istream& input, const char* field) {
    std::string value;
    if (!(input >> std::quoted(value))) throw qm::ModelError("invalid-input", field, "Missing input field");
    return value;
}
template<class T> T integer(std::istream& input, const char* field) {
    const auto text = token(input, field);
    T value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw qm::ModelError("invalid-input", field, "Expected an in-range whole integer");
    return value;
}
double real(std::istream& input, const char* field) {
    const auto text = token(input, field);
    std::size_t consumed = 0;
    double value;
    try { value = std::stod(text, &consumed); }
    catch (const std::exception&) { throw qm::ModelError("invalid-input", field, "Expected a finite number"); }
    if (consumed != text.size() || !std::isfinite(value))
        throw qm::ModelError("invalid-input", field, "Expected a finite number");
    return value;
}
void end(std::istream& input) {
    std::string extra;
    if (input >> extra) throw qm::ModelError("invalid-input", "", "Unexpected command fields");
}
void element(std::ostream& out, const qm::Element& e) {
    out << "{\"atomicNumber\":" << e.Z << ",\"symbol\":"; string(out, e.symbol);
    out << ",\"name\":"; string(out, e.name);
    out << ",\"configuration\":"; string(out, e.config);
    out << ",\"atomicMass\":";
    if (std::isfinite(e.atomic_mass)) out << e.atomic_mass; else out << "null";
    out << '}';
}
void execution(std::ostream& out, const qm::ComputeExecution& info) {
    out << "{\"backend\":"; string(out, info.backend);
    out << ",\"fallbackReason\":"; string(out, info.fallbackReason); out << '}';
}
void result(std::ostream& out, const qm::OrbitalResult& r, double time,
            const qm::ComputeExecution& operation, const qm::SuperpositionRequest* coherent = nullptr) {
    const auto& request = r.request;
    const auto& data = r.observables;
    const auto count = std::min(previewLimit, data.points.size());
    out << "{\"contractVersion\":" << request.contractVersion << ",\"modelVersion\":" << r.modelVersion;
    out << ",\"request\":{\"contractVersion\":" << request.contractVersion << ",\"model\":";
    string(out, request.model);
    out << ",\"element\":{\"atomicNumber\":" << request.atomicNumber << "},\"state\":{\"n\":"
        << request.state.n << ",\"l\":" << request.state.l << ",\"m\":" << request.state.m << "},\"screening\":";
    string(out, request.screening == qm::ScreeningMode::PureZ ? "pure-z" : "slater-neutral");
    out << ",\"sampleCount\":" << request.sampleCount << ",\"seed\":" << request.seed << ",\"backend\":";
    string(out, request.backend == qm::BackendPreference::Cpu ? "cpu" : "auto");
    if (coherent) {
        out << ",\"superposition\":{\"state\":{\"n\":" << coherent->second.n
            << ",\"l\":" << coherent->second.l << ",\"m\":" << coherent->second.m
            << "},\"weight\":" << coherent->secondWeight << ",\"phase\":" << coherent->relativePhase << '}';
    }
    out << "},\"element\":"; element(out, r.element);
    out << ",\"metadataAvailable\":" << (r.metadataAvailable ? "true" : "false")
        << ",\"actualSampleCount\":" << r.actualSampleCount << ",\"previewCount\":" << count
        << ",\"effectiveCharge\":" << data.Zeff << ",\"energyEV\":" << data.energy_eV
        << ",\"meanRadiusA0\":" << data.meanRadius_a0 << ",\"timeAtomicUnits\":" << time
        << ",\"units\":{\"coordinates\":\"a0\",\"energy\":\"eV\",\"time\":\"atomic-unit\",\"polarAxis\":\"Y\"}"
        << ",\"samplingExecution\":"; execution(out, data.execution);
    out << ",\"operationExecution\":"; execution(out, operation);
    double meanY = 0;
    for (const auto& point : data.points) meanY += point[1];
    out << ",\"meanYA0\":" << meanY / data.points.size();
    out << ",\"points\":[";
    for (std::size_t i = 0; i < count; ++i) {
        if (i) out << ',';
        // Fixed, evenly spaced indices preserve preview identity during current updates.
        const auto& p = data.points[i * data.points.size() / count];
        out << '[' << p[0] << ',' << p[1] << ',' << p[2] << ']';
    }
    out << "]}";
}
}
namespace qm {
ApiSession::ApiSession(const std::string& executablePath, int sampleLimit)
    : database_("", executablePath), model_(database_), superposition_(database_), sampleLimit_(sampleLimit) {
    if (sampleLimit < 1 || sampleLimit > 1000000) throw std::invalid_argument("Invalid session sample limit");
}
std::string ApiSession::execute(const std::string& line) {
    std::ostringstream out;
    out << std::setprecision(17);
        try {
            if (line.size() > 8192) throw qm::ModelError("resource-limit", "", "Command too large");
            std::istringstream input(line);
            const auto operation = token(input, "operation");
            if (operation == "catalog") {
                end(input);
                const auto caps = model_.capabilities();
                out << "{\"contractVersion\":" << caps.contractVersion << ",\"modelVersion\":" << caps.modelVersion
                          << ",\"model\":\"hydrogenic-orbital\",\"maxN\":" << caps.maxN << ",\"maxL\":" << caps.maxL
                          << ",\"maxSamples\":" << sampleLimit_ << ",\"maxPreviewPoints\":" << previewLimit
                          << ",\"maxSuperpositionN\":4,\"maxSuperpositionSamples\":20000,\"models\":[\"hydrogenic-orbital\",\"orbital-superposition\"]"
                          << ",\"units\":{\"coordinates\":\"a0\",\"energy\":\"eV\",\"time\":\"atomic-unit\",\"polarAxis\":\"Y\"},\"elements\":[";
                bool first = true;
                for (const auto& e : database_.all()) { if (!first) out << ','; first = false; element(out, e); }
                out << "]}";
            } else if (operation == "sample") {
                qm::OrbitalRequest request;
                request.contractVersion = integer<int>(input, "contractVersion");
                request.model = token(input, "model");
                request.atomicNumber = integer<int>(input, "element.atomicNumber");
                request.state.n = integer<int>(input, "state.n");
                request.state.l = integer<int>(input, "state.l");
                request.state.m = integer<int>(input, "state.m");
                const auto screening = token(input, "screening");
                if (screening != "pure-z" && screening != "slater-neutral")
                    throw qm::ModelError("invalid-input", "screening", "Unknown screening mode");
                request.screening = screening == "pure-z" ? qm::ScreeningMode::PureZ : qm::ScreeningMode::SlaterNeutral;
                request.sampleCount = integer<std::int64_t>(input, "sampleCount");
                request.seed = integer<std::uint32_t>(input, "seed");
                const auto backend = token(input, "backend");
                if (backend != "auto" && backend != "cpu") throw qm::ModelError("invalid-input", "backend", "Unknown backend");
                request.backend = backend == "cpu" ? qm::BackendPreference::Cpu : qm::BackendPreference::Auto;
                if (request.sampleCount > sampleLimit_)
                    throw qm::ModelError("resource-limit", "sampleCount", "Sample count exceeds this execution environment's limit");
                if (request.model == "orbital-superposition") {
                    qm::SuperpositionRequest combination;
                    combination.orbital = request;
                    combination.second.n = integer<int>(input, "superposition.state.n");
                    combination.second.l = integer<int>(input, "superposition.state.l");
                    combination.second.m = integer<int>(input, "superposition.state.m");
                    combination.secondWeight = real(input, "superposition.weight");
                    combination.relativePhase = real(input, "superposition.phase");
                    end(input);
                    auto sampled = superposition_.run(combination);
                    coherent_ = std::move(sampled); current_.reset(); time_ = 0;
                    result(out, coherent_->orbital, time_, coherent_->orbital.observables.execution, &coherent_->request);
                    return out.str();
                }
                end(input);
                auto sampled = model_.run(request);
                current_ = std::move(sampled); coherent_.reset(); time_ = 0;
                result(out, *current_, time_, current_->observables.execution);
            } else if (operation == "advance") {
                if (!current_ && !coherent_) throw qm::ModelError("invalid-input", "runId", "Sample an orbital first");
                const auto text = token(input, "dt");
                std::size_t consumed = 0;
                double dt;
                try { dt = std::stod(text, &consumed); }
                catch (const std::exception&) { throw qm::ModelError("invalid-input", "dt", "Expected finite atomic-unit time"); }
                if (consumed != text.size() || !std::isfinite(dt) || !std::isfinite(time_ + dt))
                    throw qm::ModelError("invalid-input", "dt", "Expected finite atomic-unit time");
                end(input);
                if (coherent_) {
                    superposition_.advance(*coherent_, dt); time_ = coherent_->timeAtomicUnits;
                    result(out, coherent_->orbital, time_, coherent_->orbital.observables.execution, &coherent_->request);
                    return out.str();
                }
                const auto info = model_.advance(*current_, dt); time_ += dt;
                result(out, *current_, time_, info);
            } else throw qm::ModelError("invalid-input", "operation", "Unknown operation");
        } catch (const qm::ModelError& error) {
            out << "{\"error\":{\"code\":"; string(out, error.code);
            out << ",\"field\":"; string(out, error.field);
            out << ",\"message\":"; string(out, error.what()); out << "}}";
        } catch (const std::exception& error) {
            out << "{\"error\":{\"code\":\"numerical-failure\",\"field\":\"\",\"message\":";
            string(out, error.what()); out << "}}";
        }
    return out.str();
}
}
