# Experimental KPatch-Next LKM

This branch carries the experimental loadable-kernel-module backend. It
provides the KernelPatch hook/KPM runtime and a root-only supercall channel on
syscall 45. The LKM does not include sucompat, manager discovery, kstorage,
accctl, or a root shell; those interfaces are intentionally outside the
KPatch-Next contract.

The module must be built for the target kernel and loaded by an already-root
process. Kernel module signing, SELinux policy, and the target kernel's module
configuration remain the integrator's responsibility. The KPM loader and hook
runtime are experimental and should be tested on each target KMI before use.

With a DDK or kernel build tree available:

```sh
cd lkm
CONFIG_KP_LKM=m CC=clang make KDIR=/path/to/kernel
```

The resulting `kernelpatch.ko` is an out-of-tree module. The repository CI
workflow builds it against the supported Android KMI/DDK matrix on this branch.
