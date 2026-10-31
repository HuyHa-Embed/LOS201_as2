"""Tien ich: khoi dong QEMU vexpress-a9 voi initramfs LAB-03 va dieu khien qua pexpect."""
import os, pexpect
H = os.path.expanduser('~')
QEMU = (f'qemu-system-arm -M vexpress-a9 -cpu cortex-a9 -m 512M -smp 2 -nographic '
        f'-kernel {H}/embedded_asgn2/output/zImage -dtb {H}/embedded_asgn2/output/vexpress-v2p-ca9.dtb '
        f'-initrd {H}/embedded_asgn2/output/initramfs_asgn2.cpio.gz '
        f'-append "console=ttyAMA0,115200 rdinit=/sbin/init mem=512M" '
        f'-nic user,model=lan9118,hostfwd=tcp::1234-:1234')

def boot(logfile):
    log = open(logfile, 'wb')
    c = pexpect.spawn('/bin/sh', ['-c', QEMU], timeout=180, logfile=log)
    c.expect('Please press Enter')
    c.sendline('')
    c.expect(r'/ # ')
    return c

def run(c, cmd, timeout=120):
    c.sendline(cmd)
    c.expect(r'/ # ', timeout=timeout)
    return c.before.decode(errors='replace')
