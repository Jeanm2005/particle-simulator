#include "Simulation.hpp"
#include "QuantumMath.hpp"
#include "Constants.hpp"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace qm {

Simulation::Simulation(const Element& element,
                       int n, int l, int m,
                       bool useSlater,
                       int nSamples)
    : element_(element)
    , n_(n), l_(l), m_(m)
    , useSlater_(useSlater)
    , nSamples_(nSamples)
    , gen_(42)
{}

SimulationResult Simulation::run() {
    if (element_.Z < 1 || nSamples_ <= 0)
        throw std::invalid_argument("Atomic number and sample count must be positive");
    SimulationResult result;
    result.qn = {n_, l_, m_};

    result.Zeff = useSlater_
                  ? slaterZeff(element_.Z, n_, l_, element_.config)
                  : static_cast<double>(element_.Z);

    result.energy_eV = hydrogenicEnergy(n_, result.Zeff);
    if (!std::isfinite(result.energy_eV))
        throw std::domain_error("Nonfinite orbital energy");

    RadialSampler  radial(n_, l_, result.Zeff);
    AngularSampler angular(l_, m_);

    std::uniform_real_distribution<double> phiDist(0.0, 2.0 * PI);

    result.points.reserve(nSamples_);
    double sumR = 0.0;

    for (int i = 0; i < nSamples_; ++i) {
        const double r     = radial.sample(gen_);
        const double theta = angular.sample(gen_);
        const double phi   = phiDist(gen_);

        const double x = r * std::sin(theta) * std::cos(phi);
        const double y = r * std::cos(theta);
        const double z = r * std::sin(theta) * std::sin(phi);

        result.points.push_back({x, y, z});
        sumR += r;
    }

    result.meanRadius_a0 = sumR / nSamples_;
    if (!std::isfinite(result.meanRadius_a0))
        throw std::domain_error("Nonfinite sampled radius");
    return result;
}

void Simulation::writeCloudXYZ(const SimulationResult& result,
                               const std::string& filename) const {
    std::ofstream out(filename);
    if (!out) {
        std::cerr << "Failed to open " << filename << " for writing\n";
        return;
    }

    out << result.points.size() << "\n";
    out << "Electron probability cloud for " << element_.symbol
        << " " << orbitalName(result.qn.n, result.qn.l)
        << "  (Z_eff=" << result.Zeff << ")\n";

    out << std::fixed << std::setprecision(6);
    for (const auto& p : result.points)
        out << "e  " << p[0] << "  " << p[1] << "  " << p[2] << "\n";
}

void Simulation::printRadialHistogram(const SimulationResult& result,
                                      int bins) const {
    if (result.points.empty()) return;

    double rMax = 0.0;
    for (const auto& p : result.points) {
        const double r = std::sqrt(p[0]*p[0] + p[1]*p[1] + p[2]*p[2]);
        rMax = std::max(rMax, r);
    }
    rMax *= 1.05;

    std::vector<int> hist(bins, 0);
    const double dr = rMax / bins;

    for (const auto& p : result.points) {
        const double r = std::sqrt(p[0]*p[0] + p[1]*p[1] + p[2]*p[2]);
        int bin = std::min(bins - 1, static_cast<int>(r / dr));
        hist[bin]++;
    }

    const int maxH = *std::max_element(hist.begin(), hist.end());
    if (maxH == 0) return;

    std::cout << "\nRadial probability histogram (r in a0):\n";
    for (int i = 0; i < bins; ++i) {
        const int stars = static_cast<int>(40.0 * hist[i] / maxH);
        std::cout << std::setw(7) << std::setprecision(2) << (i + 0.5) * dr
                  << " | " << std::string(stars, '*') << "\n";
    }
}

} // namespace qm