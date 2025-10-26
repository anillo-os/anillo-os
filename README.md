# Anillo OS
A microkernel-based extensible operating system

# License
Anillo OS is licensed under the LGPLv3. Refer to the`COPYING` and `COPYING.LESSER` files distributed along with it for more information.

## Building

Building Anillo OS requires the following programs:
  * Clang - to compile the OS (Clang version >= 12).
  * `x86_64-apple-darwin-ld`/`aarch64-apple-darwin-ld` - to link the OS.
    * We'd love to use LLD instead, but unfortunately, it's missing support for certain required flags (namely: `-dylinker`, `-image_base`, and `-static`)
  * CMake - to compile the OS (CMake version >= 3.20).
  * Ninja (optional) - to compile the OS quickly.
  * Python 3 - for various scripts during the build process.
    * [Lark](https://github.com/lark-parser/lark) package - for the `spookygen` script

Creating a disk image for Anillo OS requires the following programs:
  * sgdisk - to create the disk image.
  * [partfs](https://github.com/braincorp/partfs) - to mount the disk image without root access.
  * FUSE - to use partfs.
  * mtools - to operate on the EFI partition.
  * mkfs.fat - to format the disk image.
  * qemu-img

To build the OS, use CMake:

```sh
export ANILLO_ARCH=$(uname -m)
mkdir -p build/${ANILLO_ARCH}

# remove `-G Ninja` if not using Ninja
# change Release to Debug for debug builds
cmake -S . -B build/${ANILLO_ARCH} -G Ninja -DCMAKE_BUILD_TYPE=Release

cmake --build build/${ANILLO_ARCH}
```

To build for a different architecture than that of your host, change the `ANILLO_ARCH` environment variable or define `ANILLO_ARCH` as a CMake variable (e.g. with a command line flag like `-DANILLO_ARCH=<whatever>`).

To generate `compile_commands.json`, add `-DCMAKE_EXPORT_COMPILE_COMMANDS=1` to the CMake configuration step above.

## Testing

Testing Anillo OS requires the following programs:
  * qemu - to run the OS.
  * OVMF/AAVMF - to run UEFI under qemu (depending on the architecture you want to build for).

To test the OS with QEMU, execute the `test/run.sh` script from the root of the source tree. To test a different architecture than that of your host, use the `ARCH` environment variable (e.g. `ARCH=aarch64 ./test/run.sh`).
