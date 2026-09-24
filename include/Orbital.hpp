#pragma once

#include <string>
#include <optional>
#include <utility>

namespace qm {
	struct QuantumNumbers {
		int n, l, m;
	};

	std::optional<std::pair<int,int>> parseOrbital(const std::string& s);
	std::string orbitalName(int n, int l);
} // namespace qm
