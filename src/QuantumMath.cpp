#include "QuantumMath.hpp"
#include "Constants.hpp"

#include <cmath>
#include <algorithm>

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

double slaterZeff(int Z, int n, int /*l*/) {
    if (n == 1)
        return Z - 0.30 * (Z > 1 ? 1.0 : 0.0);
    if (n == 2) {
        const double core = std::min(static_cast<double>(Z - 2), 2.0);
        const double same = std::max(0.0, static_cast<double>(Z - 4));
        return Z - 0.85 * core - 0.35 * same;
    }
    return Z - 0.85 * (Z - 2) - 0.35 * std::max(0, Z - 10);
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