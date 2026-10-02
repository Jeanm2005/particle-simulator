#pragma once

#include <string>

namespace qm {

double factorial(int n);
double associatedLaguerre(int k, int alpha, double x);
double associatedLegendre(int l, int m, double x);
double sphericalHarmonicNorm(int l, int m);
double radialWavefunction(int n, int l, double r, double Z);
double radialPdf(int n, int l, double r, double Z);
double angularPdf(int l, int m, double theta);
double hydrogenicEnergy(int n, double Z);
// Configuration must be expanded (e.g. "1s2 2s2 2p2").
// Screening requires an occupied Slater group in this neutral configuration.
double slaterZeff(int Z, int n, int l, const std::string& configuration);

void probabilityCurrentVelocity(double x, double /*y*/, double z, int m,
                                double& vx, double& vy, double& vz);

} // namespace qm