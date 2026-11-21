package com.halo.bridge;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.Binder;
import android.os.ParcelFileDescriptor;
import android.os.Process;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.nio.file.FileVisitResult;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.SimpleFileVisitor;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.List;

/**
 * The old app's files, read-only, for the game under its new id (port/android/app/.../Migration.java, which has the
 * protocol), and for no app signed with another key: each call checks its caller's.
 *
 *     content://com.halo.decomp.migrate/list                  path, size, modified: one row per file
 *     content://com.halo.decomp.migrate/file/<path segments>  the file
 *
 * A path is "files/<path>", in the old data folder (/sdcard/Android/data/com.halo.decomp/files), or
 * "prefs/<name>", in its shared_prefs (the touch controls' layout). Symbolic links are not followed.
 */
public class MigrationProvider extends ContentProvider {
    private static final String[] COLUMNS = { "path", "size", "modified" };

    @Override
    public boolean onCreate() {
        return true;
    }

    /** the caller is signed with this app's key, or it gets nothing */
    private void checkCaller() {
        int caller = Binder.getCallingUid();

        if (caller == Process.myUid())
            return;
        if (getContext().getPackageManager().checkSignatures(caller, Process.myUid()) != PackageManager.SIGNATURE_MATCH)
            throw new SecurityException("only an app signed as this one may read its files");
    }

    private File filesRoot() {
        return getContext().getExternalFilesDir(null);
    }

    private File prefsRoot() {
        return new File(getContext().getDataDir(), "shared_prefs");
    }

    private static void walk(File root, String prefix, MatrixCursor cursor) throws IOException {
        if (root == null || !root.isDirectory())
            return;
        Path base = root.toPath();

        // (no FOLLOW_LINKS: a link is visited as a file of its own, and left out)
        Files.walkFileTree(base, new SimpleFileVisitor<Path>() {
            @Override
            public FileVisitResult visitFile(Path file, BasicFileAttributes attributes) {
                if (attributes.isRegularFile())
                    cursor.addRow(new Object[] { prefix + base.relativize(file).toString().replace(File.separatorChar, '/'),
                        attributes.size(), attributes.lastModifiedTime().toMillis() });
                return FileVisitResult.CONTINUE;
            }

            @Override
            public FileVisitResult visitFileFailed(Path file, IOException exception) {
                return FileVisitResult.CONTINUE;
            }
        });
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs, String sortOrder) {
        checkCaller();
        List<String> segments = uri.getPathSegments();

        if (segments.size() != 1 || !"list".equals(segments.get(0)))
            return null;
        MatrixCursor cursor = new MatrixCursor(COLUMNS);
        try {
            walk(filesRoot(), "files/", cursor);
            walk(prefsRoot(), "prefs/", cursor);
        } catch (IOException e) {
            throw new IllegalStateException("the files could not be listed: " + e.getMessage(), e);
        }
        return cursor;
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        checkCaller();
        List<String> segments = uri.getPathSegments();

        if (!"r".equals(mode) || segments.size() < 3 || !"file".equals(segments.get(0)))
            throw new FileNotFoundException(uri.toString());
        File base;
        switch (segments.get(1)) {
            case "files":
                base = filesRoot();
                break;
            case "prefs":
                base = prefsRoot();
                break;
            default:
                throw new FileNotFoundException(uri.toString());
        }
        if (base == null)
            throw new FileNotFoundException(uri.toString());
        File file = base;
        for (String segment : segments.subList(2, segments.size())) {
            if (segment.isEmpty() || segment.equals(".") || segment.equals("..") || segment.contains("/"))
                throw new FileNotFoundException(uri.toString());
            file = new File(file, segment);
        }
        try {
            if (Files.isSymbolicLink(file.toPath())
                || !file.getCanonicalPath().startsWith(base.getCanonicalPath() + File.separator))
                throw new FileNotFoundException(uri.toString());
        } catch (IOException e) {
            throw new FileNotFoundException(uri.toString());
        }
        return ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public String getType(Uri uri) {
        return null;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        return null;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        return 0;
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        return 0;
    }
}
