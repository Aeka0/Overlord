"""Validate Knuckles hardware paths and the OpenVR action contract."""
import json
from pathlib import Path
import unittest


class KnucklesBindingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        folder = Path(__file__).resolve().parents[2] / 'data/vr_input'
        cls.manifest = json.loads((folder / 'actions.json').read_text(encoding='utf-8'))
        cls.binding = json.loads((folder / 'knuckles.json').read_text(encoding='utf-8'))
        cls.gameplay = cls.binding['bindings']['/actions/gameplay']
        cls.sources = cls.gameplay['sources']

    def source_for(self, action):
        output = '/actions/gameplay/in/' + action
        candidates = [source for source in self.sources
                      if any(value.get('output') == output
                             for value in source.get('inputs', {}).values())]
        self.assertEqual(len(candidates), 1, action)
        return candidates[0]

    def test_outputs_match_declared_action_types(self):
        actions = {action['name']: action['type'] for action in self.manifest['actions']}
        self.assertEqual(self.binding['controller_type'], 'knuckles')
        self.assertTrue(any(binding['controller_type'] == 'knuckles' and
                            binding['binding_url'] == 'knuckles.json'
                            for binding in self.manifest['default_bindings']))
        types = {'position': 'vector2', 'click': 'boolean', 'touch': 'boolean'}
        for source in self.sources:
            for component, value in source['inputs'].items():
                self.assertEqual(actions[value['output']], types[component])
        for group, kind in (('poses', 'pose'), ('haptics', 'vibration')):
            for value in self.gameplay[group]:
                self.assertEqual(actions[value['output']], kind)

    def test_locomotion_uses_physical_thumbsticks(self):
        for hand, axis, click in (('left', 'move', 'sprint'), ('right', 'turn', 'jump')):
            source = self.source_for(axis)
            self.assertEqual(source['path'], '/user/hand/' + hand + '/input/thumbstick')
            self.assertEqual(source['mode'], 'joystick')
            self.assertEqual(source['inputs']['position']['output'], '/actions/gameplay/in/' + axis)
            self.assertEqual(source['inputs']['click']['output'], '/actions/gameplay/in/' + click)

    def test_trigger_pull_and_capacitive_touch_are_independent(self):
        for hand in ('left', 'right'):
            fire = self.source_for(hand + '_trigger')
            touch = self.source_for(hand + '_trigger_touch')
            path = '/user/hand/' + hand + '/input/trigger'
            self.assertEqual(fire['path'], path)
            self.assertEqual(fire['mode'], 'button')
            self.assertEqual(fire['parameters']['force_input'], 'value')
            activate = float(fire['parameters']['click_activate_threshold'])
            deactivate = float(fire['parameters']['click_deactivate_threshold'])
            self.assertGreater(deactivate, 0)
            self.assertLess(deactivate, activate)
            self.assertLessEqual(activate, 0.01)
            self.assertEqual(touch['path'], path)
            self.assertEqual(touch['mode'], 'button')
            self.assertEqual(touch['inputs'], {'touch': {
                'output': '/actions/gameplay/in/' + hand + '_trigger_touch'}})
            self.assertNotIn('force_input', touch.get('parameters', {}))

    def test_grip_pressure_has_release_hysteresis(self):
        for hand in ('left', 'right'):
            source = self.source_for(hand + '_squeeze')
            self.assertEqual(source['path'], '/user/hand/' + hand + '/input/grip')
            self.assertEqual(source['mode'], 'button')
            self.assertEqual(source['parameters']['force_input'], 'force')
            activate = float(source['parameters']['click_activate_threshold'])
            deactivate = float(source['parameters']['click_deactivate_threshold'])
            self.assertTrue(0 < deactivate < activate < 1)

    def test_pause_and_secondary_actions_are_accessible(self):
        for hand, component, action in (
                ('left', 'a', 'menu_recenter'), ('right', 'a', 'right_primary'),
                ('left', 'b', 'left_secondary'), ('right', 'b', 'right_secondary')):
            source = self.source_for(action)
            self.assertEqual(source['path'], '/user/hand/' + hand + '/input/' + component)
            self.assertEqual(source['mode'], 'button')


if __name__ == '__main__':
    unittest.main()
