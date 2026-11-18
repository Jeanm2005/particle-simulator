#!/usr/bin/env python3
"""Check the element catalog against downloaded CIAAW and NIST reference pages.

Usage: python3 tools/audit_elements.py CIAAW_HTML NIST_CONFIG_HTML
Sources: https://ciaaw.org/abridged-atomic-weights.htm (standard atomic weights)
and https://math.nist.gov/DFTdata/atomdata/configuration.html (neutral
configurations, H through U). Higher-Z configurations are withheld; zero mass
means unavailable. No network access or third-party Python packages required.
"""
import html
import json
from pathlib import Path
import re
import sys


def references(weights_path, configurations_path):
    identities, weights = {}, {}
    for row in re.findall(r"<tr[^>]*>(.*?)</tr>", Path(weights_path).read_text(), re.S):
        cells = [html.unescape(re.sub(r"<[^>]+>", "", cell)).strip()
                 for cell in re.findall(r"<td[^>]*>(.*?)</td>", row, re.S)]
        if len(cells) < 4 or not cells[0].isdigit():
            continue
        z = int(cells[0])
        identities[z] = cells[1]
        match = re.match(r"([\d.]+)\s*±", cells[3])
        weights[z] = float(match[1]) if match else 0.0
    configurations, symbols = {}, {}
    for line in Path(configurations_path).read_text().splitlines():
        match = re.match(r"^\s*(\d+)\s+(\w+)\s{2,}(.+?)\s{2,}", line)
        if not match:
            continue
        z, symbol, configuration = int(match[1]), match[2], match[3]
        core = re.match(r"\[(\w+)\]\s*", configuration)
        entries = {} if not core else dict(configurations[symbols[core[1]]])
        entries.update({n + l: int(count) for n, l, count in
                        re.findall(r"(\d+)([spdfgh])\^(\d+)", configuration)})
        if sum(entries.values()) != z:
            raise ValueError(f"Incomplete NIST configuration for {symbol}")
        configurations[z], symbols[symbol] = entries, z
    if set(identities) != set(range(1, 119)) or set(configurations) != set(range(1, 93)):
        raise ValueError("Reference layout or coverage changed; review the parser")
    return identities, weights, configurations


def audit(weights_path, configurations_path):
    identities, weights, configurations = references(weights_path, configurations_path)
    catalog = json.loads((Path(__file__).resolve().parents[1] / "data/elements.json").read_text())["elements"]
    if [entry["Z"] for entry in catalog] != list(range(1, 119)):
        raise ValueError("Expected exactly the ordered catalog Z = 1 through 118")
    for entry in catalog:
        z = entry["Z"]
        if entry["symbol"] != identities[z] or entry["atomic_mass"] != weights[z]:
            raise ValueError(f"Identity or abridged atomic weight mismatch for Z={z}")
        tokens = re.findall(r"(\d+)([spdfgh])(\d+)", entry["config"])
        configuration = {n + l: int(count) for n, l, count in tokens}
        if configuration != configurations.get(z, {}) or len(tokens) != len(configuration):
            raise ValueError(f"Configuration mismatch for Z={z}")
    print("Audited 118 identities, 84 abridged weights, 92 neutral configurations; remaining fields explicitly unavailable")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    audit(*sys.argv[1:])
