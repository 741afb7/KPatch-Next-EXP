# Codex Notes

This repo has a persistent on-disk preset layout. Treat changes in `kernel/include/preset.h` as ABI changes, not normal refactors. The tools build copies this header into `tools/preset.h`, so regenerate/keep that generated mirror in sync when building tools.

## Rules

- Do not change `MAP_SYMBOL_NUM` casually.
- `map_symbol_t` sits before `header_backup` inside `setup_preset_t`, so changing `MAP_SYMBOL_NUM` moves `header_backup` and every field after it.
- This project's versioning diverges from upstream KernelPatch. Do not reuse upstream's `0.13.1` / `0.13.2` header-backup split without mapping it to this repo's own commits and tags.
- The relevant offset drift happened after this repo's `0.13.7` tag and before `0.13.8`.

## Header Backup Layout Change

- (`add new method to local kallsyms_lookup & fix 4.14 kallsyms lookup`) changes `MAP_SYMBOL_NUM` from `5` to `6`, adding one `uint64_t` before `header_backup`; this moves `header_backup` by `+8` bytes.
- (`disable failed fallback`) changes `MAP_SYMBOL_NUM` from `6` to `7`, replacing the temporary `memblock_start_of_DRAM_relo` slot with two type slots; this moves `header_backup` by another `+8` bytes.
- then updates the project version to `0.13.8`.
- (`fix header backup`) imports the upstream compatibility reader (`preset_header_backup()`), but its `VERSION(0, 13, 1)` split is upstream-specific and must be adjusted for this project if changing the logic.

## Known Header Backup Offsets

- KPatch-Next `0.13.7` and older tagged builds: `current_header_backup_offset - 16`
- KPatch-Next `0.13.8` and newer tagged builds/current `main`: `current_header_backup_offset`
- `current_header_backup_offset - 8` corresponds to the non-release intermediate layout after `ab26ccb` but before `a7e1734`; keep it only as a fallback for local/intermediate images.

`tools/patch.c` resolves this through `preset_header_backup()` and validates candidates with the saved primary-entry header.

## Safe Future Changes

- Prefer fixed-capacity storage for persistent structs.
- Prefer using reserved space like `__[]` for small new metadata.
- If a serialized struct must change, add explicit versioned parsing instead of relying on current `offsetof(...)`.

## Validation

- Rebuild tools from this repo so `kernel/include/preset.h` is copied into `tools/preset.h` before compiling.
- Re-test upgrade flow with:
  - old patched boot -> extract kernel with the external `magiskboot` workflow
  - repatch extracted kernel
  - unpatch or restore the kernel
  - repatch again and verify the result
