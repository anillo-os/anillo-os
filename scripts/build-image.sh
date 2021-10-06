#!/bin/bash

SCRIPT_PATH="$(cd "$(dirname "$0")" >/dev/null 2>&1; pwd -P)"
SOURCE_ROOT="${SCRIPT_PATH}/.."

source "${SOURCE_ROOT}/scripts/util.sh"

DISK_PATH="${BUILD_DIR}/disk.img"
MOUNT_PATH="${BUILD_DIR}/mnt"

BUILD_TOO=0

for ((i=0; i <= "${#@}"; ++i)); do
	arg="${!i}"
	if [ "x${arg}" == "x-b" ] || [ "x${arg}" == "x--build" ]; then
		BUILD_TOO=1
	fi
done

EFI_BOOTSTRAP="${BUILD_DIR}/kernel/src/bootstrap/uefi/ferro-bootstrap.efi"
FERRO_KERNEL="${BUILD_DIR}/kernel/ferro"
STARTUP_SCRIPT="${SCRIPT_PATH}/../imagesrc/startup.nsh"
CONFIG_FILE="${SCRIPT_PATH}/../imagesrc/config.txt"
RAMDISK_FILE="${BUILD_DIR}/ramdisk"

#
# default EFI size in MiB
#
DEFAULT_EFI_SIZE=64

#
# this is where compilation and execution actually starts
#

mkdir -p "${BUILD_DIR}"

pushd "${BUILD_DIR}" >/dev/null

if [ "${BUILD_TOO}" -eq 1 ]; then
	"${SOURCE_ROOT}/scripts/compile.sh" || command-failed
	"${SOURCE_ROOT}/scripts/build-ramdisk.py" -b || command-failed
fi

cleanup-disk() {
	rm "${DISK_PATH}"
}

cleanup-disk-and-die() {
	cleanup-disk
	die-red "Failed to create disk"
}

partfs-mount() {
	partfs -o dev="${DISK_PATH}" "${MOUNT_PATH}"
}

cleanup-partfs() {
	fusermount -u "${MOUNT_PATH}" || command-failed
}

create-disk() {
	qemu-img create -f raw "${DISK_PATH}" 1G || return 1

	sgdisk -o "${DISK_PATH}" || return 1

	INFO="$(sgdisk -p "${DISK_PATH}")"
	SECTOR_SIZE="$(echo "${INFO}" | sed -rn 's/Sector size \(logical\): ([0-9]+).*/\1/p')"
	FIRST_SECTOR="$(echo "${INFO}" | sed -rn 's/First usable sector is ([0-9]+).*/\1/p')"
	SECTOR_ALIGNMENT="$(echo "${INFO}" | sed -rn 's/Partitions will be aligned on ([0-9]+).*/\1/p')"

	FIRST_ALIGNED_SECTOR="$(round-up ${FIRST_SECTOR} ${SECTOR_ALIGNMENT})"
	EFI_SECTOR_COUNT="$(( (${DEFAULT_EFI_SIZE} * 1024 * 1024) / ${SECTOR_SIZE} ))"
	LAST_EFI_SECTOR="$(( ${FIRST_ALIGNED_SECTOR} + ${EFI_SECTOR_COUNT} ))"

	sgdisk -o -n 1:${FIRST_ALIGNED_SECTOR}:${LAST_EFI_SECTOR} -t 1:0700 "${DISK_PATH}" || return 1

	partfs-mount || return 1

	mkfs.fat "${MOUNT_PATH}/p1" || (cleanup-partfs && return 1)

	cleanup-partfs
}

mkdir -p "${MOUNT_PATH}" || command-failed

if ! [ -f "${DISK_PATH}" ]; then
	create-disk || cleanup-disk-and-die
fi

partfs-mount || command-failed

cleanup-partfs-and-die() {
	cleanup-partfs
	die-red "Failed to setup EFI"
}

mmd -D s -i "${MOUNT_PATH}/p1" ::EFI
mmd -D s -i "${MOUNT_PATH}/p1" ::EFI/anillo
mmd -D s -i "${MOUNT_PATH}/p1" ::EFI/BOOT

mcopy -D o -i "${MOUNT_PATH}/p1" "${STARTUP_SCRIPT}" ::startup.nsh || cleanup-partfs-and-die
mcopy -D o -i "${MOUNT_PATH}/p1" "${CONFIG_FILE}" ::EFI/anillo/config.txt || cleanup-partfs-and-die
mcopy -D o -i "${MOUNT_PATH}/p1" "${EFI_BOOTSTRAP}" ::EFI/anillo/ferro-bootstrap.efi || cleanup-partfs-and-die
mcopy -D o -i "${MOUNT_PATH}/p1" "${EFI_BOOTSTRAP}" ::EFI/BOOT/BOOTx64.efi || cleanup-partfs-and-die
mcopy -D o -i "${MOUNT_PATH}/p1" "${FERRO_KERNEL}" ::EFI/anillo/ferro || cleanup-partfs-and-die

if [ -f "${RAMDISK_FILE}" ]; then
	mcopy -D o -i "${MOUNT_PATH}/p1" "${RAMDISK_FILE}" ::EFI/anillo/ramdisk || cleanup-partfs-and-die
fi

cleanup-partfs

popd >/dev/null
