#pragma once

#include "Element.hpp"
#include "Orbital.hpp"
#include "RadialSampler.hpp"
#include "ComputeBackend.hpp"

#include <vector>
#include <array>
#include <string>
#include <random>

namespace qm {

struct SimulationResult {
    QuantumNumbers qn;
    double         Zeff;
    double         energy_eV;
    double         meanRadius_a0;
    std::vector<std::array<double,3>> points;
    ComputeExecution execution;
};

class Simulation {
public:
    Simulation(const Element& element,
               int n, int l, int m,
               bool useSlater = false,
               int nSamples = 50000,
               std::uint32_t seed = 42,
               BackendPreference backend = BackendPreference::Auto);

    SimulationResult run();

    // Throws if the output cannot be opened or completely written.
    void writeCloudXYZ(const SimulationResult& result,
                       const std::string& filename) const;

    void printRadialHistogram(const SimulationResult& result,
                              int bins = 40) const;

private:
    Element element_;
    int n_, l_, m_;
    bool useSlater_;
    int nSamples_;
    mutable std::mt19937 gen_;
    BackendPreference backend_;
};

} // namespace qm
