#include "Element.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <filesystem>
#include <cstdlib>
#include "DataPaths.hpp"

namespace qm {

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return s;
}

void skipWs(const std::string& s, size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
}

std::string readString(const std::string& s, size_t& i) {
    skipWs(s, i);
    if (i >= s.size() || s[i] != '"')
        throw std::runtime_error("expected string");
    ++i;
    std::string out;
    while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\' && i + 1 < s.size()) {
            ++i;
            switch (s[i]) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case 'n':  out += '\n'; break;
                case 't':  out += '\t'; break;
                default:   out += s[i]; break;
            }
        } else {
            out += s[i];
        }
        ++i;
    }
    if (i >= s.size()) throw std::runtime_error("unterminated string");
    ++i;
    return out;
}

std::string extractValue(const std::string& obj, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = obj.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    while (pos < obj.size() && (std::isspace(static_cast<unsigned char>(obj[pos])) || obj[pos] == ':'))
        ++pos;
    if (pos >= obj.size()) return {};

    if (obj[pos] == '"') {
        size_t i = pos;
        return readString(obj, i);
    }
    size_t end = pos;
    while (end < obj.size() && obj[end] != ',' && obj[end] != '}' &&
           !std::isspace(static_cast<unsigned char>(obj[end])))
        ++end;
    return obj.substr(pos, end - pos);
}

} // anonymous namespace

ElementDatabase::ElementDatabase(const std::string& jsonPath,
                                 const std::string& executablePath) {
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;
    if (!jsonPath.empty()) {
        candidates.emplace_back(jsonPath);
    } else {
        if (const char* overrideDirectory = std::getenv("QM_DATA_DIR"))
            candidates.push_back(fs::path(overrideDirectory) / "elements.json");
        candidates.emplace_back("data/elements.json");
        if (!executablePath.empty()) {
            fs::path executable(executablePath);
            std::error_code error;
            if (!executable.has_parent_path()) {
                // argv[0] may be a bare name when launched through PATH.
                if (const char* searchPath = std::getenv("PATH")) {
                    std::istringstream paths(searchPath);
                    std::string directory;
#ifdef _WIN32
                    const char separator = ';';
#else
                    const char separator = ':';
#endif
                    while (std::getline(paths, directory, separator)) {
                        fs::path candidate = fs::path(directory) / executable;
                        if (fs::is_regular_file(candidate, error)) {
                            executable = candidate;
                            break;
                        }
                        error.clear();
                    }
                }
            }
            const fs::path resolved = fs::canonical(executable, error);
            if (!error) {
                candidates.push_back(resolved.parent_path() / QM_INSTALL_DATA_FROM_BIN / "elements.json");
            }
        }
        candidates.push_back(fs::path(QM_INSTALL_DATA_DIR) / "elements.json");
        candidates.push_back(fs::path(QM_SOURCE_DATA_DIR) / "elements.json");
    }
    for (const auto& candidate : candidates) {
        elements_.clear();
        try {
            if (loadFromJson(candidate.string())) {
                std::cerr << "[ElementDatabase] Loaded " << elements_.size()
                          << " elements from " << candidate.lexically_normal().string() << "\n";
                return;
            }
        } catch (const std::exception& error) {
            std::cerr << "[ElementDatabase] Invalid data in " << candidate.string()
                      << ": " << error.what() << "\n";
        }
    }
    std::cerr << "[ElementDatabase] Could not load element data"
              << (jsonPath.empty() ? "" : " from " + jsonPath)
              << " – using small built-in fallback table.\n";
    loadFallback();
}

bool ElementDatabase::loadFromJson(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;

    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string content = buffer.str();

    size_t pos = 0;
    while (true) {
        size_t start = content.find('{', pos);
        if (start == std::string::npos) break;
        size_t end = content.find('}', start);
        if (end == std::string::npos) break;

        std::string obj = content.substr(start, end - start + 1);
        pos = end + 1;

        std::string zStr = extractValue(obj, "Z");
        if (zStr.empty()) zStr = extractValue(obj, "number");
        if (zStr.empty()) continue;

        Element e;
        try {
            e.Z = static_cast<int>(std::stod(zStr));
        } catch (...) { continue; }

        e.symbol = extractValue(obj, "symbol");
        e.name   = extractValue(obj, "name");
        e.config = extractValue(obj, "config");
        if (e.config.empty())
            e.config = extractValue(obj, "electron_configuration");

        std::string massStr = extractValue(obj, "atomic_mass");
        try {
            e.atomic_mass = massStr.empty() ? 0.0 : std::stod(massStr);
        } catch (...) {
            e.atomic_mass = 0.0;
        }

        if (!e.symbol.empty() && e.Z > 0)
            elements_.push_back(std::move(e));
    }

    std::sort(elements_.begin(), elements_.end(),
              [](const Element& a, const Element& b){ return a.Z < b.Z; });

    return !elements_.empty();
}

void ElementDatabase::loadFallback() {
    elements_ = {
        {1,  "H",  "Hydrogen",  "1s1", 1.008},
        {2,  "He", "Helium",    "1s2", 4.003},
        {6,  "C",  "Carbon",    "1s2 2s2 2p2", 12.011},
        {8,  "O",  "Oxygen",    "1s2 2s2 2p4", 15.999},
        {26, "Fe", "Iron",      "1s2 2s2 2p6 3s2 3p6 4s2 3d6", 55.845},
        {79, "Au", "Gold",      "1s2 2s2 2p6 3s2 3p6 4s2 3d10 4p6 5s2 4d10 5p6 6s1 4f14 5d10", 196.967},
    };
}

std::optional<Element> ElementDatabase::findBySymbol(const std::string& symbol) const {
    const std::string key = toLower(symbol);
    for (const auto& e : elements_) {
        if (toLower(e.symbol) == key || toLower(e.name) == key)
            return e;
    }
    return std::nullopt;
}

std::optional<Element> ElementDatabase::findByZ(int Z) const {
    for (const auto& e : elements_) {
        if (e.Z == Z)
            return e;
    }
    return std::nullopt;
}

} // namespace qm