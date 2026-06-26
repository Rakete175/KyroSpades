package com.example.kyrospades;

import org.libsdl.app.SDLActivity;

import android.content.Intent;
import android.net.Uri;
import android.view.View;
import androidx.core.content.FileProvider;

import java.io.File;

public class KyroSpadesActivity extends SDLActivity {
    // SDL2 is statically linked into libmain.so — don't try to load libSDL2.so separately
    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }

    /* Called from native (window.c) via JNI when the in-game Fullscreen toggle
       changes. Hides only the top status bar and leaves the bottom navigation
       bar visible, unlike SDL's built-in fullscreen which hides both and leaves
       the player with no back button to exit the menu. Must run on the UI
       thread: system-UI changes from the SDL thread are illegal. */
    public static void setStatusBarHidden(final boolean hidden) {
        final SDLActivity activity = mSingleton;
        if (activity == null)
            return;
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                int flags = hidden
                    ? (View.SYSTEM_UI_FLAG_FULLSCREEN
                       | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                       | View.SYSTEM_UI_FLAG_LAYOUT_STABLE)
                    : (View.SYSTEM_UI_FLAG_VISIBLE
                       | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
                activity.getWindow().getDecorView().setSystemUiVisibility(flags);
            }
        });
    }

    /* Called from native (window.c) when the user taps Share on a demo. `path`
       is relative to the app's external files dir (the CWD the native side
       chdir()s into), e.g. "demos/foo.demo". Resolves it to an absolute file,
       wraps it in a FileProvider content URI, and fires an ACTION_SEND chooser.
       Runs on the UI thread because it starts an activity. */
    public static void shareFile(final String path) {
        final SDLActivity activity = mSingleton;
        if (activity == null || path == null)
            return;
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    File file = new File(activity.getExternalFilesDir(null), path);
                    if (!file.exists())
                        return;
                    Uri uri = FileProvider.getUriForFile(
                        activity, activity.getPackageName() + ".fileprovider", file);
                    Intent intent = new Intent(Intent.ACTION_SEND);
                    intent.setType("application/octet-stream");
                    intent.putExtra(Intent.EXTRA_STREAM, uri);
                    intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                    activity.startActivity(
                        Intent.createChooser(intent, "Share demo").addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
                } catch (Exception e) {
                    /* Bad provider config or no app to receive — nothing to do. */
                }
            }
        });
    }
}
