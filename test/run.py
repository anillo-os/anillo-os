#!/usr/bin/env python3

from asyncio.subprocess import DEVNULL, PIPE
import os
from pathlib import Path
import subprocess
import sys
import argparse
import shutil
import platform

SCRIPT_DIR = os.path.dirname(__file__)
SOURCE_ROOT = os.path.join(SCRIPT_DIR, '..')

sys.path.append(os.path.join(SOURCE_ROOT, 'scripts'))
import anillo_util

VALID_ARCHES = ['x86_64', 'aarch64']
VALID_NETDEVS = ['none', 'user', 'tap', 'vmnet-shared', 'socket_vmnet', 'socket_vmnet-bridged']
VALID_TRACES = ['net', 'usb', 'ps2', 'ioapic', 'gic']

QEMU_TCG_HOTBLOCKS_PLUGIN_PATH='/usr/local/lib/qemu/contrib/plugins/libhotblocks.so'

def which_or_die(name: str) -> str:
	result = shutil.which(name)
	if result == None:
		raise FileNotFoundError
	return result

EFI_CODE_PATHS_TO_TRY = {
	'x86_64': [
		os.path.join(os.path.dirname(os.path.realpath(which_or_die('qemu-system-x86_64'))), '..', 'share', 'qemu', 'edk2-x86_64-code.fd'),
		'/usr/share/OVMF/OVMF_CODE.fd',
		'/usr/share/OVMF/x64/OVMF_CODE.fd',
		'/usr/share/OVMF/OVMF_CODE.secboot.fd',
		'/usr/share/ovmf/x64/OVMF_CODE.4m.fd',
	],
	'aarch64': [
		os.path.join(os.path.dirname(os.path.realpath(which_or_die('qemu-system-aarch64'))), '..', 'share', 'qemu', 'edk2-aarch64-code.fd'),
		'/usr/share/AAVMF/AAVMF_CODE.fd',
	],
}

EFI_VARS_PATHS_TO_TRY = {
	'x86_64': [
		os.path.join(os.path.dirname(os.path.realpath(which_or_die('qemu-system-x86_64'))), '..', 'share', 'qemu', 'edk2-i386-vars.fd'),
		'/usr/share/OVMF/OVMF_VARS.fd',
		'/usr/share/OVMF/x64/OVMF_VARS.fd',
		'/usr/share/ovmf/x64/OVMF_VARS.4m.fd',
	],
	'aarch64': [
		os.path.join(os.path.dirname(os.path.realpath(which_or_die('qemu-system-aarch64'))), '..', 'share', 'qemu', 'edk2-arm-vars.fd'),
		'/usr/share/AAVMF/AAVMF_VARS.fd',
	],
}

parser = argparse.ArgumentParser(description='Run Anillo OS inside QEMU')

parser.add_argument('-qd', '--qemu-debug', action='store_true', help='Enable QEMU-based debugging')
parser.add_argument('-cr', '--cpu-reset', action='store_true', help='Enable QEMU debugging information on virtual CPU reset')
parser.add_argument('-di', '--debug-interrupts', action='store_true', help='Enable QEMU interrupt debugging')
parser.add_argument('-k', '--kvm', action='store_true', help='Enable hardware acceleration (if available). This is KVM on Linux and Hypervisor.framework on macOS')
parser.add_argument('-s', '--serial', choices=['pty', 'stdio', 'none'], default='pty', help='Where to connect the serial console (or whether to connect it at all)')
parser.add_argument('-b', '--build', action='store_true', help='Build Anillo OS automatically before running it')
parser.add_argument('-d', '--debug', action='store_true', help='Enable Anillo OS gdbstub-based debugging')
parser.add_argument('-a', '--arch', choices=VALID_ARCHES, default=os.environ.get('ARCH'), help='Architecture to run Anillo OS under')
parser.add_argument('-bd', '--build-dir', type=str, default=None, help='Directory where Anillo OS has been/will be built')
parser.add_argument('-n', '--net', choices=VALID_NETDEVS, default='user', help='Network device connection type to configure QEMU to use')
parser.add_argument('--net-dump', type=str, default=None, help='Generate a dump of the network traffic to the given file')
parser.add_argument('--wireshark', action='store_true', help='Open Wireshark to view the network dump in real-time')
parser.add_argument('-t', '--trace', nargs='+', choices=VALID_TRACES, help='List of QEMU subsystems to trace')
parser.add_argument('-H', '--headless', action='store_true', help='Starts QEMU in headless mode (no graphics)')
parser.add_argument('--hotblocks', action='store_true', help='Run QEMU with the Hotblocks plugin to find hot code paths')
parser.add_argument('-r', '--release', action='store_true', help='Test a release build of Anillo OS rather than a debug build')
parser.add_argument('-u', '--usb', nargs='+', help='Pass a USB device from the host to the guest (format is `bus.addr`)')
parser.add_argument('-m', '--memory', help='The amount of memory to give to the VM (128M by default)')
parser.add_argument('-R', '--record', type=str, default=None, help='Record the execution of the VM into the given recording file')
parser.add_argument('-P', '--play', type=str, default=None, help='Play the execution of the VM from the given recording file')
parser.add_argument('-c', '--cores', type=int, nargs='?', const=1, default=1, help='The number of CPU cores to virtualize (this can be greater or lesser than the number of host CPU cores)')

