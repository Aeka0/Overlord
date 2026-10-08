"""Build the local React launcher and prepare its pinned native WebView2 SDK."""
from __future__ import annotations

import io
import json
import mimetypes
import os
from pathlib import Path
import shutil
import subprocess
import urllib.request
import zipfile
from contextlib import contextmanager
import time

ROOT = Path(__file__).resolve().parents[1]
FRONTEND = ROOT / "src/launcher-ui"
OUTPUT = ROOT / "build/launcher-ui"
SDK_VERSION = "1.0.4258.31"
SDK = ROOT / "build/deps/webview2"


@contextmanager
def preparation_lock():
    """Different client configurations share one generated frontend bundle."""
    OUTPUT.mkdir(parents=True, exist_ok=True)
    with (OUTPUT / "prepare.lock").open("a+b") as lock:
        if os.name == "nt":
            import msvcrt
            if not lock.tell():
                lock.write(b"0")
                lock.flush()
            deadline = time.monotonic() + 180
            while True:
                try:
                    lock.seek(0)
                    msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
                    break
                except OSError:
                    if time.monotonic() > deadline:
                        raise TimeoutError("Another launcher build did not finish resource preparation")
                    time.sleep(0.1)
            try:
                yield
            finally:
                lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
        else:
            import fcntl
            fcntl.flock(lock, fcntl.LOCK_EX)
            try:
                yield
            finally:
                fcntl.flock(lock, fcntl.LOCK_UN)


def write_changed(path: Path, text: str) -> None:
    if path.exists() and path.read_text(encoding="utf-8") == text:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def prepare_sdk() -> None:
    files = {"build/native/include/WebView2.h": SDK / "include/WebView2.h",
             "build/native/x64/WebView2LoaderStatic.lib": SDK / "lib/WebView2LoaderStatic.lib"}
    marker = SDK / "version.txt"
    if marker.exists() and marker.read_text().strip() == SDK_VERSION and all(p.is_file() for p in files.values()):
        return
    url = f"https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/{SDK_VERSION}/microsoft.web.webview2.{SDK_VERSION}.nupkg"
    with urllib.request.urlopen(url, timeout=60) as response:
        data = response.read(32 * 1024 * 1024 + 1)
    if len(data) > 32 * 1024 * 1024:
        raise ValueError("WebView2 SDK archive is unexpectedly large")
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        for name, destination in files.items():
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(archive.read(name))
        # Keep Microsoft's package notice alongside the downloaded SDK.
        for name in archive.namelist():
            if name.lower().endswith("license.txt"):
                (SDK / "LICENSE.txt").write_bytes(archive.read(name))
                break
    write_changed(marker, SDK_VERSION + "\n")


def prepare_frontend() -> None:
    node = os.environ.get("H2_LAUNCHER_NODE") or shutil.which("node")
    npm = os.environ.get("H2_LAUNCHER_NPM") or shutil.which("npm.cmd" if os.name == "nt" else "npm")
    if not node or not npm:
        raise RuntimeError("Install Node.js 22.12 or newer, including npm, and add it to PATH")
    version = subprocess.check_output([node, "--version"], text=True).strip().lstrip("v")
    major, minor = map(int, version.split(".")[:2])
    if major < 22 or (major == 22 and minor < 12):
        raise RuntimeError("The React launcher requires Node.js 22.12 or newer")
    if os.name == "nt" and not npm.endswith(".js"):
        npm_cli = Path(npm).parent / "node_modules/npm/bin/npm-cli.js"
        if npm_cli.is_file():
            npm = str(npm_cli)
    prefix = [node, npm] if npm.endswith(".js") else ["cmd.exe", "/d", "/c", npm] if os.name == "nt" else [npm]
    environment = dict(os.environ, PATH=str(Path(node).parent) + os.pathsep + os.environ.get("PATH", ""))
    lock = FRONTEND / "package-lock.json"
    install_stamp = FRONTEND / "node_modules/.overlord-installed"
    if not install_stamp.exists() or install_stamp.stat().st_mtime < lock.stat().st_mtime:
        subprocess.run(prefix + ["ci", "--no-audit", "--no-fund"], cwd=FRONTEND, env=environment, check=True)
        install_stamp.touch()
    inputs = [lock, FRONTEND / "package.json", FRONTEND / "index.html", FRONTEND / "vite.config.ts",
              FRONTEND / "tsconfig.json", *list((FRONTEND / "src").rglob("*")),
              *list((FRONTEND / "public").rglob("*")),
              *list((ROOT / "src/client/resources/launcher").rglob("*.json"))]
    stamp = OUTPUT / "frontend-built.txt"
    latest = max(path.stat().st_mtime for path in inputs if path.is_file())
    if not stamp.exists() or not (OUTPUT / "dist/index.html").is_file() or stamp.stat().st_mtime < latest:
        subprocess.run(prefix + ["run", "build"], cwd=FRONTEND, env=environment, check=True)
        stamp.touch()


def embed_resources() -> None:
    assets = sorted(path for path in (OUTPUT / "dist").rglob("*") if path.is_file())
    if not assets or len(assets) > 512 or sum(p.stat().st_size for p in assets) > 16 * 1024 * 1024:
        raise ValueError("Launcher resource bundle is empty or exceeds its size limit")
    resources = []
    entries = []
    for index, path in enumerate(assets, 4000):
        name = path.relative_to(OUTPUT / "dist").as_posix()
        mime = {".js": "text/javascript", ".css": "text/css", ".html": "text/html",
                ".woff": "font/woff", ".json": "application/json"}.get(path.suffix)
        mime = mime or mimetypes.guess_type(name)[0] or "application/octet-stream"
        resources.append(f'{index} RCDATA "{path.as_posix()}"')
        entries.append(f'    embedded_asset{{{json.dumps("/" + name)}, {index}, {json.dumps(mime)}}},')
    write_changed(OUTPUT / "launcher_assets.rcinc", "\n".join(resources) + "\n")
    write_changed(OUTPUT / "launcher_assets.hpp", "#pragma once\n#include <array>\n#include <string_view>\n"
                  "namespace launcher_assets {\nstruct embedded_asset { std::string_view path; int resource; std::string_view mime; };\n"
                  "inline constexpr std::array assets{\n" + "\n".join(entries) + "\n};\n}\n")
    print(f"Embedded launcher: {len(assets)} local resources")


def main() -> None:
    with preparation_lock():
        prepare_sdk()
        prepare_frontend()
        embed_resources()


if __name__ == "__main__":
    main()
