/*
HOST_MEMORY_POSIX_MACOS.C

The five posix.h helpers the guest imports for its map cache files
(tools/android_imports.py: host_memory_posix_*), which Android's, the
PS5's and the Switch's hosts answer from cache files kept in their memory
(port/android/host/host_memory_files.c). The macOS host keeps its cache
files on the storage, as the desktop builds do: each is the helper itself.
*/

#include "posix.h"

int host_memory_posix_stat(const char *path, struct posix_file_information *information)
{
	return posix_stat(path, information);
}

int host_memory_posix_fstat(int descriptor, struct posix_file_information *information)
{
	return posix_fstat(descriptor, information);
}

int host_memory_posix_set_file_times(const char *path, posix_ulong access_seconds,
	posix_ulong access_nanoseconds, posix_ulong modification_seconds, posix_ulong modification_nanoseconds)
{
	return posix_set_file_times(path, access_seconds, access_nanoseconds, modification_seconds,
		modification_nanoseconds);
}

int host_memory_posix_seek(int descriptor, posix_long offset_low, posix_long offset_high, int whence,
	posix_ulong *position_low, posix_ulong *position_high)
{
	return posix_seek(descriptor, offset_low, offset_high, whence, position_low, position_high);
}

int host_memory_posix_truncate(int descriptor, posix_ulong size_low, posix_ulong size_high)
{
	return posix_truncate(descriptor, size_low, size_high);
}
