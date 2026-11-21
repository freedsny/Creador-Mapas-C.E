/* The Steam client's offline parts (port/linux/src/steam_*.c), run natively for tools/test_steam_client.py: its
 * crypto against known answers, the session file (desktop's, its owner's alone; and, built with HALO_ANDROID, the
 * Android app's, its token sealed by a stand-in for the Keystore that takes STEAM_SEAL_TEST_SECRET), the depots'
 * chunk packings, a content server's chunked answers, manifests' bounds, a staged folder's removal past links, and
 * where a Workshop item's files may go. Nothing here reaches Steam: the sockets are stubs that fail.
 * steam_downloads.c is included, for its file's static calls. */
#include "../../port/linux/src/steam_downloads.c"

#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <time.h>

/* ---------- what the game would give it */

static char test_root[1024] = ".";

const char *platform_data_root(void)
{
	return test_root;
}

void platform_log(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	fputc('\n', stderr);
}

unsigned long system_milliseconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (unsigned long)now.tv_sec * 1000ul + (unsigned long)(now.tv_nsec / 1000000);
}

int community_net_address(char *server, int size)
{
	(void)server; (void)size;
	return 0;
}

int update_https_request(const char *method, const char *url, const char *headers, const void *body,
	unsigned long body_size, unsigned long maximum, struct update_response *response, char *error, int error_size)
{
	(void)method; (void)url; (void)headers; (void)body; (void)body_size; (void)maximum; (void)response;
	snprintf(error, (size_t)error_size, "offline");
	return 0;
}

int update_request(const char *method, const char *url, const char *headers, const char *body, char *response,
	int response_size, char *error, int error_size)
{
	(void)method; (void)url; (void)headers; (void)body; (void)response; (void)response_size;
	snprintf(error, (size_t)error_size, "offline");
	return 0;
}

/* (workshop_authors.c's: offline, no author is known) */
int workshop_authors_get(const char *item_id, char *name, int size)
{
	(void)item_id;
	if (size > 0)
		name[0] = 0;
	return 0;
}

#if defined(STEAM_SESSION_SEALED)
/* ---------- the Android app's host, standing in: the private folder under the root's "private", and a "seal"
 * that a secret opens (STEAM_SEAL_TEST_SECRET; none, and nothing can be sealed, as a Keystore that refuses) */

struct host_https_sizes;
int host_https_get(const char *url, char *headers, char *body, char *error, struct host_https_sizes *sizes);
int host_https_get(const char *url, char *headers, char *body, char *error, struct host_https_sizes *sizes)
{
	(void)url; (void)headers; (void)body; (void)sizes;
	snprintf(error, 64, "offline");
	return 0;
}

int host_steam_private_folder(char *folder, int size)
{
	char parent[1100];

	snprintf(parent, sizeof(parent), "%s/private", test_root);
	mkdir(parent, 0700);
	snprintf(folder, (size_t)size, "%s/steam", parent);
	mkdir(folder, 0700);
	return 1;
}

int host_steam_seal(const char *text, char *out, int out_size)
{
	char const *secret = getenv("STEAM_SEAL_TEST_SECRET");
	size_t i, used;

	out[0] = 0;
	if (!secret || !*secret)
		return 0;
	used = (size_t)snprintf(out, (size_t)out_size, "keystore:%s:", secret);
	for (i = 0; text[i] && used + 3 < (size_t)out_size; i++, used += 2)
		snprintf(out + used, 3, "%02x", (unsigned char)text[i] ^ 0x5a);
	return 1;
}

int host_steam_unseal(const char *sealed, char *out, int out_size)
{
	char const *secret = getenv("STEAM_SEAL_TEST_SECRET");
	char prefix[128];
	char const *at;
	int length = 0;

	out[0] = 0;
	if (!secret || !*secret)
		return 0;
	snprintf(prefix, sizeof(prefix), "keystore:%s:", secret);
	if (strncmp(sealed, prefix, strlen(prefix)))
		return 0;
	for (at = sealed + strlen(prefix); at[0] && at[1] && length + 1 < out_size; at += 2)
	{
		unsigned byte;

		if (sscanf(at, "%2x", &byte) != 1)
			return 0;
		out[length++] = (char)(byte ^ 0x5a);
	}
	out[length] = 0;
	return 1;
}

static int forgotten;

void host_steam_seal_forget(void)
{
	forgotten = 1;
}
#endif

/* (the sockets: none, offline; but for "http", whose CANNED_SOCKET answers
with a file's bytes) */
#define CANNED_SOCKET 7
static unsigned char *canned;
static size_t canned_size, canned_at;

