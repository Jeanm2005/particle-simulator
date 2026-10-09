#include "Superposition.hpp"
#include "Constants.hpp"
#include "QuantumMath.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
double meanY(const qm::SuperpositionResult& r) {
    double sum = 0; for (const auto& p : r.orbital.observables.points) sum += p[1];
    return sum / r.orbital.actualSampleCount;
}
template<class F> void fails(F fn, const char* code, const char* field) {
    try { fn(); } catch (const qm::ModelError& error) {
        require(error.code == code && error.field == field, "Unexpected superposition error"); return;
    }
    throw std::runtime_error("Expected superposition rejection");
}
}
int main() {
    try {
        qm::ElementDatabase elements;
        qm::SuperpositionModel model(elements);
        qm::SuperpositionRequest request;
        request.orbital.model = "orbital-superposition";
        request.orbital.sampleCount = 20000;
        request.orbital.backend = qm::BackendPreference::Cpu;
        const double beatPeriod = 2 * qm::PI / 0.375;
        const std::array<double, 3> point{0, 1, 0};
        const auto a = qm::orbitalAmplitude({1,0,0}, 1, point);
        const auto b = qm::orbitalAmplitude({2,1,0}, 1, point);
        const double rho0 = std::norm(qm::superpositionAmplitude(request, 0, point));
        require(std::abs(rho0 - std::norm((a+b)/std::sqrt(2.0))) < 1e-14, "Missing coherent cross term");
        const double quarter = std::norm(qm::superpositionAmplitude(request, beatPeriod/4, point));
        require(std::abs(quarter - (std::norm(a)+std::norm(b))/2) < 1e-14, "Incorrect energy phase evolution");
        require(std::abs(rho0 - std::norm(qm::superpositionAmplitude(request, beatPeriod, point))) < 1e-14, "Wrong beat period");
        auto shifted = request; shifted.relativePhase = qm::PI;
        require(std::abs(std::norm(qm::superpositionAmplitude(shifted, 0, point)) - std::norm(qm::superpositionAmplitude(request, beatPeriod/2, point))) < 1e-14, "Relative phase ignored");
        auto degenerate = request; degenerate.orbital.state = {2,1,-1}; degenerate.second = {2,1,1};
        require(std::abs(std::norm(qm::superpositionAmplitude(degenerate, 0, {1,2,3})) - std::norm(qm::superpositionAmplitude(degenerate, 12, {1,2,3}))) < 1e-14, "Degenerate state density evolved");
        const auto positive = qm::orbitalAmplitude({2,1,1}, 1, {1,2,3});
        require(std::abs(qm::orbitalAmplitude({2,1,-1}, 1, {1,2,3}) + std::conj(positive)) < 1e-14, "Negative-m harmonic phase convention is wrong");

        // Independent midpoint volume quadrature with analytic azimuth integration for m=0.
        for (double time : {0.0, beatPeriod/4, beatPeriod/2}) {
            double probability = 0;
            const double dr = 40.0/800, du = 2.0/160;
            for (int i=0;i<800;++i) {
                const double r=(i+0.5)*dr;
                for(int j=0;j<160;++j) {
                    const double u=-1+(j+0.5)*du;
                    probability += std::norm(qm::superpositionAmplitude(request,time,{r*std::sqrt(1-u*u),r*u,0})) * r*r*dr*du*2*qm::PI;
                }
            }
            require(std::abs(probability-1) < 0.001, "Time evolution lost normalization");
        }
        auto first = model.run(request);
        require(first.orbital.observables.points == model.run(request).orbital.observables.points, "Superposition seed not repeatable");
        require(std::abs(first.orbital.observables.energy_eV - (-0.3125*qm::HARTREE_TO_EV)) < 1e-12, "Wrong energy expectation");
        const double expectedY = 256.0/(243*std::sqrt(2.0));
        require(std::abs(meanY(first)-expectedY)<0.08, "Initial coherent dipole disagrees with analytic matrix element");
        require(std::abs(first.orbital.observables.meanRadius_a0-3.25)<0.09, "Superposition radial mean mismatch");
        model.advance(first, beatPeriod/2);
        require(std::abs(meanY(first)+expectedY)<0.08, "Dipole did not reverse at half beat");
        auto before=first.orbital.observables.points;
        fails([&]{model.advance(first, std::numeric_limits<double>::infinity());},"invalid-input","dt");
        require(before==first.orbital.observables.points,"Invalid time changed cloud");
        fails([&]{model.advance(first, 1e7);},"invalid-input","dt");
        auto invalid=request; invalid.second=request.orbital.state;
        fails([&]{model.run(invalid);},"invalid-input","superposition.state");
        invalid=request; invalid.orbital.screening=qm::ScreeningMode::SlaterNeutral;
        fails([&]{model.run(invalid);},"invalid-input","screening");
        invalid=request; invalid.secondWeight=1.1;
        fails([&]{model.run(invalid);},"invalid-input","superposition.weight");
        invalid=request; invalid.relativePhase=std::numeric_limits<double>::quiet_NaN();
        fails([&]{model.run(invalid);},"invalid-input","superposition.phase");
        invalid=request; invalid.second={5,1,0};
        fails([&]{model.run(invalid);},"invalid-input","superposition.state.n");
        invalid=request; invalid.orbital.sampleCount=20001;
        fails([&]{model.run(invalid);},"resource-limit","sampleCount");
        for(double weight:{0.0,1.0}) {
            auto endpoint=request; endpoint.secondWeight=weight;
            auto state=model.run(endpoint);
            require(std::abs(state.orbital.observables.meanRadius_a0-(weight==0?1.5:5.0))<0.06,"Pure-state endpoint failed");
        }
        std::cout << "Complex harmonics, normalization, coherent phase, dipole and sampling checks passed\n";
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
