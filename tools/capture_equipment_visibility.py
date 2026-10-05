"""Operator-gated, read-only H2 equipment witness. No live work on import.

Requires a local address profile tied to the exact installed EXE hash. External
reads are best-effort snapshots, never proof of a coherent GPU frame or HMD
visibility. See docs/vr-equipment-visibility-capture.md before running.
"""
import argparse
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import time
from datetime import datetime, timezone

DEFAULT_SECONDS = 30
MAX_SECONDS = 60


def u32(data, offset=0):
    return struct.unpack_from('<I', data, offset)[0]


def u64(data, offset=0):
    return struct.unpack_from('<Q', data, offset)[0]


def utc():
    return datetime.now(timezone.utc).isoformat()


class Reader:
    def __init__(self, pid):
        if os.name != 'nt':
            raise RuntimeError('Live capture requires Windows')
        from ctypes import wintypes as w
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
        self.kernel.OpenProcess.restype = w.HANDLE
        self.kernel.ReadProcessMemory.argtypes = [w.HANDLE, ctypes.c_void_p,
            ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
        self.kernel.ReadProcessMemory.restype = w.BOOL
        self.kernel.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD,
            w.LPWSTR, ctypes.POINTER(w.DWORD)]
        self.kernel.QueryFullProcessImageNameW.restype = w.BOOL
        self.kernel.CloseHandle.argtypes = [w.HANDLE]
        # QUERY_INFORMATION | VM_READ only; no write, injection or suspend rights.
        self.handle = self.kernel.OpenProcess(0x410, False, pid)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())

    def close(self):
        self.kernel.CloseHandle(self.handle)

    def image(self):
        from ctypes import wintypes as w
        buf = ctypes.create_unicode_buffer(32768)
        size = w.DWORD(len(buf))
        if not self.kernel.QueryFullProcessImageNameW(self.handle, 0, buf, ctypes.byref(size)):
            raise ctypes.WinError(ctypes.get_last_error())
        return Path(buf.value)

    def read(self, address, size):
        if not 0x10000 <= address < 0x7fffffffffff or not 0 < size <= 1048576:
            raise ValueError('Read outside allowed bounds')
        buf = ctypes.create_string_buffer(size)
        received = ctypes.c_size_t()
        if not self.kernel.ReadProcessMemory(self.handle, address, buf, size,
                ctypes.byref(received)) or received.value != size:
            raise OSError('Process read failed')
        return buf.raw


def validate_profile(profile):
    if profile.get('schema') != 'h2-equipment-v1':
        raise ValueError('Unsupported address profile')
    digest = profile.get('image_sha256', '')
    if len(digest) != 64 or any(c not in '0123456789abcdef' for c in digest):
        raise ValueError('Profile requires an exact EXE SHA-256')
    names = ('input', 'scene', 'equipment', 'inventory', 'scene_count',
        'scene_models', 'scene_visibility', 'dvar_count', 'dvar_pool',
        'chest_lighting', 'knife_lighting', 'magazine_lighting',
        'attachment_counters', 'chest_counters')
    addresses = {key: int(profile['addresses'][key], 0) for key in names}
    if any(not 0x10000 <= value < 0x7fffffffffff for value in addresses.values()):
        raise ValueError('Invalid profile address')
    return addresses


def find_pause(reader, addresses):
    value = 0x319712c3
    for byte in b'cl_paused\0':
        value = ((value ^ byte) * 0xb3cb2e29) & 0xffffffff
    count = u32(reader.read(addresses['dvar_count'], 4))
    if not 0 < count <= 16384:
        raise ValueError('Unexpected dvar pool count')
    for start in range(0, count, 1024):
        block = reader.read(addresses['dvar_pool'] + start * 96,
                            min(1024, count - start) * 96)
        for at in range(0, len(block), 96):
            if u32(block, at) == value and block[at + 8] == 5:
                return addresses['dvar_pool'] + start * 96 + at + 16
    raise ValueError('cl_paused contract not found')


def invalid_reasons(state):
    reasons = []
    if state['paused']:
        reasons.append('game_paused')
    if not state['focused']:
        reasons.append('tracking_not_focused')
    if not state['grips_valid']:
        reasons.append('controller_pose_invalid')
    if not state['gameplay']:
        reasons.append('gameplay_inactive')
    for key in ('input_age_ms', 'scene_age_ms', 'equipment_age_ms'):
        if not math.isfinite(state[key]) or not 0 <= state[key] <= 150:
            reasons.append(key + '_stale')
    if not state['magazines']:
        reasons.append('no_held_magazine')
    return reasons


