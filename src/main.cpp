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
#include <stdexcept>

static void printUsage(const char* argv0) {
    std::cout << "Usage:\n"
              << "  " << argv0 << "              interactive (asks visual or console)\n"
              << "  " << argv0 << " --visual     OpenGL visualiser\n"
              << "  " << argv0 << " --console    console sampler only\n";
}

static std::string readToken() {
    std::string token;
    if (!(std::cin >> token)) throw std::runtime_error("Input ended before setup was complete");
    return token;
}

static std::optional<int> parseInteger(const std::string& token) {
    try {
        std::size_t consumed = 0;
        int value = std::stoi(token, &consumed);
        if (consumed == token.size()) return value;
    } catch (const std::exception&) {}
    return std::nullopt;
}

static int run(int argc, char** argv) {
    using namespace qm;

    bool forceVisual  = false;
    bool forceConsole = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--visual") forceVisual = true;
        else if (a == "--console") forceConsole = true;
        else if (a == "-h" || a == "--help") { printUsage(argv[0]); return 0; }
        else throw std::invalid_argument("Unknown option: " + a);
    }

    if (forceVisual && forceConsole)
        throw std::invalid_argument("Choose either --visual or --console");

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "========================================================\n"
              << "  Quantum Atom Simulator\n"
              << "  Exact hydrogenic orbitals  |  any element\n"
              << "========================================================\n\n";

    ElementDatabase db;

    std::cout << "Enter element symbol (e.g. C, Fe, Au) or atomic number: ";
    const std::string input = readToken();
    std::optional<Element> elem;
    const auto number = parseInteger(input);
    if (number) {
        if (*number < 1) throw std::invalid_argument("Atomic number must be positive");
        elem = db.findByZ(*number);
    } else {
        if (input.find_first_of("0123456789+-") != std::string::npos)
            throw std::invalid_argument("Atomic number must be a whole integer");
        elem = db.findBySymbol(input);
    }

    if (!elem) {
        std::cout << "Element not found. Enter custom Z: ";
        const auto customZ = parseInteger(readToken());
        if (!customZ || *customZ < 1)
            throw std::invalid_argument("Custom Z must be a positive whole integer");
        elem = Element{*customZ, "X", "Custom", "unknown", 0.0};
    }

    std::cout << "\nSelected: " << elem->name << " (" << elem->symbol
              << ")  Z = " << elem->Z << "\n"
              << "Ground-state configuration: " << elem->config << "\n\n";

    std::cout << "Enter orbital (e.g. 1s, 2p, 3d, 4f): ";
    const std::string orbStr = readToken();

    auto parsed = parseOrbital(orbStr);
    int n = 1, l = 0;
    if (parsed) { n = parsed->first; l = parsed->second; }
    else throw std::invalid_argument("Invalid orbital; use 1s through 7f with l < n");

    int m = 0;
    if (l > 0) {
        std::cout << "Enter m (-" << l << " … +" << l << "): ";
        const auto enteredM = parseInteger(readToken());
        if (!enteredM || *enteredM < -l || *enteredM > l)
            throw std::invalid_argument("m must be a whole integer with |m| <= l");
        m = *enteredM;
    }

    std::cout << "Use pure Z or Slater Zeff? (p/s): ";
    const std::string choice = readToken();
    if (choice != "p" && choice != "P" && choice != "s" && choice != "S")
        throw std::invalid_argument("Screening choice must be p or s");
    bool useSlater = (choice == "s" || choice == "S");

    bool visual = forceVisual;
    if (!forceVisual && !forceConsole) {
#ifdef QM_HAS_OPENGL
        std::cout << "\nMode: (v)isual OpenGL  or  (c)onsole sampler? (v/c): ";
        const std::string mode = readToken();
        if (mode != "v" && mode != "V" && mode != "c" && mode != "C")
            throw std::invalid_argument("Mode must be v or c");
        visual = (mode == "v" || mode == "V");
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

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }
}
