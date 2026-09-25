# Syscall Hook

KPatch provides dedicated APIs for hooking Linux system calls. These are built
on the inline hook framework and handle kernels with and without syscall
wrappers.

## Overview

Two direct strategies are available:

| Strategy | API prefix | Description |
|----------|-----------|-------------|
| Inline hook | `inline_hook_syscalln` | Patches the syscall handler function's code directly |
| Function pointer hook | `fp_hook_syscalln` | Replaces the function pointer in the syscall table |

Both strategies support multiple hooks on the same syscall through a chain.

## Accessing Syscall Arguments

Some kernels wrap syscalls with a `pt_regs` parameter. Always use the helpers
below instead of reading `fargs->argN` directly:

```c
#include <syscall.h>

uint64_t value = syscall_argn(args, 0);
set_syscall_argn(args, 0, new_value);
```

## Inline Syscall Hook

```c
hook_err_t inline_hook_syscalln(int nr, int narg, void *before, void *after, void *udata);
void inline_unhook_syscalln(int nr, void *before, void *after);
hook_err_t inline_hook_compat_syscalln(int nr, int narg, void *before, void *after, void *udata);
void inline_unhook_compat_syscalln(int nr, void *before, void *after);
```

## Function Pointer Syscall Hook

```c
hook_err_t fp_hook_syscalln(int nr, int narg, void *before, void *after, void *udata);
void fp_unhook_syscalln(int nr, void *before, void *after);
hook_err_t fp_hook_compat_syscalln(int nr, int narg, void *before, void *after, void *udata);
void fp_unhook_compat_syscalln(int nr, void *before, void *after);
```

## Generic Hook and Dispatcher

```c
hook_err_t hook_syscalln(int nr, int narg, void *before, void *after, void *udata);
void unhook_syscalln(int nr, void *before, void *after);
hook_err_t hook_compat_syscalln(int nr, int narg, void *before, void *after, void *udata);
void unhook_compat_syscalln(int nr, void *before, void *after);
```

These APIs use the best available mechanism for the current kernel. After
`syscall_dispatch_init()` has run, kernels with syscall wrappers use one shared
dispatcher hook. The target search order is:

1. `invoke_syscall`, at syscall-handler granularity.
2. A compiler-generated suffixed `invoke_syscall` symbol.
3. `el0_svc_common`, at the common entry granularity.
4. A compiler-generated suffixed `el0_svc_common` symbol.
5. The existing per-syscall function-pointer or inline fallback.

With a dispatcher, registrations are kept in a bounded slot table. Native and
compat32 registrations are separate, the syscall tables are not modified, and
all dispatched syscalls use the same trampoline. If syscall wrappers are
unavailable, the target cannot be resolved, or installation fails, registration
continues through the per-syscall fallback.

`syscall_hook_global_enabled()` reports whether the shared dispatcher is active.
`hook_syscalln_legacy()` always uses the original per-syscall mechanism and is
useful when legacy handler-level behavior is required. The public dispatcher is
initialized by KPatch after syscall discovery; KPMs normally only call the hook
registration APIs.

## Callback Signature

Callbacks use the same `hook_fargs*_t` types as inline hooks:

```c
void before_openat(hook_fargs4_t *args, void *udata)
{
    int dfd = (int)syscall_argn(args, 0);
    const char __user *filename = (typeof(filename))syscall_argn(args, 1);
    char buf[256];
    compat_strncpy_from_user(buf, filename, sizeof(buf));
    pr_info("openat: dfd=%d, path=%s\n", dfd, buf);
}

void after_openat(hook_fargs4_t *args, void *udata)
{
    pr_info("openat returned: %ld\n", (long)args->ret);
    args->ret = (uint64_t)-EPERM;
}
```

The after callback observes the return value through `args->ret`; changes to it
are written back to the syscall's return register.

## Skipping the Original Syscall

Set `args->skip_origin = 1` in a before callback and provide a return value:

```c
void before_openat(hook_fargs4_t *args, void *udata)
{
    args->skip_origin = 1;
    args->ret = (uint64_t)-EPERM;
}
```

Register this callback with `hook_syscalln_override`, not `hook_syscalln`:

```c
hook_err_t hook_syscalln_override(int nr, int narg, void *before,
                                  void *after, void *udata);
```

The override is honored by the shared dispatcher only when its target is
`invoke_syscall`. At that granularity it skips only the real syscall handler;
`el0_svc_common` still performs syscall entry and exit processing. If the target
is `el0_svc_common`, or the dispatcher is unavailable, the override falls back
to the per-syscall mechanism. Ordinary `hook_syscalln` registrations never
enable `skip_origin` in the shared dispatcher.

The magic supercall uses this override API. This preserves its short-circuit
behavior without making ordinary dispatcher hooks capable of skipping the
common syscall path.

## Notes

- Use `syscall_argn()` and `set_syscall_argn()` rather than `args->argN` when
  accessing syscall parameters.
- Always call the matching unhook function from the KPM exit callback.
- The shared dispatcher has 64 registration slots and dispatches at most 16
  matching callbacks for one syscall; the legacy strategies keep their own
  per-syscall chain limits.
- Use `compat_strncpy_from_user` and `compat_copy_to_user` for userspace memory
  access inside hooks.