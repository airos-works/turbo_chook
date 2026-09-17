#!/usr/bin/env python3
"""Live-copy another Turbo Chook/Kiri style QEMU VM disk through QMP drive-backup.
Usage: clone-disk.py SOURCE_QMP_SOCKET TARGET.qcow2"""
import json, os, socket, sys, time
src, target = sys.argv[1], os.path.abspath(sys.argv[2])
s = socket.socket(socket.AF_UNIX); s.connect(src); f = s.makefile('rwb', buffering=0)
def read():
    return json.loads(f.readline())
def cmd(name, **args):
    f.write((json.dumps({'execute': name, 'arguments': args}) + '\n').encode())
    while True:
        r = read()
        if 'error' in r: raise SystemExit('QMP error: %r' % r['error'])
        if 'return' in r: return r['return']
read(); cmd('qmp_capabilities')
blocks = cmd('query-block')
device = next(b['device'] for b in blocks if b.get('inserted') and b['inserted']['file'].endswith('work.qcow2'))
print('source device', device, flush=True)
cmd('drive-backup', device=device, target=target, format='qcow2', sync='full', mode='absolute-paths', **{'job-id': 'tc-clone'})
while True:
    jobs = cmd('query-block-jobs')
    job = next((j for j in jobs if j['device'] == 'tc-clone'), None)
    if job is None: break
    print('progress %d%%' % (100 * job['offset'] // max(1, job['len'])), flush=True)
    time.sleep(5)
print('done', flush=True)
