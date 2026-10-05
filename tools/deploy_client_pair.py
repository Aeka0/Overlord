"""Deploy optimized/Debug clients and SteamVR resources, with backups and hashes.

Build both configurations from the same checkout first. This updates only the
four binaries, application manifests, covers and matching vr_input package;
user profiles stay put.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import uuid


def digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open('rb') as stream:
        while chunk := stream.read(1024 * 1024):
            result.update(chunk)
    return result.hexdigest()


def input_sources(build_root: Path) -> dict[str, Path]:
    folders = [build_root / config / 'vr_input' for config in ('RelWithDebInfo', 'Debug')]
    manifests = [folder / 'actions.json' for folder in folders]
    manifest = json.loads(manifests[0].read_text(encoding='utf-8'))
    names = {'actions.json'}
    for binding in manifest['default_bindings']:
        name = binding['binding_url']
        relative = Path(name)
        if not name or relative.is_absolute() or relative.drive or '..' in relative.parts:
            raise ValueError(f'Input binding must stay within vr_input: {name}')
        names.add(relative.as_posix())
    sources = {}
    for name in sorted(names):
        paths = [folder / name for folder in folders]
        for folder, path in zip(folders, paths):
            if not path.resolve().is_relative_to(folder.resolve()):
                raise ValueError(f'Input binding escapes vr_input: {name}')
            json.loads(path.read_text(encoding='utf-8'))
        if paths[0].read_bytes() != paths[1].read_bytes():
            raise ValueError(f'Optimized/Debug input packages differ: {name}')
        sources['vr_input/' + name] = paths[0]
    return sources


def application_sources(build_root: Path) -> dict[str, Path]:
    sources = {}
    for config, name in [('RelWithDebInfo', 'h2-mod-vr'), ('Debug', 'h2-mod-vr-debug')]:
        manifest = build_root / config / (name + '.vrmanifest')
        json.loads(manifest.read_text(encoding='utf-8'))
        sources[manifest.name] = manifest
    for name in ('cover.png', 'cover-small.png', 'cover-capsule.png'):
        paths = [build_root / config / 'steamvr' / name for config in ('RelWithDebInfo', 'Debug')]
        if paths[0].read_bytes() != paths[1].read_bytes():
            raise ValueError(f'Optimized/Debug SteamVR artwork differs: {name}')
        sources['steamvr/' + name] = paths[0]
    return sources


def deploy(build_root: Path, destination: Path, backup_root: Path) -> dict:
    build_root, destination, backup_root = (p.resolve() for p in (build_root, destination, backup_root))
    sources = {name + suffix: build_root / config / (name + suffix)
               for config, name in [('RelWithDebInfo', 'h2-mod-vr'), ('Debug', 'h2-mod-vr-debug')]
               for suffix in ('.exe', '.pdb')}
    sources.update(input_sources(build_root))
    sources.update(application_sources(build_root))
    # Validate the whole pair before changing any installed file.
    for name, source in sources.items():
        if not source.is_file() or source.stat().st_size == 0:
            raise FileNotFoundError(f'Build both configurations first: {source}')
        if source == destination / name:
            raise ValueError('Build output cannot also be the deployment destination')
    if not destination.is_dir():
        raise NotADirectoryError(destination)
    token = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + uuid.uuid4().hex[:8]
    backup = backup_root / token
    backup.mkdir(parents=True, exist_ok=False)
    record = {'time_utc': datetime.now(timezone.utc).isoformat(), 'destination': str(destination),
              'backup': str(backup), 'deployed': False, 'files': {}}
    staged, replaced = {}, []
    try:
        for name, source in sources.items():
            target = destination / name
            previous = None
            if target.exists():
                previous = digest(target)
                (backup / name).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(target, backup / name)
                if digest(backup / name) != previous:
                    raise OSError(f'Backup verification failed: {name}')
            expected = digest(source)
            stage = destination / (name + '.staging-' + token)
            stage.parent.mkdir(parents=True, exist_ok=True)
            staged[name] = stage
            shutil.copy2(source, stage)
            if digest(stage) != expected:
                raise OSError(f'Stage verification failed: {name}')
            record['files'][name] = {'source': str(source), 'before': previous, 'after': expected}
        for name in sources:
            os.replace(staged[name], destination / name)
            replaced.append(name)
        for name in sources:
            if digest(destination / name) != record['files'][name]['after']:
                raise OSError(f'Deployment verification failed: {name}')
        record['deployed'] = True
    except Exception:
        # A locked running executable or failed copy must not leave a reported
        # success or intentionally mixed pair. Preserve backups if rollback fails.
        for name in reversed(replaced):
            if record['files'][name]['before'] is None:
                (destination / name).unlink()
            else:
                shutil.copy2(backup / name, staged[name])
                os.replace(staged[name], destination / name)
        raise
    finally:
        for stage in staged.values():
            if stage.exists():
                stage.unlink()
        (backup / 'deployment.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
    return record


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game_directory', type=Path)
    parser.add_argument('--build-root', type=Path, default=root / 'build/bin/x64')
    parser.add_argument('--backup-root', type=Path, default=root / 'output/deployments')
    args = parser.parse_args()
    print(json.dumps(deploy(args.build_root, args.game_directory, args.backup_root), indent=2))


if __name__ == '__main__':
    main()
