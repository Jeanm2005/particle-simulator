#pragma once

#include <vector>
#include <random>

namespace qm {

class RadialSampler {
public:
    RadialSampler() = default;
    RadialSampler(int n, int l, double Z, int nBins = 4096);

    void rebuild(int n, int l, double Z, int nBins = 4096);
    double sample(std::mt19937& gen) const;
    double rMax() const { return rMax_; }
    const std::vector<double>& grid() const { return rGrid_; }
    const std::vector<double>& cdf() const { return cdf_; }
    bool ready() const { return !cdf_.empty(); }

private:
    int n_ = 0, l_ = 0;
    double Z_ = 0.0;
    double rMax_ = 0.0;
    std::vector<double> rGrid_;
    std::vector<double> cdf_;
};

class AngularSampler {
public:
    AngularSampler() = default;
    AngularSampler(int l, int m, int nBins = 2048);

    void rebuild(int l, int m, int nBins = 2048);
    double sample(std::mt19937& gen) const;
    const std::vector<double>& grid() const { return thetaGrid_; }
    const std::vector<double>& cdf() const { return cdf_; }
    bool ready() const { return !cdf_.empty(); }

private:
    int l_ = 0, m_ = 0;
    std::vector<double> thetaGrid_;
    std::vector<double> cdf_;
};

} // namespace qm