"""Package a built VR client overlay without game files or debug symbols."""
from __future__ import annotations

import argparse
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
LICENSE_SOURCES = {
    "GSL": "deps/GSL/LICENSE",
    "WinToast": "deps/WinToast/LICENSE.txt",
    "asmjit": "deps/asmjit/LICENSE.md",
    "CascLib": "deps/casclib/LICENSE",
    "curl": "deps/curl/COPYING",
    "discord-rpc": "deps/discord-rpc/LICENSE",
    "gsc-tool": "deps/gsc-tool/LICENSE",
    "cxxopts": "deps/gsc-tool/deps/cxxopts/LICENSE",
    "imgui": "deps/imgui/LICENSE.txt",
    "json": "deps/json/LICENSE.MIT",
    "libtomcrypt": "deps/libtomcrypt/LICENSE",
    "libtommath": "deps/libtommath/LICENSE",
    "minhook": "deps/minhook/LICENSE.txt",
    "OpenVR": "deps/openvr/LICENSE",
    "rapidjson": "deps/rapidjson/license.txt",
    "sol2": "deps/sol2/LICENSE.txt",
    "stb": "deps/stb/LICENSE",
    "udis86": "deps/udis86/LICENSE",
    "zlib": "deps/zlib/LICENSE",
}


def collect(configuration: str, base_data: Path | None = None) -> dict[str, Path]:
    build = ROOT / "build/bin/x64" / configuration
    binary = "h2-mod-vr-debug" if configuration == "Debug" else "h2-mod-vr"
    files: dict[str, Path] = {}

    def add(source: Path, destination: str) -> None:
        if not source.is_file() or source.is_symlink() or not source.stat().st_size:
            display = source.relative_to(ROOT) if source.is_relative_to(ROOT) else source
            raise ValueError(f"Missing or invalid package input: {display}")
        if destination in files:
            raise ValueError(f"Duplicate package destination: {destination}")
        files[destination] = source

    def tree(source: Path, destination: str, suffixes: set[str]) -> None:
        if not source.is_dir() or source.is_symlink():
            raise ValueError(f"Missing resource directory: {source.relative_to(ROOT)}")
        for path in sorted(source.rglob("*")):
            if path.is_file() and path.suffix.lower() in suffixes:
                add(path, str(PurePosixPath(destination) / path.relative_to(source).as_posix()))

    add(build / f"{binary}.exe", f"{binary}.exe")
    add(build / f"{binary}.vrmanifest", f"{binary}.vrmanifest")
    manifest = json.loads((build / f"{binary}.vrmanifest").read_text(encoding="utf-8-sig"))
    applications = manifest.get("applications", [])
    if len(applications) != 1 or applications[0].get("binary_path_windows") != f"{binary}.exe":
        raise ValueError("Application manifest does not target this client configuration")
    for name in ["cover.png", "cover-small.png", "cover-capsule.png"]:
        add(build / "steamvr" / name, "steamvr/" + name)
    tree(build / "vr_input", "vr_input", {".json"})
    for field in ("image_path", "image_path_capsule", "action_manifest_path"):
        reference = applications[0].get(field, "")
        path = PurePosixPath(reference)
        if (not reference or "\\" in reference or ":" in reference or path.is_absolute()
                or ".." in path.parts or str(path) not in files):
            raise ValueError(f"Missing or unsafe application resource ({field}): {reference}")
    actions = json.loads((build / "vr_input/actions.json").read_text(encoding="utf-8-sig"))
    for binding in actions.get("default_bindings", []):
        path = PurePosixPath(binding["binding_url"])
        if path.is_absolute() or ".." in path.parts or ("vr_input/" + str(path)) not in files:
            raise ValueError(f"Missing or unsafe default binding: {path}")
    tree(ROOT / "data/cdata", "h2-mod", {".lua", ".gsc", ".cfg", ".json", ".csv", ".flac"})
    tree(build / "h2-mod/ui_scripts/vr_gameplay", "h2-mod/ui_scripts/vr_gameplay", {".lua"})
    for name in ["LICENSE", "THIRD_PARTY_NOTICES.md"]:
        add(ROOT / name, name)
    add(ROOT / "docs/client-installation.md", "README.md")
    tree(ROOT / "licenses", "licenses", {".txt"})
    add(ROOT / "docs/releasing.md", "docs/releasing.md")
    add(ROOT / "docs/source-provenance.md", "docs/source-provenance.md")
    for name, path in LICENSE_SOURCES.items():
        add(ROOT / path, f"licenses/{name}.txt")
    add(ROOT / "version.json", "version.json")
    if base_data is not None:
        base_data = base_data.resolve()
        zone = base_data / "zone"
        expected = {source.stem + ".ff" for source in (ROOT / "data/zone_source").glob("*.csv")}
        found = set()
        for source in sorted(zone.rglob("*.ff")):
            if source.name not in expected:
                continue  # Never collect original game zones or unrelated user files.
            if source.is_symlink() or not source.resolve().is_relative_to(base_data):
                raise ValueError("Base data resource escapes its selected directory")
            if source.name in found:
                raise ValueError(f"Duplicate base fastfile: {source.name}")
            found.add(source.name)
            add(source, "h2-mod/" + source.relative_to(base_data).as_posix())
        # Czech/Turkish are optional MOD extensions, outside official game packs.
        required = {name for name in expected if not name.startswith(("cze_", "tur_"))}
        if missing := required - found:
            raise ValueError("Incomplete base mod data: " + ", ".join(sorted(missing)))
    return files