int posix_socket(int family, int type, int protocol) { (void)family; (void)type; (void)protocol; return -1; }
int posix_socket_close(int socket) { (void)socket; return 0; }
int posix_socket_connect(int socket, const void *address, int length) { (void)socket; (void)address; (void)length; return -1; }
int posix_socket_send(int socket, const void *buffer, int length, int flags)
{
	(void)buffer; (void)flags;
	return canned && socket == CANNED_SOCKET ? length : -1;
}
int posix_socket_recv(int socket, void *buffer, int length, int flags)
{
	size_t count;

	(void)flags;
	if (!canned || socket != CANNED_SOCKET)
		return -1;
	count = canned_size - canned_at < (size_t)length ? canned_size - canned_at : (size_t)length;
	memcpy(buffer, canned + canned_at, count);
	canned_at += count;
	return (int)count;
}
int posix_socket_set_nonblocking(int socket, int on) { (void)socket; (void)on; return 0; }
int posix_socket_set_nodelay(int socket) { (void)socket; return 0; }
int posix_socket_getsockopt(int socket, int level, int name, void *value, int *length)
{
	(void)socket; (void)level; (void)name; (void)value; (void)length;
	return -1;
}
int posix_socket_select(int *read, int *read_count, int *write, int *write_count, int *error, int *error_count,
	posix_long seconds, posix_long microseconds, int infinite)
{
	(void)read; (void)read_count; (void)write; (void)write_count; (void)error; (void)error_count;
	(void)seconds; (void)microseconds; (void)infinite;
	return canned ? 1 : -1;
}
posix_ulong posix_resolve_ipv4(const char *host) { (void)host; return 0; }
void posix_random_bytes(void *buffer, posix_ulong size)
{
	unsigned char *bytes = buffer;
	FILE *random = fopen("/dev/urandom", "rb");

	if (!random || fread(bytes, 1, size, random) != size)
		abort();
	fclose(random);
}

/* ---------- the commands */

static void print_hex(unsigned char const *bytes, size_t size)
{
	char text[2 * 64 + 1];

	steam_hex(bytes, size, text);
	printf("%s\n", text);
}

static int crypto_answers(void)
{
	static unsigned char const key[32] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
		21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 };
	static unsigned char const block[16] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
		0xcc, 0xdd, 0xee, 0xff };
	unsigned char digest[20], out[16], back[16];
	struct steam_aes aes;
	unsigned char decoded[16];
	long count;

	steam_sha1("abc", 3, digest);
	print_hex(digest, 20);
	steam_hmac_sha1("key", 3, "The quick brown fox jumps over the lazy dog", 43, digest);
	print_hex(digest, 20);
	steam_aes_key(&aes, key);
	steam_aes_encrypt_block(&aes, block, out);
	print_hex(out, 16);
	steam_aes_decrypt_block(&aes, out, back);
	print_hex(back, 16);
	count = steam_base64_decode("aGVsbG8gd29ybGQ=", 16, decoded, sizeof(decoded));
	printf("%.*s\n", (int)count, (char const *)decoded);
	printf("%08x\n", (unsigned)steam_crc32("123456789", 9));
	return 0;
}

/* the connection's envelope: a round trip, and a byte changed refused */
static int envelope(void)
{
	static unsigned char const key[32] = { 9 };
	static unsigned char const hmac_key[16] = { 7 };
	char const *text = "a refresh token, as long as it likes to be";
	unsigned char sealed[256], plain[256];
	size_t size = strlen(text), sealed_size = steam_aes_encrypted_size(size), plain_size = 0;
	struct steam_aes aes;
	int ok;

	steam_aes_key(&aes, key);
	steam_aes_encrypt_hmac(&aes, hmac_key, (unsigned char const *)text, size, sealed);
	ok = steam_aes_decrypt_hmac(&aes, hmac_key, sealed, sealed_size, plain, &plain_size) && plain_size == size &&
		!memcmp(plain, text, size);
	printf("round trip %d\n", ok);
	sealed[sealed_size - 1] ^= 1;
	printf("tampered %d\n", steam_aes_decrypt_hmac(&aes, hmac_key, sealed, sealed_size, plain, &plain_size));
	return 0;
}

