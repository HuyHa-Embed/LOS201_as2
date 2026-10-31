"""ASGN-02: do bo sung tren QEMU: time (user/sys) va jitter duoi tai nang (6 CPU hog)."""
import sys, os, re
sys.path.insert(0, os.path.expanduser('~/embedded_asgn2/tools'))
import qemu_session as q
H = os.path.expanduser('~/embedded_asgn2'); L = f'{H}/logs'
clean = lambda s: re.sub(r'\x1b\[[0-9;]*m', '', s.replace('\r', ''))
c = q.boot(f'{L}/qemu_asgn2_extra.txt')
def r(cmd, t=300):
    out = clean(q.run(c, cmd, t)); print('>>>', cmd[:80]); print(out[-900:]); return out
t = r('sms_app -d 30 -l /tmp/x.log >/dev/null 2>&1 & P=$!; sleep 28; echo "uptime_s=$(cut -d. -f1 /proc/uptime)"; '
      'awk \'{print "utime_ticks="$14" stime_ticks="$15" (CLK_TCK=100)"}\' /proc/$P/stat; '
      'for t in /proc/$P/task/*; do echo "$(cat $t/comm) $(awk \'{print "utime="$14" stime="$15}\' $t/stat)"; done; wait $P', 100)
open(f'{L}/time_sms_arm.txt', 'w').write('(do tai T=28s cua mot lan chay 30 s tren QEMU)\n' + t.split('\n', 1)[1].strip() + '\n')
r('for i in 1 2 3 4 5 6; do sh -c "while :; do :; done" & done; sleep 1')
out = ''
for pol in ('fifo', 'other'):
    out += f'##### HEAVY LOAD: 6 CPU hogs on 2 vCPU ({pol}) #####\n'
    out += clean(q.run(c, f'sms_app -i 10 -d 15 -p {pol} -l /tmp/j.log -j /tmp/jh_{pol}.txt >/dev/null 2>&1; cat /tmp/jh_{pol}.txt', 100)).split('\n', 1)[1]
r('kill $(jobs -p) 2>/dev/null; killall sh 2>/dev/null; true')
open(f'{L}/jitter_report_heavy_load.txt', 'w').write(out); print(out)
c.close(force=True)
