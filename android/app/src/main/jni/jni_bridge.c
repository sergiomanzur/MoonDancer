/*
 * Moon Dancer - Android JNI bridge for the SAF ROM import.
 *
 * The launcher's BROWSE button reaches this file through
 * mw_android_open_rom_picker(): the activity shows the system document
 * picker, copies the chosen stream to files/mwalk.zip, and calls
 * nativeRomImported(path) back into here.  That callback pushes an SDL
 * SDL_DROPFILE event carrying the copied path; the launcher's existing
 * DROPFILE handler adopts it (main_launcher.c), so the Android browse flow
 * needs no launcher-side branch of its own.
 *
 * Both directions are cached in JNI_OnLoad: FindClass() for an application
 * class only reliably works on the thread that loaded the library, with the
 * app's own class loader.  A lazy lookup from SDL's thread would run with the
 * system class loader and throw ClassNotFoundException on many devices.
 */
#include <jni.h>

#include <SDL.h>
#include <SDL_system.h>

#include "host_android.h"

#define MW_ACTIVITY_CLASS "com/moondancer/recomp/MoonDancerActivity"

/* Filled in JNI_OnLoad.  Either may stay NULL: the bridge degrades to "no
 * picker" and the launcher opens its in-UI browser instead of failing. */
static jclass s_activity_class;
static jmethodID s_open_rom_picker;

/* Java -> native: the imported archive is in place at `path`.  Push it as the
 * event the launcher already understands.  Runs on the UI thread; SDL_PushEvent
 * is thread-safe and the launcher's event loop picks it up next frame. */
static void JNICALL jni_native_rom_imported(JNIEnv *env, jclass cls, jstring jpath)
{
    const char *path;
    SDL_Event ev;

    (void)cls;
    if (!jpath) return;
    path = (*env)->GetStringUTFChars(env, jpath, NULL);
    if (!path) return;

    SDL_zero(ev);
    ev.type = SDL_DROPFILE;
    ev.drop.file = SDL_strdup(path);
    (*env)->ReleaseStringUTFChars(env, jpath, path);
    if (!ev.drop.file) {
        SDL_Log("ROM import: out of memory queuing the drop event");
        return;
    }
    if (SDL_PushEvent(&ev) <= 0) {
        SDL_Log("ROM import: SDL_PushEvent failed for %s", ev.drop.file);
        SDL_free(ev.drop.file);
        return;
    }
    SDL_Log("ROM import: queued SDL_DROPFILE for %s", ev.drop.file);
}

static const JNINativeMethod k_methods[] = {
    { "nativeRomImported", "(Ljava/lang/String;)V", (void *)jni_native_rom_imported },
};

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
{
    JNIEnv *env = NULL;
    jclass cls;

    (void)reserved;
    if (!vm || (*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK || !env)
        return JNI_ERR;

    cls = (*env)->FindClass(env, MW_ACTIVITY_CLASS);
    if (!cls) {
        (*env)->ExceptionClear(env);
        SDL_Log("ROM import: JNI_OnLoad could not find %s; BROWSE falls back "
                "to the in-UI browser", MW_ACTIVITY_CLASS);
        return JNI_VERSION_1_6;
    }
    s_activity_class = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);
    if (!s_activity_class) {
        SDL_Log("ROM import: could not pin the activity class");
        return JNI_VERSION_1_6;
    }

    s_open_rom_picker = (*env)->GetMethodID(env, s_activity_class, "openRomPicker", "()V");
    if (!s_open_rom_picker) {
        (*env)->ExceptionClear(env);
        SDL_Log("ROM import: MoonDancerActivity.openRomPicker() is missing; "
                "BROWSE falls back to the in-UI browser");
    }
    if ((*env)->RegisterNatives(env, s_activity_class, k_methods, 1) != JNI_OK) {
        (*env)->ExceptionClear(env);
        SDL_Log("ROM import: RegisterNatives(nativeRomImported) failed; the "
                "imported archive would never reach the launcher");
    }
    SDL_Log("ROM import: JNI bridge ready");
    return JNI_VERSION_1_6;
}

int mw_android_open_rom_picker(void)
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject activity;

    if (!env || !s_activity_class || !s_open_rom_picker) return 0;
    activity = (jobject)SDL_AndroidGetActivity();
    if (!activity) {
        SDL_Log("ROM import: no activity to show the picker from");
        return 0;
    }
    (*env)->CallVoidMethod(env, activity, s_open_rom_picker);
    (*env)->DeleteLocalRef(env, activity);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        return 0;
    }
    return 1;
}
