"""Bounded x64-zt typed records, independent of game pointers and runtimes.

Protocol: Joelrau/x64-zt utils/io/assetmanager.hpp (3802db4). The guarded
reader follows the existing Baseplate offline adapter; this module adds the
writer needed by H2 asset conversion. No stored native pointer is dereferenced.
"""
from __future__ import annotations
import struct
from pathlib import Path

LIMIT = 64 * 1024 * 1024


class Reader:
    def __init__(self, path):
        path = Path(path)
        if path.stat().st_size > LIMIT:
            raise ValueError('asset dump exceeds 64 MiB')
        self.data = path.read_bytes()
        self.offset = 0
        self.entries = []

    def take(self, size):
        if not 0 <= size <= len(self.data) - self.offset:
            raise ValueError('truncated typed asset record')
        start = self.offset
        self.offset += size
        return self.data[start:self.offset]

    def record(self, kind, stride=1):
        tag = self.take(1)[0]
        if tag == 9:
            index, element = struct.unpack('<II', self.take(8))
            if index >= len(self.entries):
                raise ValueError('invalid asset record reference')
            old_kind, old_stride, value = self.entries[index]
            if old_kind != kind or old_stride != stride:
                raise ValueError('incompatible asset record reference')
            if isinstance(value, str):
                if element:
                    raise ValueError('invalid string reference offset')
            else:
                if element >= len(value) // stride:
                    raise ValueError('array reference outside record')
                value = value[element * stride:]
            return value
        if tag != kind:
            raise ValueError(f'unexpected typed record {tag}, expected {kind}')
        present = self.take(1)[0]
        if present not in (0, 1):
            raise ValueError('invalid record presence')
        if not present:
            return '' if kind in (6, 7) else b''
        if kind in (6, 7):
            end = self.data.find(b'\0', self.offset, self.offset + 4097)
            if end < 0:
                raise ValueError('unterminated asset string')
            value = self.take(end - self.offset + 1)[:-1].decode('utf-8')
        else:
            count = struct.unpack('<I', self.take(4))[0]
            if count > LIMIT // stride:
                raise ValueError('unbounded asset array')
            value = self.take(count * stride)
        self.entries.append((kind, stride, value))
        return value

    def array(self, stride, count=None):
        value = self.record(8, stride)
        if count is not None and len(value) != stride * count:
            raise ValueError('array extent does not match header')
        return value

    def text(self, asset=False):
        return self.record(7 if asset else 6)

    def finish(self):
        if self.offset != len(self.data):
            raise ValueError('unconsumed asset records')


class Writer:
    def __init__(self):
        self.data = bytearray()

    def record(self, kind, value, stride=1):
        self.data.extend((kind, bool(value)))
        if not value:
            return
        if kind in (6, 7):
            encoded = value.encode('utf-8')
            if b'\0' in encoded or len(encoded) > 4096:
                raise ValueError('invalid asset string')
            self.data.extend(encoded + b'\0')
        else:
            if len(value) % stride:
                raise ValueError('unaligned asset array')
            self.data.extend(struct.pack('<I', len(value) // stride))
            self.data.extend(value)
        if len(self.data) > LIMIT:
            raise ValueError('asset dump exceeds 64 MiB')

    def array(self, value, stride):
        self.record(8, value, stride)

    def text(self, value, asset=False):
        self.record(7 if asset else 6, value)

    def save(self, path):
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(self.data)
