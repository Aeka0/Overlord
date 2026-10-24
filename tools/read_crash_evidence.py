"""Read bounded Overlord unwind evidence without game binaries or dependencies."""
from __future__ import annotations

import argparse
import json
import mmap
from pathlib import Path
import struct

STREAM = 0x4F564C44
TEXTURE_STREAM = 0x4F565458
HEADER = struct.Struct('<16Q')
MODULE = struct.Struct('<6Q')
RANGE = struct.Struct('<3Q')
MAX_MODULES, MAX_RANGES = 16, 256


def read_texture_evidence(data, entries):
    selected = next((entry for entry in entries if entry[0] == TEXTURE_STREAM), None)
    if not selected:
        return {'status': 'absent'}
    _, size, location = selected
    event = struct.Struct('<29Q')
    required = HEADER.size + (32 + 256 + 1) * event.size
    if size != required or location > len(data) or size > len(data) - location:
        raise ValueError('Truncated texture evidence stream')
    header = HEADER.unpack_from(data, location)
    if header[0] != 1 or header[1] != event.size or header[2] != 256 or header[3] > 256 or header[4] > 32:
        raise ValueError('Unsupported texture evidence layout')
    fields = ('sequence call tick thread operation device generation caller texture context pair eye result '
              'descriptor_valid initial_data width height mips array_size format samples quality usage bind cpu misc '
              'source command detail').split()
    phases = {1: 'texture_begin', 2: 'texture_end', 3: 'execute_begin', 4: 'execute_end', 5: 'resources_reset',
              6: 'device_observed', 7: 'post_aa_begin', 8: 'post_aa_end', 9: 'scope_abandoned'}

    def record(offset):
        item = dict(zip(fields, event.unpack_from(data, offset)))
        item['phase'] = phases.get(item['operation'], 'unknown')
        item['result_contract'] = 'HRESULT' if item['operation'] == 2 else (
            'native boolean' if item['operation'] == 8 else 'CPU observation; no GPU completion claim')
        item['detail_contract'] = 'AA mode' if item['operation'] in (7, 8) else 'phase-specific metadata'
        for key in ('device', 'caller', 'texture', 'context', 'source', 'command', 'result'):
            item[key] = hex(item[key])
        return item

    active = location + HEADER.size
    recent = active + 32 * event.size
    first = recent + 256 * event.size
    return {'status': 'available', 'version': header[0], 'newest': header[5], 'dropped': header[6],
            'texture_calls': header[7], 'texture_failures': header[8], 'active_overflow': header[9],
            'hooked_vtables': header[10], 'hook_failures': header[11], 'identity_overflow': header[12],
            'sampled_tick': header[14], 'busy_or_missing_records': header[15],
            'failure_policy': 'first device error, otherwise first texture failure; not root-cause proof',
            'ownership_contract': 'pointer identities only; no resource lifetime proof',
            'completion_contract': 'CPU call boundaries only; no GPU retirement proof',
            'active': [record(active + i * event.size) for i in range(header[4])],
            'recent': [record(recent + i * event.size) for i in range(header[3])],
            'first_failure': record(first) if header[13] else None}


