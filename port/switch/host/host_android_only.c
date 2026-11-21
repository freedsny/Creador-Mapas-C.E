/*
HOST_ANDROID_ONLY.C

The Android app's host imports the Switch's import table takes with the rest
of port/android/host_imports.list, which the Switch's guest never calls
(steam_downloads.c asks for the Keystore's seal only on Android): each
refuses, as a host without them would. (The MCC fingerprints are the
Switch host's own too: tools/switch_build.py builds host_mcc_hash.c.)
*/

#include <stdint.h>

/* the Steam sign-in's seal, the Android Keystore's (port/android/host/host_steam_seal.c) */
int host_steam_private_folder(char *folder, int size)
{
	(void)folder; (void)size;
	return 0;
}

int host_steam_seal(const char *text, char *out, int out_size)
{
	(void)text;
	if (out && out_size > 0)
		out[0] = 0;
	return 0;
}

int host_steam_unseal(const char *sealed, char *out, int out_size)
{
	(void)sealed;
	if (out && out_size > 0)
		out[0] = 0;
	return 0;
}

void host_steam_seal_forget(void)
{
}
