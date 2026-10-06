"""Build the pinned, unmodified Khronos loader beside the client outputs."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def find_cmake(explicit: str | None) -> Path:
    if explicit:
        candidate = Path(explicit).resolve()
        if not candidate.is_file():
            raise FileNotFoundError(f"CMake executable is unavailable: {candidate}")
        return candidate
    if found := shutil.which("cmake"):
        return Path(found)
    # Visual Studio's bundled CMake need not be on PATH. Locations are derived
    # from installation roots, never tied to a developer account or drive.
    for variable in ("ProgramFiles", "ProgramFiles(x86)"):
        base = os.environ.get(variable)
        if not base:
            continue
        visual_studio = Path(base) / "Microsoft Visual Studio"
        candidates = sorted(visual_studio.glob("*/*/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"), reverse=True)
        if candidates:
            return candidates[0]
    raise FileNotFoundError("Install CMake or pass --cmake to build the OpenXR loader")


def build(configuration: str, generator: str, toolset: str, jobs: int,
          cmake: Path, destination: Path) -> Path:
    source = ROOT / "deps/openxr"
    if not (source / "src/loader/CMakeLists.txt").is_file():
        raise FileNotFoundError("Initialize the deps/openxr submodule before building the loader")
    build_dir = ROOT / "build/openxr-loader" / toolset
    configure = [str(cmake), "-S", str(source), "-B", str(build_dir),
                 "-G", generator, "-A", "x64", "-T", toolset,
                 "-DBUILD_LOADER=ON", "-DDYNAMIC_LOADER=ON",
                 "-DBUILD_API_LAYERS=OFF", "-DBUILD_TESTS=OFF",
                 "-DBUILD_CONFORMANCE_TESTS=OFF"]
    subprocess.run(configure, cwd=ROOT, check=True)
    subprocess.run([str(cmake), "--build", str(build_dir), "--config", configuration,
                    "--target", "openxr_loader", "--parallel", str(jobs)], cwd=ROOT, check=True)
    library = build_dir / "src/loader" / configuration / "openxr_loader.dll"
    if not library.is_file():
        raise RuntimeError("The loader target did not produce its canonical openxr_loader.dll output")
    destination.mkdir(parents=True, exist_ok=True)
    output = destination / "openxr_loader.dll"
    shutil.copyfile(library, output)
    print(f"OpenXR loader: {configuration} -> {output}")
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configuration", choices=("Debug", "RelWithDebInfo", "Release"), required=True)
    parser.add_argument("--generator", default="Visual Studio 17 2022")
    parser.add_argument("--toolset", default="v143")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 4))
    parser.add_argument("--cmake")
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 32:
        parser.error("--jobs must be between 1 and 32")
    build(args.configuration, args.generator, args.toolset, args.jobs,
          find_cmake(args.cmake), args.output_dir.resolve())


if __name__ == "__main__":
    main()
