package com.mr0ta.babelrecomp;

import android.content.res.AssetManager;
import android.os.Bundle;
import android.content.pm.ActivityInfo;
import android.graphics.Color;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

import org.libsdl.app.SDLActivity;

/*
 * The game reads its data "beside the executable": on Android that is the
 * app's external files folder (Android/data/com.mr0ta.babelrecomp/files/),
 * where the player also puts rom.sfc. The files bundled in the APK (launcher
 * fonts and images, i18n/, the mod catalog) are copied there once per
 * version. Nothing the player added is touched.
 */
public class BabelActivity extends SDLActivity {
    private static final String TAG = "babel-recomp";
    private static final String[] BUNDLED = { "assets", "i18n", "mods" };

    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };   /* SDL3 is linked into libmain.so */
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        installBundledFiles();
        super.onCreate(savedInstanceState);
        if (mBrokenLibraries || mLayout == null) return;
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (android.os.Build.VERSION.SDK_INT >= 26) mSurface.setDefaultFocusHighlightEnabled(false);
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
        ViewGroup parent = (ViewGroup) mLayout.getParent();
        if (parent != null) parent.removeView(mLayout);
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        root.addView(mLayout, new FrameLayout.LayoutParams(-1, 1));
        LinearLayout actions = new LinearLayout(this);
        actions.setOrientation(LinearLayout.HORIZONTAL);
        actions.setBackgroundColor(0xff171d27);
        addAction(actions, "设置", KeyEvent.KEYCODE_ESCAPE);
        addAction(actions, "存档", KeyEvent.KEYCODE_F11);
        addAction(actions, "助手", KeyEvent.KEYCODE_I);
        root.addView(actions, new FrameLayout.LayoutParams(-1, dp(42)));
        setContentView(root);
        Runnable layout = () -> layoutSafeArea(root, actions);
        root.addOnLayoutChangeListener((v,l,t,r,b,ol,ot,or,ob) -> layout.run());
        root.setOnApplyWindowInsetsListener((v,insets) -> { root.post(layout); return insets; });
        root.post(layout);
    }

    private void layoutSafeArea(FrameLayout root, LinearLayout actions) {
        int w = root.getWidth(), h = root.getHeight();
        if (w <= 0 || h <= 0) return;
        int inset = 0;
        android.view.WindowInsets insets = root.getRootWindowInsets();
        if (insets != null) {
            if (android.os.Build.VERSION.SDK_INT >= 30)
                inset = insets.getInsetsIgnoringVisibility(android.view.WindowInsets.Type.statusBars()
                        | android.view.WindowInsets.Type.displayCutout()).top;
            else inset = insets.getSystemWindowInsetTop();
        }
        int[] location = new int[2];
        root.getLocationOnScreen(location);
        android.util.DisplayMetrics metrics = new android.util.DisplayMetrics();
        getWindowManager().getDefaultDisplay().getRealMetrics(metrics);
        int safe = (int)(Math.max(metrics.widthPixels, metrics.heightPixels) * 2.0 / 3.0
                - 50.0 * w / 1080.0) - location[1];
        int toolbarTop = Math.max(0, inset-location[1]);
        FrameLayout.LayoutParams bar = (FrameLayout.LayoutParams)actions.getLayoutParams();
        if (bar.topMargin != toolbarTop) { bar.topMargin = toolbarTop; actions.setLayoutParams(bar); }
        int top = toolbarTop + dp(42);
        int height = Math.max(1, Math.min(h, safe) - top);
        FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams)mLayout.getLayoutParams();
        if (lp.height != height || lp.topMargin != top) {
            lp.height = height; lp.topMargin = top; mLayout.setLayoutParams(lp);
            Log.i(TAG, "FlipPad surface " + w + "x" + height + " top=" + (location[1]+top)
                    + " bottom=" + (location[1]+top+height));
        }
    }

    private static native void nativeSuspend();

    @Override protected void onPause() {
        if (!mBrokenLibraries) nativeSuspend();
        super.onPause();
    }

    @Override public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
    }

    @Override protected String[] getArguments() {
        return new String[] { "--no-launcher", new File(getExternalFilesDir(null), "rom.sfc").getAbsolutePath() };
    }

    private int dp(int n) { return Math.round(n * getResources().getDisplayMetrics().density); }
    private void addAction(LinearLayout bar, String label, int key) {
        Button b = new Button(this);
        b.setText(label); b.setTextColor(Color.WHITE); b.setTextSize(14);
        b.setAllCaps(false); b.setFocusable(false);
        b.setBackgroundColor(Color.TRANSPARENT);
        b.setOnClickListener(v -> { onNativeKeyDown(key); onNativeKeyUp(key); });
        bar.addView(b, new LinearLayout.LayoutParams(0, -1, 1));
    }

    private void installBundledFiles() {
        File root = getExternalFilesDir(null);
        if (root == null) return;
        try {
            File rom = new File(root, "rom.sfc");
            if (!rom.exists()) copyFile(getAssets(), "private/rom.sfc", rom);
        } catch (IOException e) { Log.w(TAG, "Private ROM not bundled: " + e); }
        try {
            File config = new File(root, "config.ini");
            if (!config.exists()) copyFile(getAssets(), "defaults/config.ini", config);
        } catch (IOException e) { Log.w(TAG, "First-run data: " + e); }
        String version;
        try {
            version = getPackageManager().getPackageInfo(getPackageName(), 0).versionName
                    + "-" + getPackageManager().getPackageInfo(getPackageName(), 0).lastUpdateTime;
        } catch (Exception e) {
            version = "unknown";
        }
        File marker = new File(root, ".bundled-version");
        if (marker.exists() && version.equals(readFile(marker))) return;
        AssetManager am = getAssets();
        for (String dir : BUNDLED) copyTree(am, dir, new File(root, dir));
        writeFile(marker, version);
    }

    private static void copyTree(AssetManager am, String path, File dst) {
        try {
            String[] children = am.list(path);
            if (children == null || children.length == 0) {
                copyFile(am, path, dst);
                return;
            }
            dst.mkdirs();
            for (String child : children)
                copyTree(am, path + "/" + child, new File(dst, child));
        } catch (IOException e) {
            Log.w(TAG, "copy " + path + ": " + e);
        }
    }

    private static void copyFile(AssetManager am, String path, File dst) throws IOException {
        /* The player's own mod selection is kept across updates. */
        if (dst.getName().equals("state.toml") && dst.exists()) return;
        File parent = dst.getParentFile();
        if (parent != null) parent.mkdirs();
        try (InputStream in = am.open(path); OutputStream out = new FileOutputStream(dst)) {
            byte[] buf = new byte[64 * 1024];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        }
    }

    private static String readFile(File f) {
        try (InputStream in = new java.io.FileInputStream(f)) {
            byte[] b = new byte[256];
            int n = in.read(b);
            return n > 0 ? new String(b, 0, n) : "";
        } catch (IOException e) {
            return "";
        }
    }

    private static void writeFile(File f, String s) {
        try (OutputStream out = new FileOutputStream(f)) {
            out.write(s.getBytes());
        } catch (IOException e) {
            Log.w(TAG, "write " + f + ": " + e);
        }
    }
}
