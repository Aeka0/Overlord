"""Filesystem failure checks for paired deployment; never touches a real game."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('deploy_client_pair',
    Path(__file__).resolve().parents[2] / 'tools/deploy_client_pair.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class DeploymentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build, self.game, self.backup = [self.root / name for name in ('build', 'game', 'backup')]
        self.game.mkdir()
        self.old, self.new = {}, {}
        for config, name in [('RelWithDebInfo', 'overlord'), ('Debug', 'overlord-debug')]:
            folder = self.build / config
            folder.mkdir(parents=True)
            for suffix in ('.exe', '.pdb'):
                filename = name + suffix
                self.old[filename], self.new[filename] = ('old:' + filename).encode(), ('new:' + filename).encode()
                (self.game / filename).write_bytes(self.old[filename])
                (folder / filename).write_bytes(self.new[filename])
        (self.game / 'config.cfg').write_bytes(b'preserve profile')
        (self.game / 'vr_input').mkdir()
        manifest = {'default_bindings': [{'controller_type': 'oculus_touch', 'binding_url': 'oculus_touch.json'}]}
        for name, value in [('actions.json', manifest), ('oculus_touch.json', {'contact': True})]:
            relative = 'vr_input/' + name
            self.old[relative] = b'{"old": true}'
            self.new[relative] = json.dumps(value).encode()
            (self.game / relative).write_bytes(self.old[relative])
            for config in ('RelWithDebInfo', 'Debug'):
                (self.build / config / 'vr_input').mkdir(exist_ok=True)
                (self.build / config / relative).write_bytes(self.new[relative])

        artwork = Path(__file__).resolve().parents[2] / 'assets/steamvr'
        for config, name in [('RelWithDebInfo', 'overlord'), ('Debug', 'overlord-debug')]:
            filename = name + '.vrmanifest'
            self.old[filename] = b'{"old": true}'
            self.new[filename] = (artwork / filename).read_bytes()
            (self.game / filename).write_bytes(self.old[filename])
            (self.build / config / filename).write_bytes(self.new[filename])
        (self.game / 'steamvr').mkdir()
        for filename in ('cover.png', 'cover-small.png', 'cover-capsule.png'):
            relative = 'steamvr/' + filename
            self.old[relative], self.new[relative] = b'old artwork', (artwork / filename).read_bytes()
            (self.game / relative).write_bytes(self.old[relative])
            for config in ('RelWithDebInfo', 'Debug'):
                (self.build / config / 'steamvr').mkdir(exist_ok=True)
                (self.build / config / relative).write_bytes(self.new[relative])

    def test_both_pairs_and_backups(self):
        record = module.deploy(self.build, self.game, self.backup)
        self.assertTrue(record['deployed'])
        for name in self.new:
            self.assertEqual((self.game / name).read_bytes(), self.new[name])
            self.assertEqual((Path(record['backup']) / name).read_bytes(), self.old[name])
        self.assertEqual((self.game / 'config.cfg').read_bytes(), b'preserve profile')
        self.assertFalse(list(self.game.rglob('*.staging-*')))
        second = module.deploy(self.build, self.game, self.backup)
        self.assertNotEqual(second['backup'], record['backup'])

    def test_missing_debug_pdb_leaves_installation_untouched(self):
        (self.build / 'Debug/overlord-debug.pdb').unlink()
        with self.assertRaises(FileNotFoundError):
            module.deploy(self.build, self.game, self.backup)
        for name in self.old:
            self.assertEqual((self.game / name).read_bytes(), self.old[name])
        self.assertFalse(self.backup.exists())

    def test_locked_debug_rolls_back_regular_pair(self):
        replace = module.os.replace
        calls = 0
        def fail_third(source, target):
            nonlocal calls
            calls += 1
            if calls == 3:
                raise PermissionError('debug executable is running')
            return replace(source, target)
        with patch.object(module.os, 'replace', side_effect=fail_third):
            with self.assertRaises(PermissionError):
                module.deploy(self.build, self.game, self.backup)
        for name in self.old:
            self.assertEqual((self.game / name).read_bytes(), self.old[name])
        self.assertFalse(list(self.game.rglob('*.staging-*')))
        manifest = next(self.backup.glob('*/deployment.json'))
        self.assertFalse(json.loads(manifest.read_text())['deployed'])

    def test_missing_input_binding_leaves_binaries_untouched(self):
        (self.build / 'Debug/vr_input/oculus_touch.json').unlink()
        with self.assertRaises(FileNotFoundError):
            module.deploy(self.build, self.game, self.backup)
        for name in self.old:
            self.assertEqual((self.game / name).read_bytes(), self.old[name])
        self.assertFalse(self.backup.exists())

    def test_missing_capsule_leaves_installation_untouched(self):
        (self.build / 'Debug/steamvr/cover-capsule.png').unlink()
        with self.assertRaises(FileNotFoundError):
            module.deploy(self.build, self.game, self.backup)
        for name in self.old:
            self.assertEqual((self.game / name).read_bytes(), self.old[name])
        self.assertFalse(self.backup.exists())

    def test_mismatched_input_packages_rejected(self):
        (self.build / 'Debug/vr_input/oculus_touch.json').write_text('{}')
        with self.assertRaises(ValueError):
            module.deploy(self.build, self.game, self.backup)
        self.assertFalse(self.backup.exists())

    def test_binding_cannot_escape_package(self):
        (self.build / 'RelWithDebInfo/vr_input/actions.json').write_text(json.dumps(
            {'default_bindings': [{'binding_url': '../outside.json'}]}))
        with self.assertRaises(ValueError):
            module.deploy(self.build, self.game, self.backup)
        self.assertFalse(self.backup.exists())

    def test_input_failure_rolls_back_binaries_and_manifest(self):
        replace = module.os.replace
        def fail_binding(source, target):
            if Path(target) == self.game / 'vr_input/oculus_touch.json':
                raise PermissionError('input binding is locked')
            return replace(source, target)
        with patch.object(module.os, 'replace', side_effect=fail_binding):
            with self.assertRaises(PermissionError):
                module.deploy(self.build, self.game, self.backup)
        for name in self.old:
            self.assertEqual((self.game / name).read_bytes(), self.old[name])
        self.assertFalse(list(self.game.rglob('*.staging-*')))

    def test_new_input_directory_is_installed(self):
        for path in (self.game / 'vr_input').iterdir():
            path.unlink()
        (self.game / 'vr_input').rmdir()
        record = module.deploy(self.build, self.game, self.backup)
        self.assertTrue(record['deployed'])
        self.assertEqual((self.game / 'vr_input/actions.json').read_bytes(), self.new['vr_input/actions.json'])


if __name__ == '__main__':
    unittest.main()
