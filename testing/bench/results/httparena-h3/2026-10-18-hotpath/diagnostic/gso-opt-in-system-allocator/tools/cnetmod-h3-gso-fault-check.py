"""Prepared offline driver: execute only after authorized GSO/shim build.
Records each actual UDP_SEGMENT sendmmsg interception and real acceptance for
observe/prefix; fake errno runs are explicitly injected attempts, not real CQEs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import uuid


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test-binary',type=Path,required=True)
    parser.add_argument('--test-sha',required=True)
    parser.add_argument('--shim',type=Path,required=True)
    parser.add_argument('--shim-sha',required=True)
    args=parser.parse_args()
    assert sha(args.test_binary)==args.test_sha.lower()
    assert sha(args.shim)==args.shim_sha.lower()
    out=Path('/var/tmp/cnetmod-h3-gso-fault-'+uuid.uuid4().hex)
    out.mkdir()
    report=dict(valid=False,test_binary=str(args.test_binary),test_sha=sha(args.test_binary),
        shim=str(args.shim),shim_sha=sha(args.shim),runs=[])
    try:
        for mode in ('observe','EMSGSIZE','ENOPROTOOPT','EOPNOTSUPP','EINVAL','EIO','EAGAIN','prefix'):
            prefix=mode=='prefix'
            test='udp_segmented_batch_reports_original_prefix_and_preserves_early_cancel' if prefix else 'udp_segmented_batch_preserves_ipv4_ipv6_tail_empty_peer_and_cap_boundaries'
            env=os.environ.copy()
            env.update(LD_PRELOAD=str(args.shim),CNETMOD_GSO_FAULT=mode,CNETMOD_TEST_FILTER=test)
            row=dict(mode=mode,filter=test,command=[str(args.test_binary)],valid=False)
            report['runs'].append(row)
            with (out/(mode+'.stdout')).open('w') as stdout,(out/(mode+'.stderr')).open('w') as stderr:
                process=subprocess.run([str(args.test_binary)],env=env,stdout=stdout,stderr=stderr,timeout=90)
            row['returncode']=process.returncode
            stdout=(out/(mode+'.stdout')).read_text()
            stderr=(out/(mode+'.stderr')).read_text()
            assert process.returncode==0 and '[       OK ] '+test in stdout
            assert re.search(r'\[  PASSED  \] 1 test\(s\)',stdout)
            markers=re.findall(r'CNETMOD_GSO_FAULT fired mode=(\w+) kernel_messages=(\d+) gso_messages=(\d+) first_stride=(\d+) first_iovs=(\d+) first_bytes=(\d+)',stderr)
            assert len(markers)==1 and markers[0][0]==mode,'injection did not hit exactly once'
            marker=markers[0]
            kernel_messages,gso_messages,stride,iovs,byte_count=map(int,marker[1:])
            assert gso_messages>0 and stride==1472
            assert iovs==(3 if prefix else 16) and byte_count==iovs*1472
            row['interception']=dict(kernel_messages=kernel_messages,gso_messages=gso_messages,stride=stride,original_iovs=iovs,bytes=byte_count)
            if mode in ('observe','prefix'):
                real=re.findall(r'CNETMOD_GSO_REAL result=(-?\d+) errno=(\d+) mode=(\w+) first_gso=(\d+)',stderr)
                assert len(real)==1 and real[0][2]==mode and real[0][3]=='1'
                assert int(real[0][0])==1 and int(real[0][1])==0,'real grouped syscall did not accept first group'
                if prefix:assert kernel_messages>=2
                row['real_syscall_accepted_first_group']=True
            else:
                assert 'CNETMOD_GSO_REAL' not in stderr
                row['injected_zero_progress_errno']=mode
            # Exact receiver order, payload, lengths/count and sentinel are
            # asserted by the selected test; this driver also proves it ran.
            row['valid']=True
            (out/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
            print('FAULT VALID',mode,flush=True)
        report['valid']=True
    except BaseException as error:
        report['error']=repr(error)
        raise
    finally:
        (out/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
        print('RESULTS',out,'valid',report['valid'],flush=True)


if __name__=='__main__':
    main()
