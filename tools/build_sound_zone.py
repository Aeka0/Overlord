"""Compile the private VR sound sources into the distributable H2 fastfile.

Source FLACs and aliases live outside Git; ordinary client builds only copy the
compiled fastfile. This optional asset build requires a selected game and H2
ZoneTool, and never runs the linker in the playable installation.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ZONE = "h2_vr_sounds"
EXCLUDED_DIRS = {"dump", "minidumps", "mods", "players2", "trainer", "zonetool", "zone", "zone_source", "h2-mod"}
EXCLUDED_FILES = {"__h2Exe", "h2_sp_patched.exe"}


def run(tool: Path, stage: Path, action: str, timeout: int) -> str:
    command = [str(tool), "-unattended", "-unbuffered-io", action, ZONE]
    try:
        result = subprocess.run(command, cwd=stage, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=timeout)
        output = result.stdout + result.stderr
    except subprocess.TimeoutExpired as error:
        # Some linker builds keep the UI loop alive after reporting completion.
        output = b"".join(part if isinstance(part, bytes) else (part or "").encode()
                          for part in (error.stdout, error.stderr)).decode("utf-8", errors="replace")
        if action != "-buildzone" or f'Successfully compiled fastfile "{ZONE}"' not in output:
            raise
        result = subprocess.CompletedProcess(command, 0)
    (stage.parent / (ZONE + action + ".log")).write_text(output, encoding="utf-8")
    if result.returncode or "[ ERROR ]" in output or "Stream must have a constant blocksize" in output:
        raise RuntimeError(action + " failed:\n" + "\n".join(output.splitlines()[-40:]))
    return output


def build(game: Path, tool: Path, assets: Path, timeout: int) -> None:
    game, tool, assets = game.resolve(strict=True), tool.resolve(strict=True), assets.resolve(strict=True)
    if not (game / "MW2CR.exe").is_file() or not tool.is_file():
        raise ValueError("Select the H2 game directory and a compatible ZoneTool executable")
    manifest = ROOT / "data/zone_source" / (ZONE + ".csv")
    aliases = [line.split(",")[1] for line in manifest.read_text(encoding="utf-8").splitlines()
               if line.startswith("sound,")]
    for alias in aliases:
        path = assets / "sounds" / (alias + ".json")
        record = json.loads(path.read_text(encoding="utf-8"))
        if record["aliasName"] != alias or record["count"] != len(record["head"]):
            raise ValueError(f"Invalid sound alias: {alias}")
        for entry in record["head"]:
            # H2's native start path uses 0x20 for random recording offsets;
            # the inherited SoundAliasFlags bitfield incorrectly calls it is3d.
            # A one-shot punch must retain its attack from sample zero.
            if alias == "vr_fist_hit" and entry["flags"] & 0x21:
                raise ValueError("Fist impacts must be non-looping with a fixed recording start (flags & 0x21 == 0)")
            name = entry["soundfile"]["name"]
            source = (assets / "loaded_sound" / (name + ".flac")).resolve(strict=True)
            if not source.is_relative_to(assets):
                raise ValueError("Sound source escapes its selected directory")
            with source.open("rb") as stream:
                header = stream.read(12)
            if header[:4] != b"fLaC" or header[8:12] != b"\x04\x00\x04\x00":
                raise ValueError(f"H2 requires FLAC with constant 1024-sample blocks: {name}")
    output = ROOT / "output/sound-zone"
    output.mkdir(parents=True, exist_ok=True)
    output = output.resolve()
    if not output.is_relative_to(ROOT):
        raise ValueError("Sound staging directory escapes the workspace")
    stage = Path(tempfile.mkdtemp(prefix="h2-vr-", dir=output))
    links: list[Path] = []
    try:
        for child in game.iterdir():
            target = stage / child.name
            if child.is_file() and child.name not in EXCLUDED_FILES:
                if child.name in {"MW2CR.exe", ".build.info", ".product.db"} or child.suffix.lower() == ".dll":
                    shutil.copy2(child, target)
            elif child.is_dir() and child.name not in EXCLUDED_DIRS:
                if os.name == "nt":
                    import _winapi
                    _winapi.CreateJunction(str(child), str(target))
                else:
                    target.symlink_to(child, target_is_directory=True)
                links.append(target)
        (stage / "zone_source").mkdir()
        shutil.copy2(manifest, stage / "zone_source" / manifest.name)
        shutil.copytree(assets, stage / "zonetool" / ZONE)
        run(tool, stage, "-buildzone", timeout)
        fastfile = next((p for p in (stage / "zone" / (ZONE + ".ff"), stage / (ZONE + ".ff")) if p.is_file()), stage / (ZONE + ".ff"))
        if not fastfile.is_file() or not fastfile.stat().st_size:
            raise RuntimeError("ZoneTool emitted no fastfile")
        run(tool, stage, "-verifyzone", timeout)
        run(tool, stage, "-dumpzone", timeout)
        # Read back the aliases and every recording, so compile success cannot
        # hide a missing variant. Dumped assets are local verification artifacts.
        dumped = stage / "dump" / ZONE
        for alias in aliases:
            record = json.loads((dumped / "sounds" / (alias + ".json")).read_text(encoding="utf-8"))
            original = json.loads((assets / "sounds" / (alias + ".json")).read_text(encoding="utf-8"))
            names = [e["soundfile"]["name"] for e in record["head"]]
            expected_names = [e["soundfile"]["name"] for e in original["head"]]
            if record["count"] != original["count"] or names != expected_names:
                raise RuntimeError(f"Missing sound variants in compiled zone: {alias}")
            if alias == "vr_fist_hit" and any(e["flags"] & 0x21 for e in record["head"]):
                raise RuntimeError("Compiled fist impacts unexpectedly allow looping or random offsets")
            for entry in record["head"]:
                sound = dumped / "loaded_sound" / (entry["soundfile"]["name"] + ".flac")
                if not sound.is_file() or not sound.stat().st_size:
                    raise RuntimeError(f"Missing compiled recording: {sound.name}")
        destination = ROOT / "data/zone" / fastfile.name
        shutil.copy2(fastfile, destination)
        print(f"Compiled and read back {len(aliases)} aliases / {sum(json.loads((assets / 'sounds' / (a + '.json')).read_text())['count'] for a in aliases)} recordings: {destination}")
    finally:
        # Remove links before removing this checked, workspace-owned staging tree.
        if stage.parent != output or not stage.name.startswith("h2-vr-"):
            raise ValueError("Unsafe staging cleanup target")
        for child in links:
            if child.parent != stage:
                raise ValueError("Unsafe staging link cleanup target")
            if os.name == "nt":
                os.rmdir(child)  # Removes only the junction, never its selected game target.
            else:
                child.unlink()
        shutil.rmtree(stage)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--h2-root", type=Path, required=True)
    parser.add_argument("--zone-tool", type=Path, required=True)
    parser.add_argument("--asset-directory", type=Path, default=ROOT / ".local/Sounds" / ZONE)
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    build(args.h2_root, args.zone_tool, args.asset_directory, args.timeout)


if __name__ == "__main__":
    main()
