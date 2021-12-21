#!/usr/bin/env python3

import os
import subprocess
import sys
import argparse
import shutil

SCRIPT_DIR = os.path.dirname(__file__)
SOURCE_ROOT = os.path.join(SCRIPT_DIR, '..')

sys.path.append(os.path.join(SOURCE_ROOT, 'scripts'))
import anillo_util

VALID_ARCHES = ['x86_64', 'aarch64']

EFI_CODE_PATHS_TO_TRY = {
	'x86_64': [
		os.path.join(os.path.dirname(os.path.realpath(shutil.which('qemu-system-x86_64'))), '..', 'share', 'qemu', 'edk2-x86_64-code.fd'),
		'/usr/share/OVMF/OVMF_CODE.fd',
		'/usr/share/OVMF/x64/OVMF_CODE.fd',
	],
	'aarch64': [
		os.path.join(os.path.dirname(os.path.realpath(shutil.which('qemu-system-aarch64'))), '..', 'share', 'qemu', 'edk2-aarch64-code.fd'),
		'/usr/share/AAVMF/AAVMF_CODE.fd',
	],
}

EFI_VARS_PATHS_TO_TRY = {
	'x86_64': [
		os.path.join(os.path.dirname(os.path.realpath(shutil.which('qemu-system-x86_64'))), '..', 'share', 'qemu', 'edk2-i386-vars.fd'),
		'/usr/share/OVMF/OVMF_VARS.fd',
		'/usr/share/OVMF/x64/OVMF_VARS.fd',
	],
	'aarch64': [
		os.path.join(os.path.dirname(os.path.realpath(shutil.which('qemu-system-aarch64'))), '..', 'share', 'qemu', 'edk2-arm-vars.fd'),
		'/usr/share/AAVMF/AAVMF_VARS.fd',
	],
}

parser = argparse.ArgumentParser(description='Run Anillo OS inside QEMU')

parser.add_argument('-qd', '--qemu-debug', action='store_true', help='Enable QEMU-based debugging')
parser.add_argument('-cr', '--cpu-reset', action='store_true', help='Enable QEMU debugging information on virtual CPU reset')
parser.add_argument('-k', '--kvm', action='store_true', help='Enable KVM')
parser.add_argument('-s', '--serial', choices=['pty', 'stdio', 'none'], default='pty', help='Where to connect the serial console (or whether to connect it at all)')
parser.add_argument('-b', '--build', action='store_true', help='Build Anillo OS automatically before running it')
parser.add_argument('-d', '--debug', action='store_true', help='Enable Anillo OS gdbstub-based debugging')
parser.add_argument('-a', '--arch', choices=VALID_ARCHES, default=os.environ.get('ARCH'), help='Architecture to run Anillo OS under')
parser.add_argument('-bd', '--build-dir', type=str, default=None, help='Directory where Anillo OS has been/will be built')

args = parser.parse_args()

if not (args.arch in VALID_ARCHES):
	raise RuntimeError(f'Invalid architecture "{args.arch}"; expected one of {VALID_ARCHES}')

if not args.build_dir:
	args.build_dir = os.path.join(SOURCE_ROOT, 'build', args.arch)

efi_code_path = os.path.join(args.build_dir, 'efi-code.fd')
efi_vars_path = os.path.join(args.build_dir, 'efi-vars.fd')

efi_code_source_path = None
efi_vars_source_path = None

disk_path = os.path.join(args.build_dir, 'disk.img')
qemu_args = ['-net', 'none']

def try_find_path(search_list):
	for path in search_list:
		if (os.path.exists(path)):
			return path
	return None

if args.arch == 'aarch64':
	# most of this was copied over from an AARCH64 libvirt VM
	# some of it was modified afterwards
	qemu_args += [
		'-name', 'guest=anillo-aarch64,debug-threads=on',

		'-blockdev', f'{{"driver":"file","filename":"{efi_code_path}","node-name":"anillo-pflash0-storage","auto-read-only":true,"discard":"unmap"}}',
		'-blockdev', f'{{"node-name":"anillo-pflash0-format","read-only":true,"driver":"raw","file":"anillo-pflash0-storage"}}',
		'-blockdev', f'{{"driver":"file","filename":"{efi_vars_path}","node-name":"anillo-pflash1-storage","auto-read-only":true,"discard":"unmap"}}',
		'-blockdev', '{"node-name":"anillo-pflash1-format","read-only":false,"driver":"raw","file":"anillo-pflash1-storage"}',
		'-machine', 'virt-4.2,accel=tcg,usb=off,gic-version=2,pflash0=anillo-pflash0-format,pflash1=anillo-pflash1-format',

		'-cpu', 'cortex-a57',

		#'-m', '1024',
		#'-overcommit', 'mem-lock=off',

		#'-smp', '1,sockets=1,cores=1,threads=1',

		'-no-user-config',
		'-nodefaults',

		'-chardev', f'socket,id=charmonitor,path={os.path.join(args.build_dir, "serial-monitor")},server=on,wait=off',
		'-mon', 'chardev=charmonitor,id=monitor,mode=control',

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

		'-blockdev', f'{{"driver":"file","filename":"{disk_path}","node-name":"anillo-1-storage","auto-read-only":true,"discard":"unmap"}}',
		'-blockdev', '{"node-name":"anillo-1-format","read-only":false,"driver":"raw","file":"anillo-1-storage"}',
		'-device', 'virtio-blk-pci,bus=pci.4,addr=0x0,drive=anillo-1-format,id=virtio-disk0,bootindex=1',

		'-chardev', f'socket,id=charchannel0,server=on,wait=off,path={os.path.join(args.build_dir, "serial")}',
		'-device', 'virtserialport,bus=virtio-serial0.0,nr=1,chardev=charchannel0,id=channel0,name=org.qemu.guest_agent.0',

		#'-spice', 'port=0,disable-ticketing,image-compression=off,seamless-migration=on',
		'-device', 'ramfb',
		'-device', 'virtio-gpu-pci,id=video0,max_outputs=1,bus=pci.5,addr=0x0',

		'-device', 'usb-kbd',
	]
elif args.arch == 'x86_64':
	qemu_args += [
		'-drive', f'if=pflash,format=raw,unit=0,file={efi_code_path}',
		'-drive', f'if=pflash,format=raw,unit=1,file={efi_vars_path}',
		'-drive', f'if=virtio,format=raw,file={disk_path}',
		'-machine', 'type=q35',
	]

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
	qemu_args += ['-d', 'cpu_reset']

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
		anillo_util.run_or_fail(['cmake', '-B', args.build_dir, '-S', SOURCE_ROOT, '-DCMAKE_BUILD_TYPE=Debug', '-DCMAKE_EXPORT_COMPILE_COMMANDS=1', f'-DANILLO_ARCH={args.arch}'], cwd=args.build_dir)
	anillo_util.run_or_fail(['cmake', '--build', args.build_dir], cwd=args.build_dir)

subprocess.run([f'qemu-system-{args.arch}'] + qemu_args, cwd=args.build_dir)
