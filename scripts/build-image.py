#!/usr/bin/env python3

import os
import sys
import tempfile
import subprocess
import re
import platform

SCRIPT_DIR = os.path.dirname(__file__)
SOURCE_ROOT = os.path.join(SCRIPT_DIR, '..')

sys.path.append(os.path.join(SOURCE_ROOT, 'scripts'))
import anillo_util

if len(sys.argv) != 8:
	print('Usage: ' + sys.argv[0] + ' <architecture> <uefi-bootstrap> <ferro-kernel> <uefi-startup-script> <config-file> <ramdisk> <output-image>')
	sys.exit(1)

ARCH = sys.argv[1]
UEFI_BOOTSTRAP_PATH = sys.argv[2]
KERNEL_PATH = sys.argv[3]
UEFI_SCRIPT_PATH = sys.argv[4]
CONFIG_PATH = sys.argv[5]
RAMDISK_PATH = sys.argv[6]
OUTPUT_IMAGE_PATH = sys.argv[7]

ARCH_MAP = {
	'x86_64': 'BOOTx64.efi',
	'aarch64': 'BOOTaa64.efi',
}

EFI_SIZE_MB = 64
DISK_SIZE_MB = 1024

mount_dir = tempfile.TemporaryDirectory()

if os.path.exists(OUTPUT_IMAGE_PATH):
	os.remove(OUTPUT_IMAGE_PATH)

def partfs_mount():
	anillo_util.run_or_fail(['partfs', '-o', 'dev=' + OUTPUT_IMAGE_PATH, mount_dir.name])

def partfs_unmount():
	if platform.system() == 'Darwin':
		anillo_util.run_or_fail(['umount', mount_dir.name])
	else:
		anillo_util.run_or_fail(['fusermount', '-u', mount_dir.name])

def fat_mkdir_p(image, path):
	curr_path = ''
	for component in path.split(os.sep):
		curr_path = os.path.join(curr_path, component)
		if subprocess.call(['mdir', '-i', image, '::' + curr_path], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT) != 0:
			anillo_util.run_or_fail(['mmd', '-i', image, '::' + curr_path])

def fat_copy(image, source, dest):
	anillo_util.run_or_fail(['mcopy', '-D', 'o', '-i', image, source, '::' + dest])

anillo_util.run_or_fail(['qemu-img', 'create', '-f', 'raw', OUTPUT_IMAGE_PATH, str(DISK_SIZE_MB) + 'M'])

anillo_util.run_or_fail(['sgdisk', '-o', OUTPUT_IMAGE_PATH])

image_info = subprocess.check_output(['sgdisk', '-p', OUTPUT_IMAGE_PATH]).decode()

sector_size = int(re.search(r'Sector size \(logical\): ([0-9]+).*', image_info).group(1))
first_sector = int(re.search(r'First usable sector is ([0-9]+).*', image_info).group(1))
sector_alignment = int(re.search(r'Partitions will be aligned on ([0-9]+).*', image_info).group(1))

first_aligned_sector = anillo_util.round_up_to_multiple(first_sector, sector_alignment)
efi_sector_count = int((EFI_SIZE_MB * 1024 * 1024) / sector_size)
last_efi_sector = first_aligned_sector + efi_sector_count

anillo_util.run_or_fail(['sgdisk', '-o', '-n', '1:' + str(first_aligned_sector) + ':' + str(last_efi_sector), '-t', '1:0700', OUTPUT_IMAGE_PATH])

partfs_mount()

efi_image_path = os.path.join(mount_dir.name, 'p1')

anillo_util.run_or_fail(['mkfs.fat', efi_image_path])

fat_mkdir_p(efi_image_path, 'EFI/anillo')
fat_mkdir_p(efi_image_path, 'EFI/BOOT')

fat_copy(efi_image_path, UEFI_SCRIPT_PATH, 'startup.nsh')
fat_copy(efi_image_path, CONFIG_PATH, 'EFI/anillo/config.txt')
fat_copy(efi_image_path, UEFI_BOOTSTRAP_PATH, 'EFI/anillo/ferro-bootstrap.efi')
fat_copy(efi_image_path, UEFI_BOOTSTRAP_PATH, 'EFI/BOOT/' + ARCH_MAP[ARCH])
fat_copy(efi_image_path, KERNEL_PATH, 'EFI/anillo/ferro')
fat_copy(efi_image_path, RAMDISK_PATH, 'EFI/anillo/ramdisk')

partfs_unmount()
