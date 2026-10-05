"""Offline regressions: no process, HMD, SteamVR or game access."""
import importlib.util
from pathlib import Path
import struct
import unittest
from unittest.mock import patch
import tempfile
import json

path = Path(__file__).resolve().parents[2] / 'tools/capture_equipment_visibility.py'
spec = importlib.util.spec_from_file_location('capture_equipment_visibility', path)
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class Clock:
    def __init__(self): self.now = 0
    def read(self): return self.now
    def sleep(self, seconds): self.now += seconds


def valid_state():
    return {'paused': False, 'focused': True, 'grips_valid': True, 'gameplay': True,
        'input_age_ms': 10, 'scene_age_ms': 10, 'equipment_age_ms': 10,
        'magazines': [{'weapon': 22, 'hand': 0, 'rounds': 30}]}


class CaptureTests(unittest.TestCase):
    def test_wrong_exe_aborts_before_address_reads(self):
        names = ('input', 'scene', 'equipment', 'inventory', 'scene_count',
            'scene_models', 'scene_visibility', 'dvar_count', 'dvar_pool',
            'chest_lighting', 'knife_lighting', 'magazine_lighting',
            'attachment_counters', 'chest_counters')
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder)
            image = base / 'image'; image.write_bytes(b'different build')
            profile = base / 'profile.json'
            profile.write_text(json.dumps({'schema': 'h2-equipment-v1',
                'image_sha256': '0' * 64, 'addresses': dict.fromkeys(names, '0x10000')}))
            class Reader:
                closed = False
                def __init__(self, pid): pass
                def image(self): return image
                def read(self, *args): raise AssertionError('Wrong-build address read')
                def close(self): Reader.closed = True
            args = ['capture', '--profile', str(profile), '--pid', '1',
                '--phase', 'visible', '--operator-ready', '--output', str(base / 'out.jsonl')]
            with patch.object(capture, 'Reader', Reader), patch('sys.argv', args):
                with self.assertRaisesRegex(ValueError, 'EXE hash differs'): capture.main()
            self.assertTrue(Reader.closed)
            self.assertFalse((base / 'out.jsonl').exists())

    def run_window(self, sample, seconds=.06):
        clock = Clock()
        rows = []
        result = capture.capture(sample, rows.append, seconds, clock.read, clock.sleep)
        self.assertEqual(rows[-1]['kind'], 'end')
        return result, rows

    def test_deadline_and_no_automatic_second_window(self):
        result, rows = self.run_window(lambda: {'state': {'invalid_reasons': []},
            'scene': {'bounds_stable': True, 'models': [{}]}})
        self.assertTrue(result['usable_window'])
        self.assertLessEqual(result['elapsed_seconds'], .060001)
        self.assertEqual(sum(row['kind'] == 'end' for row in rows), 1)

    def test_entirely_invalid_window_runs_to_deadline_without_becoming_usable(self):
        result, rows = self.run_window(lambda: {'state': {'invalid_reasons': ['game_paused']}})
        self.assertFalse(result['usable_window'])
        self.assertTrue(result['complete'])
        self.assertGreater(result['samples'], 1)
        self.assertEqual(result['invalid_samples'], result['samples'])
        self.assertAlmostEqual(result['elapsed_seconds'], .06)
        self.assertEqual(result['stop_reason'], 'deadline')
        self.assertTrue(all(not row['eligible'] for row in rows[:-1]))

    def test_default_thirty_seconds_and_rotation_phase(self):
        args = capture.argument_parser().parse_args(['--profile', 'unused.json',
            '--pid', '1', '--operator-ready', '--output', 'unused.jsonl'])
        self.assertEqual(args.seconds, 30)
        self.assertEqual(args.phase, 'reproduce')
        result, _ = self.run_window(lambda: {'state': {'invalid_reasons': []},
            'scene': {'bounds_stable': True, 'models': [{}]}}, seconds=args.seconds)
        self.assertAlmostEqual(result['elapsed_seconds'], 30)
        self.assertTrue(result['all_samples_eligible'])

    def test_recovery_and_repeated_invalid_spells_keep_original_deadline(self):
        reasons = [['no_held_magazine'], [], ['game_paused'], [],
                   ['scene_age_ms_stale'], []]
        calls = []
        def sample():
            index = min(len(calls), len(reasons) - 1); calls.append(index)
            return {'state': {'invalid_reasons': reasons[index]},
                'scene': {'bounds_stable': True, 'models': [{}]}}
        result, rows = self.run_window(sample)
        self.assertTrue(result['complete'])
        self.assertTrue(result['usable_window'])
        self.assertFalse(result['all_samples_eligible'])
        self.assertEqual(result['invalid_samples'], 3)
        self.assertEqual(result['models_observed'], result['samples'] - 3)
        self.assertAlmostEqual(result['elapsed_seconds'], .06)
        self.assertEqual([run['invalid_reasons'] for run in result['validity_runs']], reasons)
        self.assertFalse(rows[0]['eligible'])
        self.assertTrue(rows[1]['eligible'])

    def test_recovery_inside_a_read_does_not_validate_earlier_invalid_data(self):
        result, _ = self.run_window(lambda: {'state': {'invalid_reasons': []},
            'state_before': {'invalid_reasons': ['game_paused']},
            'scene': {'bounds_stable': True, 'models': [{}]}})
        self.assertTrue(result['complete'])
        self.assertEqual(result['models_observed'], 0)
        self.assertFalse(result['usable_window'])

    def test_empty_or_racing_scene_is_not_a_successful_reproduction(self):
        for scene in ({'bounds_stable': True, 'models': []},
                      {'bounds_stable': False, 'models': [{}]}):
            result, _ = self.run_window(lambda: {'state': {'invalid_reasons': []}, 'scene': scene})
            self.assertTrue(result['complete'])
            self.assertFalse(result['usable_window'])

    def test_read_error_preserves_partial_window_and_end_marker(self):
        calls = []
        def sample():
            if calls: raise OSError('process exited')
            calls.append(1)
            return {'state': {'invalid_reasons': []}, 'scene': {'bounds_stable': True, 'models': [{}]}}
        result, rows = self.run_window(sample)
        self.assertFalse(result['complete'])
        self.assertEqual(rows[0]['kind'], 'sample')
        self.assertIn('process exited', result['stop_reason'])

    def test_freshness_pause_focus_and_inventory_gates(self):
        self.assertEqual(capture.invalid_reasons(valid_state()), [])
        for key, value in (('paused', True), ('focused', False), ('grips_valid', False),
                ('gameplay', False), ('magazines', []), ('input_age_ms', 151),
                ('scene_age_ms', -1), ('equipment_age_ms', float('nan'))):
            state = valid_state(); state[key] = value
            with self.subTest(key=key): self.assertTrue(capture.invalid_reasons(state))

    def test_model_kind_and_visibility_stride(self):
        addresses = {'scene_count': 0x10000, 'scene_models': 0x20000,
            'scene_visibility': 0x30000, 'chest_lighting': 0x40000,
            'knife_lighting': 0x50000, 'magazine_lighting': 0x60000,
            'attachment_counters': 0x70000, 'chest_counters': 0x80000}
        entries = bytearray(3 * 0x98)
        for i, handle in enumerate((0x40002, 0x60000, 0x60001)):
            struct.pack_into('<Q', entries, i * 0x98 + 0x68, handle)
        visibility = bytearray(8 * 1536)
        visibility[1536 + 1] = 2
        blocks = {0x10000: struct.pack('<I', 3), 0x20000: entries,
            0x30000: visibility, 0x70000: bytes(48), 0x80000: bytes(16)}
        class Reader:
            def read(self, at, size):
                data = blocks[at]; assert len(data) == size; return data
        result = capture.scene_rows(Reader(), addresses)
        self.assertEqual([row['kind'] for row in result['models']], ['chest', 'held_magazine'])
        self.assertEqual(result['models'][1]['visibility'][1], 2)
        blocks[0x10000] = struct.pack('<I', 1537)
        with self.assertRaises(ValueError): capture.scene_rows(Reader(), addresses)


if __name__ == '__main__':
    unittest.main()
