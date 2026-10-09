"""Check the embedded WebView2/React bridge and fonts without loading the game."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "output/launcher-smoke")
    parser.add_argument("--size", choices=("1040x720", "760x560", "640x480"), default="1040x720")
    parser.add_argument("--page", choices=("settings", "home"), default="settings")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("The native launcher probe requires Windows")
    client = args.client.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    fixture = Path(tempfile.mkdtemp(prefix="probe-", dir=output)).resolve()
    if not fixture.is_relative_to(output):
        raise ValueError("Probe directory must stay under its output directory")
    report = output / (client.stem + "-" + args.page + "-" + args.size + ".json")
    try:
        executable = fixture / client.name
        shutil.copyfile(client, executable)
        profile = fixture / "players2/h2-mod/config.cfg"
        profile.parent.mkdir(parents=True)
        profile.write_text('seta r_ssaaSamples "1"\nseta 0x70BF1633 "4"\n'
                           'seta r_preloadShadersFrontendAllow "1"\nseta r_preloadShaders "0"\n'
                           'seta sm_cacheSunShadow "Enabled"\nseta sm_cacheSpotShadows "Disabled"\n', encoding="utf-8")
        probe_report = fixture / "renderer.json"
        result = subprocess.run([str(executable), "-launcher-smoke", str(probe_report), "-launcher-probe-size", args.size, "-launcher-probe-page", args.page],
                                cwd=fixture, creationflags=subprocess.CREATE_NO_WINDOW,
                                timeout=35, check=False)
        if not probe_report.is_file():
            raise RuntimeError(f"Launcher did not produce a renderer report (exit {result.returncode})")
        data = json.loads(probe_report.read_text(encoding="utf-8"))
        report.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        if result.returncode or not data.get("ok") or data.get("controls", 0) < 1:
            raise RuntimeError(f"Launcher renderer probe failed: {data}")
        if not data.get('declaration', {}).get('ok'):
            raise RuntimeError('Startup declaration did not render or dismiss correctly')
        expected_version = json.loads((ROOT / 'version.json').read_text(encoding='utf-8'))['name']
        if data.get('version') != expected_version:
            raise RuntimeError('Embedded product version differs from version.json')
        expected = [field['name'] for field in json.loads(
            (ROOT / 'src/client/resources/launcher/settings-schema.json').read_text(encoding='utf-8'))]
        if data.get('settingNames') != expected:
            raise RuntimeError('Embedded launcher schema differs from the current source; rebuild launcher assets and the client')
        if args.page == 'settings' and (not data.get('presetFields') or
                not set(data['presetFields']).issubset(data.get('controlNames', []))):
            raise RuntimeError('The controller preset edits fields that are not displayed by the launcher')
        print(f"Launcher renderer passed ({args.page}, {args.size}): {data['settings']} settings, page layout, borderless frame and embedded assets")
        print(f"Report: {report}")
    finally:
        # The target was created here and checked before recursive cleanup.
        if fixture.resolve().is_relative_to(output):
            shutil.rmtree(fixture)


if __name__ == "__main__":
    main()
