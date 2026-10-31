#!/usr/bin/env python3
"""Dong goi initramfs bang gen_init_cpio (khong can root de tao device node)."""
import os, subprocess, sys
root = os.path.expanduser('~/embedded_asgn2/rootfs/initramfs')
gen = os.path.expanduser('~/embedded_lab1/kernel/linux-5.15/usr/gen_init_cpio')
out = os.path.expanduser('~/embedded_asgn2/output/initramfs_asgn2.cpio.gz')
lines = ['dir /dev 755 0 0', 'nod /dev/console 600 0 0 c 5 1', 'nod /dev/null 666 0 0 c 1 3',
         'nod /dev/zero 666 0 0 c 1 5', 'nod /dev/random 666 0 0 c 1 8', 'nod /dev/urandom 666 0 0 c 1 9']
for d, ds, fs in os.walk(root):
    rel = '/' + os.path.relpath(d, root) if d != root else ''
    if rel == '/dev': continue
    if rel: lines.append(f'dir {rel} 755 0 0')
    for f in sorted(fs):
        p = os.path.join(d, f); r = f'{rel}/{f}'
        if os.path.islink(p): lines.append(f'slink {r} {os.readlink(p)} 777 0 0')
        else: lines.append(f'file {r} {p} {os.stat(p).st_mode & 0o777:o} 0 0')
    for s in ds:
        p = os.path.join(d, s)
        if os.path.islink(p): lines.append(f'slink {rel}/{s} {os.readlink(p)} 777 0 0')
lst = '/tmp/claude-1000/-home-huy/9eae6f4b-c685-4b06-ba9f-077df0d00aad/scratchpad/cpio.lst'
open(lst, 'w').write('\n'.join(lines) + '\n')
raw = subprocess.check_output([gen, lst])
import gzip; open(out, 'wb').write(gzip.compress(raw, 9))
print('wrote', out, os.path.getsize(out))
