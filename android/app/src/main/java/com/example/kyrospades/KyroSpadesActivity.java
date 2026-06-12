package com.example.kyrospades;

import org.libsdl.app.SDLActivity;

public class KyroSpadesActivity extends SDLActivity {
    // SDL2 is statically linked into libmain.so — don't try to load libSDL2.so separately
    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }
}
