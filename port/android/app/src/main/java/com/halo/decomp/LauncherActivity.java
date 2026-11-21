package com.halo.decomp;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.provider.Settings;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.nio.channels.FileChannel;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * Starts the game once its data is in place.
 *
 * The game reads the Xbox game data (the folder holding maps/) from the
 * app's external files directory, /sdcard/Android/data/com.halo.decomp/files.
 * If it is missing, this screen lets the player pick an Xbox disc image of
 * the game (.xiso or .iso, any version) with the system file picker, and
 * copies its maps folder there (XisoExtractor), as the desktop games do; or
 * they can push the maps folder with adb.
 *
 * Before the game starts, on an Adreno whose data folder lacks it, the
 * open-source Vulkan driver for its series is downloaded
 * (Updater.driverDownload), its progress on screen, so that the game finds
 * it when the player chooses it (Video Setup's VULKAN DRIVER lists it by its name). A failed download does not stop the game,
 * and "Skip" starts it at once.
 *
 * With debug.profile_run in config.toml, each start makes the run's profile
 * folder, Download/halo_profiles/DDMMYYYY_HHMMSS (on Android 9 and 10 once
 * storage is allowed, which is asked the first time), with the build and the
 * phone in run.txt and the settings in config.toml, and names it in
 * profile_folder.txt for the game (port/android/host/host_main.c), which
 * writes its log and profiles there.
 *
 * First of all, with the old app (com.halo.decomp, this app's id up to build
 * 141) on the phone, its files are moved here (Migration), and the player is
 * offered its uninstall.
 */
public class LauncherActivity extends Activity {
    private static final int PICK_IMAGE = 1;
    private static final int PROFILE_STORAGE = 2;
    private static final int UNINSTALL_OLD = 3;

    private File dataRoot;
    private TextView status;
    private ProgressBar progress;
    private Button pick;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private boolean proceeded;
    /** waiting for the player to update the old app to the bridge (Migration.State.OLD_VERSION) */
    private boolean waitingForBridge;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        dataRoot = getExternalFilesDir(null);
        // created by the app, so that files pushed into it with adb stay
        // readable (a directory adb creates there belongs to the shell user)
        if (dataRoot != null) {
            new File(dataRoot, "maps").mkdirs();
            new File(dataRoot, "custom_maps").mkdirs();
            recoverLooseMaps();
        }
        if (offerMove())
            return;
        proceed();
    }

    /** what the launcher does once the old app's files are moved (or there were none) */
    private void proceed() {
        if (proceeded)
            return;
        proceeded = true;
        passOnHardwareId();
        passOnInvite(getIntent());
        Updater.moveToVulkanDefault(dataRoot);
        if (haveData()) {
            startGameAfterDriver();
            return;
        }
        buildInterface();
    }

    /**
     * An internet play invite link the app was opened with: the game
     * (port/linux/src/p2p.c) picks it up from join_link.txt, whether it is
     * starting now or already running.
     */
    private void passOnInvite(Intent intent) {
        if (intent == null || !Intent.ACTION_VIEW.equals(intent.getAction()) || intent.getData() == null
            || dataRoot == null)
            return;
        // written whole under another name, then renamed: the game never
        // reads it half written
        File partial = new File(dataRoot, "join_link.txt.tmp");
        try (OutputStream out = new FileOutputStream(partial)) {
            out.write(intent.getData().toString().getBytes("UTF-8"));
        } catch (java.io.IOException e) {
            // the link is lost; the player can copy it instead
            partial.delete();
            return;
        }
        if (!partial.renameTo(new File(dataRoot, "join_link.txt")))
            partial.delete();
    }

    /**
     * This device's ANDROID_ID (the app's own: one per app signing key and
     * user, until a factory reset), which native code cannot read: the game
     * (port/linux/src/p2p.c) hashes it from hardware_id.txt into the
     * hardware id a host it joins is told.
     */
    private void passOnHardwareId() {
        String id;

        if (dataRoot == null)
            return;
        try {
            id = Settings.Secure.getString(getContentResolver(), Settings.Secure.ANDROID_ID);
        } catch (RuntimeException e) {
            return;
        }
        if (id == null || id.isEmpty())
            return;
        File partial = new File(dataRoot, "hardware_id.txt.tmp");
        try (OutputStream out = new FileOutputStream(partial)) {
            out.write(id.getBytes("UTF-8"));
        } catch (java.io.IOException e) {
            partial.delete();
            return;
        }
        if (!partial.renameTo(new File(dataRoot, "hardware_id.txt")))
            partial.delete();
    }

    private boolean haveData() {
        return dataRoot != null && new File(mapsFolder(), "ui.map").isFile();
    }

    /**
     * The folder the game reads its maps from (port/android/host/host_main.c): config.toml's game.maps.path (an
     * absolute path, or one under the data folder), else the data folder's maps. The disc image is extracted into it.
     */
    private File mapsFolder() {
        File config = new File(dataRoot, "config.toml");
        String value = config.isFile() ? Updater.readSetting(config, "game", "maps.path") : null;
        String path = value != null ? unquote(value) : "";

        if (path.isEmpty())
            return new File(dataRoot, "maps");
        return path.startsWith("/") ? new File(path) : new File(dataRoot, path);
    }

    /** a TOML string's text ("..." or '...'), empty for anything else */
    private static String unquote(String value) {
        if (value.startsWith("'")) {
            int end = value.indexOf('\'', 1);
            return end > 0 ? value.substring(1, end) : "";
        }
        if (!value.startsWith("\""))
            return "";
        StringBuilder text = new StringBuilder();
        for (int index = 1; index < value.length(); index++) {
            char c = value.charAt(index);
            if (c == '"')
                break;
            if (c == '\\' && index + 1 < value.length())
                c = value.charAt(++index);
            text.append(c);
        }
        return text.toString();
    }

    /**
     * Builds from 2026-11-15 on extracted the disc image's maps into the data folder itself (files/ui.map, ...)
     * instead of its maps folder, and then said maps/ui.map was missing: those files are moved where they belong,
     * so the players they failed for need not extract again, and the folder that copy went through beside the data
     * folder (files.partial) is removed.
     */
    private void recoverLooseMaps() {
        File maps = mapsFolder();
        File[] loose = dataRoot.listFiles();

        if (new File(maps, "ui.map").isFile() || !new File(dataRoot, "ui.map").isFile() || loose == null)
            return;
        if (!maps.isDirectory() && !maps.mkdirs())
            return;
        File userInterface = null;
        for (File file : loose) {
            String name = file.getName().toLowerCase(Locale.ROOT);

            if (!file.isFile() || !(isStockMap(name) || name.equals("loading.tga")))
                continue;
            if (name.equals("ui.map")) {
                userInterface = file;
                continue;
            }
            File target = new File(maps, file.getName());
            target.delete();
            file.renameTo(target);
        }
        // (ui.map last: it is the sign that the maps are there)
        if (userInterface != null) {
            File target = new File(maps, userInterface.getName());
            target.delete();
            userInterface.renameTo(target);
        }
        File partial = new File(dataRoot.getParentFile(), dataRoot.getName() + ".partial");
        File[] leftovers = partial.listFiles();
        if (leftovers != null) {
            for (File leftover : leftovers)
                leftover.delete();
        }
        partial.delete();
    }

    /** the maps of every Xbox disc's maps folder (lower case, with .map) */
    static boolean isStockMap(String name) {
        switch (name) {
            case "a10.map": case "a30.map": case "a50.map": case "b30.map": case "b40.map": case "c10.map":
            case "c20.map": case "c40.map": case "d20.map": case "d40.map": case "beavercreek.map":
            case "bloodgulch.map": case "boardingaction.map": case "carousel.map": case "chillout.map":
            case "damnation.map": case "hangemhigh.map": case "longest.map": case "prisoner.map": case "putput.map":
            case "ratrace.map": case "sidewinder.map": case "wizard.map": case "ui.map":
                return true;
            default:
                return false;
        }
    }

    private boolean gameStarted;
    private boolean profileStorageAsked;

    private void startGame() {
        if (gameStarted)
            return;
        if (!profileFolderReady())
            return;
        gameStarted = true;
        startActivity(new Intent(this, HaloActivity.class));
        finish();
    }

    /* ---------- a profiled run's folder (debug.profile_run) */

    /**
     * The run's profile folder made and named for the game, when the settings ask for one; false while storage is
     * being asked for (Android 9 and 10: the answer starts the game, whatever it is).
     */
    private boolean profileFolderReady() {
        File config = dataRoot != null ? new File(dataRoot, "config.toml") : null;
        File named = dataRoot != null ? new File(dataRoot, "profile_folder.txt") : null;
        String value = config != null && config.isFile() ? Updater.readSetting(config, "debug", "profile_run") : null;

        if (named == null)
            return true;
        named.delete();
        if (value == null || !value.startsWith("true"))
            return true;
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R
            && checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED
            && !profileStorageAsked) {
            profileStorageAsked = true;
            requestPermissions(new String[] { Manifest.permission.WRITE_EXTERNAL_STORAGE }, PROFILE_STORAGE);
            return false;
        }
        String stamp = new SimpleDateFormat("ddMMyyyy_HHmmss", Locale.US).format(new Date());
        File folder = new File(new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS),
            "halo_profiles"), stamp);
        // (not made: the game makes one in the data folder, and its log says so)
        if (!folder.mkdirs() && !folder.isDirectory())
            return true;
        String text = "Halo for Android, build " + BuildConfig.HALO_BUILD_NUMBER + " (" + BuildConfig.VERSION_NAME + ")\n"
            + "started " + stamp + "\n"
            + "phone: " + Build.MANUFACTURER + " " + Build.MODEL + " (" + Build.DEVICE + ")\n"
            + "android: " + Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + ")\n"
            + (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S ? "soc: " + Build.SOC_MANUFACTURER + " " + Build.SOC_MODEL + "\n" : "")
            + "abis: " + String.join(", ", Build.SUPPORTED_ABIS) + "\n";
        try (OutputStream out = new FileOutputStream(new File(folder, "run.txt"))) {
            out.write(text.getBytes("UTF-8"));
        } catch (java.io.IOException e) {
            return true;
        }
        copyFile(config, new File(folder, "config.toml"));
        try (OutputStream out = new FileOutputStream(named)) {
            out.write(folder.getAbsolutePath().getBytes("UTF-8"));
        } catch (java.io.IOException e) {
            named.delete();
        }
        return true;
    }

    private static void copyFile(File from, File to) {
        try (FileInputStream in = new FileInputStream(from); FileOutputStream out = new FileOutputStream(to)) {
            byte[] buffer = new byte[65536];
            int got;

            while ((got = in.read(buffer)) > 0)
                out.write(buffer, 0, got);
        } catch (java.io.IOException e) {
            // (the settings are in the game's log too)
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(requestCode, permissions, results);
        if (requestCode == PROFILE_STORAGE)
            startGame();
    }

    /* ---------- the Vulkan driver, before the game */

    private boolean driverStarted;

    /** the game started once this phone's driver is downloaded, when it has one and lacks it */
    private void startGameAfterDriver() {
        if (driverStarted)
            return;
        driverStarted = true;
        if (Updater.driverReady(dataRoot)) {
            startGame();
            return;
        }
        new Thread(() -> {
            Updater.Choice choice = Updater.driverChoose(dataRoot);

            if (choice == null || choice.driver == null || Updater.driverPresent(dataRoot, choice.driver)) {
                handler.post(this::startGame);
                return;
            }
            handler.post(() -> buildDriverInterface(choice));
            try {
                Updater.driverDownload(dataRoot, choice.driver, choice.gpu, (received, total) -> report(
                    "Downloading the driver... (" + (received >> 10) + " of " + (total >> 10) + " KB)",
                    total > 0 ? (int) (received * 1000 / total) : 0));
                handler.post(this::startGame);
            } catch (Exception exception) {
                report("The driver could not be downloaded: " + exception.getMessage()
                    + "\nStarting the game. The download is tried again at the next start.", -1);
                handler.postDelayed(this::startGame, 3000);
            }
        }, "driver download").start();
    }

    private void buildDriverInterface(Updater.Choice choice) {
        if (gameStarted)
            return;
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        layout.setPadding(dp(48), dp(24), dp(48), dp(24));
        layout.setBackgroundColor(Color.rgb(12, 16, 20));

        TextView title = new TextView(this);
        title.setText("Downloading the Vulkan driver");
        title.setTextColor(Color.WHITE);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 24);
        title.setGravity(Gravity.CENTER);
        layout.addView(title);

        TextView message = new TextView(this);
        message.setText("This phone's GPU (" + choice.gpu + ") can run the game's Vulkan renderer on Turnip, the "
            + "open-source Vulkan driver for Adreno GPUs. It is downloaded once into the freedreno_drivers folder of "
            + "the game's data folder (about " + ((choice.driver.size + (1 << 19)) >> 20) + " MB), and is not used "
            + "until you choose it: in the game's Video Setup, VULKAN DRIVER lists it as "
            + choice.driver.name.replaceFirst("\\.zip$", "") + ". Other driver archives put in that folder, or "
            + "added with IMPORT DRIVER, are listed there too.");
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
        status.setText("Connecting...");
        status.setTextColor(Color.rgb(160, 200, 160));
        status.setGravity(Gravity.CENTER);
        status.setPadding(0, dp(8), 0, dp(16));
        layout.addView(status);

        // (the download goes on; the game starting first uses the phone's driver for this run)
        Button skip = new Button(this);
        skip.setText("Skip");
        skip.setOnClickListener(v -> startGame());
        layout.addView(skip, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT,
            LinearLayout.LayoutParams.WRAP_CONTENT));

        setContentView(layout);
        skip.requestFocus();
    }

    private int dp(float value) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value,
            getResources().getDisplayMetrics());
    }

    private void buildInterface() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        layout.setPadding(dp(48), dp(24), dp(48), dp(24));
        layout.setBackgroundColor(Color.rgb(12, 16, 20));

        TextView title = new TextView(this);
        title.setText("Halo needs its game data");
        title.setTextColor(Color.WHITE);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 24);
        title.setGravity(Gravity.CENTER);
        layout.addView(title);

        TextView message = new TextView(this);
        message.setText("Choose an Xbox disc image of Halo: Combat Evolved (an .iso or .xiso file, any "
            + "version) on this device. Its maps folder is copied into the app's storage (about 1.8 GB), "
            + "and you can delete the image afterwards.\n\n"
            + "You can also copy a maps folder from a computer:\n"
            + "adb push <folder with maps>/. " + (dataRoot != null ? dataRoot.getAbsolutePath() : "") + "/");
        message.setTextColor(Color.rgb(200, 205, 210));
        message.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        message.setGravity(Gravity.CENTER);
        message.setPadding(0, dp(16), 0, dp(16));
        layout.addView(message);

        pick = new Button(this);
        Button mccImport = new Button(this);
        mccImport.setText("Import MCC maps");
        mccImport.setOnClickListener(v -> startActivity(new Intent(this, MccImportActivity.class)));
        layout.addView(mccImport);
        pick.setText("Choose disc image");
        pick.setOnClickListener(v -> {
            // (disc images have no MIME type of their own: any file, checked
            // when it is read)
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            startActivityForResult(intent, PICK_IMAGE);
        });
        layout.addView(pick, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT,
            LinearLayout.LayoutParams.WRAP_CONTENT));

        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(1000);
        progress.setVisibility(View.GONE);
        LinearLayout.LayoutParams progressLayout = new LinearLayout.LayoutParams(dp(480),
            LinearLayout.LayoutParams.WRAP_CONTENT);
        progressLayout.topMargin = dp(16);
        layout.addView(progress, progressLayout);

        status = new TextView(this);
        status.setTextColor(Color.rgb(160, 200, 160));
        status.setGravity(Gravity.CENTER);
        status.setPadding(0, dp(8), 0, 0);
        layout.addView(status);

        setContentView(layout);
        pick.requestFocus();
    }

    /* ---------- the old app's files (Migration) */

    /** true when the old app's files are being offered or moved: proceed() follows */
    private boolean offerMove() {
        Migration.State state = Migration.state(this, dataRoot);

        if (state == Migration.State.OLD_VERSION) {
            showOldVersion();
            return true;
        }
        if (state != Migration.State.READY)
            return false;
        if (!Migration.hasOwnData(dataRoot)) {
            startMove(true);
            return true;
        }
        new AlertDialog.Builder(this)
            .setTitle("The old Halo app's files")
            .setMessage("The old Halo app is on this phone too. Bring its settings, saves, profiles, mods and maps "
                + "over to this one? Where both have the same file, this app's is replaced.")
            .setCancelable(false)
            .setPositiveButton("Bring them over", (dialog, which) -> startMove(true))
            .setNegativeButton("No", (dialog, which) -> {
                Migration.markDone(dataRoot, "declined");
                proceed();
            })
            .show();
        return true;
    }

    /** a screen of the move: a title, a message and a row of buttons, which the caller fills */
    private LinearLayout buildMoveInterface(String titleText, String messageText, boolean withProgress) {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        layout.setPadding(dp(48), dp(24), dp(48), dp(24));
        layout.setBackgroundColor(Color.rgb(12, 16, 20));

        TextView title = new TextView(this);
        title.setText(titleText);
        title.setTextColor(Color.WHITE);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 24);
        title.setGravity(Gravity.CENTER);
        layout.addView(title);

        TextView message = new TextView(this);
        message.setText(messageText);
        message.setTextColor(Color.rgb(200, 205, 210));
        message.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        message.setGravity(Gravity.CENTER);
        message.setPadding(0, dp(16), 0, dp(16));
        layout.addView(message);

        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(1000);
        progress.setVisibility(withProgress ? View.VISIBLE : View.GONE);
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

        setContentView(layout);
        return buttons;
    }

    private Button addButton(LinearLayout buttons, String text, View.OnClickListener listener) {
        Button button = new Button(this);
        button.setText(text);
        button.setOnClickListener(listener);
        buttons.addView(button, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT,
            LinearLayout.LayoutParams.WRAP_CONTENT));
        return button;
    }

    /** the old app is older than the bridge: it updates itself once opened, then sends the player back */
    private void showOldVersion() {
        waitingForBridge = true;
        LinearLayout buttons = buildMoveInterface("Bring over your files from the old app",
            "The old Halo app is on this phone. To bring its settings, saves, profiles, mods and maps over to this "
                + "one, open it once and say Yes to the update it offers: it updates itself, then sends you back "
                + "here.\n\n(If it offers no update, its updates are turned off: set auto = true in the [update] "
                + "section of its config.toml.)", false);
        Button open = addButton(buttons, "Open the old app", v -> {
            Intent intent = getPackageManager().getLaunchIntentForPackage(Migration.LEGACY_PACKAGE);

            if (intent != null)
                startActivity(intent);
        });
        addButton(buttons, "Start without them", v -> {
            waitingForBridge = false;
            Migration.markDone(dataRoot, "skipped");
            proceed();
        });
        open.requestFocus();
    }

    private void startMove(boolean withGameData) {
        buildMoveInterface("Moving your files", "Your settings, saves, profiles, mods and maps are being brought "
            + "over from the old Halo app.", true);
        status.setText("Looking at the old app's files...");
        new Thread(() -> {
            try {
                Migration.Listing listing = Migration.list(this, dataRoot);
                long free = dataRoot.getUsableSpace();

                if (free < Migration.needed(listing, withGameData)) {
                    handler.post(() -> showNoSpace(listing, free, withGameData));
                    return;
                }
                Migration.copy(this, dataRoot, listing, withGameData, (done, total) -> report(
                    "Moving files... (" + (done >> 20) + " of " + (total >> 20) + " MB)",
                    total > 0 ? (int) (done * 1000 / total) : 0));
                handler.post(this::moved);
            } catch (Exception exception) {
                android.util.Log.i("halo", "move: failed: " + exception);
                handler.post(() -> showMoveFailed(exception.getMessage(), withGameData));
            }
        }, "move from the old app").start();
    }

    private static String gigabytes(long bytes) {
        return String.format(Locale.ROOT, "%.1f GB", bytes / (1024.0 * 1024 * 1024));
    }

    private void showNoSpace(Migration.Listing listing, long free, boolean withGameData) {
        boolean fitsWithout = withGameData && free >= Migration.needed(listing, false);
        String text = "Moving the old app's files needs about " + gigabytes(Migration.needed(listing, withGameData))
            + ", and " + gigabytes(free) + " are free.";

        if (fitsWithout)
            text += "\n\nWithout the game data (its maps folder, " + gigabytes(listing.gameDataBytes) + "), which you "
                + "then extract again from your disc image, it fits.";
        else
            text += "\n\nFree some space, then try again.";
        LinearLayout buttons = buildMoveInterface("Not enough space", text, false);
        Button first;

        if (fitsWithout)
            first = addButton(buttons, "Move without the game data", v -> startMove(false));
        else
            first = addButton(buttons, "Try again", v -> startMove(withGameData));
        addButton(buttons, "Start without them", v -> proceed());
        first.requestFocus();
    }

    private void showMoveFailed(String why, boolean withGameData) {
        LinearLayout buttons = buildMoveInterface("The move failed",
            "The old app's files could not be brought over:\n\n" + why + "\n\nThe old app keeps them.", false);
        Button again = addButton(buttons, "Try again", v -> startMove(withGameData));
        addButton(buttons, "Start without them", v -> proceed());
        again.requestFocus();
    }

    private void moved() {
        Migration.markDone(dataRoot, "moved");
        new AlertDialog.Builder(this)
            .setTitle("Your files are here")
            .setMessage("The old Halo app's settings, saves, profiles, mods and maps are in this app now. The old "
                + "app is not needed any more: uninstall it?")
            .setCancelable(false)
            .setPositiveButton("Uninstall it", (dialog, which) -> {
                try {
                    Intent intent = new Intent(Intent.ACTION_DELETE, Uri.parse("package:" + Migration.LEGACY_PACKAGE));
                    intent.putExtra(Intent.EXTRA_RETURN_RESULT, true);
                    startActivityForResult(intent, UNINSTALL_OLD);
                } catch (RuntimeException e) {
                    proceed();
                }
            })
            .setNegativeButton("Keep it", (dialog, which) -> proceed())
            .show();
    }

    @Override
    protected void onResume() {
        super.onResume();
        // the old app updated to the bridge while the player was away (showOldVersion)
        if (waitingForBridge && !proceeded && Migration.state(this, dataRoot) == Migration.State.READY) {
            waitingForBridge = false;
            offerMove();
            return;
        }
        // data pushed with adb while this screen was open
        if (pick != null && pick.isEnabled() && haveData())
            startGameAfterDriver();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == UNINSTALL_OLD) {
            proceed();
            return;
        }
        if (requestCode != PICK_IMAGE || resultCode != RESULT_OK || data == null || data.getData() == null)
            return;
        Uri image = data.getData();
        pick.setEnabled(false);
        progress.setVisibility(View.VISIBLE);
        status.setText("Reading the disc image...");
        new Thread(() -> importImage(image)).start();
    }

    private void report(String text, int permille) {
        handler.post(() -> {
            if (status != null)
                status.setText(text);
            if (permille >= 0 && progress != null)
                progress.setProgress(permille);
        });
    }

    private void fail(String text) {
        handler.post(() -> {
            status.setText(text);
            progress.setVisibility(View.GONE);
            pick.setEnabled(true);
            pick.requestFocus();
        });
    }

    private void importImage(Uri image) {
        try (ParcelFileDescriptor descriptor = getContentResolver().openFileDescriptor(image, "r")) {
            if (descriptor == null)
                throw new java.io.IOException("the file could not be opened");
            try (FileInputStream in = new FileInputStream(descriptor.getFileDescriptor())) {
                FileChannel channel = in.getChannel();

                XisoExtractor.extractMaps(channel, mapsFolder(), (file, done, total) ->
                    report("Extracting maps/" + file + " (" + (done >> 20) + " of " + (total >> 20) + " MB)",
                        total > 0 ? (int) (done * 1000 / total) : 0));
            }
            handler.post(() -> {
                if (haveData()) {
                    startGameAfterDriver();
                } else {
                    fail("The extraction finished but " + new File(mapsFolder(), "ui.map").getPath() + " is missing.");
                }
            });
        } catch (XisoExtractor.ExtractException exception) {
            fail(exception.getMessage());
        } catch (Exception exception) {
            fail("Extracting failed: " + exception.getMessage());
        }
    }
}