def version_directory(base: Path, name: str) -> Path:
    """Keep each release in one safe, portable version-name directory."""
    reserved = {"CON", "PRN", "AUX", "NUL", *(f"COM{i}" for i in range(1, 10)),
                *(f"LPT{i}" for i in range(1, 10))}
    if (not isinstance(name, str) or not name or len(name) > 64 or name != name.strip()
            or name in {".", ".."} or name.endswith(".")
            or any(ord(c) < 32 or c in '<>:"/\\|?*' for c in name)
            or name.split(".", 1)[0].upper() in reserved):
        raise ValueError("Release name must be a safe single directory name")
    parent = base.resolve()
    candidate = parent / name
    if candidate.resolve() != candidate:
        raise ValueError("Version directory cannot redirect through a filesystem link")
    return candidate


def stage(configuration: str, destination: Path, base_data: Path | None = None) -> dict:
    """Stage a local candidate without publishing or requiring a Git commit."""
    files = collect(configuration, base_data)
    binary = "h2-mod-vr-debug" if configuration == "Debug" else "h2-mod-vr"
    symbols = ROOT / "build/bin/x64" / configuration / f"{binary}.pdb"
    if not symbols.is_file() or symbols.is_symlink() or not symbols.stat().st_size:
        raise ValueError(f"Missing symbols from the selected build: {binary}.pdb")
    files[f"{binary}.pdb"] = symbols
    version = json.loads((ROOT / "version.json").read_text(encoding="utf-8"))
    destination = version_directory(destination, version["name"])
    # Check every destination before writing any file. User-selected output must
    # not resolve through an existing link into the source tree or another path.
    for name, source in files.items():
        target = destination / name
        if not target.resolve().is_relative_to(destination) or target.resolve() == source.resolve():
            raise ValueError(f"Unsafe staging destination: {name}")
    record = {"version": version["name"], "file_version": version["file_version"],
              "configuration": configuration, "base_data_included": base_data is not None,
              "symbols_included": True,
              "files": sorted(files)}
    for name, source in files.items():
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    (destination / "release-info.json").write_text(
        json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return record


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configuration", choices=["RelWithDebInfo", "Release", "Debug"],
                        default="RelWithDebInfo")
    parser.add_argument("--staging-directory", type=Path,
                        help="Stage a local candidate under ROOT/<version-name>/ instead of an archive")
    parser.add_argument("--base-data", type=Path,
                        help="Explicit source of compiled base H2-Mod cdata; no game data is copied")
    args = parser.parse_args()
    try:
        if args.staging_directory is not None:
            record = stage(args.configuration, args.staging_directory, args.base_data)
            destination = version_directory(args.staging_directory, record["version"])
            print(f"Staged {record['version']} / {args.configuration}: {len(record['files'])} files in {destination}")
            return 0
        files = collect(args.configuration, args.base_data)
        revision = subprocess.check_output(
            ["git", "-C", str(ROOT), "rev-parse", "--short=12", "HEAD"],
            text=True, encoding="utf-8").strip()
        if subprocess.check_output(
            ["git", "-C", str(ROOT), "status", "--porcelain"], text=True, encoding="utf-8").strip():
            raise ValueError("Commit the release candidate before packaging")
        version = json.loads((ROOT / "version.json").read_text(encoding="utf-8"))
        output = version_directory(ROOT / "output/packages", version["name"])
        output.mkdir(parents=True, exist_ok=True)
        destination = output / f"h2-mod-vr-{args.configuration}-{revision}.zip"
        with tempfile.NamedTemporaryFile(dir=output, suffix=".tmp", delete=False) as handle:
            temporary = Path(handle.name)
        try:
            with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
                for name, path in sorted(files.items()):
                    archive.write(path, name)
            temporary.replace(destination)
        finally:
            temporary.unlink(missing_ok=True)
        print(f"Created {destination} ({len(files)} files)")
        print("Client overlay only: base game/mod data and clean-install acceptance are separate.")
        return 0
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Package failed: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
