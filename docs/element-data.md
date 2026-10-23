# Element metadata audit

Reviewed 2026-10-23. The catalog contains the 118 named elements. Its previous
Z = 119 entry (including a speculative mass of 315 and 8s configuration) was
removed. Custom positive nuclear charge remains supported in pure-Z mode;
missing catalog metadata does not reject that mathematical input.

## Sources and field meanings

- Symbols and atomic-number coverage: [CIAAW abridged table](https://ciaaw.org/abridged-atomic-weights.htm)
  and [IUPAC periodic table](https://iupac.org/what-we-do/periodic-table-of-elements/).
- `atomic_mass`: nominal values from the CIAAW **Abridged Standard Atomic Weights
  2024** table, for its 84 available entries. These are relative standard atomic
  weights (dimensionless), not isotope masses or isotope mass numbers. The legacy
  field name remains for compatibility. Values omit uncertainty and natural
  isotopic variation; consult the source for those. They are display metadata and
  do not enter the orbital equations. For the other 34 entries the value is zero,
  the existing unavailable-metadata sentinel. Previously stored integer isotope
  mass numbers must not be interpreted as standard atomic weights.
- `config`: [NIST neutral electronic configurations, H through U](https://math.nist.gov/DFTdata/atomdata/configuration.html).
  Expand noble-gas cores and compare subshell occupancies independently of their
  printed ordering. All 92 existing configurations match this reference. This is
  the dated NIST compilation identified on that page, not a claim that every
  ground state has been remeasured or checked against newer spectroscopy.
  The 26 higher-Z configurations are empty until separately sourced. Slater
  screening rejects them and explains that pure Z is available.

Names retain existing English spelling/capitalization conventions. Identity is
atomic number plus symbol. No source supports the old Z = 119 catalog entry.
This audit establishes the stated source coverage, not accuracy of hydrogenic or
Slater approximations for many-electron atoms, especially at high Z.

## Reproduce the audit

Download the two HTML references, then run from the repository root:

```bash
curl -fL https://ciaaw.org/abridged-atomic-weights.htm -o /tmp/ciaaw.html
curl -fL https://math.nist.gov/DFTdata/atomdata/configuration.html -o /tmp/nist.html
python3 tools/audit_elements.py /tmp/ciaaw.html /tmp/nist.html
```

The script checks all catalog identities and nominal weights, expands NIST cores,
and checks every available configuration. It rejects changed reference coverage
so a future source/layout update requires review. It does not modify data.
C++ screening and relocated-install regressions additionally cover the 118-entry
catalog, unavailable fields, and the absence of speculative Z = 119 metadata.
Future updates should retain source URLs, version/date, and explicit field limits.