args = parser.parse_args()

use_sudo = False

if not (args.arch in VALID_ARCHES):
	raise RuntimeError(f'Invalid architecture "{args.arch}"; expected one of {VALID_ARCHES}')

if not args.build_dir:
	args.build_dir = os.path.join(SOURCE_ROOT, 'build', args.arch)
	if args.release:
		args.build_dir += '/release'
	else:
		args.build_dir += '/debug'

efi_code_path = os.path.join(args.build_dir, 'efi-code.fd')
efi_vars_path = os.path.join(args.build_dir, 'efi-vars.fd')

efi_code_source_path = None
efi_vars_source_path = None

disk_path = os.path.join(args.build_dir, 'disk.img')
qemu_args = []
prefix_args = []

accel_name = "tcg"

record_file = os.path.join(os.getcwd(), args.record) if args.record != None else None
play_file = os.path.join(os.getcwd(), args.play) if args.play != None else None
using_rr = record_file != None or play_file != None

if using_rr:
	qemu_args += [
		'-icount', f'shift=auto,rr={"record" if record_file != None else "replay"},rrfile={record_file if record_file != None else play_file}'
	]

if args.kvm:
	if platform.system() == 'Linux':
		accel_name = 'kvm'
	elif platform.system() == 'Darwin':
		accel_name = 'hvf'

qemu_debug_args = []

if args.trace != None and 'net' in args.trace:
	qemu_args += [
		'--trace', 'e1000e*',
	]

if args.trace != None and 'usb' in args.trace:
	qemu_args += [
		'--trace', '*usb*',
	]

if args.trace != None and 'ps2' in args.trace:
	qemu_args += ['--trace', '*ps2*', '--trace', '*pckbd*']

if args.trace != None and 'ioapic' in args.trace:
	qemu_args += ['--trace', '*ioapic*']

if args.trace != None and 'gic' in args.trace:
	qemu_args += ['--trace', 'gic*']

if args.net_dump != None:
	qemu_args += ['-object', f'filter-dump,id=netdump0,netdev=netdev0,file={os.path.join(os.getcwd(), args.net_dump)},maxlen=104857600']

if args.memory != None:
	qemu_args += ['-m', args.memory]

qemu_args += ['-smp', str(args.cores)]

def try_find_path(search_list):
	for path in search_list:
		if (os.path.exists(path)):
			return path
	return None

