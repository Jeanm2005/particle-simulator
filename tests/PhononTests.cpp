#include "PhononModel.hpp"
#include "OrbitalModel.hpp"
#include "Constants.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void fails(F fn, const char* field) {
    try { fn(); } catch (const qm::ModelError& error) {
        require(error.field == field, "Unexpected lattice error field"); return;
    }
    throw std::runtime_error("Expected lattice rejection");
}
using Matrix = std::vector<std::vector<double>>;
// Cyclic Jacobi eigenvalues of a real symmetric matrix (independent of the analytic dispersion).
std::vector<double> eigenvalues(Matrix a) {
    const std::size_t n = a.size();
    for (int sweep = 0; sweep < 100; ++sweep) {
        double off = 0;
        for (std::size_t p = 0; p < n; ++p) for (std::size_t q = p + 1; q < n; ++q) off += a[p][q] * a[p][q];
        if (off < 1e-26) break;
        for (std::size_t p = 0; p < n; ++p)
            for (std::size_t q = p + 1; q < n; ++q) {
                if (std::abs(a[p][q]) < 1e-300) continue;
                const double theta = (a[q][q] - a[p][p]) / (2 * a[p][q]);
                const double t = (theta >= 0 ? 1 : -1) / (std::abs(theta) + std::sqrt(theta * theta + 1));
                const double c = 1 / std::sqrt(t * t + 1), s = t * c;
                for (std::size_t k = 0; k < n; ++k) {
                    const double akp = a[k][p], akq = a[k][q];
                    a[k][p] = c * akp - s * akq; a[k][q] = s * akp + c * akq;
                }
                for (std::size_t k = 0; k < n; ++k) {
                    const double apk = a[p][k], aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk; a[q][k] = s * apk + c * aqk;
                }
            }
    }
    std::vector<double> values(n);
    for (std::size_t i = 0; i < n; ++i) values[i] = a[i][i];
    std::sort(values.begin(), values.end());
    return values;
}
// Mass-weighted 2N x 2N dynamical matrix of the periodic chain, built from its bonds.
Matrix dynamical(int cells, double m2) {
    const int n = 2 * cells;
    Matrix d(n, std::vector<double>(n, 0.0));
    auto mass = [&](int i) { return i % 2 ? m2 : 1.0; };
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n; // each atom bonds to the next atom along the ring
        d[i][i] += 1 / mass(i); d[j][j] += 1 / mass(j);
        d[i][j] -= 1 / std::sqrt(mass(i) * mass(j)); d[j][i] = d[i][j];
    }
    return d;
}
// Velocity Verlet for the full chain; returns energy drift.
double integrate(std::vector<double>& u, std::vector<double> v, double m2, double dt, int steps) {
    const std::size_t n = u.size();
    auto forces = [&](const std::vector<double>& x) {
        std::vector<double> f(n);
        for (std::size_t i = 0; i < n; ++i) f[i] = x[(i + 1) % n] + x[(i + n - 1) % n] - 2 * x[i];
        return f;
    };
    auto mass = [&](std::size_t i) { return i % 2 ? m2 : 1.0; };
    auto energy = [&](const std::vector<double>& x, const std::vector<double>& y) {
        double e = 0;
        for (std::size_t i = 0; i < n; ++i) { e += 0.5 * mass(i) * y[i] * y[i]; const double s = x[(i + 1) % n] - x[i]; e += 0.5 * s * s; }
        return e;
    };
    const double start = energy(u, v);
    auto f = forces(u);
    for (int step = 0; step < steps; ++step) {
        for (std::size_t i = 0; i < n; ++i) { v[i] += 0.5 * dt * f[i] / mass(i); u[i] += dt * v[i]; }
        f = forces(u);
        for (std::size_t i = 0; i < n; ++i) v[i] += 0.5 * dt * f[i] / mass(i);
    }
    return std::abs(energy(u, v) - start) / start;
}
}