def read_state(reader, addresses, pause):
    inp = reader.read(addresses['input'], 720)
    scene = reader.read(addresses['scene'], 1776)
    equipment = reader.read(addresses['equipment'], 224)
    inventory = reader.read(addresses['inventory'], 373680)
    now = time.perf_counter_ns()
    magazines = []
    for index in range(15):
        at = index * 24912
        hand = struct.unpack_from('<i', inventory, at + 0x30)[0]
        if inventory[at] and not inventory[at + 1] and hand in (0, 1):
            magazines.append({'weapon': u32(inventory, at + 8), 'hand': hand,
                              'rounds': u32(inventory, at + 0x2c)})
    state = {'paused': u32(reader.read(pause, 4)) != 0,
        'sequence': u64(inp), 'reference': u64(inp, 8), 'focused': bool(inp[24]),
        'grips_valid': all(inp[0x170 + hand * 52] for hand in range(2)),
        'gameplay': bool(scene[0x310]) and bool(scene[0x550]),
        'input_age_ms': (now - u64(inp, 16)) / 1e6,
        'scene_age_ms': (now - u64(scene, 16)) / 1e6,
        'equipment_age_ms': (now - u64(equipment, 216)) / 1e6,
        'magazines': magazines}
    state['invalid_reasons'] = invalid_reasons(state)
    return state


def scene_rows(reader, addresses):
    count = u32(reader.read(addresses['scene_count'], 4))
    if count > 1536:
        raise ValueError('Scene model count exceeds native capacity')
    if not count:
        return {'count': 0, 'bounds_stable': True, 'models': []}
    # Read the whole bounded native table once; do not assume late-added models
    # are still the last entries after concurrent native submission/sorting.
    raw = reader.read(addresses['scene_models'], count * 0x98)
    visibility = reader.read(addresses['scene_visibility'], 8 * 1536)
    rows = []
    for index in range(count):
        entry = raw[index * 0x98:(index + 1) * 0x98]
        handle = u64(entry, 0x68)
        kind = None
        if handle in (addresses['chest_lighting'], addresses['chest_lighting'] + 2):
            kind = 'chest'
        elif handle == addresses['knife_lighting']:
            kind = 'knife'
        elif handle in range(addresses['magazine_lighting'], addresses['magazine_lighting'] + 30, 2):
            kind = 'held_magazine'
        if kind:
            rows.append({'kind': kind, 'index': index, 'model': hex(u64(entry, 8)),
                'handle': hex(handle), 'origin': struct.unpack_from('<3f', entry, 0x28),
                'visibility': [visibility[view * 1536 + index] for view in range(8)],
                'entry_hex': entry.hex()})
    count_after = u32(reader.read(addresses['scene_count'], 4))
    return {'count': count, 'count_after': count_after, 'bounds_stable': count == count_after,
        'models': rows,
        'attachment_counters': struct.unpack('<6Q', reader.read(addresses['attachment_counters'], 48)),
        'chest_counters': struct.unpack('<2Q', reader.read(addresses['chest_counters'], 16))}


def sample_reasons(row):
    # A readiness change during the scene read invalidates that observation,
    # even if the second check has already recovered.
    return sorted(set(row['state']['invalid_reasons']) |
                  set(row.get('state_before', {}).get('invalid_reasons', [])))


