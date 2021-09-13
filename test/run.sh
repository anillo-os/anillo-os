#!/bin/bash

SCRIPT_PATH="$(cd "$(dirname "$0")" >/dev/null 2>&1; pwd -P)"
SOURCE_ROOT="${SCRIPT_PATH}/.."

source "${SOURCE_ROOT}/scripts/util.sh"

EFI_CODE_PATH="${BUILD_DIR}/efi-code.fd"
EFI_VARS_PATH="${BUILD_DIR}/efi-vars.fd"

if [ -z "${EFI_CODE_SOURCE_PATH}" ]; then
	case "${ARCH}" in
		x86_64)
			EFI_CODE_SOURCE_PATH="/usr/share/OVMF/OVMF_CODE.fd"
			if [ ! -f "${EFI_CODE_SOURCE_PATH}" ]; then
				EFI_CODE_SOURCE_PATH="/usr/share/OVMF/x64/OVMF_CODE.fd"
			fi
			;;

		aarch64)
			EFI_CODE_SOURCE_PATH="/usr/share/AAVMF/AAVMF_CODE.fd"
			;;

		*)
			die-red "Unsupported architecture: ${ARCH}"
			;;
	esac
fi

if [ -z "${EFI_VARS_SOURCE_PATH}" ]; then
	case "${ARCH}" in
		x86_64)
			EFI_VARS_SOURCE_PATH="/usr/share/OVMF/OVMF_VARS.fd"
			if [ ! -f "${EFI_VARS_SOURCE_PATH}" ]; then
				EFI_VARS_SOURCE_PATH="/usr/share/OVMF/x64/OVMF_VARS.fd"
			fi
			;;

		aarch64)
			EFI_VARS_SOURCE_PATH="/usr/share/AAVMF/AAVMF_VARS.fd"
			;;

		*)
			die-red "Unsupported architecture: ${ARCH}"
			;;
	esac
fi

DISK_PATH="${BUILD_DIR}/disk.img"
MOUNT_PATH="${BUILD_DIR}/mnt"

SERIAL=pty
QEMU_ARGS=(
	-net none
)
DEBUG_SERIAL=0

BUILD_TOO=0

for ((i=0; i <= "${#@}"; ++i)); do
	arg="${!i}"
	if [ "x${arg}" == "x-qd" ] || [ "x${arg}" == "x--qemu-debug" ]; then
		QEMU_ARGS+=(
			-S
			-gdb tcp::2345
		)
	fi
	if [ "x${arg}" == "x-cr" ] || [ "x${arg}" == "x--cpu-reset" ]; then
		QEMU_ARGS+=(
			-d cpu_reset
		)
	fi
	if [ "x${arg}" == "x-k" ] || [ "x${arg}" == "x--kvm" ]; then
		QEMU_ARGS+=(
			-enable-kvm
			-cpu host
		)
	fi
	if [ "x${arg}" == "x-s" ] || [ "x${arg}" == "x--serial" ]; then
		((++i))
		SERIAL="${!i}"
		if [ "x${SERIAL}" != "xpty" ] && [ "x${SERIAL}" != "xstdio" ] && [ "x${SERIAL}" != "xnone" ]; then
			die-red "Unrecognized/unsupported serial option: ${SERIAL}"
		fi
	fi
	if [ "x${arg}" == "x-b" ] || [ "x${arg}" == "x--build" ]; then
		BUILD_TOO=1
	fi
	if [ "x${arg}" == "x-d" ] || [ "x${arg}" == "x--debug" ]; then
		DEBUG_SERIAL=1
	fi
done

if [ "${ARCH}" == "aarch64" ]; then
	# most of this was copied over from an AARCH64 libvirt VM
	# some of it was modified afterwards
	QEMU_ARGS+=(
		-name guest=anillo-aarch64,debug-threads=on

		-blockdev '{"driver":"file","filename":"'"${EFI_CODE_PATH}"'","node-name":"anillo-pflash0-storage","auto-read-only":true,"discard":"unmap"}'
		-blockdev '{"node-name":"anillo-pflash0-format","read-only":true,"driver":"raw","file":"anillo-pflash0-storage"}'
		-blockdev '{"driver":"file","filename":"'"${EFI_VARS_PATH}"'","node-name":"anillo-pflash1-storage","auto-read-only":true,"discard":"unmap"}'
		-blockdev '{"node-name":"anillo-pflash1-format","read-only":false,"driver":"raw","file":"anillo-pflash1-storage"}'
		-machine virt-4.2,accel=tcg,usb=off,dump-guest-core=off,gic-version=2,pflash0=anillo-pflash0-format,pflash1=anillo-pflash1-format

		-cpu cortex-a57

		#-m 1024
		#-overcommit mem-lock=off

		#-smp 1,sockets=1,cores=1,threads=1

		-no-user-config
		-nodefaults

		-chardev 'socket,id=charmonitor,path='"${BUILD_DIR}"'/serial-monitor,server=on,wait=off'
		-mon chardev=charmonitor,id=monitor,mode=control

		-rtc base=utc

		-no-shutdown

		-boot strict=on

		-device pcie-root-port,port=0x8,chassis=1,id=pci.1,bus=pcie.0,multifunction=on,addr=0x1
		-device pcie-root-port,port=0x9,chassis=2,id=pci.2,bus=pcie.0,addr=0x1.0x1
		-device pcie-root-port,port=0xa,chassis=3,id=pci.3,bus=pcie.0,addr=0x1.0x2
		-device pcie-root-port,port=0xb,chassis=4,id=pci.4,bus=pcie.0,addr=0x1.0x3
		-device pcie-root-port,port=0xc,chassis=5,id=pci.5,bus=pcie.0,addr=0x1.0x4
		-device pcie-root-port,port=0xd,chassis=6,id=pci.6,bus=pcie.0,addr=0x1.0x5

		-device qemu-xhci,p2=15,p3=15,id=usb,bus=pci.2,addr=0x0

		-device virtio-serial-pci,id=virtio-serial0,bus=pci.3,addr=0x0

		-blockdev '{"driver":"file","filename":"'"${DISK_PATH}"'","node-name":"anillo-1-storage","auto-read-only":true,"discard":"unmap"}'
		-blockdev '{"node-name":"anillo-1-format","read-only":false,"driver":"raw","file":"anillo-1-storage"}'
		-device virtio-blk-pci,scsi=off,bus=pci.4,addr=0x0,drive=anillo-1-format,id=virtio-disk0,bootindex=1

		-chardev 'socket,id=charchannel0,server=on,wait=off,path='"${BUILD_DIR}"'/serial'
		-device virtserialport,bus=virtio-serial0.0,nr=1,chardev=charchannel0,id=channel0,name=org.qemu.guest_agent.0

		#-spice port=0,disable-ticketing,image-compression=off,seamless-migration=on
		-device ramfb
		-device virtio-gpu-pci,id=video0,max_outputs=1,bus=pci.5,addr=0x0

		-device usb-kbd
	)
