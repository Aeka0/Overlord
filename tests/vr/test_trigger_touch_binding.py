"""Contact must use the native capacitive component, independently of trigger pull."""
import json
from pathlib import Path
import unittest


class TriggerContactBindingTests(unittest.TestCase):
    def test_native_touch_is_explicit_and_separate_from_fire(self):
        root = Path(__file__).resolve().parents[2] / 'data/vr_input'
        manifest = json.loads((root / 'actions.json').read_text(encoding='utf-8'))
        binding = json.loads((root / 'oculus_touch.json').read_text(encoding='utf-8'))
        actions = {action['name']: action for action in manifest['actions']}
        sources = binding['bindings']['/actions/gameplay']['sources']
        for hand in ('left', 'right'):
            contact = '/actions/gameplay/in/' + hand + '_trigger_touch'
            fire = '/actions/gameplay/in/' + hand + '_trigger'
            candidates = [source for source in sources if any(value.get('output') == contact
                          for value in source.get('inputs', {}).values())]
            self.assertEqual(len(candidates), 1)
            source = candidates[0]
            self.assertEqual(source['path'], '/user/hand/' + hand + '/input/trigger')
            self.assertEqual(source['mode'], 'button')
            self.assertNotIn('force_input', source['parameters'])
            self.assertEqual(source['inputs'], {'touch': {'output': contact}})
            self.assertEqual(source['parameters']['haptic_amplitude'], '0')
            self.assertEqual(actions[contact]['type'], 'boolean')
            self.assertEqual(actions[contact]['requirement'], 'optional')
            self.assertTrue(any(s['mode'] == 'trigger' and s.get('inputs', {}).get('click', {}).get('output') == fire
                                for s in sources))


if __name__ == '__main__':
    unittest.main()