def capture(sample, emit, seconds, monotonic=time.monotonic, sleep=time.sleep):
    started = monotonic()
    summary = {'samples': 0, 'scene_samples': 0, 'models_observed': 0,
               'unstable_scene_samples': 0, 'invalid_samples': 0,
               'invalid_reason_counts': {}, 'validity_runs': [],
               'complete': False, 'stop_reason': 'deadline', 'operator_visibility_only': True}
    try:
        while monotonic() - started < seconds:
            row = sample()
            row['elapsed_seconds'] = monotonic() - started
            reasons = sample_reasons(row)
            row['eligible'] = not reasons
            row['invalid_reasons'] = reasons
            emit({'kind': 'sample', **row})
            summary['samples'] += 1
            runs = summary['validity_runs']
            if not runs or runs[-1]['invalid_reasons'] != reasons:
                runs.append({'first_seconds': row['elapsed_seconds'],
                             'invalid_reasons': reasons, 'samples': 0})
            runs[-1]['last_seconds'] = row['elapsed_seconds']
            runs[-1]['samples'] += 1
            if reasons:
                summary['invalid_samples'] += 1
                for reason in reasons:
                    counts = summary['invalid_reason_counts']
                    counts[reason] = counts.get(reason, 0) + 1
            else:
                summary['scene_samples'] += 1
                scene = row.get('scene', {})
                if scene.get('bounds_stable'):
                    summary['models_observed'] += len(scene.get('models', []))
                else:
                    summary['unstable_scene_samples'] += 1
            # Invalid readiness marks a sample, not the end of the session.
            # Recovery resumes scene reads within the SAME original deadline;
            # neither an invalid spell nor recovery extends or restarts it.
            sleep(min(.01, max(0, seconds - (monotonic() - started))))
        else:
            summary['complete'] = True
    except (OSError, ValueError, struct.error, KeyboardInterrupt) as error:
        summary['stop_reason'] = type(error).__name__ + ': ' + str(error)
    summary['elapsed_seconds'] = monotonic() - started
    summary['usable_window'] = summary['complete'] and summary['models_observed'] > 0
    summary['all_samples_eligible'] = summary['samples'] > 0 and summary['invalid_samples'] == 0
    emit({'kind': 'end', 'utc': utc(), **summary})
    return summary


def argument_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', required=True, type=Path)
    parser.add_argument('--pid', required=True, type=int)
    parser.add_argument('--phase', default='reproduce', choices=('reproduce', 'visible', 'missing'),
        help='reproduce permits turning to alternate visible/missing states; it does not label them automatically')
    parser.add_argument('--operator-ready', required=True, action='store_true',
        help='Use only after fresh operator confirmation and an explicit start message')
    parser.add_argument('--seconds', type=float, default=DEFAULT_SECONDS)
    parser.add_argument('--output', required=True, type=Path)
    return parser


def main():
    parser = argument_parser()
    args = parser.parse_args()
    if not math.isfinite(args.seconds) or not 0 < args.seconds <= MAX_SECONDS:
        parser.error(f'Capture duration must be greater than zero and at most {MAX_SECONDS} seconds')
    profile = json.loads(args.profile.read_text(encoding='utf-8'))
    addresses = validate_profile(profile)
    reader = Reader(args.pid)
    try:
        image = reader.image()
        with image.open('rb') as image_file:
            hasher = hashlib.sha256()
            for chunk in iter(lambda: image_file.read(1048576), b''):
                hasher.update(chunk)
            digest = hasher.hexdigest()
        if digest != profile['image_sha256']:
            raise ValueError('EXE hash differs from profile; refresh verified addresses before sampling')
        pause = find_pause(reader, addresses)
        with args.output.open('x', encoding='utf-8') as output:
            def emit(row):
                output.write(json.dumps(row, ensure_ascii=False, allow_nan=False) + '\n')
                output.flush()
            emit({'kind': 'begin', 'utc': utc(), 'pid': args.pid, 'image': str(image),
                'image_sha256': digest, 'phase': args.phase, 'seconds': args.seconds,
                'read_only': True, 'external_reads_are_best_effort': True,
                'invalid_state_policy': 'mark_sample_continue_to_original_deadline'})
            print(f'CAPTURE BEGIN: {args.phase}, {args.seconds:g}s', flush=True)
            def sample():
                state = read_state(reader, addresses, pause)
                if state['invalid_reasons']:
                    return {'state': state}
                scene = scene_rows(reader, addresses)
                # Check readiness again after the scene read; neither check
                # implies an atomic native frame or proves the HMD is worn.
                after = read_state(reader, addresses, pause)
                return {'state': after, 'state_before': state, 'scene': scene}
            result = capture(sample, emit, args.seconds)
            # Full timing runs are in JSONL; keep the terminal end notice short.
            print('CAPTURE END: ' + json.dumps({key: value for key, value in result.items()
                if key != 'validity_runs'}), flush=True)
            return 0 if result['usable_window'] else 2
    finally:
        reader.close()


if __name__ == '__main__':
    raise SystemExit(main())
