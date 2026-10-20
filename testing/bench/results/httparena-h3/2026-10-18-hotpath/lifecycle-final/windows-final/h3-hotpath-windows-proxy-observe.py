"""Observe one fixture proxy lifecycle without changing source or test timing."""
import importlib.util
import json
from pathlib import Path
import sys
source = Path('C:/Users/25007/.codex/worktrees/2c3c/cnetmod/testing/http3/http_client_e2e.py')
spec = importlib.util.spec_from_file_location('observed_client_fixture', source)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
original_stop = module.stop_process
observations = []
def observed_stop(process):
    is_proxy = any('early_data_loss_proxy.py' in str(argument) for argument in process.args)
    before = process.poll()
    output = original_stop(process)
    if is_proxy:
        row = dict(pid=process.pid, poll_before_cleanup=before, exit_after_cleanup=process.returncode,
                   stdout=output[0], stderr=output[1])
        observations.append(row)
        print('PROXY_LIFECYCLE ' + json.dumps(row), flush=True)
    return output
module.stop_process = observed_stop
result = module.main()
assert len(observations) == 1, observations
assert observations[0]['poll_before_cleanup'] is None, observations
assert 'Traceback' not in observations[0]['stderr'], observations
raise SystemExit(result)
