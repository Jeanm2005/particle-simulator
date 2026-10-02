#include "Element.hpp"
#include "Orbital.hpp"
#include "Simulation.hpp"
#include "Constants.hpp"

#ifdef QM_HAS_OPENGL
#include "Engine.hpp"
#endif

#include <iostream>
#include <iomanip>
#include <string>
#include <cstdlib>

static void printUsage(const char* argv0) {
    std::cout << "Usage:\n"
              << "  " << argv0 << "              interactive (asks visual or console)\n"
              << "  " << argv0 << " --visual     OpenGL visualiser\n"
              << "  " << argv0 << " --console    console sampler only\n";
}

int main(int argc, char** argv) {
    using namespace qm;

    bool forceVisual  = false;
    bool forceConsole = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--visual")  forceVisual  = true;
        if (a == "--console") forceConsole = true;
        if (a == "-h" || a == "--help") { printUsage(argv[0]); return 0; }
    }

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "========================================================\n"
              << "  Quantum Atom Simulator\n"
              << "  Exact hydrogenic orbitals  |  any element\n"
              << "========================================================\n\n";

    ElementDatabase db;

    std::cout << "Enter element symbol (e.g. C, Fe, Au) or atomic number: ";
    std::string input;
    std::cin >> input;

    std::optional<Element> elem;
    try {
        int Z = std::stoi(input);
        elem = db.findByZ(Z);
    } catch (...) {
        elem = db.findBySymbol(input);
    }

    if (!elem) {
        std::cout << "Element not found. Enter custom Z: ";
        int Z = 0;
        std::cin >> Z;
        if (Z < 1) { std::cerr << "Invalid Z\n"; return 1; }
        elem = Element{Z, "X", "Custom", "unknown", 0.0};
    }

    std::cout << "\nSelected: " << elem->name << " (" << elem->symbol
              << ")  Z = " << elem->Z << "\n"
              << "Ground-state configuration: " << elem->config << "\n\n";

    std::cout << "Enter orbital (e.g. 1s, 2p, 3d, 4f): ";
    std::string orbStr;
    std::cin >> orbStr;

    auto parsed = parseOrbital(orbStr);
    int n = 1, l = 0;
    if (parsed) { n = parsed->first; l = parsed->second; }
    else std::cerr << "Could not parse orbital – using 1s.\n";

    int m = 0;
    if (l > 0) {
        std::cout << "Enter m (-" << l << " … +" << l << "): ";
        std::cin >> m;
        if (std::abs(m) > l) { std::cerr << "Invalid m – using 0.\n"; m = 0; }
    }

    std::cout << "Use pure Z or Slater Zeff? (p/s) [p]: ";
    char choice = 'p';
    std::cin >> choice;
    bool useSlater = (choice == 's' || choice == 'S');

    bool visual = forceVisual;
    if (!forceVisual && !forceConsole) {
#ifdef QM_HAS_OPENGL
        std::cout << "\nMode: (v)isual OpenGL  or  (c)onsole sampler? [v]: ";
        char mode = 'v';
        std::cin >> mode;
        visual = !(mode == 'c' || mode == 'C');
#else
        visual = false;
        std::cout << "\n(OpenGL support not compiled – console mode)\n";
#endif
    }

#ifdef QM_HAS_OPENGL
    if (visual) {
        try {
            Engine engine(1280, 720);
            engine.run(*elem, n, l, m, useSlater);
        } catch (const std::exception& ex) {
            std::cerr << "Visualiser error: " << ex.what() << "\n";
            return 1;
        }
        return 0;
    }
#else
    if (visual) {
        std::cerr << "This binary was built without OpenGL (install glfw/glew/glm and rebuild).\n";
        return 1;
    }
#endif

    const int N_SAMPLES = 50000;
    Simulation sim(*elem, n, l, m, useSlater, N_SAMPLES);
    SimulationResult result;
    try {
        result = sim.run();
    } catch (const std::exception& ex) {
        std::cerr << "Simulation error: " << ex.what() << "\n";
        return 1;
    }

    std::cout << "\n--- Results ---\n"
              << "Quantum numbers : n=" << result.qn.n
              << ", l=" << result.qn.l << ", m=" << result.qn.m << "\n"
              << "Z_eff           : " << result.Zeff << "\n"
              << "Hydrogenic energy E_n = " << result.energy_eV << " eV\n\n"
              << "Sampled " << result.points.size() << " positions from |ψ|²\n"
              << "Mean radial distance ⟨r⟩ ≈ " << result.meanRadius_a0
              << " a0  (" << result.meanRadius_a0 * BOHR_RADIUS_ANG << " Å)\n";

    std::string filename = elem->symbol + "_" + orbitalName(n, l) + "_cloud.xyz";
    sim.writeCloudXYZ(result, filename);
    std::cout << "\nWrote probability cloud to: " << filename << "\n";
    sim.printRadialHistogram(result);

    return 0;
}