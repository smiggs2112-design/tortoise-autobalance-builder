"""Execute the real C++ intent policy and patch the pinned upstream source."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class IntentTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ is required (provided in CI)')
    def test_cpp_intent_and_privacy_policy(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / ('intents.exe' if os.name == 'nt' else 'intents')
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'enhancements'),
                            str(ROOT / 'tests/test_intents.cpp'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


@unittest.skipUnless(os.environ.get('VOICES_SOURCE_TEST_ROOT'), 'pinned source checkout required')
class PinnedPatchTests(unittest.TestCase):
    def test_real_upstream_patch_and_fail_closed_repeat(self):
        source = Path(os.environ['VOICES_SOURCE_TEST_ROOT']) / 'src'
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / 'src'
            shutil.copytree(source, target)
            command = [os.sys.executable, str(ROOT / 'enhancements/enhance_voices.py'),
                       '--source', str(target), '--assets', str(ROOT / 'enhancements'),
                       '--knowledge-target', str(Path(tmp) / 'knowledge.tsv')]
            subprocess.run(command, check=True)
            manager = (target / 'AzerothVoicesManager.cpp').read_text()
            scripts = (target / 'AzerothVoicesScripts.cpp').read_text()
            # Both entry points actually use the same reader; worker never looks up a Player.
            self.assertIn('IntelligenceV4::ReadState(', manager)
            self.assertIn('IntelligenceV4::ReadState(', (target / 'AzerothVoicesDeterministic.h').read_text())
            self.assertIn('botName + " pull my target"', scripts)
            self.assertTrue((target / 'AzerothVoicesIntentV4.h').is_file())
            repeated = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(repeated.returncode, 0)
            self.assertIn('expected one anchor', repeated.stderr)


if __name__ == '__main__':
    unittest.main()
