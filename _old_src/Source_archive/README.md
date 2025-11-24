Archive of original small headers

What this is

This folder contains original header backups copied from `Source/` prior to consolidating them into `UiMenus.h` and `UiAssets.h`.

Files saved here (auto-backups and original .orig.h files):
- GridScaleMenu.orig.h
- StepOffsetMenu.orig.h
- ShuffleModeMenu.orig.h
- SvgUtils.orig.h
- DancerFramesCache.orig.h
- GridScaleMenu.auto-20251124.orig.h (automated timestamped backup)
- StepOffsetMenu.auto-20251124.orig.h
- ShuffleModeMenu.auto-20251124.orig.h
- SvgUtils.auto-20251124.orig.h
- DancerFramesCache.auto-20251124.orig.h

How to restore a header

1. Copy the desired backup file back into `Source/` with the original name. For example:

   cp _old_src/Source_archive/GridScaleMenu.orig.h Source/GridScaleMenu.h

2. Re-run CMake and rebuild the project:

   cmake --build --preset release

Why these files were archived

To reduce header fragmentation and centralize small UI helpers, the implementations were consolidated into `Source/UiMenus.h` and `Source/UiAssets.h`. The original headers are kept here to allow easy rollback if needed.

Questions or rollbacks

If you want me to restore any of these automatically (copy and rebuild), tell me which file and I'll do it.