int main() {
    try {
        using qm::PhononBranch;
        // 1. Analytic dispersion equals every eigenfrequency of the full chain.
        for (int cells : {2, 8, 13}) {
            for (double m2 : {0.4, 1.0, 2.5}) {
                const auto lambda = eigenvalues(dynamical(cells, m2));
                std::vector<double> analytic;
                for (int j = -(cells - 1) / 2; 2 * j <= cells; ++j)
                    for (auto branch : {PhononBranch::Acoustic, PhononBranch::Optical}) {
                        const double w = qm::PhononModel::frequency(2 * qm::PI * j / cells, m2, branch);
                        analytic.push_back(w * w);
                    }
                std::sort(analytic.begin(), analytic.end());
                require(analytic.size() == lambda.size(), "Mode count differs from 2N");
                for (std::size_t i = 0; i < lambda.size(); ++i)
                    require(std::abs(analytic[i] - lambda[i]) < 1e-9, "Dispersion disagrees with dynamical-matrix eigenvalues");
            }
        }
        // 2. Equal masses fold the monatomic chain (spacing 1/2): 2|sin(k/4)| and 2|cos(k/4)|.
        for (double k : {-3.0, -1.1, 0.0, 0.4, 2.2, qm::PI}) {
            require(std::abs(qm::PhononModel::frequency(k, 1.0, PhononBranch::Acoustic) - 2 * std::abs(std::sin(k / 4))) < 1e-7, "Monatomic acoustic limit");
            require(std::abs(qm::PhononModel::frequency(k, 1.0, PhononBranch::Optical) - 2 * std::abs(std::cos(k / 4))) < 1e-7, "Monatomic optical limit");
        }
        // 3. Band gap edges: sqrt(2/m_heavy), sqrt(2/m_light); optical top sqrt(2(1/m1 + 1/m2)).
        require(std::abs(qm::PhononModel::frequency(qm::PI, 2.5, PhononBranch::Acoustic) - std::sqrt(2 / 2.5)) < 1e-12, "Acoustic band edge");
        require(std::abs(qm::PhononModel::frequency(qm::PI, 2.5, PhononBranch::Optical) - std::sqrt(2.0)) < 1e-12, "Optical band edge");
        require(std::abs(qm::PhononModel::frequency(0, 2.5, PhononBranch::Optical) - std::sqrt(2 * (1 + 1 / 2.5))) < 1e-12, "Optical maximum");
        // 4. Group velocity: analytic vs central differences; long-wavelength sound speed.
        for (double m2 : {0.3, 2.5}) {
            const double c = std::sqrt(1 / (2 * (1 + m2)));
            require(std::abs(qm::PhononModel::mode(0, m2, PhononBranch::Acoustic).groupVelocity - c) < 1e-6, "Sound speed");
            for (double k : {-2.7, -0.9, 0.3, 1.7, 2.9})
                for (auto branch : {PhononBranch::Acoustic, PhononBranch::Optical}) {
                    const double h = 1e-6;
                    const double numeric = (qm::PhononModel::frequency(k + h, m2, branch) - qm::PhononModel::frequency(k - h, m2, branch)) / (2 * h);
                    require(std::abs(qm::PhononModel::mode(k, m2, branch).groupVelocity - numeric) < 1e-6, "Group velocity mismatch");
                }
        }
        require(std::abs(qm::PhononModel::mode(qm::PI, 2.5, PhononBranch::Optical).groupVelocity) < 1e-9, "Band edge group velocity");
        // 5. Integrating the full chain from the exact mode reproduces the analytic motion.
        for (auto request : {qm::PhononRequest{1, "lattice-phonons", 16, 2.5, PhononBranch::Optical, 3, 0.1},
                             qm::PhononRequest{1, "lattice-phonons", 16, 0.4, PhononBranch::Acoustic, -5, 0.2},
                             qm::PhononRequest{1, "lattice-phonons", 12, 1.0, PhononBranch::Acoustic, 6, 0.1},
                             qm::PhononRequest{1, "lattice-phonons", 10, 3.0, PhononBranch::Optical, 5, 0.1}}) {
            auto u = qm::PhononModel::displacements(request, 0);
            const auto v = qm::PhononModel::velocities(request, 0);
            const double omega = qm::PhononModel::mode(2 * qm::PI * request.modeIndex / request.cells, request.massRatio, request.branch).angularFrequency;
            const double dt = 1e-3, time = 3 * 2 * qm::PI / omega;
            const int steps = static_cast<int>(std::lround(time / dt));
            const double drift = integrate(u, v, request.massRatio, dt, steps);
            const auto exact = qm::PhononModel::displacements(request, steps * dt);
            double error = 0, largest = 0;
            for (std::size_t i = 0; i < u.size(); ++i) { error = std::max(error, std::abs(u[i] - exact[i])); largest = std::max(largest, std::abs(exact[i])); }
            require(largest <= request.amplitude + 1e-12 && largest > 0.5 * request.amplitude, "Amplitude normalization");
            require(error < 1e-4 * request.amplitude, "Verlet trajectory departs from the analytic mode");
            require(drift < 1e-6, "Energy not conserved");
        }
        // 6. Model results, preview mapping, time advance and validation.
        qm::PhononModel model;
        qm::PhononRequest request;
        auto result = model.run(request);
        require(result.points.size() == 64 && result.dispersion.size() == 129, "Wrong output sizes");
        double largest = 0;
        for (double u : result.displacements) largest = std::max(largest, std::abs(u));
        // Discrete atoms sample the wave envelope, so the largest |u| is within cos(k/2) of the amplitude.
        require(largest <= 0.1 + 1e-12 && largest >= 0.1 * std::cos(qm::PI / 32) - 1e-12, "Largest displacement is not the amplitude");
        require(std::abs(result.points[3][1] - result.displayGain * result.displacements[3]) < 1e-12, "Display gain mismatch");
        require(std::abs(result.acousticMax - 1.0) < 1e-12 && std::abs(result.opticalMin - std::sqrt(2.0)) < 1e-12, "Band gap report");
        model.advance(result, 2.5);
        require(result.timeReduced == 2.5 && result.displacements == qm::PhononModel::displacements(request, 2.5), "Advance mismatch");
        auto invalid = request; invalid.cells = 1; fails([&] { model.run(invalid); }, "lattice.cells");
        invalid = request; invalid.cells = 257; fails([&] { model.run(invalid); }, "lattice.cells");
        invalid = request; invalid.massRatio = 0; fails([&] { model.run(invalid); }, "lattice.massRatio");
        invalid = request; invalid.massRatio = std::nan(""); fails([&] { model.run(invalid); }, "lattice.massRatio");
        invalid = request; invalid.modeIndex = -16; fails([&] { model.run(invalid); }, "lattice.modeIndex");
        invalid = request; invalid.modeIndex = 17; fails([&] { model.run(invalid); }, "lattice.modeIndex");
        invalid = request; invalid.amplitude = 0.3; fails([&] { model.run(invalid); }, "lattice.amplitude");
        invalid = request; invalid.branch = static_cast<PhononBranch>(7); fails([&] { model.run(invalid); }, "lattice.branch");
        invalid = request; invalid.model = "hydrogenic-orbital"; fails([&] { model.run(invalid); }, "model");
        fails([&] { model.advance(result, std::numeric_limits<double>::infinity()); }, "dt");
        std::cout << "Phonon dispersion, eigenvalue, group-velocity, Verlet and validation checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
