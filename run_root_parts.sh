#!/bin/bash
# Cac buoc can root cua ASGN-02 tren HOST x86_64: perf, strace, jitter SCHED_FIFO vs SCHED_OTHER.
#   sudo bash ~/embedded_asgn2/tools/run_root_parts.sh
set -u
U=${SUDO_USER:-huy}; H=/home/$U; B=$H/embedded_asgn2; L=$B/logs; A=$B/app
T=$H/embedded_lab3/tools/host
export PATH=$T/usr/bin:$PATH LD_LIBRARY_PATH=$T/usr/lib/x86_64-linux-gnu
cd $A
rm -f /dev/mqueue/sms_* 2>/dev/null   # queue con sot tu lan chay truoc (root-owned) lam mq_open that bai

rm -f /dev/mqueue/sms_* 2>/dev/null
echo "== strace -c (x86, 30 s) =="
strace -c -f -o $L/strace_sms.txt ./sms_app_x86 -d 30 -p other -l /tmp/sms_x86.log > /dev/null 2>&1
cat $L/strace_sms.txt | head -15

echo "== perf record/report (x86, 30 s) =="
perf record -F 2000 -g -o $L/perf_sms.data ./sms_app_x86 -d 30 -p other -l /tmp/sms_x86.log > /dev/null 2>&1
perf report -i $L/perf_sms.data --stdio --no-children > $L/perf_sms.txt 2>&1
perf report -i $L/perf_sms.data --stdio --no-children -g none --sort comm,dso,symbol > $L/perf_sms_flat.txt 2>&1
perf stat -e task-clock,context-switches,cpu-migrations,page-faults ./sms_app_x86 -d 10 -p other -l /tmp/sms_x86.log 2>&1 >/dev/null | tee $L/perf_stat_sms.txt
rm -f $L/perf_sms.data; rm -f perf.data*

echo "== jitter host: SCHED_FIFO vs SCHED_OTHER, co tai stress-ng =="
OUT=$L/jitter_report_host.txt; : > $OUT
for pol in other fifo; do
  stress-ng --cpu $(nproc) --io 4 --vm 2 --vm-bytes 256M --timeout 22s > /dev/null 2>&1 &
  sleep 2
  echo "##### HOST x86_64 (stress-ng load) policy=$pol #####" >> $OUT
  ./sms_app_x86 -i 10 -d 15 -p $pol -j /tmp/jh_$pol.txt -l /tmp/sms_x86.log > /dev/null 2>&1
  cat /tmp/jh_$pol.txt >> $OUT
  wait
done
cat $OUT
rm -f /dev/mqueue/sms_* 2>/dev/null
chown -R $U:$U $L
echo DONE
