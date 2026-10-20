"""Collect finished serial CTest evidence; list tests without running them."""
from pathlib import Path
import json
import re
import shutil
import subprocess
records=Path('C:/Users/25007/.codex/tmp/cnetmod-h3-pmtu-final-system-windows-20261018-ffa91ab479f4422791aaeacc51dbe847')
build=Path('C:/Users/25007/.codex/tmp/cnetmod-h3-windows')
ctest='D:/runtime/vs/Microsoft Visual Studio/18/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe'
driver=(records/'cnetmod-h3-pmtu-final-windows-final.ps1').read_text()
pattern=re.search(r"-R '([^']+)'",driver)[1]
command=[ctest,'--test-dir',str(build),'-C','Release','--show-only=json-v1','-R',pattern]
data=subprocess.check_output(command)
(records/'test-enumeration.json').write_bytes(data)
enum=json.loads(data)
assert len(enum['tests'])==38,len(enum['tests'])
text=(records/'tests.log').read_text()
assert '100% tests passed, 0 tests failed out of 38' in text
assert 'Skipped' not in text and 'Not Run' not in text
shutil.copy2(build/'Testing/Temporary/LastTest.log',records/'LastTest-full38.log')
summary={'ctest_count':38,'passed':38,'skipped':0,
 'enumeration_command':command,'tests':[r['name'] for r in enum['tests']]}
(records/'ctest-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print('CTest38/38,0skip, enumeration verified')
