/*
HOST_STEAM_SEAL.C

The Steam sign-in's seal on Android, for the guest's Steam client
(port/linux/src/steam_downloads.c, whose session file holds the refresh
token, as good as the account to whoever has it until it is revoked):

- the file goes in the app's private storage (SDL_GetAndroidInternalStoragePath),
  not the data folder, which a file manager, a USB cable or another app with
  storage access can read;
- the token in it is sealed with an AES-GCM key of the Android Keystore that
  never leaves it (HaloActivity.steamSeal): a copy of the file opens on no
  other phone, nor on this one once the app is uninstalled or the player has
  logged out (steamForget deletes the key).

From the Android port's Steam client (OpenCE's port/android/host/steam_seal.c).
*/

#include "host.h"

#include <SDL3/SDL.h>
#include <jni.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

enum
{
	/* the most a token sealed can be (a refresh token is some 600 bytes) */
	SEALED_MAXIMUM = 4096
};

/* (through a volatile pointer: a store the compiler may not drop as dead) */
static void wipe(void *memory, size_t size)
{
	volatile unsigned char *at = memory;

	while (size--)
		*at++ = 0;
}

static void hex_into(unsigned char const *bytes, size_t size, char *out)
{
	static char const digits[] = "0123456789abcdef";
	size_t i;

	for (i = 0; i < size; i++)
	{
		out[2 * i] = digits[bytes[i] >> 4];
		out[2 * i + 1] = digits[bytes[i] & 15];
	}
	out[2 * size] = 0;
}

static int digit(char ch)
{
	return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
}

/* hex into bytes (at most capacity): the count, or -1 */
static long hex_from(char const *text, unsigned char *out, size_t capacity)
{
	size_t length = strlen(text), i;

	if (length % 2 || length / 2 > capacity)
		return -1;
	for (i = 0; i < length / 2; i++)
	{
		int high = digit(text[2 * i]), low = digit(text[2 * i + 1]);

		if (high < 0 || low < 0)
			return -1;
		out[i] = (unsigned char)(high * 16 + low);
	}
	return (long)(length / 2);
}

/* bytes through one of HaloActivity's static byte[] (byte[]) methods, from
any thread (SDL attaches it): the answer's count into out (at most
capacity), or -1. What went in and came out is wiped from the Java heap */
static long activity_bytes(char const *name, unsigned char const *in, size_t in_size, unsigned char *out,
	size_t capacity)
{
	JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
	jobject activity = env ? (jobject)SDL_GetAndroidActivity() : NULL;
	jclass activity_class;
	jmethodID method;
	jbyteArray argument = NULL, answer = NULL;
	long count = -1;

	if (!env || !activity)
		return -1;
	activity_class = (*env)->GetObjectClass(env, activity);
	method = activity_class ? (*env)->GetStaticMethodID(env, activity_class, name, "([B)[B") : NULL;
	if (method)
		argument = (*env)->NewByteArray(env, (jsize)in_size);
	if (argument)
	{
		(*env)->SetByteArrayRegion(env, argument, 0, (jsize)in_size, (jbyte const *)in);
		answer = (jbyteArray)(*env)->CallStaticObjectMethod(env, activity_class, method, argument);
		{
			jbyte *bytes = (*env)->GetByteArrayElements(env, argument, NULL);

			if (bytes)
			{
				wipe(bytes, in_size);
				(*env)->ReleaseByteArrayElements(env, argument, bytes, 0);
			}
		}
	}
	if ((*env)->ExceptionCheck(env))
	{
		(*env)->ExceptionClear(env);
		answer = NULL;
	}
	if (answer)
	{
		jsize length = (*env)->GetArrayLength(env, answer);

		if (length >= 0 && (size_t)length <= capacity)
		{
			jbyte *bytes = (*env)->GetByteArrayElements(env, answer, NULL);

			if (bytes)
			{
				memcpy(out, bytes, (size_t)length);
				wipe(bytes, (size_t)length);
				(*env)->ReleaseByteArrayElements(env, answer, bytes, 0);
				count = length;
			}
		}
		(*env)->DeleteLocalRef(env, answer);
	}
	if (argument)
		(*env)->DeleteLocalRef(env, argument);
	if (activity_class)
		(*env)->DeleteLocalRef(env, activity_class);
	(*env)->DeleteLocalRef(env, activity);
	return count;
}

/* the folder the session file goes in (made if need be): 1, or 0 where the
app has no private storage */
int host_steam_private_folder(char *folder, int size)
{
	char const *internal = SDL_GetAndroidInternalStoragePath();

	if (!internal || !folder || size <= 0 || snprintf(folder, (size_t)size, "%s/steam", internal) >= size)
		return 0;
	mkdir(folder, 0700);
	return 1;
}

/* text sealed into out ("keystore:<hex>"): 1, or 0 (out empty) where the
Keystore cannot */
int host_steam_seal(const char *text, char *out, int out_size)
{
	static unsigned char sealed[SEALED_MAXIMUM];
	long count;

	if (!out || out_size <= 0)
		return 0;
	out[0] = 0;
	count = activity_bytes("steamSeal", (unsigned char const *)text, strlen(text), sealed, sizeof(sealed));
	if (count <= 0 || (size_t)out_size < sizeof("keystore:") + 2 * (size_t)count)
		return 0;
	snprintf(out, (size_t)out_size, "keystore:");
	hex_into(sealed, (size_t)count, out + strlen(out));
	return 1;
}

/* a sealed text opened into out: 1, or 0 (another device's, damaged, or the
key gone) */
int host_steam_unseal(const char *sealed, char *out, int out_size)
{
	static unsigned char bytes[SEALED_MAXIMUM];
	static unsigned char plain[SEALED_MAXIMUM];
	long size, count;

	if (!out || out_size <= 0)
		return 0;
	out[0] = 0;
	if (strncmp(sealed, "keystore:", 9) || (size = hex_from(sealed + 9, bytes, sizeof(bytes))) <= 0)
		return 0;
	count = activity_bytes("steamUnseal", bytes, (size_t)size, plain, sizeof(plain));
	if (count <= 0 || count >= out_size)
	{
		wipe(plain, sizeof(plain));
		return 0;
	}
	memcpy(out, plain, (size_t)count);
	out[count] = 0;
	wipe(plain, sizeof(plain));
	return 1;
}

/* the Keystore's key deleted (a log out): what it sealed opens no more */
void host_steam_seal_forget(void)
{
	unsigned char none[1];

	activity_bytes("steamForget", none, 0, none, sizeof(none));
}
