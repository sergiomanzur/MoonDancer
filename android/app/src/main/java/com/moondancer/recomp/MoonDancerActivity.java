package com.moondancer.recomp;

import org.libsdl.app.SDLActivity;

/**
 * Moon Dancer's Android entry point.
 *
 * SDLActivity owns the SDL thread, the surface, audio routing and the pad
 * manager; libmain.so's SDL_main is the launcher, which starts the game
 * in-process (see mw_host_spawn_game).  Task 9 adds the SAF archive picker
 * to this class.
 */
public class MoonDancerActivity extends SDLActivity {
    private static final String TAG = "MoonDancer";

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
