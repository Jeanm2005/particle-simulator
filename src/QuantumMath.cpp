#include "QuantumMath.hpp"
#include "Constants.hpp"

#include <cmath>
#include <algorithm>
#include <sstream>
#include <regex>
#include <map>
#include <stdexcept>

namespace qm {

double factorial(int n) {
    if (n < 0) return 0.0;
    double f = 1.0;
    for (int i = 2; i <= n; ++i)
        f *= i;
    return f;
}

double associatedLaguerre(int k, int alpha, double x) {
    if (k < 0) return 0.0;
    if (k == 0) return 1.0;

    double Lm2 = 1.0;
    double Lm1 = 1.0 + alpha - x;
    if (k == 1) return Lm1;

    double L = 0.0;
    for (int j = 2; j <= k; ++j) {
        L = ((2 * j - 1 + alpha - x) * Lm1 - (j - 1 + alpha) * Lm2) / j;
        Lm2 = Lm1;
        Lm1 = L;
    }
    return L;
}

double associatedLegendre(int l, int m, double x) {
    m = std::abs(m);
    if (m > l) return 0.0;

    double pmm = 1.0;
    if (m > 0) {
        double somx2 = std::sqrt(std::max(0.0, (1.0 - x) * (1.0 + x)));
        double fact  = 1.0;
        for (int i = 1; i <= m; ++i) {
            pmm *= -fact * somx2;
            fact += 2.0;
        }
    }
    if (l == m) return pmm;

    double pmmp1 = x * (2 * m + 1) * pmm;
    if (l == m + 1) return pmmp1;

    double pll = 0.0;
    for (int ll = m + 2; ll <= l; ++ll) {
        pll = ((2 * ll - 1) * x * pmmp1 - (ll + m - 1) * pmm) / (ll - m);
        pmm   = pmmp1;
        pmmp1 = pll;
    }
    return pll;
}

double sphericalHarmonicNorm(int l, int m) {
    m = std::abs(m);
    const double num = (2.0 * l + 1.0) * std::tgamma(l - m + 1.0);
    const double den = 4.0 * PI * std::tgamma(l + m + 1.0);
    return std::sqrt(num / den);
}

double radialWavefunction(int n, int l, double r, double Z) {
    if (n <= l || n < 1 || l < 0 || r < 0.0) return 0.0;

    const double rho = 2.0 * Z * r / n;

    const double norm = std::sqrt(
        std::pow(2.0 * Z / n, 3.0)
        * std::tgamma(n - l)
        / (2.0 * n * std::tgamma(n + l + 1.0))
    );

    const double lag = associatedLaguerre(n - l - 1, 2 * l + 1, rho);
    return norm * std::exp(-rho / 2.0) * std::pow(rho, l) * lag;
}

double radialPdf(int n, int l, double r, double Z) {
    const double R = radialWavefunction(n, l, r, Z);
    return R * R * r * r;
}

double angularPdf(int l, int m, double theta) {
    const double x   = std::cos(theta);
    const double Plm = associatedLegendre(l, m, x);
    const double N   = sphericalHarmonicNorm(l, m);
    const double y   = N * Plm;
    return y * y;
}

double hydrogenicEnergy(int n, double Z) {
    return -RYDBERG_EV * Z * Z / (n * n);
}

double slaterZeff(int Z, int n, int l, const std::string& configuration) {
    if (Z < 1 || n < 1 || l < 0 || l >= n)
        throw std::invalid_argument("Invalid atomic number or quantum numbers for screening");

    // Slater groups combine ns and np; nd, nf, ... are separate groups.
    std::map<std::pair<int, int>, int> groups;
    std::istringstream input(configuration);
    const std::regex orbital(R"((\d+)([spdfgh])(\d+))");
    std::string token;
    int electrons = 0;
    while (input >> token) {
        std::smatch match;
        if (!std::regex_match(token, match, orbital))
            throw std::invalid_argument("Screening requires an expanded electron configuration; use pure Z instead");
        int shell = std::stoi(match[1].str());
        int angular = static_cast<int>(std::string("spdfgh").find(match[2].str()));
        int count = std::stoi(match[3].str());
        if (shell < 1 || shell > 100 || angular >= shell || count < 1 || count > 2 * (2 * angular + 1))
            throw std::invalid_argument("Invalid electron configuration for screening");
        groups[{shell, angular <= 1 ? 0 : angular}] += count;
        electrons += count;
    }
    if (electrons != Z)
        throw std::invalid_argument("Screening requires a neutral electron configuration matching Z");

    const std::pair<int, int> target{n, l <= 1 ? 0 : l};
    if (groups.find(target) == groups.end())
        throw std::invalid_argument("Selected Slater group is unoccupied; use pure Z for this orbital");

    double shielding = 0.0;
    for (const auto& entry : groups) {
        const int shell = entry.first.first;
        int count = entry.second;
        if (entry.first == target) {
            shielding += (count - 1) * (n == 1 ? 0.30 : 0.35);
        } else if (l <= 1) {
            if (shell == n - 1) shielding += 0.85 * count;
            else if (shell < n - 1) shielding += count;
        } else if (entry.first < target) {
            shielding += count;
        }
    }
    const double effective = Z - shielding;
    if (!std::isfinite(effective) || effective <= 0.0)
        throw std::domain_error("Screening produced a nonpositive or nonfinite effective charge");
    return effective;
}

void probabilityCurrentVelocity(double x, double /*y*/, double z, int m,
                                double& vx, double& vy, double& vz) {
    if (m == 0) {
        vx = vy = vz = 0.0;
        return;
    }
    const double rho = std::sqrt(x * x + z * z);
    if (rho < 1e-8) {
        vx = vy = vz = 0.0;
        return;
    }
    const double vmag = static_cast<double>(m) / rho;
    vx = -vmag * (z / rho);
    vy = 0.0;
    vz =  vmag * (x / rho);
}

} // namespace qm