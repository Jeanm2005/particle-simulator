#include "Orbital.hpp"

#include <cctype>
#include <string>

namespace qm {

std::optional<std::pair<int,int>> parseOrbital(const std::string& s) {
    if (s.size() < 2) return std::nullopt;

    const int n = s[0] - '0';
    if (n < 1 || n > 7) return std::nullopt;

    const char L = static_cast<char>(std::tolower(static_cast<unsigned char>(s[1])));
    int l = -1;
    if      (L == 's') l = 0;
    else if (L == 'p') l = 1;
    else if (L == 'd') l = 2;
    else if (L == 'f') l = 3;
    else return std::nullopt;

    if (l >= n) return std::nullopt;
    return std::make_pair(n, l);
}

std::string orbitalName(int n, int l) {
    static const char* letters = "spdf";
    if (l < 0 || l > 3) return std::to_string(n) + "?";
    return std::to_string(n) + letters[l];
}

} // namespace qm