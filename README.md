# Anillo OS
A microkernel-based extensible operating system

# License
Anillo OS is licensed under the LGPLv3. Refer to the`COPYING` and `COPYING.LESSER` files distributed along with it for more information.

## Building

Building Anillo OS requires the following programs:
  * Clang - to compile the OS.
  * LLD - to link the OS.
  * jq (optional) - to generate `compile_commands.json` for compatible code analysis tools.

To build the OS, execute the `scripts/compile.sh` script from the root of the source tree. To build for a different architecture than that of your host, use the `ARCH` environment variable (e.g. `ARCH=aarch64 ./scripts/compile.sh`).

To generate `compile_commands.json` for all available architectures for use with compatible code analysis tools (such as [ccls](https://github.com/MaskRay/ccls)), execute the `scripts/compile-commands.sh` script from the root of the source tree. This will generate a separate `compile_commands.json` for each supported architecture in the build directory for each architecture (e.g. `build/x86_64/compile_commands.json`, `build/aarch64/compile_commands.json`, etc.). To use this with code analysis tools, symlink or copy the desired architecture's `compile_commands.json` to the root of source tree.

## Testing

Testing Anillo OS requires the following programs:
  * qemu - to run the OS.
  * OVMF/AAVMF - to run UEFI under qemu (depending on the architecture you want to build for).
  * gdisk - to partition the disk image to install Anillo OS on.
  * losetup - to mount the disk image and copy the necessary files onto it.
  * mkfs.fat - to format the disk image to install Anillo OS on.

To test the OS with QEMU, execute the `test/run.sh` script from the root of the source tree. To test a different architecture than that of your host, use the `ARCH` environment variable (e.g. `ARCH=aarch64 ./test/run.sh`). Note that testing Anillo OS in this way requires sudo access (to mount the disk as a loopback device).
