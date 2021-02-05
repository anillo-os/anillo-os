#!/bin/bash

SCRIPT_PATH="$(cd "$(dirname "$0")" >/dev/null 2>&1; pwd -P)"
KERNEL_SOURCE_ROOT="${SCRIPT_PATH}/../kernel"
ARCH=x86_64
OVMF_CODE_PATH="/usr/share/OVMF/OVMF_CODE.fd"
OVMF_VARS_PATH="${SCRIPT_PATH}/ovmf-vars.fd"
DISK_PATH="${SCRIPT_PATH}/disk.img"
MOUNT_PATH="${SCRIPT_PATH}/mnt"
QEMU_GDB_ARGS=""
QEMU_CPU_DEBUG_ARGS=""

EFI_BOOTSTRAP="${KERNEL_SOURCE_ROOT}/build/src/bootstrap/uefi/ferro-bootstrap.efi"
FERRO_KERNEL="${KERNEL_SOURCE_ROOT}/build/ferro"

die() {
	echo "Command failed"
	exit 1
}

for i in "$@"; do
	if [ "x$i" == "x-g" ] || [ "x$i" == "x--gdb" ]; then
		QEMU_GDB_ARGS="-s -S"
	fi
	if [ "x$i" == "x-cr" ] || [ "x$i" == "x--cpu-reset" ]; then
		QEMU_CPU_DEBUG_ARGS="-d cpu_reset"
	fi
done

"${KERNEL_SOURCE_ROOT}/scripts/bootstraps/uefi/compile.sh" || die
"${KERNEL_SOURCE_ROOT}/scripts/compile.sh" || die

mkdir -p "${MOUNT_PATH}"

LOOP_DEV=$(sudo losetup -f -P ./test/disk.img --show)
sudo mount "${LOOP_DEV}p1" "${MOUNT_PATH}" -t vfat -o user,umask=0000

cp "${SCRIPT_PATH}/startup.nsh" "${MOUNT_PATH}/startup.nsh"
cp "${EFI_BOOTSTRAP}" "${MOUNT_PATH}/EFI/anillo/ferro-bootstrap.efi"
cp "${FERRO_KERNEL}" "${MOUNT_PATH}/EFI/anillo/ferro"

sudo umount "${MOUNT_PATH}"
sudo losetup -d "${LOOP_DEV}"

qemu-system-${ARCH} \
	-drive "if=pflash,format=raw,unit=0,file=${OVMF_CODE_PATH},readonly=on" \
	-drive "if=pflash,format=raw,unit=1,file=${OVMF_VARS_PATH}" \
	-drive "if=ide,format=raw,file=${DISK_PATH}" \
	-net none \
	${QEMU_CPU_DEBUG_ARGS} \
	${QEMU_GDB_ARGS}
