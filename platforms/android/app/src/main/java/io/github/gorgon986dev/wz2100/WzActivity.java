package io.github.gorgon986dev.wz2100;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.res.AssetManager;
import android.net.wifi.WifiManager;
import android.os.Bundle;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

/**
 * Warzone 2100 Android activity.
 *
 * The game reads its data (base.wz, mp.wz, music, fonts...) from a normal directory, so on first launch
 * (and after every app update) the data bundled in the APK's assets is copied to internal storage.
 * That copy runs on SDL's main thread, before the native game starts, so the UI thread stays responsive.
 */
public class WzActivity extends SDLActivity {
    private static final String TAG = "warzone2100";
    private static final String ASSET_DATA_DIR = "data";

    private WifiManager.MulticastLock multicastLock;

    @Override
    protected String[] getLibraries() {
        // SDL3 is statically linked into libmain.so
        return new String[] { "c++_shared", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        try {
            // Needed to discover LAN games
            WifiManager wifi = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            if (wifi != null) {
                multicastLock = wifi.createMulticastLock("wz2100-lan");
                multicastLock.setReferenceCounted(false);
                multicastLock.acquire();
            }
        } catch (Exception e) {
            Log.w(TAG, "Could not acquire multicast lock", e);
        }
    }

    @Override
    protected void onDestroy() {
        if (multicastLock != null && multicastLock.isHeld()) {
            multicastLock.release();
        }
        super.onDestroy();
    }

    private File dataDir() {
        return new File(getFilesDir(), "data");
    }

    private File configDir() {
        return new File(getFilesDir(), "config");
    }

    @Override
    protected void main() {
        // Runs on SDL's thread
        try {
            ensureDataExtracted();
        } catch (IOException e) {
            Log.e(TAG, "Failed to extract game data", e);
        }
        super.main();
    }

    @Override
    protected String[] getArguments() {
        File config = configDir();
        //noinspection ResultOfMethodCallIgnored
        config.mkdirs();
        return new String[] {
            "--datadir=" + dataDir().getAbsolutePath(),
            "--configdir=" + config.getAbsolutePath(),
        };
    }

    private String currentDataStamp() {
        try {
            PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
            return info.getLongVersionCode() + ":" + info.lastUpdateTime;
        } catch (Exception e) {
            return "unknown";
        }
    }

    private void ensureDataExtracted() throws IOException {
        File dest = dataDir();
        File stampFile = new File(getFilesDir(), "data.stamp");
        String stamp = currentDataStamp();
        if (dest.isDirectory() && stampFile.isFile() && stamp.equals(readFile(stampFile))) {
            return;
        }
        long start = System.currentTimeMillis();
        Log.i(TAG, "Extracting game data to " + dest);
        deleteRecursive(dest);
        copyAssetDir(getAssets(), ASSET_DATA_DIR, dest);
        writeFile(stampFile, stamp);
        Log.i(TAG, "Game data extracted in " + (System.currentTimeMillis() - start) + " ms");
    }

    private static void copyAssetDir(AssetManager am, String assetPath, File dest) throws IOException {
        String[] children = am.list(assetPath);
        if (children == null || children.length == 0) {
            // A file (or an empty dir)
            try (InputStream in = am.open(assetPath)) {
                File parent = dest.getParentFile();
                if (parent != null) {
                    //noinspection ResultOfMethodCallIgnored
                    parent.mkdirs();
                }
                try (OutputStream out = new FileOutputStream(dest)) {
                    byte[] buf = new byte[1 << 16];
                    int n;
                    while ((n = in.read(buf)) > 0) {
                        out.write(buf, 0, n);
                    }
                }
            } catch (IOException e) {
                // am.open() fails for an empty directory - just create it
                //noinspection ResultOfMethodCallIgnored
                dest.mkdirs();
            }
            return;
        }
        //noinspection ResultOfMethodCallIgnored
        dest.mkdirs();
        for (String child : children) {
            copyAssetDir(am, assetPath + "/" + child, new File(dest, child));
        }
    }

    private static void deleteRecursive(File f) {
        if (f.isDirectory()) {
            File[] children = f.listFiles();
            if (children != null) {
                for (File c : children) {
                    deleteRecursive(c);
                }
            }
        }
        //noinspection ResultOfMethodCallIgnored
        f.delete();
    }

    private static String readFile(File f) {
        try (InputStream in = new FileInputStream(f)) {
            byte[] b = new byte[(int) f.length()];
            int off = 0, n;
            while (off < b.length && (n = in.read(b, off, b.length - off)) > 0) {
                off += n;
            }
            return new String(b, 0, off, StandardCharsets.UTF_8);
        } catch (IOException e) {
            return "";
        }
    }

    private static void writeFile(File f, String s) throws IOException {
        try (OutputStream out = new FileOutputStream(f)) {
            out.write(s.getBytes(StandardCharsets.UTF_8));
        }
    }
}
