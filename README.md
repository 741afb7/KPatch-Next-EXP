# KPatch-Next

**Patching and hooking the Linux kernel with only stripped Linux kernel image.**

``` shell
 _  ______       _       _           _   _           _   
| |/ /  _ \ __ _| |_ ___| |__       | \ | | _____  _| |_ 
| ' /| |_) / _` | __/ __| '_ \ _____|  \| |/ _ \ \/ / __|
| . \|  __/ (_| | || (__| | | |_____| |\  |  __/>  <| |_ 
|_|\_\_|   \__,_|\__\___|_| |_|     |_| \_|\___/_/\_\\__|

```

- Obtain all symbol information without source code and symbol information.
- Inject arbitrary code into the kernel. (Static patching the kernel image or Runtime dynamic loading).
- Kernel function inline hook and syscall table hook are provided.
- Pure KPM module support for all root managers i.e Magisk & KernelSU/N (Except APatch).
- Checkout our magisk/kernelsu module! [KPatch-Next-Module-EXP](https://github.com/741afb7/KPatch-Next-Module-EXP)

## Requirement

CONFIG_KALLSYMS=y
CONFIG_KALLSYMS_ALL=y

or

CONFIG_KALLSYMS=y
CONFIG_KALLSYMS_ALL=n (Initial support)

or

CONFIG_KALLSYMS=y

## Supported Versions

Currently only supports arm64 architecture.  

Linux 3.18 - 6.12 (theoretically)  

## Get Involved

## More Information

[Documentation](./doc/)

## Credits

- [KernelPatch](https://github.com/bmax121/KernelPatch): Special thanks to the author for making this project possible.
- [KernelSU-Next/KPatch-Next](https://github.com/KernelSU-Next/KPatch-Next): The original project of this fork.
- [vmlinux-to-elf](https://github.com/marin-m/vmlinux-to-elf): Some ideas for parsing kernel symbols.
- [android-inline-hook](https://github.com/bytedance/android-inline-hook): Some code for fixing arm64 inline hook instructions.
- [tlsf](https://github.com/mattconte/tlsf): Memory allocator used for KPM. (Need another to allocate ROX memory.)

## License

KPatch-Next is licensed under the **GNU General Public License (GPL) 2.0** (<https://www.gnu.org/licenses/old-licenses/gpl-2.0.html>).