def read_evidence(data):
    def unpack(layout, offset):
        if offset < 0 or offset > len(data) or layout.size > len(data) - offset:
            raise ValueError('Truncated minidump structure')
        return layout.unpack_from(data, offset)

    if len(data) < 32 or data[:4] != b'MDMP':
        raise ValueError('Not a Windows minidump')
    count, directory = unpack(struct.Struct('<II'), 8)
    if count > 4096 or directory > len(data) or count * 12 > len(data) - directory:
        raise ValueError('Invalid minidump directory')
    entries = [unpack(struct.Struct('<III'), directory + i * 12) for i in range(count)]
    selected = next((entry for entry in entries if entry[0] == STREAM), None)
    if not selected:
        return {'status': 'absent', 'detail': 'This dump has no Overlord bounded-evidence stream'}
    _, size, rva = selected
    required = HEADER.size + MAX_MODULES * MODULE.size + MAX_RANGES * RANGE.size
    if size < required or rva > len(data) or size > len(data) - rva:
        raise ValueError('Truncated Overlord evidence stream')
    header = unpack(HEADER, rva)
    if header[0] != 1 or header[6] > MAX_RANGES or header[7] > MAX_MODULES:
        raise ValueError('Unsupported Overlord evidence layout')

    spans = []
    for kind, stream_size, location in entries:
        if location > len(data) or stream_size > len(data) - location:
            raise ValueError('Stream extends beyond minidump')
        if kind == 5:  # MemoryListStream
            rows, = unpack(struct.Struct('<I'), location)
            if rows > 65536 or 4 + rows * 16 > stream_size:
                raise ValueError('Invalid memory list')
            for index in range(rows):
                address, length, file_offset = unpack(struct.Struct('<QII'), location + 4 + index * 16)
                if file_offset <= len(data) and length <= len(data) - file_offset:
                    spans.append((address, address + length))
        elif kind == 9:  # Memory64ListStream
            rows, file_offset = unpack(struct.Struct('<QQ'), location)
            if rows > 65536 or 16 + rows * 16 > stream_size:
                raise ValueError('Invalid memory64 list')
            for index in range(rows):
                address, length = unpack(struct.Struct('<QQ'), location + 16 + index * 16)
                if file_offset <= len(data) and length <= len(data) - file_offset:
                    spans.append((address, address + length))
                file_offset += length
    merged = []
    for start, end in sorted(spans):
        if merged and start <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(end, merged[-1][1]))
        else:
            merged.append((start, end))

    def covered(address, length):
        return bool(length) and any(start <= address and address + length <= end for start, end in merged)

    modules = []
    for index in range(header[7]):
        base, image_size, timestamp, table, length, scheduled = unpack(MODULE, rva + HEADER.size + index * MODULE.size)
        modules.append({'base': hex(base), 'image_size': hex(image_size), 'pe_timestamp': hex(timestamp),
                        'function_table_rva': hex(table), 'function_table_bytes': length,
                        'table_scheduled': bool(scheduled), 'table_captured': covered(base + table, length)})
    names = {1: 'headers', 2: 'function_table', 3: 'unwind_info', 4: 'code', 5: 'registers'}
    ranges = []
    location = rva + HEADER.size + MAX_MODULES * MODULE.size
    for index in range(header[6]):
        address, length, kind = unpack(RANGE, location + index * RANGE.size)
        ranges.append({'address': hex(address), 'bytes': length, 'kind': names.get(kind, str(kind)),
                       'captured': covered(address, length)})
    return {'status': 'available', 'version': header[0], 'focus_thread': header[1],
            'exception_code': hex(header[2]), 'pc': hex(header[3]), 'stack_pointer': hex(header[4]),
            'sampled_tick': header[5], 'context_available': bool(header[13]),
            'snapshot_contract': 'bounded pre-dump sample; not an atomic process snapshot',
            'candidate_contract': 'stack values nominate code addresses; not an unwound call chain',
            'candidate_count': header[8], 'skipped': header[9], 'requested_bytes': header[10],
            'limits': [name for flag, name in ((1, 'range_count'), (2, 'total_bytes'), (4, 'module_count'),
                       (8, 'candidate_count'), (16, 'unwind_chain'), (32, 'context_unavailable'),
                       (64, 'stack_scan_boundary'), (128, 'module_table_bytes')) if header[15] & flag],
            'byte_budget': header[11], 'scanned_stack_bytes': header[12], 'modules': modules,
            'ranges': ranges, 'captured_ranges': sum(item['captured'] for item in ranges),
            'graphics': read_texture_evidence(data, entries)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dump', type=Path)
    args = parser.parse_args()
    try:
        with args.dump.open('rb') as source, mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ) as data:
            print(json.dumps(read_evidence(data), indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
