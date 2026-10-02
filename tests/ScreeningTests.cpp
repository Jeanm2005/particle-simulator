#include "Element.hpp"
#include "QuantumMath.hpp"
#include "RadialSampler.hpp"
#include "Simulation.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected) {
    require(std::abs(actual - expected) < 1e-10, "Incorrect screening value");
}
template<class F> void rejects(F operation) {
    bool rejected = false;
    try { operation(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Invalid input was accepted");
}
}

int main() {
    try {
        qm::ElementDatabase database;
        require(database.size() == 119, "Regression test requires full element JSON");
        auto gold = database.findByZ(79).value();
        near(qm::slaterZeff(79, 6, 0, gold.config), 3.7);
        near(qm::slaterZeff(1, 1, 0, "1s1"), 1.0);
        near(qm::slaterZeff(2, 1, 0, "1s2"), 1.7);
        near(qm::slaterZeff(6, 2, 1, "1s2 2s2 2p2"), 3.25);
        const auto iron = database.findByZ(26).value();
        near(qm::slaterZeff(26, 3, 2, iron.config), 6.25);
        near(qm::slaterZeff(26, 4, 0, iron.config), 3.75);

        // Exercise every occupied subshell in the supplied database.
        const std::regex orbital(R"((\d+)([spdfgh])(\d+))");
        for (const auto& element : database.all()) {
            std::istringstream tokens(element.config);
            std::string token;
            while (tokens >> token) {
                std::smatch match;
                require(std::regex_match(token, match, orbital), "Invalid fixture configuration");
                int n = std::stoi(match[1].str());
                int l = static_cast<int>(std::string("spdfgh").find(match[2].str()));
                auto result = qm::Simulation(element, n, l, 0, true, 32).run();
                require(result.Zeff > 0 && std::isfinite(result.energy_eV), "Invalid screened energy");
                require(std::isfinite(result.meanRadius_a0), "Invalid screened radius");
                for (const auto& point : result.points)
                    for (double coordinate : point)
                        require(std::isfinite(coordinate), "Nonfinite sampled position");
            }
        }
        const auto goldCloud = qm::Simulation(gold, 6, 0, 0, true, 50000).run();
        // Hydrogenic mean radius: (3n^2-l(l+1))/(2 Zeff).
        require(std::abs(goldCloud.meanRadius_a0 - 108.0 / 7.4) < 0.15,
                "Gold 6s sampled mean is incorrect");
        const auto hydrogen = database.findByZ(1).value();
        const auto hydrogenCloud = qm::Simulation(hydrogen, 1, 0, 0, false, 50000).run();
        require(std::abs(hydrogenCloud.meanRadius_a0 - 1.5) < 0.02, "Hydrogen regression");

        rejects([] { qm::slaterZeff(79, 6, 0, "unknown"); });
        rejects([] { qm::slaterZeff(2, 1, 0, "1s1"); });
        rejects([] { qm::slaterZeff(1, 2, 1, "1s1"); });
        rejects([] { qm::slaterZeff(1, 1, 0, "1s3"); });
        rejects([] { qm::slaterZeff(1, 1, 1, "1s1"); });
        for (double charge : {0.0, -10.6, std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()})
            rejects([=] { qm::RadialSampler sampler(6, 0, charge); });
        rejects([] { qm::RadialSampler sampler(200, 199, 1, 64); });
        rejects([] { qm::RadialSampler sampler(1, 1, 1); });
        rejects([] { qm::RadialSampler sampler(1, 0, 1, 1); });
        rejects([] { qm::AngularSampler sampler(1, 2); });
        rejects([] { qm::AngularSampler sampler(0, 0, 0); });
        std::mt19937 random(42);
        rejects([&] { qm::RadialSampler sampler; sampler.sample(random); });
        rejects([&] { qm::AngularSampler sampler; sampler.sample(random); });
        qm::RadialSampler sampler(1, 0, 1);
        rejects([&] { sampler.rebuild(1, 0, -1); });
        require(!sampler.ready(), "Failed rebuild left a usable distribution");
        rejects([&] { qm::Simulation(hydrogen, 1, 0, 0, false, 0).run(); });
        std::cout << "Screening and sampling regressions passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