elif [ "${ARCH}" == "x86_64" ]; then
	QEMU_ARGS+=(
		-drive "if=pflash,format=raw,unit=0,file=${EFI_CODE_PATH}"
		-drive "if=pflash,format=raw,unit=1,file=${EFI_VARS_PATH}"
		-drive "if=virtio,format=raw,file=${DISK_PATH}"
		-machine type=q35
	)
fi

if [ "${SERIAL}" == "pty" ]; then
	QEMU_ARGS+=(
		-chardev pty,id=charserial0
		-serial chardev:charserial0
	)
elif [ "${SERIAL}" == "stdio" ]; then
	QEMU_ARGS+=(
		-serial stdio
	)
fi

if [ "${DEBUG_SERIAL}" -eq 1 ]; then
	QEMU_ARGS+=(
		-serial tcp::1234,server,nowait
	)
fi

EFI_BOOTSTRAP="${BUILD_DIR}/kernel/src/bootstrap/uefi/ferro-bootstrap.efi"
FERRO_KERNEL="${BUILD_DIR}/kernel/ferro"

#
# default EFI size in MiB
#
DEFAULT_EFI_SIZE=64

#
# this is where compilation and execution actually starts
#

mkdir -p "${BUILD_DIR}"

pushd "${BUILD_DIR}" >/dev/null

# copy efi vars from source if they don't already exist
setup-efi-images() {
	if ! [ -f "${EFI_VARS_PATH}" ]; then
		cp "${EFI_VARS_SOURCE_PATH}" "${EFI_VARS_PATH}" || return 1
	fi
	if ! [ -f "${EFI_CODE_PATH}" ]; then
		cp "${EFI_CODE_SOURCE_PATH}" "${EFI_CODE_PATH}" || return 1
	fi
}

setup-efi-images || die-red "Failed to set up EFI firmware/variables"

if [ "${BUILD_TOO}" -eq 1 ]; then
	"${SOURCE_ROOT}/scripts/compile.sh" || command-failed
fi

cleanup-disk() {
	rm "${DISK_PATH}"
}

cleanup-disk-and-die() {
	cleanup-disk
	die-red "Failed to create disk"
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

	LOOP_DEV=$(sudo losetup -f -P "${DISK_PATH}" --show)

	cleanup-loop-dev() {
		sudo losetup -d "${LOOP_DEV}" || command-failed
	}

	sudo mkfs.fat "${LOOP_DEV}p1" || (cleanup-loop-dev && return 1)

	cleanup-loop-dev
}

if ! [ -f "${DISK_PATH}" ]; then
	create-disk || cleanup-disk-and-die
fi

mkdir -p "${MOUNT_PATH}" || command-failed

LOOP_DEV=$(sudo losetup -f -P "${DISK_PATH}" --show)

cleanup-loop-dev() {
	sudo losetup -d "${LOOP_DEV}" || command-failed
}

cleanup-loop-dev-and-die() {
	cleanup-loop-dev
	die-red "Failed to setup EFI"
}

sudo mount "${LOOP_DEV}p1" "${MOUNT_PATH}" -t vfat -o user,umask=0000,rw || cleanup-loop-dev-and-die

cleanup-mount() {
	sudo umount "${MOUNT_PATH}" || command-failed
}

cleanup-mount-and-die() {
	cleanup-mount
	die-red "Failed to setup EFI"
}

mkdir -p "${MOUNT_PATH}/EFI/anillo" || cleanup-mount-and-die
mkdir -p "${MOUNT_PATH}/EFI/BOOT" || cleanup-mount-and-die

cp "${SCRIPT_PATH}/startup.nsh" "${MOUNT_PATH}/startup.nsh" || cleanup-mount-and-die
cp "${SCRIPT_PATH}/config.txt" "${MOUNT_PATH}/EFI/anillo/config.txt" || cleanup-mount-and-die
cp "${EFI_BOOTSTRAP}" "${MOUNT_PATH}/EFI/anillo/ferro-bootstrap.efi" || cleanup-mount-and-die
cp "${FERRO_KERNEL}" "${MOUNT_PATH}/EFI/anillo/ferro" || cleanup-mount-and-die

cleanup-mount
cleanup-loop-dev

qemu-system-${ARCH} "${QEMU_ARGS[@]}"

popd >/dev/null
