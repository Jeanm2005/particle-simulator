#include "RadialSampler.hpp"
#include "QuantumMath.hpp"
#include "Constants.hpp"

#include <algorithm>
#include <cmath>

namespace qm {

RadialSampler::RadialSampler(int n, int l, double Z, int nBins) {
    rebuild(n, l, Z, nBins);
}

void RadialSampler::rebuild(int n, int l, double Z, int nBins) {
    n_ = n; l_ = l; Z_ = Z;
    rMax_ = 15.0 * n * n / std::max(Z, 0.5);

    rGrid_.assign(nBins, 0.0);
    cdf_.assign(nBins, 0.0);

    const double dr = rMax_ / (nBins - 1);
    double sum = 0.0;

    for (int i = 0; i < nBins; ++i) {
        const double r = i * dr;
        rGrid_[i] = r;
        sum += radialPdf(n, l, r, Z) * dr;
        cdf_[i] = sum;
    }
    if (sum > 0.0)
        for (auto& c : cdf_) c /= sum;
}

double RadialSampler::sample(std::mt19937& gen) const {
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    const double u = uni(gen);
    auto it = std::lower_bound(cdf_.begin(), cdf_.end(), u);
    size_t idx = std::distance(cdf_.begin(), it);
    if (idx >= rGrid_.size()) idx = rGrid_.size() - 1;
    return rGrid_[idx];
}

AngularSampler::AngularSampler(int l, int m, int nBins) {
    rebuild(l, m, nBins);
}

void AngularSampler::rebuild(int l, int m, int nBins) {
    l_ = l; m_ = m;
    thetaGrid_.assign(nBins, 0.0);
    cdf_.assign(nBins, 0.0);

    const double dtheta = PI / (nBins - 1);
    double sum = 0.0;

    for (int i = 0; i < nBins; ++i) {
        const double theta = i * dtheta;
        thetaGrid_[i] = theta;
        sum += angularPdf(l, m, theta) * std::sin(theta) * dtheta;
        cdf_[i] = sum;
    }
    if (sum > 0.0)
        for (auto& c : cdf_) c /= sum;
}

double AngularSampler::sample(std::mt19937& gen) const {
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    const double u = uni(gen);
    auto it = std::lower_bound(cdf_.begin(), cdf_.end(), u);
    size_t idx = std::distance(cdf_.begin(), it);
    if (idx >= thetaGrid_.size()) idx = thetaGrid_.size() - 1;
    return thetaGrid_[idx];
}

} // namespace qm