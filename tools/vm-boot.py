#!/usr/bin/env python3
import os
import argparse
import subprocess
from colors import *
from utils import *

CUR_DIR = os.path.abspath(os.path.dirname(__file__))

def vm_boot(vm, freeze, cpu, memory, kernel, dir, port):
    cmd_boot = """\
set -e;
sudo qemu-system-x86_64 \
     -s \
     {freeze} \
     -nographic \
     -enable-kvm \
     -cpu host \
     -smp cpus={cpu} \
     -m {memory} \
     -hda {vm} \
     -device e1000,netdev=net0 \
     -net nic \
     -netdev user,id=net0,hostfwd=tcp::{port}-:22 \
    """
    cmd_dir = """\
     -kernel {kernel} \
     -append "nokaslr root=/dev/sda4 console=ttyS0" \
     -fsdev local,security_model=passthrough,id=fsdev0,path={dir} \
     -device virtio-9p-pci,id=fs0,fsdev=fsdev0,mount_tag=/dev/host \
    """
    cmd = cmd_boot.format(vm=vm, freeze=freeze, cpu=cpu, \
                          memory=memory, port=port)
    if kernel != None and dir != None:
        cmd = cmd + " " + cmd_dir.format(kernel=kernel, dir=dir)

    print(cmd)
    os.system(cmd)

if __name__ == '__main__':
    # options
    parser = argparse.ArgumentParser(prog='vm-boot.py')
    parser.add_argument('-v', '--vm',
                        help="VM image")
    parser.add_argument('-S', '--freeze', action='store_true', default=False,
                        help="freeze CPU at startup")
    parser.add_argument('-c', '--cpu', default="2",
                        help="number of CPU: default = 2")
    parser.add_argument('-m', '--memory', default="4G",
                        help="memory size: default = 4G")
    parser.add_argument('-k', '--kernel',
                        help="kernel boot image")
    parser.add_argument('-d', '--dir', default=os.path.join(CUR_DIR, ".."),
                        help="shared directory: mount tag = /dev/host, default = ../")
    parser.add_argument('-p', '--port', default=port_number(),
                        help="ssh port number: default is %s" % port_number())

    # options parsing
    args = parser.parse_args()
    if args.vm is None:
        parser.print_help()
        exit(1)

    # boot
    vm_boot(args.vm, "-S" if args.freeze else "", args.cpu, args.memory, \
            args.kernel, args.dir, args.port)
