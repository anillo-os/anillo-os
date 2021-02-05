# Ferro
Anillo OS' microkernel

## Layout
`bootstrap` contains various bootloaders to allow the kernel to be booted via different methods for different supported architectures/platforms (e.g. UEFI, Multiboot, etc.).

`core` contains the actual core of the kernel.

`libk` implements a minimalistic library for kernel-space utilities similar to what libc provides (e.g. `memcpy`, `memcmp`, `strlen`, etc.).
