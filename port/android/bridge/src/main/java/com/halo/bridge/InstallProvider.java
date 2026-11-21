package com.halo.bridge;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileNotFoundException;

/**
 * Hands Android's package installer the downloaded game (BridgeActivity), read-only, the one file
 * content://<application id>.install/halo.apk, as the game's UpdateProvider does.
 */
public class InstallProvider extends ContentProvider {
    static final String AUTHORITY = BuildConfig.APPLICATION_ID + ".install";
    static final String DIRECTORY = "install";
    static final String APK = "halo.apk";

    static File apk(File cacheDir) {
        return new File(new File(cacheDir, DIRECTORY), APK);
    }

    private boolean isApk(Uri uri) {
        return uri != null && APK.equals(uri.getLastPathSegment());
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        if (!isApk(uri) || !"r".equals(mode))
            throw new FileNotFoundException(uri.toString());
        return ParcelFileDescriptor.open(apk(getContext().getCacheDir()), ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public String getType(Uri uri) {
        return isApk(uri) ? "application/vnd.android.package-archive" : null;
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs, String sortOrder) {
        if (!isApk(uri))
            return null;
        MatrixCursor cursor = new MatrixCursor(new String[] { OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE });
        cursor.addRow(new Object[] { APK, apk(getContext().getCacheDir()).length() });
        return cursor;
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
