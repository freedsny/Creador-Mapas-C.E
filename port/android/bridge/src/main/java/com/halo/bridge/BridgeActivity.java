package com.halo.bridge;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.content.pm.SigningInfo;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * The bridge's one screen (build.gradle). With the game under its new id installed, it opens it (an invite link
 * going on to it) and closes. Without it, it downloads the latest release's game (halo-contentevolved-*.zip, which
 * the game's own updater downloads too), checks that it is the game signed with this app's key, and hands it to the
 * package installer; once it is installed, it opens it, and the game brings this app's files over
 * (MigrationProvider).
 */
public class BridgeActivity extends Activity {
    private static final String REPOSITORY = "Watanoge/halo-content-evolved";
    private static final String USER_AGENT = "halo-ce-universal-bridge";
    private static final String ASSET_PREFIX = "halo-contentevolved-";
    private static final int TIMEOUT_MILLISECONDS = 20000;
    /** the most a download (a release's zip, about 35 MB) or the app in it may be */
    private static final long MAXIMUM_SIZE = 256L * 1024 * 1024;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private TextView status;
    private ProgressBar progress;
    private Button retry;
    private boolean downloading;
    /** the download is ready for the installer (it is handed over again by "Install") */
    private boolean downloaded;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (openGame(getIntent()))
            return;
        buildInterface();
        download();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        openGame(intent);
    }

    @Override
    protected void onResume() {
        super.onResume();
        // (back from the package installer)
        openGame(getIntent());
    }

    private boolean gameInstalled() {
        try {
            getPackageManager().getPackageInfo(BuildConfig.NEW_PACKAGE, 0);
            return true;
        } catch (PackageManager.NameNotFoundException e) {
            return false;
        }
    }

    /** the game opened, with the invite link this app was opened with, when it is installed */
    private boolean openGame(Intent intent) {
        if (!gameInstalled())
            return false;
        Intent start = null;

        if (intent != null && Intent.ACTION_VIEW.equals(intent.getAction()) && intent.getData() != null) {
            start = new Intent(Intent.ACTION_VIEW, intent.getData());
            start.setPackage(BuildConfig.NEW_PACKAGE);
        }
        if (start == null || start.resolveActivity(getPackageManager()) == null)
            start = getPackageManager().getLaunchIntentForPackage(BuildConfig.NEW_PACKAGE);
        if (start == null)
            return false;
        start.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            startActivity(start);
        } catch (RuntimeException e) {
            return false;
        }
        finish();
        return true;
    }

    /* ---------- the screen */

    private int dp(float value) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value, getResources().getDisplayMetrics());
    }

    private void buildInterface() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        layout.setPadding(dp(48), dp(24), dp(48), dp(24));
        layout.setBackgroundColor(Color.rgb(12, 16, 20));

        TextView title = new TextView(this);
        title.setText("Halo has moved to a new app");
        title.setTextColor(Color.WHITE);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 24);
        title.setGravity(Gravity.CENTER);
        layout.addView(title);

        TextView message = new TextView(this);
        message.setText("This old version is replaced by a new app, which is being installed now. When it first "
            + "starts, it brings over your settings, saves, profiles, mods and maps from this one, then offers to "
            + "uninstall this one.");
        message.setTextColor(Color.rgb(200, 205, 210));
        message.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        message.setGravity(Gravity.CENTER);
        message.setPadding(0, dp(16), 0, dp(16));
        layout.addView(message);

        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(1000);
        LinearLayout.LayoutParams progressLayout = new LinearLayout.LayoutParams(dp(480),
            LinearLayout.LayoutParams.WRAP_CONTENT);
        progressLayout.topMargin = dp(16);
        layout.addView(progress, progressLayout);

        status = new TextView(this);
        status.setTextColor(Color.rgb(160, 200, 160));
        status.setGravity(Gravity.CENTER);
        status.setPadding(0, dp(8), 0, dp(16));
        layout.addView(status);

        LinearLayout buttons = new LinearLayout(this);
        buttons.setOrientation(LinearLayout.HORIZONTAL);
        buttons.setGravity(Gravity.CENTER);
        layout.addView(buttons);

        retry = new Button(this);
        retry.setText("Install the new app");
        retry.setVisibility(View.GONE);
        retry.setOnClickListener(v -> {
            if (downloaded)
                install();
            else
                download();
        });
        buttons.addView(retry);

        Button page = new Button(this);
        page.setText("Download page");
        page.setOnClickListener(v -> {
            try {
                startActivity(new Intent(Intent.ACTION_VIEW,
                    Uri.parse("https://github.com/" + REPOSITORY + "/releases/latest")));
            } catch (RuntimeException e) {
                // no browser
            }
        });
        buttons.addView(page);

        setContentView(layout);
    }

    private void report(String text, int permille) {
        handler.post(() -> {
            status.setText(text);
            if (permille >= 0)
                progress.setProgress(permille);
        });
    }

    private void fail(String text) {
        handler.post(() -> {
            downloading = false;
            status.setText(text);
            retry.setVisibility(View.VISIBLE);
            retry.requestFocus();
        });
    }

    /* ---------- the download */

    private static HttpURLConnection open(String address) throws IOException {
        HttpURLConnection connection = (HttpURLConnection) new URL(address).openConnection();

        connection.setConnectTimeout(TIMEOUT_MILLISECONDS);
        connection.setReadTimeout(TIMEOUT_MILLISECONDS);
        connection.setRequestProperty("User-Agent", USER_AGENT);
        connection.setInstanceFollowRedirects(true);
        return connection;
    }

    private static byte[] readAll(InputStream stream) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[16384];
        int count;

        while ((count = stream.read(buffer)) > 0)
            out.write(buffer, 0, count);
        return out.toByteArray();
    }

    /** the latest release's game zip (its -release one if it has both) */
    private static String latestGameZip() throws Exception {
        HttpURLConnection connection = open("https://api.github.com/repos/" + REPOSITORY + "/releases/latest");

        connection.setRequestProperty("Accept", "application/vnd.github+json");
        try (InputStream stream = connection.getInputStream()) {
            JSONObject release = new JSONObject(new String(readAll(stream), StandardCharsets.UTF_8));
            JSONArray assets = release.optJSONArray("assets");
            String zip = null;

            for (int index = 0; assets != null && index < assets.length(); index++) {
                JSONObject asset = assets.getJSONObject(index);
                String name = asset.optString("name");

                if (name.startsWith(ASSET_PREFIX) && name.endsWith(".zip")
                    && (zip == null || name.equals(ASSET_PREFIX + "release.zip")))
                    zip = asset.optString("browser_download_url");
            }
            if (zip == null || zip.isEmpty())
                throw new IOException("the latest release (" + release.optString("tag_name") + ") has no new app");
            return zip;
        } finally {
            connection.disconnect();
        }
    }

    private void download() {
        if (downloading)
            return;
        downloading = true;
        downloaded = false;
        retry.setVisibility(View.GONE);
        report("Looking for the new app...", 0);
        File apk = InstallProvider.apk(getCacheDir());
        File zip = new File(apk.getParentFile(), "download.zip");

        new Thread(() -> {
            try {
                apk.getParentFile().mkdirs();
                String address = latestGameZip();
                HttpURLConnection connection = open(address);

                try {
                    int code = connection.getResponseCode();
                    if (code != 200)
                        throw new IOException("the server answered " + code);
                    long total = connection.getContentLengthLong(), received = 0, reported = 0;
                    byte[] buffer = new byte[65536];

                    try (InputStream in = connection.getInputStream(); OutputStream out = new FileOutputStream(zip)) {
                        int count;

                        while ((count = in.read(buffer)) > 0) {
                            received += count;
                            if (received > MAXIMUM_SIZE)
                                throw new IOException("the download is larger than expected");
                            out.write(buffer, 0, count);
                            if (received - reported >= 256 * 1024 || received == total) {
                                report("Downloading the new app... (" + (received >> 20) + " of " + (total >> 20)
                                    + " MB)", total > 0 ? (int) (received * 1000 / total) : 0);
                                reported = received;
                            }
                        }
                    }
                    if (total > 0 && received != total)
                        throw new IOException("the download broke off");
                } finally {
                    connection.disconnect();
                }
                extractApk(zip, apk);
                zip.delete();
                checkApk(apk);
                handler.post(() -> {
                    downloading = false;
                    downloaded = true;
                    status.setText("Downloaded: install the new app.");
                    retry.setVisibility(View.VISIBLE);
                    install();
                });
            } catch (Exception e) {
                zip.delete();
                fail("The new app could not be downloaded: " + e.getMessage());
            }
        }, "bridge download").start();
    }

    /** the zip's app (its one .apk) to apk */
    private static void extractApk(File zip, File apk) throws IOException {
        try (ZipInputStream in = new ZipInputStream(new FileInputStream(zip))) {
            ZipEntry entry;

            while ((entry = in.getNextEntry()) != null) {
                if (entry.isDirectory() || !entry.getName().endsWith(".apk"))
                    continue;
                try (OutputStream out = new FileOutputStream(apk)) {
                    byte[] buffer = new byte[65536];
                    long written = 0;
                    int count;

                    while ((count = in.read(buffer)) > 0) {
                        written += count;
                        if (written > MAXIMUM_SIZE)
                            throw new IOException("the app in the download is larger than expected");
                        out.write(buffer, 0, count);
                    }
                }
                return;
            }
        }
        throw new IOException("the download has no app in it");
    }

    private static Signature[] signers(SigningInfo info) {
        if (info == null)
            return null;
        return info.hasMultipleSigners() ? info.getApkContentsSigners() : info.getSigningCertificateHistory();
    }

    /** the download is the game, signed as this app is: anything else could never read this app's files */
    private void checkApk(File apk) throws IOException {
        PackageManager manager = getPackageManager();
        PackageInfo archive = manager.getPackageArchiveInfo(apk.getPath(), PackageManager.GET_SIGNING_CERTIFICATES);

        if (archive == null)
            throw new IOException("the download is not an app");
        if (!BuildConfig.NEW_PACKAGE.equals(archive.packageName))
            throw new IOException("the download is " + archive.packageName + ", not " + BuildConfig.NEW_PACKAGE);
        Signature[] theirs = signers(archive.signingInfo);
        if (theirs == null)
            return;  // (the package installer has the last word)
        try {
            Signature[] ours = signers(manager.getPackageInfo(getPackageName(),
                PackageManager.GET_SIGNING_CERTIFICATES).signingInfo);

            if (ours == null)
                return;
            for (Signature signature : ours)
                if (Arrays.asList(theirs).contains(signature))
                    return;
            throw new IOException("the download is not signed as this app is");
        } catch (PackageManager.NameNotFoundException e) {
            // (cannot happen: this app)
        }
    }

    /** Android's package installer, with the game: it asks the player to allow this app to install apps the first
     * time (the old app was allowed already: the bridge is the same app) */
    private void install() {
        Intent intent = new Intent(Intent.ACTION_VIEW);

        intent.setDataAndType(Uri.parse("content://" + InstallProvider.AUTHORITY + "/" + InstallProvider.APK),
            "application/vnd.android.package-archive");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            startActivity(intent);
        } catch (RuntimeException e) {
            fail("The package installer could not be opened: " + e.getMessage());
        }
    }
}
