#include "ComputeBackend.hpp"
#include "QuantumMath.hpp"
#include "Constants.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
double radius(const std::array<double,3>& p) { return std::hypot(std::hypot(p[0],p[1]),p[2]); }
}
int main(int argc, char** argv) {
    try {
        const bool gpu = qm::cudaAvailable();
        if (argc > 1 && std::string(argv[1]) == "--cuda" && !gpu) {
            std::cout << "SKIP: no usable CUDA device/compiler\n"; return 77;
        }
        qm::RadialSampler radial(1,0,1);
        qm::AngularSampler angular(0,0);
        qm::Cloud points{{1,2,3}}, sentinel = points;
        if (!gpu) {
            require(!qm::cudaSample(radial,angular,100,42,points), "Missing GPU must fall back");
            require(points == sentinel, "Fallback changed sampling output");
            require(!qm::cudaAdvance(points,1,0.1), "Missing GPU must fall back for flow");
            require(points == sentinel, "Fallback changed flow output");
            std::cout << "CPU fallback contract passed\n";
            for (const auto count : {50000u,500000u,1000000u}) {
                std::mt19937 rng(42); std::uniform_real_distribution<double> phi(0,2*qm::PI);
                qm::Cloud cloud(count);
                const auto start=std::chrono::steady_clock::now();
                for(auto& p:cloud) { double r=radial.sample(rng),t=angular.sample(rng),f=phi(rng);p={r*sin(t)*cos(f),r*cos(t),r*sin(t)*sin(f)}; }
                const auto sampled=std::chrono::steady_clock::now();
                for(auto& p:cloud) qm::advanceProbabilityCurrent(p[0],p[2],1,0.01);
                const auto flowed=std::chrono::steady_clock::now();
                std::cout<<count<<" particles: CPU sample "<<std::chrono::duration<double,std::milli>(sampled-start).count()
                         <<" ms; CPU flow "<<std::chrono::duration<double,std::milli>(flowed-sampled).count()<<" ms\n";
            }
            return 0;
        }
        for (const auto count : {50000u,500000u,1000000u}) {
            const auto start = std::chrono::steady_clock::now();
            require(qm::cudaSample(radial,angular,count,42,points), "CUDA sampling failed");
            const auto sampled = std::chrono::steady_clock::now();
            require(points.size()==count,"Wrong sample count");
            double mean=0, y2=0;
            for (const auto& p:points) { const double r=radius(p); require(std::isfinite(r),"Nonfinite sample"); mean+=r; if(r>0)y2+=p[1]*p[1]/(r*r); }
            require(std::abs(mean/count-1.5)<0.025,"Hydrogen radial mean mismatch");
            require(std::abs(y2/count-1.0/3)<0.015,"Isotropic angular mismatch");
            std::mt19937 rng(42); std::uniform_real_distribution<double> phi(0,2*qm::PI);
            qm::Cloud cpu(count);
            const auto cpuStart=std::chrono::steady_clock::now();
            double cpuMean=0;
            for(auto& p:cpu) {double r=radial.sample(rng),t=angular.sample(rng),f=phi(rng); p={r*sin(t)*cos(f),r*cos(t),r*sin(t)*sin(f)};cpuMean+=r;}
            const auto cpuEnd=std::chrono::steady_clock::now();
            require(std::abs(mean/count-cpuMean/count)<0.03,"CPU/GPU radial statistics mismatch");
            qm::Cloud reference=points;
            for (int m : {-1,0,1}) {
                auto flowed=points;
                require(qm::cudaAdvance(flowed,m,0.01),"CUDA flow failed");
                for(std::size_t i=0;i<flowed.size();++i) {
                    auto expected=reference[i];qm::advanceProbabilityCurrent(expected[0],expected[2],m,0.01);
                    require(flowed[i][1]==expected[1],"Flow changed height");
                    require(std::abs(radius(flowed[i])-radius(expected))<1e-10,"Flow changed radius");
                    require(std::abs(flowed[i][0]-expected[0])<1e-9 && std::abs(flowed[i][2]-expected[2])<1e-9,"CPU/GPU flow mismatch");
                }
            }
            const auto flowStart=std::chrono::steady_clock::now();
            require(qm::cudaAdvance(points,1,0.01),"CUDA flow benchmark failed");
            const auto flowEnd=std::chrono::steady_clock::now();
            for(auto& p:cpu) qm::advanceProbabilityCurrent(p[0],p[2],1,0.01);
            const auto cpuFlowEnd=std::chrono::steady_clock::now();
            auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
            std::cout<<count<<" particles: sample GPU "<<ms(start,sampled)<<" ms, CPU "<<ms(cpuStart,cpuEnd)
                     <<" ms; flow GPU "<<ms(flowStart,flowEnd)<<" ms, CPU "<<ms(flowEnd,cpuFlowEnd)<<" ms (GPU includes allocations/transfers)\n";
        }
        for(const auto state : {std::array<int,3>{2,1,1},std::array<int,3>{3,2,-2},std::array<int,3>{6,0,0}}) {
            qm::RadialSampler r(state[0],state[1],3.7); qm::AngularSampler a(state[1],state[2]);
            require(qm::cudaSample(r,a,100000,99,points),"Excited-state CUDA sampling failed");
            double sum=0,y2=0; for(const auto& p:points){sum+=radius(p);y2+=p[1]*p[1]/(radius(p)*radius(p));}
            const double expected=(3.0*state[0]*state[0]-state[1]*(state[1]+1))/7.4;
            require(std::abs(sum/points.size()-expected)<expected*0.02,"Excited-state radial mean mismatch");
            std::mt19937 rng(99);double cpuY2=0;for(std::size_t i=0;i<points.size();++i){double t=a.sample(rng);cpuY2+=cos(t)*cos(t);}
            require(std::abs(y2/points.size()-cpuY2/points.size())<0.01,"Excited-state angular mismatch");
        }
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
