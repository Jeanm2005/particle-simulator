#include "OrbitalModel.hpp"
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
            const qm::ComputeExecution& operation) {
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
    out << "},\"element\":"; element(out, r.element);
    out << ",\"metadataAvailable\":" << (r.metadataAvailable ? "true" : "false")
        << ",\"actualSampleCount\":" << r.actualSampleCount << ",\"previewCount\":" << count
        << ",\"effectiveCharge\":" << data.Zeff << ",\"energyEV\":" << data.energy_eV
        << ",\"meanRadiusA0\":" << data.meanRadius_a0 << ",\"timeAtomicUnits\":" << time
        << ",\"units\":{\"coordinates\":\"a0\",\"energy\":\"eV\",\"time\":\"atomic-unit\",\"polarAxis\":\"Y\"}"
        << ",\"samplingExecution\":"; execution(out, data.execution);
    out << ",\"operationExecution\":"; execution(out, operation);
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
int main(int argc, char** argv) {
    if (argc != 1) { std::cerr << "atom_api reads commands from stdin; launch tools/serve.py for the browser UI\n"; return 1; }
    qm::ElementDatabase database("", argv[0]);
    qm::OrbitalModel model(database);
    std::optional<qm::OrbitalResult> current;
    double time = 0;
    std::cout << std::setprecision(17);
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            if (line.size() > 8192) throw qm::ModelError("resource-limit", "", "Command too large");
            std::istringstream input(line);
            const auto operation = token(input, "operation");
            if (operation == "catalog") {
                end(input);
                const auto caps = model.capabilities();
                std::cout << "{\"contractVersion\":" << caps.contractVersion << ",\"modelVersion\":" << caps.modelVersion
                          << ",\"model\":\"hydrogenic-orbital\",\"maxN\":" << caps.maxN << ",\"maxL\":" << caps.maxL
                          << ",\"maxSamples\":" << caps.maxSamples << ",\"maxPreviewPoints\":" << previewLimit
                          << ",\"units\":{\"coordinates\":\"a0\",\"energy\":\"eV\",\"time\":\"atomic-unit\",\"polarAxis\":\"Y\"},\"elements\":[";
                bool first = true;
                for (const auto& e : database.all()) { if (!first) std::cout << ','; first = false; element(std::cout, e); }
                std::cout << "]}";
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
                end(input);
                auto sampled = model.run(request);
                current = std::move(sampled); time = 0;
                result(std::cout, *current, time, current->observables.execution);
            } else if (operation == "advance") {
                if (!current) throw qm::ModelError("invalid-input", "runId", "Sample an orbital first");
                const auto text = token(input, "dt");
                std::size_t consumed = 0;
                double dt;
                try { dt = std::stod(text, &consumed); }
                catch (const std::exception&) { throw qm::ModelError("invalid-input", "dt", "Expected finite atomic-unit time"); }
                if (consumed != text.size() || !std::isfinite(dt) || !std::isfinite(time + dt))
                    throw qm::ModelError("invalid-input", "dt", "Expected finite atomic-unit time");
                end(input);
                const auto info = model.advance(*current, dt); time += dt;
                result(std::cout, *current, time, info);
            } else throw qm::ModelError("invalid-input", "operation", "Unknown operation");
        } catch (const qm::ModelError& error) {
            std::cout << "{\"error\":{\"code\":"; string(std::cout, error.code);
            std::cout << ",\"field\":"; string(std::cout, error.field);
            std::cout << ",\"message\":"; string(std::cout, error.what()); std::cout << "}}";
        } catch (const std::exception& error) {
            std::cout << "{\"error\":{\"code\":\"numerical-failure\",\"field\":\"\",\"message\":";
            string(std::cout, error.what()); std::cout << "}}";
        }
        std::cout << '\n' << std::flush;
    }
}
