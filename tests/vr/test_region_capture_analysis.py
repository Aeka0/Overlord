import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('analysis', Path(__file__).resolve().parents[2] / 'tools/analyze_region_capture.py')
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


class AnalysisTests(unittest.TestCase):
    def test_expanded_storage_capture_declares_its_own_limit(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'capture.bin'
            rows=[(100,1,19,0,0,524288,8,8,32),
                  (101,1,18,1,2,123,313592,1135,0,3,99),(102,2,11,0,0,0,0,2)]
            with path.open('wb') as target:
                target.write(b'H2VREG01'+struct.pack('<3Q',1000,1,0))
                for row in rows:target.write(analysis.ROW.pack(*(row+(0,)*(11-len(row)))))
            result=analysis.analyze(path,Path(temp)/'report')
            self.assertEqual(result['surface_budget']['capacity_bytes'],524288)
            self.assertEqual(result['surface_budget']['index_unit_bytes'],8)
            self.assertEqual(result['surface_budget']['over_limit_observations'],0)

    def test_surface_exhaustion_is_distinct_from_adapter_omission(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'capture.bin'
            rows = [
                (100, 7, 18, 1, 0, 123, 200000, 1000, 0, 4, 999),
                (101, 7, 15, 8, 0, 44, 55, 66, 77, 2 << 32, 88),
                (102, 7, 18, 8, 3, 123, 262240, 0, 0, 2 << 32, 55),
                (103, 7, 15, 9, 0, 44, 55, 66, 77, 3 << 32, 88),
                (104, 7, 18, 9, 3, 123, 262240, 0, 0, 3 << 32, 55),
                (105, 7, 15, 10, 0, 44, 55, 66, 77, 2 << 32, 88),
                (106, 7, 18, 10, 3, 123, 262144, 0, 0, 2 << 32, 55),
                (107, 7, 16, 8, 0, 123, 0, 4, 1024, 12345, 999),
                (108, 7, 17, 8, 1, 0x3f800000, 0, 0, 0, 0, 0),
                (109, 7, 18, 1, 2, 123, 262240, 1000, 0, 4, 999),
                (110, 2, 11, 0, 0, 0, 0, 2),
            ]
            with path.open('wb') as target:
                target.write(b'H2VREG01' + struct.pack('<3Q', 1000, 1, 0))
                for row in rows:
                    target.write(analysis.ROW.pack(*(row + (0,) * (11 - len(row)))))
            result = analysis.analyze(path, Path(temp) / 'report')
            self.assertEqual(result['surface_budget']['native_model_failures_over_limit'], 1)
            self.assertEqual(result['surface_budget']['maximum_cursor_bytes'], 262240)
            self.assertEqual(result['surface_budget']['generator_draw_types'], {'4': 1})
            import json
            captured = [json.loads(line) for line in (Path(temp) / 'report/rigid_models.jsonl').read_text().splitlines()]
            self.assertEqual(captured[-1]['position'], [1.0, 0.0, 0.0])
            self.assertEqual(captured[-2]['record_index'], 0)

    def test_pair_timing_failure_and_partial_tail(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'capture.bin'
            rows = [
                (1000, 1, 9, 0, 0, 1),       # normal marker
                (1000, 1, 4, 23, 1),         # right owner begin
                (1100, 1, 5, 23, 1, 7, 90),  # 100 ms; 7 DrawIndexed calls
                (1101, 1, 6, 23, 1, 18, 0, 7, 13, 999, 0),
                (1110, 1, 8, 23, 1, 16, 1),
                (1110, 2, 10, 0, 0, 0),
                (1111, 2, 10, 0, 0, 2),      # two dropped events after measured span
            ]
            with path.open('wb') as target:
                target.write(b'H2VREG01' + struct.pack('<3Q', 1000, 123, 999))
                for row in rows:
                    target.write(analysis.ROW.pack(*(row + (0,) * (11 - len(row)))))
                target.write(b'partial')
            result = analysis.analyze(path, Path(temp) / 'report')
            self.assertEqual(result['slow_spans'][0]['ms'], 100)
            self.assertEqual(result['anomalies'][0]['count'], 7)
            self.assertEqual(result['dropped_reported'], 2)
            self.assertFalse(result['clean_stop'])
            self.assertEqual(result['trailing_bytes'], 7)
            self.assertEqual(result['unmatched_begins'], 0)

    def test_successful_right_submit_intervals(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'capture.bin'
            with path.open('wb') as target:
                target.write(b'H2VREG01' + struct.pack('<3Q', 1000, 1, 0))
                for tick in (100, 200):
                    target.write(analysis.ROW.pack(tick, 1, 1, 1, 0, 6, 0, 0, 0, 0, 0))
                target.write(analysis.ROW.pack(201, 2, 11, 0, 0, 0, 0, 1, 0, 0, 0))
            result = analysis.analyze(path, Path(temp) / 'report')
            self.assertTrue(result['clean_stop'])
            self.assertEqual(result['slow_spans'][0]['stage'], 'stereo_submit_interval')
            self.assertEqual(result['slow_spans'][0]['ms'], 100)
            self.assertIn('stereo_submit_interval_ms', (Path(temp) / 'report/timeline.csv').read_text())

    def test_nested_native_waits_and_job_state(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'capture.bin'
            rows = [
                (100, 7, 12, 99, 11, 123),
                (110, 7, 12, 99, 11, 123),
                (150, 7, 13, 99, 11, 123),
                (190, 7, 13, 99, 11, 123),
                (191, 8, 14, 44, 3, 123, (1 << 32) | 1, 1, 13, 44, 1000),
            ]
            with path.open('wb') as target:
                target.write(b'H2VREG01' + struct.pack('<3Q', 1000, 1, 0))
                for row in rows:
                    target.write(analysis.ROW.pack(*(row + (0,) * (11 - len(row)))))
            result = analysis.analyze(path, Path(temp) / 'report')
            self.assertEqual([s['ms'] for s in result['slow_spans']], [40, 90])
            self.assertEqual(result['unmatched_begins'], 0)
            import csv
            with (Path(temp) / 'report/phases.csv').open() as source:
                states = list(csv.DictReader(source))
            self.assertEqual(states[-1]['flag_BEC'], '1')
            self.assertEqual(states[-1]['flag_BF4'], '0')
            self.assertEqual(states[-1]['list24_type'], '13')
            self.assertEqual(states[-1]['family'], '1000')

    def test_lost_phase_end_cannot_become_long_native_wait(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'capture.bin'
            rows = [(100, 7, 12, 99, 11), (101, 2, 10),
                    (200, 2, 10, 0, 0, 1),  # missing end, reported loss
                    (300, 7, 12, 99, 11), (310, 7, 13, 99, 11),
                    (5000, 7, 13, 99, 11),  # missing new begin: must not pair with 100
                    (5100, 2, 11, 0, 0, 2)]
            with path.open('wb') as target:
                target.write(b'H2VREG01' + struct.pack('<3Q', 1000, 1, 0))
                for row in rows:
                    target.write(analysis.ROW.pack(*(row + (0,) * (11 - len(row)))))
            result = analysis.analyze(path, Path(temp) / 'report')
            self.assertEqual(result['slow_spans'], [])
            self.assertGreater(result['timing_intervals_excluded_for_loss'], 0)


if __name__ == '__main__':
    unittest.main()