# TODO: rr support for pflash
if args.arch == 'aarch64':
	rr_snapshot = ""

	if using_rr:
		rr_snapshot = ',"snapshot":true'

	# most of this was copied over from an AARCH64 libvirt VM
	# some of it was modified afterwards
	qemu_args += [
		'-name', 'guest=anillo-aarch64,debug-threads=on',

		'-blockdev', f'{{"driver":"file","filename":"{efi_code_path}","node-name":"anillo-pflash0-storage","auto-read-only":true,"discard":"unmap"}}',
		'-blockdev', f'{{"node-name":"anillo-pflash0-format","read-only":true,"driver":"raw","file":"anillo-pflash0-storage"}}',
		'-blockdev', f'{{"driver":"file","filename":"{efi_vars_path}","node-name":"anillo-pflash1-storage","auto-read-only":true,"discard":"unmap"}}',
		'-blockdev', '{"node-name":"anillo-pflash1-format","read-only":false,"driver":"raw","file":"anillo-pflash1-storage"}',
		'-machine', f'virt-4.2,accel={accel_name},usb=off,gic-version=2,pflash0=anillo-pflash0-format,pflash1=anillo-pflash1-format',

		'-cpu', 'host' if args.kvm else 'max',

		#'-m', '1024',
		#'-overcommit', 'mem-lock=off',

		#'-smp', '1,sockets=1,cores=1,threads=1',

		'-no-user-config',
		'-nodefaults',

		'-chardev', f'socket,id=charmonitor,path={os.path.join(args.build_dir, "serial-monitor")},server=on,wait=off',
		'-mon', 'chardev=charmonitor,id=monitor',

		'-rtc', 'base=utc',

		'-no-shutdown',

		'-boot', 'strict=on',

		'-device', 'pcie-root-port,port=0x8,chassis=1,id=pci.1,bus=pcie.0,multifunction=on,addr=0x1',
		'-device', 'pcie-root-port,port=0x9,chassis=2,id=pci.2,bus=pcie.0,addr=0x1.0x1',
		'-device', 'pcie-root-port,port=0xa,chassis=3,id=pci.3,bus=pcie.0,addr=0x1.0x2',
		'-device', 'pcie-root-port,port=0xb,chassis=4,id=pci.4,bus=pcie.0,addr=0x1.0x3',
		'-device', 'pcie-root-port,port=0xc,chassis=5,id=pci.5,bus=pcie.0,addr=0x1.0x4',
		'-device', 'pcie-root-port,port=0xd,chassis=6,id=pci.6,bus=pcie.0,addr=0x1.0x5',

		'-device', 'qemu-xhci,p2=15,p3=15,id=usb,bus=pci.2,addr=0x0',

		'-device', 'virtio-serial-pci,id=virtio-serial0,bus=pci.3,addr=0x0',

		'-blockdev', f'{{"driver":"file","filename":"{disk_path}","node-name":"anillo-1-storage","auto-read-only":true,"discard":"unmap"{rr_snapshot}}}',
		'-blockdev', f'{{"node-name":"anillo-1-format","read-only":false,"driver":"raw","file":"anillo-1-storage"}}',
		'-device', f'virtio-blk-pci,bus=pci.4,addr=0x0,drive=anillo-1-format{"-rr" if using_rr else ""},id=virtio-disk0,bootindex=1',

		'-chardev', f'socket,id=charchannel0,server=on,wait=off,path={os.path.join(args.build_dir, "serial")}',
		'-device', 'virtserialport,bus=virtio-serial0.0,nr=1,chardev=charchannel0,id=channel0,name=org.qemu.guest_agent.0',

		#'-spice', 'port=0,disable-ticketing,image-compression=off,seamless-migration=on',
		#'-device', 'virtio-gpu-pci,id=video0,max_outputs=1,bus=pci.5,addr=0x0',

		'-device', 'usb-kbd',
		'-device', 'usb-mouse',
	]

	if using_rr:
		qemu_args += [
			'-blockdev', '{"node-name":"anillo-1-format-rr","read-only":false,"driver":"blkreplay","image":"anillo-1-storage"}',
		]

	qemu_debug_args += ['mmu', 'guest_errors']

	if not args.headless:
		qemu_args += [
			'-device', 'ramfb',
		]
elif args.arch == 'x86_64':
	rr_snapshot = ""

	if using_rr:
		rr_snapshot = ',snapshot=on'

	if using_rr:
		qemu_args += [
			'-drive', f'if=none,format=raw,file={efi_code_path},id=anillo-pflash0',
			'-drive', f'if=none,format=raw,file={efi_vars_path},id=anillo-pflash1',
		]
	else:
		qemu_args += [
			'-drive', f'if=pflash,format=raw,unit=0,file={efi_code_path}',
			'-drive', f'if=pflash,format=raw,unit=1,file={efi_vars_path}',
		]

	qemu_args += [
		'-drive', f'if=none,format=raw,file={disk_path},id=anillo-disk0{rr_snapshot}',
		'-device', f'virtio-blk-pci,drive=anillo-disk0{"-rr" if using_rr else ""}',
		'-machine', f'type=q35,accel={accel_name}',
		'-device', 'qemu-xhci',
		'-device', 'usb-kbd',
		'-device', 'usb-mouse',
		'-cpu', 'host' if args.kvm else 'max',
	]

	if using_rr:
		qemu_args += [
			'-drive', 'if=pflash,driver=blkreplay,image=anillo-pflash0,id=anillo-pflash0-rr,unit=0',
			'-drive', 'if=pflash,driver=blkreplay,image=anillo-pflash1,id=anillo-pflash1-rr,unit=1',
			'-drive', 'if=none,driver=blkreplay,image=anillo-disk0,id=anillo-disk0-rr',
		]

	if args.headless:
		qemu_args += [
			'-chardev', f'socket,id=charmonitor,path={os.path.join(args.build_dir, "serial-monitor")},server=on,wait=off',
			'-mon', 'chardev=charmonitor,id=monitor,mode=control',
		]

if args.net == 'none':
	qemu_args += ['-net', 'none']
