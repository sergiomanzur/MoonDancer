#ifndef MW_HOST_ANDROID_H
#define MW_HOST_ANDROID_H

#ifdef __ANDROID__

#ifdef __cplusplus
extern "C" {
#endif

/* Android SAF archive picker (JNI bridge in android/app/src/main/jni/
 * jni_bridge.c).  1 = the activity's system picker is opening: the browse flow
 * is handled, but there is no path yet -- the imported archive arrives later
 * as an SDL_DROPFILE event.  0 = the bridge or the call is unavailable, so the
 * caller opens its in-UI browser instead (the fallback that keeps BROWSE
 * usable on a device without a working document picker). */
int mw_android_open_rom_picker(void);

#ifdef __cplusplus
}
#endif

#endif /* __ANDROID__ */

#endif /* MW_HOST_ANDROID_H */
