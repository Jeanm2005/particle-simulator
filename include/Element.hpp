#pragma once

#include <string>
#include <vector>
#include <optional>

namespace qm {

struct Element {
    int         Z;
    std::string symbol;
    std::string name;
    std::string config;
    double      atomic_mass;
};

class ElementDatabase {
public:
    explicit ElementDatabase(const std::string& jsonPath = "data/elements.json");

    std::optional<Element> findBySymbol(const std::string& symbol) const;
    std::optional<Element> findByZ(int Z) const;

    const std::vector<Element>& all() const { return elements_; }
    std::size_t size() const { return elements_.size(); }

private:
    bool loadFromJson(const std::string& path);
    void loadFallback();

    std::vector<Element> elements_;
};

} // namespace qm