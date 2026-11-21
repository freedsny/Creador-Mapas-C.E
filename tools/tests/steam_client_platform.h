/* What the Steam client (port/linux/src/steam_*.c) takes of platform.h, for tools/test_steam_client.py's native
 * build: given with -include, its guard defined so that platform.h itself (and the SDK headers it reads) is passed
 * over. tools/tests/steam_client_driver.c defines them. */
#ifndef STEAM_CLIENT_PLATFORM_H
#define STEAM_CLIENT_PLATFORM_H
#define __HALO_LINUX_PLATFORM_H
/* (the SDK's Winsock names, as the host's: the sockets are the driver's stubs) */
#include <netinet/in.h>
#include <sys/socket.h>
void platform_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
const char *platform_data_root(void);
#endif
