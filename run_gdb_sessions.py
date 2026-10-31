import sys, os, subprocess, time, re
sys.path.insert(0, os.path.expanduser('~/embedded_asgn2/tools'))
import qemu_session as q
H = os.path.expanduser('~/embedded_asgn2'); L = f'{H}/logs'
for name, args in (('A', '-d 20 -i 200 -l /tmp/g.log'), ('B', '-d 25 -i 200 -l /tmp/g.log')):
    c = q.boot(f'{L}/qemu_gdb_{name}.txt')
    c.sendline(f'gdbserver 0.0.0.0:1234 /usr/bin/sms_app_dbg {args}')
    c.expect('Listening on port 1234'); time.sleep(1)
    with open(f'{L}/gdb_session_{name}.txt', 'w') as out:
        subprocess.run([os.path.expanduser('~/embedded_lab3/tools/gdb-multiarch'), '-batch', '-x', f'{H}/gdb/scenario_{name}.gdb'],
                       stdout=out, stderr=subprocess.STDOUT, timeout=600)
    try: c.expect(r'/ # ', timeout=60)
    except Exception as e: print('no prompt', name, e)
    c.close(force=True)
