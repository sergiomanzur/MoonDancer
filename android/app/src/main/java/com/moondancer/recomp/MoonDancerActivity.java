package com.moondancer.recomp;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.net.Uri;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

import org.libsdl.app.SDLActivity;

/**
 * Moon Dancer's Android entry point.
 *
 * SDLActivity owns the SDL thread, the surface, audio routing and the pad
 * manager; libmain.so's SDL_main is the launcher, which starts the game
 * in-process (see mw_host_spawn_game).
 *
 * This class also owns the SAF archive import: the launcher's BROWSE button
 * calls openRomPicker() through the JNI bridge (src/../jni/jni_bridge.c), the
 * system document picker returns a content:// URI, and the stream is copied to
 * the app's files/mwalk.zip.  The copy is announced to the launcher as an
 * SDL_DROPFILE event carrying that path (nativeRomImported), which is the same
 * event the desktop launcher receives when a file is dropped on its window.
 */
public class MoonDancerActivity extends SDLActivity {
    private static final String TAG = "MoonDancer";

    /** startActivityForResult request code for the SAF ROM picker ("MW"). */
    private static final int REQ_ROM_PICK = 0x4D57;

    /** Imported archive name, exactly where find_default_zip() looks. */
    private static final String ROM_FILE = "mwalk.zip";

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    /**
     * Native entry point (mw_android_open_rom_picker): show the system
     * document picker for a zip archive.
     *
     * Called on SDL's thread, but the picker must start from the UI thread,
     * hence runOnUiThread().  The result arrives later in onActivityResult on
     * the UI thread, which is why the launcher does not wait for a path: the
     * browse call returns as soon as the picker is requested.
     */
    public void openRomPicker() {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("application/zip");
                try {
                    startActivityForResult(intent, REQ_ROM_PICK);
                    Log.i(TAG, "ROM picker requested");
                } catch (ActivityNotFoundException e) {
                    // No DocumentsUI on this device.  The launcher keeps
                    // working with whatever archive last_archive.txt names;
                    // the import is simply unavailable.
                    Log.e(TAG, "no document picker for " + intent, e);
                }
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != REQ_ROM_PICK) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        if (resultCode != Activity.RESULT_OK || data == null || data.getData() == null) {
            Log.i(TAG, "ROM picker cancelled");
            return;
        }

        final Uri uri = data.getData();
        final File dest = new File(getFilesDir(), ROM_FILE);
        final File tmp = new File(getFilesDir(), ROM_FILE + ".import");
        InputStream in = null;
        OutputStream out = null;
        long total = 0;

        try {
            in = getContentResolver().openInputStream(uri);
            if (in == null) throw new IOException("openInputStream returned null");
            // Copy beside the archive, then rename: a failed or interrupted
            // import must never destroy the archive the launcher already has.
            out = new FileOutputStream(tmp);
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
                total += n;
            }
            out.flush();
        } catch (IOException e) {
            Log.e(TAG, "ROM import failed for " + uri, e);
            tmp.delete();
            return;
        } finally {
            try {
                if (out != null) out.close();
            } catch (IOException ignored) {
            }
            try {
                if (in != null) in.close();
            } catch (IOException ignored) {
            }
        }

        if (total <= 0) {
            Log.e(TAG, "ROM import produced an empty file, discarded");
            tmp.delete();
            return;
        }
        if (dest.exists() && !dest.delete()) {
            Log.e(TAG, "could not replace " + dest);
            tmp.delete();
            return;
        }
        if (!tmp.renameTo(dest)) {
            Log.e(TAG, "could not rename " + tmp + " to " + dest);
            tmp.delete();
            return;
        }

        Log.i(TAG, "ROM imported: " + uri + " -> " + dest + " (" + total + " bytes)");
        nativeRomImported(dest.getAbsolutePath());
    }

    /**
     * Hands the copied archive to the launcher as an SDL_DROPFILE event.
     * Registered with RegisterNatives in libmain.so's JNI_OnLoad.
     */
    private static native void nativeRomImported(String path);
}