int main(int argc, char **argv)
{
	char const *command = argc > 1 ? argv[1] : "";

	if (!strcmp(command, "crypto"))
		return crypto_answers();
	if (!strcmp(command, "envelope"))
		return envelope();
	/* a manifest's unzipped bytes (a file's, names not encrypted) read:
	"ok <files> <chunks>" or "fail <error>" */
	if (!strcmp(command, "manifest") && argc == 3)
	{
		static unsigned char content[1 << 20];
		unsigned char key[32];
		struct steam_depot_manifest manifest;
		char error[256] = "";
		FILE *file = fopen(argv[2], "rb");
		size_t size = file ? fread(content, 1, sizeof(content), file) : 0;
		int i, chunks = 0;

		if (file)
			fclose(file);
		memset(key, 0, sizeof(key));
		if (!steam_depot_manifest_parse(content, size, key, &manifest, error, sizeof(error)))
		{
			printf("fail %s\n", error);
			return 0;
		}
		for (i = 0; i < manifest.file_count; i++)
			chunks += manifest.files[i].chunk_count;
		printf("ok %d %d\n", manifest.file_count, chunks);
		steam_depot_manifest_free(&manifest);
		return 0;
	}
	/* a staged folder removed, as an install's staging is */
	if (!strcmp(command, "remove_tree") && argc == 3)
	{
		remove_tree(argv[2]);
		return 0;
	}
	/* a folder moved, as a staged install is into mcc_maps: "moved 1" or "moved 0" */
	if (!strcmp(command, "move_folder") && argc == 4)
	{
		printf("moved %d\n", move_folder(argv[2], argv[3]));
		return 0;
	}
	/* a content server's answer (a file's bytes) to one request, at most
	maximum bytes of body: "ok <size> <body>" or "fail <error>" */
	if (!strcmp(command, "http") && argc == 4)
	{
		struct steam_http http;
		unsigned char *body = NULL;
		unsigned long size = 0;
		int status = 0;
		char error[256] = "";
		FILE *file = fopen(argv[2], "rb");
		long length;

		if (!file || fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET))
			return 2;
		canned = malloc((size_t)length + 1);
		if (!canned || fread(canned, 1, (size_t)length, file) != (size_t)length)
			return 2;
		fclose(file);
		canned_size = (size_t)length;
		steam_http_init(&http);
		http.socket = CANNED_SOCKET;
		snprintf(http.host, sizeof(http.host), "cache");
		if (steam_http_get(&http, "cache", "/depot/1/chunk/0", strtoul(argv[3], NULL, 10), &body, &size, &status,
			error, sizeof(error)))
			printf("ok %lu %.*s\n", size, (int)size, (char const *)body);
		else
			printf("fail %s\n", error);
		free(body);
		return 0;
	}
	if (!strcmp(command, "save") && argc == 5)
	{
		snprintf(test_root, sizeof(test_root), "%s", argv[2]);
		steam_random(steam.seed, sizeof(steam.seed));
		steam.seeded = 1;
		snprintf(steam.account, sizeof(steam.account), "%s", argv[3]);
		snprintf(steam.token, sizeof(steam.token), "%s", argv[4]);
		session_save();
		return 0;
	}
	if (!strcmp(command, "load") && argc == 3)
	{
		snprintf(test_root, sizeof(test_root), "%s", argv[2]);
		session_load();
		printf("account %s\ntoken %s\n", steam.account, steam.token);
		return 0;
	}
	if (!strcmp(command, "forget") && argc == 3)
	{
		snprintf(test_root, sizeof(test_root), "%s", argv[2]);
		session_load();
		session_forget();
#if defined(STEAM_SESSION_SEALED)
		printf("key forgotten %d\n", forgotten);
#endif
		return 0;
	}
	if (!strcmp(command, "unpack") && argc == 4)
	{
		FILE *file = fopen(argv[2], "rb");
		static unsigned char data[1 << 20];
		size_t size = file ? fread(data, 1, sizeof(data), file) : 0;
		size_t expected = (size_t)strtoul(argv[3], NULL, 10);
		unsigned char *out = malloc(expected + 1);
		int ok;

		if (file)
			fclose(file);
		ok = out && steam_unpack_chunk(data, size, out, expected);
		printf("unpacked %d\n", ok);
		if (ok)
			fwrite(out, 1, expected, stdout);
		free(out);
		return 0;
	}
	if (!strcmp(command, "path") && argc == 3)
	{
		char path[260];

		if (item_path(argv[2], path, sizeof(path)))
			printf("%s\n", path);
		else
			printf("(refused)\n");
		return 0;
	}
	fprintf(stderr, "usage: crypto | envelope | manifest FILE | remove_tree DIR | move_folder FROM TO | "
		"http FILE MAXIMUM | save ROOT ACCOUNT TOKEN | load ROOT | forget ROOT | unpack FILE SIZE | path NAME\n");
	return 2;
}
