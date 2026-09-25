/* zzdf_platform_hooks.h - hooks injected into patched engine copies
 * in place of SDL calls. Implemented in zzdf_input.cpp / zzdf_video.cpp.
 * ASCII only. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void zzdf_getRelativeMouse(int* dx, int* dy);
void zzdf_getAbsoluteMouse(int* x, int* y);
#ifdef __cplusplus
}
#endif
