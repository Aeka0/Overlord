import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('pipeline_analysis', ROOT / 'tools/analyze_vr_pipeline_capture.py')
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


def snapshot(end=False):
    return f'''[VR] status
  frame_pipeline: schema=1 pid=42 tick_ms={15000 if end else 10000} snapshot_span_ms=2 session=1 device=3 reference=4 backend=openxr state=running focused=1 enabled=1 native_ready=1 owner_ready=1
  frame_pipeline_counts: presents={1800 if end else 800} runtime_presents={1800 if end else 800} completed={900 if end else 400} world={880 if end else 380} menu_only=10 empty=10 runtime_failures=0 owner_completed={880 if end else 380} owner_failures=0
  frame_pipeline_admission: unarmed=20 accepted={880 if end else 380} invalid_pair=0 production_busy={900 if end else 400} pair_unavailable=0
  frame_pipeline_cpu: transaction_samples={880 if end else 380} transaction_total_us={4000000 if end else 1000000} eye0_samples={880 if end else 380} eye0_total_us={2000000 if end else 500000} eye1_samples={880 if end else 380} eye1_total_us={2000000 if end else 500000}
  client: product="Overlord" version="Beta 7" configuration=RelWithDebInfo git_hash=abc123
  target_ready=yes scene_initialized=yes frontend=no
  mapname="trainer"
  cl_ingame=yes
  cl_paused=0
  r_postAA=3 "SMAA T2x"
  r_vsync=no
  com_maxfps=250
  com_waitEndFrameMode=0
'''


class PipelineAnalysisTests(unittest.TestCase):
    def test_window_deltas_and_wall_time(self):
        result = analysis.analyze_pair(analysis.parse_snapshot(snapshot()), analysis.parse_snapshot(snapshot(True)))
        self.assertTrue(result['usable'])
        self.assertEqual(result['metrics']['native_presents_hz'], 200)
        self.assertEqual(result['metrics']['world_submissions_hz'], 100)
        self.assertEqual(result['metrics']['native_presents_per_world'], 2)
        self.assertEqual(result['delta']['admission']['production_busy'], 500)
        self.assertEqual(result['metrics']['owner_wall_us_per_sample']['transaction'], 6000)
        self.assertEqual(result['metrics']['owner_wall_us_per_sample']['eye0'], 3000)

    def test_invalid_context_never_produces_metrics(self):
        for original, changed, reason in (
            ('pid=42', 'pid=43', 'pid_changed'),
            ('session=1', 'session=2', 'session_changed'),
            ('cl_paused=0', 'cl_paused=1', 'end_paused_or_unknown'),
            ('backend=openxr', 'backend=openvr', 'end_not_openxr'),
            ('presents=1800', 'presents=200', 'counts_counter_reset'),
            ('focused=1', 'focused=0', 'end_focused_false'),
        ):
            with self.subTest(reason=reason):
                after = snapshot(True).replace(original, changed)
                result = analysis.analyze_pair(analysis.parse_snapshot(snapshot()), analysis.parse_snapshot(after))
                self.assertFalse(result['usable'])
                self.assertIn(reason, result['invalid_reasons'])
                self.assertIsNone(result['metrics'])

    def test_fresh_zip_entries_and_unicode_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / '采集基线.zip'
            with zipfile.ZipFile(path, 'w') as archive:
                archive.writestr('previous-status.txt', 'historical unrelated report')
                archive.writestr('status-1.txt', snapshot())
                archive.writestr('status-2.txt', snapshot(True))
            self.assertTrue(analysis.read_bundle(path)['usable'])

    def test_old_or_incomplete_capture_is_explicit(self):
        with self.assertRaisesRegex(ValueError, 'prepared capture build'):
            analysis.parse_snapshot('[VR] status\n  historical counters only\n')
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'incomplete.zip'
            with zipfile.ZipFile(path, 'w') as archive:
                archive.writestr('status-1.txt', snapshot())
            with self.assertRaisesRegex(ValueError, 'status-2'):
                analysis.read_bundle(path)


if __name__ == '__main__':
    unittest.main()
