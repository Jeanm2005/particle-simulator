#include "Constants.hpp"
#include "Orbital.hpp"
#include "QuantumMath.hpp"
#include "Simulation.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    try {
        require(std::abs(qm::PI - 3.141592653589793) < 1e-15, "Incorrect PI");
        require(!qm::parseOrbital("2pjunk"), "Trailing orbital text accepted");
        require(!qm::parseOrbital("10s"), "Unsupported shell accepted");
        require(!qm::parseOrbital("1p"), "Invalid orbital accepted");
        require(qm::parseOrbital("3D").value() == std::make_pair(3, 2), "Valid orbital rejected");

        // A quarter-turn and its inverse fix the orientation and sign convention.
        double x = 2.0, z = 0.0;
        qm::advanceProbabilityCurrent(x, z, 1, 2.0 * qm::PI);
        require(std::abs(x) < 1e-12 && std::abs(z - 2) < 1e-12, "Wrong current direction");
        qm::advanceProbabilityCurrent(x, z, -1, 2.0 * qm::PI);
        require(std::abs(x - 2) < 1e-12 && std::abs(z) < 1e-12, "Current not reversible");
        x = 20; z = 4;
        const double initialRadius = std::hypot(x, z);
        for (int step = 0; step < 100000; ++step)
            qm::advanceProbabilityCurrent(x, z, 2, 0.1);
        require(std::abs(std::hypot(x, z) - initialRadius) < 1e-8, "Flow changes radial distribution");
        double oldX = x, oldZ = z;
        qm::advanceProbabilityCurrent(x, z, 0, 100);
        require(x == oldX && z == oldZ, "m=0 particles moved");
        x = z = 0;
        qm::advanceProbabilityCurrent(x, z, 1, 1);
        require(x == 0 && z == 0, "Axis update is singular");

        // For 2p, m=0 the polar-axis second moment is three times either transverse moment.
        auto cloud = qm::Simulation({1, "H", "Hydrogen", "1s1", 1.008},
                                    2, 1, 0, false, 50000).run();
        double xx = 0, yy = 0, zz = 0;
        for (const auto& p : cloud.points) {
            xx += p[0] * p[0]; yy += p[1] * p[1]; zz += p[2] * p[2];
        }
        require(std::abs(yy / xx - 3.0) < 0.12 && std::abs(yy / zz - 3.0) < 0.12,
                "Console cloud does not use Y as polar axis");
        std::cout << "Physics regressions passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
