# Contributing

Base changes on main. Clone recursively and follow
[development.md](docs/development.md) to build the Windows x64 client.

- Preserve native gameplay, asset, ammunition, animation and lifecycle authority.
  Keep VR adapters narrow and record the source or runtime evidence behind them.
- Reuse shared input, rendering and interaction components. Keep weapon- or
  mission-specific policy in the corresponding module.
- Keep machine paths, credentials, game exports, captures and generated files
  out of source control. Use UTF-8 and repository-relative documentation links.
  Local research belongs in the ignored .agents/ directory.
- Treat dependency submodules as upstream projects. Keep project-specific
  behavior here and propose general upstream changes separately.
- Consider invalid input, large data, thread ownership, shutdown and resource
  lifetime. UI changes must preserve English/Chinese text, high-DPI behavior and
  the existing light/dark presentation.

Run the smallest relevant existing checks. Build the affected configuration for
native changes; include Debug and the optimized build when behavior could differ.
Explain checks that could not run. Distinguish source inspection, offline tests,
successful builds and actual in-game/headset acceptance.

Describe the problem, resulting behavior and validation evidence in a PR.
Preserve copyright and license notices and identify the origin of contributed
code and assets. Do not upload proprietary game files or unlicensed exports.
