import importlib.util
import json
from pathlib import Path
import tempfile
from unittest.mock import patch
source = Path('C:/Users/25007/.codex/worktrees/2c3c/cnetmod/testing/http3/http_client_e2e.py')
spec = importlib.util.spec_from_file_location('snapshot_fixture', source)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
class PathProbe:
    def __init__(self, *outcomes):
        self.outcomes = iter(outcomes)
    def read_text(self, **kwargs):
        value = next(self.outcomes)
        if isinstance(value, Exception):
            raise value
        return value
assert module.read_json_snapshot(PathProbe('{"ok": 1}')) == {'ok': 1}
with patch.object(module.time, 'sleep', return_value=None):
    assert module.read_json_snapshot(PathProbe(PermissionError('sharing'), '{"ok": 2}')) == {'ok': 2}
    assert module.read_json_snapshot(PathProbe('{', '{"ok": 3}')) == {'ok': 3}
for outcome, expected in ((PermissionError('persistent'), PermissionError), ('{', json.JSONDecodeError)):
    try:
        module.read_json_snapshot(PathProbe(outcome), timeout=0)
    except expected:
        pass
    else:
        raise AssertionError('persistent error was accepted')
with tempfile.TemporaryDirectory(prefix='cnetmod-snapshot-sharing-') as directory:
    destination = Path(directory) / 'stats.json'
    temporary = Path(directory) / 'stats.tmp'
    destination.write_text('{"old": true}')
    temporary.write_text('{"new": true}')
    with destination.open('r'):
        try:
            temporary.replace(destination)
        except PermissionError:
            print('Windows open reader denies concurrent destination replace: observed')
        else:
            print('Windows concurrent replace permitted on this filesystem')
    if temporary.exists():
        temporary.replace(destination)
    assert module.read_json_snapshot(destination) == {'new': True}
print('snapshot checks: 5 PASS; persistent PermissionError/JSONDecodeError fail closed')
