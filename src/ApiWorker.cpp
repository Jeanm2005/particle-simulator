#include "ApiSession.hpp"
#include <iostream>
#include <string>
int main(int argc, char** argv) {
    if (argc != 1) { std::cerr << "atom_api reads commands from stdin; launch tools/serve.py for the browser UI\n"; return 1; }
    qm::ApiSession session(argv[0]);
    std::string command;
    while (std::getline(std::cin, command)) std::cout << session.execute(command) << '\n' << std::flush;
}