else:
	net_name = args.net
	net_args = ''
	if args.net == 'tap':
		net_args = ',ifname=anillo-tap0,script=no,downscript=no'
	elif args.net == 'vmnet-shared':
		# we unfortunately need to be root to use vmnet-shared on macOS right now
		use_sudo = True
	elif args.net == 'socket_vmnet' or args.net == 'socket_vmnet-bridged':
		net_name = 'socket'
		net_args = ',fd=3'
	qemu_args += [
		'-netdev', f'{net_name},id=netdev0{net_args}',
		'-device', 'e1000e,netdev=netdev0,id=net0',
	]
	if using_rr:
		qemu_args += [
			'-object', 'filter-replay,id=replay,netdev=netdev0',
		]

if args.headless:
	qemu_args += ['-nographic']

if args.serial == 'pty':
	qemu_args += [
		'-chardev', 'pty,id=charserial0',
		'-serial', 'chardev:charserial0',
	]
elif args.serial == 'stdio':
	qemu_args += ['-serial', 'stdio']

if args.debug:
	qemu_args += ['-serial', 'tcp::1234,server,nowait']

if args.qemu_debug:
	qemu_args += ['-s', '-S']

if args.cpu_reset:
	qemu_debug_args += ['cpu_reset']

if args.debug_interrupts:
	qemu_debug_args += ['int']

if args.hotblocks:
	qemu_args += ['-plugin', QEMU_TCG_HOTBLOCKS_PLUGIN_PATH]
	qemu_debug_args += ['plugin']

if len(qemu_debug_args) > 0:
	qemu_args += ['-d', ','.join(set(qemu_debug_args))]

if args.usb != None:
	dev: str
	for dev in args.usb:
		[bus, addr] = dev.split('.')
		qemu_args += ['-device', f'usb-host,hostbus={bus},hostaddr={addr}']

if args.net == 'socket_vmnet':
	prefix_args = ['/opt/socket_vmnet/bin/socket_vmnet_client', '/var/run/socket_vmnet'] + prefix_args
elif args.net == 'socket_vmnet-bridged':
	prefix_args = ['/opt/socket_vmnet/bin/socket_vmnet_client', '/var/run/socket_vmnet.bridged.en0'] + prefix_args

if use_sudo:
	prefix_args = ['sudo'] + prefix_args

#
# this is where the heart of the logic begins
#

# create the build directory if it doesn't exist yet
anillo_util.mkdir_p(args.build_dir)

#
# copy over efi resources if they don't exist already
#

if not os.path.exists(efi_code_path):
	efi_code_source_path = try_find_path(EFI_CODE_PATHS_TO_TRY[args.arch])

	if not efi_code_source_path:
		raise RuntimeError(f'Failed to find EFI code binary for architecture {args.arch}')

	anillo_util.mkdir_p(os.path.dirname(efi_code_path))
	shutil.copyfile(efi_code_source_path, efi_code_path)

if not os.path.exists(efi_vars_path):
	efi_vars_source_path = try_find_path(EFI_VARS_PATHS_TO_TRY[args.arch])

	if not efi_vars_source_path:
		raise RuntimeError(f'Failed to find EFI vars binary for architecture {args.arch}')

	anillo_util.mkdir_p(os.path.dirname(efi_vars_path))
	shutil.copyfile(efi_vars_source_path, efi_vars_path)

# build the OS if we were told to
if args.build:
	if not os.path.exists(os.path.join(args.build_dir, 'CMakeCache.txt')):
		anillo_util.run_or_fail(['cmake', '-B', args.build_dir, '-S', SOURCE_ROOT, f'-DCMAKE_BUILD_TYPE={"Release" if args.release else "Debug"}', '-DCMAKE_EXPORT_COMPILE_COMMANDS=1', f'-DANILLO_ARCH={args.arch}'], cwd=args.build_dir)
	anillo_util.run_or_fail(['cmake', '--build', args.build_dir], cwd=args.build_dir)

if args.net_dump != None and args.wireshark:
	cap_file = os.path.join(os.getcwd(), args.net_dump)
	if Path(cap_file).exists():
		Path(cap_file).unlink()
	Path(cap_file).touch()
	proc1 = subprocess.Popen([which_or_die('tail'), '-f', '-c', '+0', cap_file], stdin=PIPE, stdout=PIPE, stderr=DEVNULL)
	proc2 = subprocess.Popen([which_or_die('wireshark'), '-k', '-i', '-'], stdin=proc1.stdout, stdout=DEVNULL, stderr=DEVNULL)

subprocess.run(prefix_args + [f'qemu-system-{args.arch}'] + qemu_args, stdin=sys.stdin)
