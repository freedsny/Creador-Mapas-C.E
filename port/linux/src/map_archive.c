/*
MAP_ARCHIVE.C

map_archive.h's model: FractumSeraph's map archive. A thread of its own reads
the list (the kept copy, then GitHub's, each checked against its
signature) and the categories, lists custom_maps to tell what is installed,
and downloads the maps asked for one at a time, a joiner's first. The
callers' functions take the lock briefly and never wait on the network.

From SlipstreamCE's port/android/host/fractum_archive.c (thelinkin3000): the
list's format and keys, its signature's check and the download are his
code's; the categories, the installed maps by name through the Map Manager's
folders, the type filter and the joiner's fetch are this fork's.
*/

#include "map_archive.h"
#include "platform.h"
#include "port_config.h"
#include "posix.h"
#include "steam_net.h"
#include "community_net.h"

#include "monocypher.h"
#include "monocypher-ed25519.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

unsigned long system_milliseconds(void);

/* the details' pictures, decoded by stb_image (a copy of its own: its
functions are static, as steam_downloads.c's are) */
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_SIMD
#define STBI_NO_HDR
#define STBI_NO_THREAD_LOCALS
#include "../../third_party/stb/stb_image.h"

/* ---------- the list's source and its keys */

#define LIST_URL "https://raw.githubusercontent.com/FractumSeraph/OpenCE/map-index/map_torrents.txt"
#define SIGNATURE_URL LIST_URL ".sig"
/* the list's first line, and what its signature covers before its bytes
(FractumSeraph's map_list_index.h) */
#define LIST_FORMAT "# halo map torrents v1"
#define LIST_CONTEXT "fractum-halo-map-index 1\n"
#define LIST_MAXIMUM_SIZE (16UL * 1024 * 1024)
#define CATEGORIES_FORMAT "# map archive categories v1"
#define CATEGORIES_FILE "map_categories.txt"
/* the maps, by file name: plain HTTP, as his web seed is (each checked
against the signed list's BLAKE2b) */
#define MAPS_HOST "halomaps.fractumseraph.net"
#define MAPS_PATH "/maps/"
#define USER_AGENT "OpenCE (Halo; Custom Edition map downloads)"

/* the keys the list may be signed by: his fork's release key and its
recovery key (his port/linux/src/map_index_key.h) */
static const uint8_t list_keys[][32] =
{
	{
		0xc1, 0x20, 0x6c, 0xa2, 0xbe, 0xca, 0x78, 0xe1, 0x1c, 0xb1, 0x05, 0x30, 0xdd, 0x6e, 0x73, 0x3c,
		0x45, 0x61, 0xa6, 0xf1, 0x76, 0x2d, 0x49, 0x5d, 0xb1, 0xa6, 0x62, 0x53, 0xf3, 0x5a, 0x24, 0x3e,
	},
	{
		0xbb, 0x14, 0x21, 0xe0, 0x14, 0x7f, 0x12, 0x10, 0xa8, 0x44, 0x65, 0x32, 0xf6, 0x70, 0x96, 0xfc,
		0xe8, 0x87, 0xc7, 0xb5, 0x97, 0x87, 0x5c, 0x55, 0x27, 0x12, 0x98, 0xe4, 0x71, 0x88, 0xf8, 0xd3,
	},
};

enum
{
	NAME_SIZE = MAP_ARCHIVE_NAME_SIZE,
	PAGE_MAPS = 20,
	/* SEARCH, TYPE, SORT, ORDER and SHOW */
	FILTER_ROWS = 5,
	QUEUE_SIZE = 64,
	SEARCH_SIZE = 64,
	MESSAGE_SIZE = 320,
	PATH_SIZE = 1024,
	/* the second version's digits a file name may end in (map_share.h's
	MAP_SHARE_NAME_DIGITS) */
	NAME_DIGITS = 8
};

enum sort
{
	SORT_NAME,
	SORT_RATING,
	SORT_DOWNLOADS,
	SORT_DATE,
	SORT_SIZE,
	SORT_COUNT
};
static char const *const sort_names[SORT_COUNT] = { "NAME", "RATING", "DOWNLOADS", "DATE", "SIZE" };

enum show
{
	SHOW_ALL,
	SHOW_INSTALLED,
	SHOW_MISSING,
	SHOW_COUNT
};
static char const *const show_names[SHOW_COUNT] = { "ALL", "INSTALLED", "NOT INSTALLED" };

/* the TYPE filter, and the categories' letters it takes */
enum type
{
	TYPE_ALL,
	TYPE_SOLO,
	TYPE_MULTIPLAYER,
	TYPE_MULTIPLAYER_AI,
	TYPE_COUNT
};
static char const *const type_names[TYPE_COUNT] = { "ALL", "SINGLEPLAYER", "MULTIPLAYER", "MULTI + AI" };

struct map
{
	char name[NAME_SIZE];
	unsigned long checksum;
	unsigned long version;
	unsigned long long size;
	uint8_t blake2b[32];
	uint8_t share[32];
	/* S, M, A, U, or 0 (not known): the catalog's, else the categories file's */
	char category;
	unsigned char installed;
	/* its catalog entry, or -1 */
	int entry;
};

enum list_state
{
	LIST_LOADING,
	LIST_READY,
	LIST_FAILED
};

enum row_kind
{
	ROW_NONE,
	ROW_LIST,
	ROW_SEARCH,
	ROW_TYPE,
	ROW_SORT,
	ROW_ORDER,
	ROW_SHOW,
	ROW_MAP,
	ROW_PREVIOUS,
	ROW_NEXT
};

/* a category's line: its map's name and version */
/* a map of the community server's catalog (api-opence's /archive/catalog:
FractumSeraph's site's page of each map, read into its database): fields
pointing into the catalog's text, "" when none */
struct entry
{
	char const *name;
	unsigned long version;
	/* S, M, A, U, or 0 */
	char type;
	char const *title;
	char const *author;
	char const *date;
	char const *rating;
	char const *category;
	char const *picture;
	char const *description;
	/* (the catalog's eleventh field: its HAC2 downloads) */
	char const *downloads;
};

enum
{
	/* the pictures kept decoded (a quarter of a megabyte each) */
	PICTURES_KEPT = 24,
	/* the rows either side of the chosen one whose pictures are fetched
	ahead while nothing else is wanted */
	PICTURES_AHEAD = 5,
	/* a row chosen this long before its picture is fetched (not each row
	the list scrolls past) */
	PICTURE_DELAY = 120,
	PICTURE_MAXIMUM = 4 * 1024 * 1024,
	CATALOG_MAXIMUM = 32 * 1024 * 1024,
	PICTURE_SIDE = MAP_ARCHIVE_PICTURE_SIDE
};

#define CATALOG_FORMAT "# map archive catalog v1"
#define PICTURES_HOST "halomaps.fractumseraph.net"

/* a picture kept: its catalog entry's, decoded */
struct picture
{
	int entry;
	unsigned long used_at;
	unsigned serial;
	unsigned long *pixels;
};

struct category
{
	char name[NAME_SIZE];
	unsigned long version;
	char letter;
};

static pthread_mutex_t archive_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t archive_wake = PTHREAD_COND_INITIALIZER;

static struct
{
	int started;
	char data_root[PATH_SIZE];
	char custom_maps[PATH_SIZE];

	enum list_state list_state;
	/* whether GitHub has been asked this run (a lookup waits for it when
	the kept list has not got the map) */
	int github_done;
	char list_error[MESSAGE_SIZE];
	unsigned long long serial;
	struct map *maps;
	int map_count;
	int *shown;
	int shown_count;
	int page;
	short first;
	short chosen;
	int moved;

	struct category *categories;
	int category_count;

	char search[SEARCH_SIZE];
	enum type type;
	enum sort sort;
	/* the sort's order, 1 descending (SORT sets names A to Z, the others
	highest first) */
	int descending;
	enum show show;
	int want_search;

	/* the screen's downloads: the one under way (-1: none), its bytes so
	far, and those waiting */
	int downloading;
	unsigned long long downloaded;
	int cancel;
	/* (the one under way stopped for a joiner's, and queued again) */
	int requeue;
	int queue[QUEUE_SIZE];
	int queue_count;
	char message[MESSAGE_SIZE];

	/* a joiner's download (map_archive_fetch) */
	struct
	{
		enum map_archive_fetch_state state;
		int wanted;
		int cancel;
		struct map map;
		char file_name[NAME_SIZE];
		unsigned long long done;
		unsigned long started_at;
		unsigned long now;
		char error[200];
	} join;

	int want_list;
	int want_scan;
	int rescan;
	int opened;

	/* the catalog (its text, which its entries point into), sorted by
	name and version, and its serial */
	char *catalog_text;
	struct entry *entries;
	int entry_count;
	unsigned long long catalog_serial;
	/* the pictures: those kept, the entry whose is wanted (-1: none) and
	since when */
	struct picture pictures[PICTURES_KEPT];
	unsigned picture_serials;
	int picture_want;
	unsigned long picture_want_at;
	int pictures_started;
} archive;

static pthread_cond_t picture_wake = PTHREAD_COND_INITIALIZER;

/* ---------- small helpers */

static int lower(int c)
{
	return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int names_equal_length(char const *a, char const *b, size_t length)
{
	size_t i;

	for (i = 0; i < length; i++)
	{
		if (lower((unsigned char)a[i]) != lower((unsigned char)b[i]))
			return 0;
		if (!a[i])
			return 1;
	}
	return 1;
}

static int names_equal(char const *a, char const *b)
{
	while (*a && lower((unsigned char)*a) == lower((unsigned char)*b))
	{
		a++;
		b++;
	}
	return !*a && !*b;
}

static int name_compare(char const *a, char const *b)
{
	while (*a && lower((unsigned char)*a) == lower((unsigned char)*b))
	{
		a++;
		b++;
	}
	return lower((unsigned char)*a) - lower((unsigned char)*b);
}

static int contains(char const *text, char const *part)
{
	size_t length = strlen(part);

	if (!length)
		return 1;
	for (; *text; text++)
	{
		size_t i = 0;

		while (i < length && text[i] && lower((unsigned char)text[i]) == lower((unsigned char)part[i]))
			i++;
		if (i == length)
			return 1;
	}
	return 0;
}

static int hex_value(int c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 :
		-1;
}

static int hex_bytes(char const *text, size_t length, uint8_t *bytes, size_t count)
{
	size_t i;

	if (length != count * 2)
		return 0;
	for (i = 0; i < count; i++)
	{
		int high = hex_value((unsigned char)text[2 * i]);
		int low = hex_value((unsigned char)text[2 * i + 1]);

		if (high < 0 || low < 0)
			return 0;
		bytes[i] = (uint8_t)(high * 16 + low);
	}
	return 1;
}

/* a share hash's version, as map_share_hash_version makes it */
static unsigned long hash_version(uint8_t const *hash)
{
	unsigned long version = (unsigned long)hash[0] << 24 | (unsigned long)hash[1] << 16 |
		(unsigned long)hash[2] << 8 | (unsigned long)hash[3];

	return version ? version : 1;
}

static void size_text(unsigned long long bytes, char *text, int size)
{
	if (bytes >= 1024ULL * 1024 * 1024)
		snprintf(text, (size_t)size, "%.2f GB", (double)bytes / (1024.0 * 1024 * 1024));
	else if (bytes >= 1024 * 1024)
		snprintf(text, (size_t)size, "%.1f MB", (double)bytes / (1024.0 * 1024));
	else
		snprintf(text, (size_t)size, "%llu KB", (bytes + 1023) / 1024);
}

/* the length of a map's file name less ".map" */
static size_t base_length(char const *name)
{
	size_t length = strlen(name);

	if (length > 4 && names_equal(name + length - 4, ".map"))
		length -= 4;
	return length;
}

static void display_name(struct map const *map, char *text, int size)
{
	snprintf(text, (size_t)size, "%.*s", (int)base_length(map->name), map->name);
}

static void path_join(char *path, size_t size, char const *folder, char const *name)
{
	snprintf(path, size, "%s/%s", folder, name);
}

static char const *category_name(char letter)
{
	switch (letter)
	{
	case 'S':
		return "Singleplayer";
	case 'M':
		return "Multiplayer";
	case 'A':
		return "Multiplayer with AI";
	case 'U':
		return "User interface";
	default:
		return "Not known";
	}
}

static int type_takes(enum type type, char letter)
{
	switch (type)
	{
	case TYPE_SOLO:
		return letter == 'S';
	case TYPE_MULTIPLAYER:
		return letter == 'M' || letter == 'A';
	case TYPE_MULTIPLAYER_AI:
		return letter == 'A';
	default:
		/* (the user interface's maps: no map to play) */
		return letter != 'U';
	}
}

/* ---------- files */

static int file_read(char const *path, unsigned long maximum, unsigned char **data, size_t *size)
{
	FILE *file = fopen(path, "rb");
	long length;

	*data = NULL;
	*size = 0;
	if (!file)
		return 0;
	if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 || (unsigned long)length > maximum ||
		fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		return 0;
	}
	*data = malloc((size_t)length + 1);
	if (!*data || fread(*data, 1, (size_t)length, file) != (size_t)length)
	{
		free(*data);
		*data = NULL;
		fclose(file);
		return 0;
	}
	fclose(file);
	(*data)[length] = 0;
	*size = (size_t)length;
	return 1;
}

static int file_write(char const *path, unsigned char const *data, size_t size)
{
	char part[PATH_SIZE + 8];
	FILE *file;
	int good;

	snprintf(part, sizeof(part), "%s.part", path);
	file = fopen(part, "wb");
	if (!file)
		return 0;
	good = fwrite(data, 1, size, file) == size;
	good = fclose(file) == 0 && good;
	if (good)
	{
		remove(path);
		good = rename(part, path) == 0;
	}
	if (!good)
		remove(part);
	return good;
}

/* ---------- the list */

static int list_signed(char const *signature_text, size_t signature_size, unsigned char const *list, size_t list_size)
{
	uint8_t signature[64];
	size_t start = 0, end = signature_size, context = strlen(LIST_CONTEXT), i;
	uint8_t *message;
	int good = 0;

	while (start < end && (signature_text[start] == ' ' || signature_text[start] == '\n' ||
		signature_text[start] == '\r' || signature_text[start] == '\t'))
		start++;
	while (end > start && (signature_text[end - 1] == ' ' || signature_text[end - 1] == '\n' ||
		signature_text[end - 1] == '\r' || signature_text[end - 1] == '\t'))
		end--;
	if (!hex_bytes(signature_text + start, end - start, signature, sizeof(signature)))
		return 0;
	message = malloc(context + list_size);
	if (!message)
		return 0;
	memcpy(message, LIST_CONTEXT, context);
	memcpy(message + context, list, list_size);
	for (i = 0; i < sizeof(list_keys) / sizeof(list_keys[0]) && !good; i++)
		good = crypto_ed25519_check(signature, list_keys[i], message, context + list_size) == 0;
	free(message);
	return good;
}

static unsigned long long list_serial(unsigned char const *list, size_t size)
{
	static char const key[] = "# serial ";
	size_t at = 0;

	while (at < size)
	{
		size_t end = at;

		while (end < size && list[end] != '\n')
			end++;
		if (list[at] != '#')
			break;
		if (end - at > sizeof(key) - 1 && !memcmp(list + at, key, sizeof(key) - 1))
			return strtoull((char const *)list + at + sizeof(key) - 1, NULL, 10);
		at = end + 1;
	}
	return 0;
}

/* a line's tab-separated fields, up to count: how many */
static size_t line_fields(unsigned char const *text, size_t start, size_t end, char const **fields, size_t *lengths,
	size_t count)
{
	size_t field = 0;

	while (field < count && start <= end)
	{
		size_t stop = start;

		while (stop < end && text[stop] != '\t')
			stop++;
		fields[field] = (char const *)text + start;
		lengths[field] = stop - start;
		field++;
		start = stop + 1;
	}
	return field;
}

/* the list's maps (its resource maps left out): their count, -1 if it is
not the list's format */
static int list_parse(unsigned char const *list, size_t size, struct map **maps_out)
{
	size_t at = 0, lines = 0, i;
	struct map *maps;
	int count = 0;

	*maps_out = NULL;
	if (size < sizeof(LIST_FORMAT) - 1 || memcmp(list, LIST_FORMAT, sizeof(LIST_FORMAT) - 1))
		return -1;
	for (i = 0; i < size; i++)
		lines += list[i] == '\n';
	maps = calloc(lines + 1, sizeof(*maps));
	if (!maps)
		return -1;
	while (at < size)
	{
		char const *fields[12];
		size_t lengths[12];
		size_t end = at, start, field_count, field;
		struct map map;
		int shared = 0;

		while (end < size && list[end] != '\n')
			end++;
		start = at;
		at = end + 1;
		if (end > start && list[end - 1] == '\r')
			end--;
		if (end == start || list[start] == '#')
			continue;
		field_count = line_fields(list, start, end, fields, lengths, 12);
		if (field_count < 7 || !lengths[0] || lengths[0] >= NAME_SIZE - NAME_DIGITS - 1 || lengths[1] != 8 ||
			lengths[5] != 8 + 64 || memcmp(fields[5], "blake2b:", 8))
			continue;
		memset(&map, 0, sizeof(map));
		memcpy(map.name, fields[0], lengths[0]);
		/* (a name with a folder in it is no file of custom_maps) */
		if (strchr(map.name, '/') || strchr(map.name, '\\') || strchr(map.name, ':') || map.name[0] == '.')
			continue;
		map.checksum = strtoul(fields[1], NULL, 16);
		map.size = strtoull(fields[2], NULL, 10);
		if (!hex_bytes(fields[5] + 8, 64, map.blake2b, 32) || !map.size)
			continue;
		for (field = 6; field < field_count && !shared; field++)
		{
			if (lengths[field] == 6 + 64 && !memcmp(fields[field], "share:", 6))
				shared = hex_bytes(fields[field] + 6, 64, map.share, 32);
		}
		/* (the resource maps, matched by name alone: no map to play; and a
		map with no share hash, which no host's game could be found by) */
		if (!map.checksum || !shared)
			continue;
		map.version = hash_version(map.share);
		maps[count++] = map;
	}
	*maps_out = maps;
	return count;
}

static int compare_categories(void const *a, void const *b)
{
	struct category const *x = a, *y = b;
	int order = name_compare(x->name, y->name);

	return order ? order : x->version < y->version ? -1 : x->version > y->version;
}

/* (thread) the categories beside config.toml, read once */
static void categories_load(void)
{
	char path[PATH_SIZE];
	unsigned char *text = NULL;
	size_t size = 0, at = 0, lines = 0, i;
	struct category *categories;
	int count = 0;

	if (archive.categories)
		return;
	config_folder(path, sizeof(path));
	snprintf(path + strlen(path), sizeof(path) - strlen(path), "%s", CATEGORIES_FILE);
	if (!file_read(path, LIST_MAXIMUM_SIZE, &text, &size) ||
		size < sizeof(CATEGORIES_FORMAT) - 1 || memcmp(text, CATEGORIES_FORMAT, sizeof(CATEGORIES_FORMAT) - 1))
	{
		platform_log("map archive: no categories (%s): BROWSE MAPS' TYPE takes every map", path);
		free(text);
		return;
	}
	for (i = 0; i < size; i++)
		lines += text[i] == '\n';
	categories = calloc(lines + 1, sizeof(*categories));
	while (categories && at < size)
	{
		char const *fields[3];
		size_t lengths[3];
		size_t end = at, start = at;
		uint8_t version[4];

		while (end < size && text[end] != '\n')
			end++;
		at = end + 1;
		if (end > start && text[end - 1] == '\r')
			end--;
		if (end == start || text[start] == '#')
			continue;
		if (line_fields(text, start, end, fields, lengths, 3) < 3 || !lengths[0] || lengths[0] >= NAME_SIZE ||
			!hex_bytes(fields[1], lengths[1], version, 4) || lengths[2] != 1)
			continue;
		memcpy(categories[count].name, fields[0], lengths[0]);
		categories[count].name[lengths[0]] = 0;
		categories[count].version = (unsigned long)version[0] << 24 | (unsigned long)version[1] << 16 |
			(unsigned long)version[2] << 8 | version[3];
		categories[count].letter = fields[2][0] == '?' ? 0 : fields[2][0];
		count++;
	}
	free(text);
	if (categories && count)
		qsort(categories, (size_t)count, sizeof(*categories), compare_categories);
	pthread_mutex_lock(&archive_lock);
	archive.categories = categories;
	archive.category_count = count;
	pthread_mutex_unlock(&archive_lock);
	platform_log("map archive: %d maps' categories from %s", count, path);
}

/* (thread, before the maps are in use) each map's category */
static void categories_give(struct map *maps, int count)
{
	int i;

	for (i = 0; i < count && archive.category_count; i++)
	{
		struct category key;
		struct category const *found;

		snprintf(key.name, sizeof(key.name), "%s", maps[i].name);
		/* (the categories' versions are the share hash's first 32 bits as
		they are, 0 included) */
		key.version = (unsigned long)maps[i].share[0] << 24 | (unsigned long)maps[i].share[1] << 16 |
			(unsigned long)maps[i].share[2] << 8 | maps[i].share[3];
		found = bsearch(&key, archive.categories, (size_t)archive.category_count, sizeof(key), compare_categories);
		maps[i].category = found ? found->letter : 0;
	}
}

static int compare_by_name(void const *a, void const *b)
{
	int x = *(int const *)a, y = *(int const *)b;
	int order = name_compare(archive.maps[x].name, archive.maps[y].name);

	return order ? order : x - y;
}

/* a map's catalog entry, or NULL */
static struct entry const *map_entry(int map)
{
	int entry = archive.maps[map].entry;

	return entry >= 0 && entry < archive.entry_count ? &archive.entries[entry] : NULL;
}

/* (locked) a map's value under the sort (not name): 0 when it has none */
static int sort_value(int map, double *value, char const **date)
{
	struct entry const *entry = map_entry(map);
	char *end;

	*value = 0;
	*date = "";
	switch (archive.sort)
	{
	case SORT_SIZE:
		*value = (double)archive.maps[map].size;
		return 1;
	case SORT_RATING:
	case SORT_DOWNLOADS:
	{
		char const *text = !entry ? NULL : archive.sort == SORT_RATING ? entry->rating : entry->downloads;

		if (!text || !text[0])
			return 0;
		*value = strtod(text, &end);
		return end != text;
	}
	case SORT_DATE:
		if (!entry || !entry->date[0])
			return 0;
		*date = entry->date;
		return 1;
	default:
		return 0;
	}
}

/* (locked) the maps by name (A to Z, or Z to A) */
static int compare_shown_by_name(void const *a, void const *b)
{
	int order = compare_by_name(a, b);

	return archive.descending ? -order : order;
}

/* (locked) the maps by the sort's value, those with none last either way,
then by name */
static int compare_by_value(void const *a, void const *b)
{
	int x = *(int const *)a, y = *(int const *)b;
	double p, q;
	char const *date_x, *date_y;
	int has_x = sort_value(x, &p, &date_x), has_y = sort_value(y, &q, &date_y), order = 0;

	if (has_x != has_y)
		return has_x ? -1 : 1;
	if (has_x)
	{
		if (archive.sort == SORT_DATE)
			order = strcmp(date_x, date_y);
		else
			order = p < q ? -1 : p > q ? 1 : 0;
		if (archive.descending)
			order = -order;
	}
	return order ? order : compare_by_name(a, b);
}

/* (locked) the maps shown again, by the search, type, show and sort */
static void shown_build(void)
{
	int i;

	archive.shown_count = 0;
	if (!archive.shown)
		return;
	for (i = 0; i < archive.map_count; i++)
	{
		struct map const *map = &archive.maps[i];

		if (!contains(map->name, archive.search) || !type_takes(archive.type, map->category))
			continue;
		if (archive.show == SHOW_INSTALLED && !map->installed)
			continue;
		if (archive.show == SHOW_MISSING && map->installed)
			continue;
		archive.shown[archive.shown_count++] = i;
	}
	qsort(archive.shown, (size_t)archive.shown_count, sizeof(int),
		archive.sort == SORT_NAME ? compare_shown_by_name : compare_by_value);
	if ((archive.page - 1) * PAGE_MAPS >= archive.shown_count)
		archive.page = 1;
}

static void catalog_link(void);
static void catalog_load(void);

/* (thread) the list taken in place of the one in use, if it is signed and
newer: 1 if taken */
static int list_take(unsigned char const *list, size_t list_size, char const *signature, size_t signature_size,
	char const *source, char *error, int error_size)
{
	struct map *maps;
	int count;
	unsigned long long serial;

	if (!list_signed(signature, signature_size, list, list_size))
	{
		snprintf(error, (size_t)error_size, "the list from %s is not signed by FractumSeraph's key", source);
		return 0;
	}
	serial = list_serial(list, list_size);
	pthread_mutex_lock(&archive_lock);
	if (archive.list_state == LIST_READY && serial <= archive.serial)
	{
		pthread_mutex_unlock(&archive_lock);
		return 0;
	}
	pthread_mutex_unlock(&archive_lock);
	count = list_parse(list, list_size, &maps);
	if (count <= 0)
	{
		free(maps);
		snprintf(error, (size_t)error_size, "the list from %s is not one of maps", source);
		return 0;
	}
	categories_give(maps, count);
	pthread_mutex_lock(&archive_lock);
	/* (no download under way holds an index into the old list: this
	thread is the one that downloads, and the join's map is a copy) */
	free(archive.maps);
	free(archive.shown);
	archive.maps = maps;
	archive.map_count = count;
	archive.shown = malloc((size_t)count * sizeof(int));
	archive.serial = serial;
	archive.list_state = LIST_READY;
	catalog_link();
	archive.queue_count = 0;
	archive.want_scan = 1;
	archive.moved = 1;
	shown_build();
	pthread_mutex_unlock(&archive_lock);
	platform_log("map archive: the list from %s is in use: serial %llu, %d maps", source, serial, count);
	return 1;
}

/* (thread) the list kept, then GitHub's */
static void list_load(void)
{
	char list_path[PATH_SIZE], signature_path[PATH_SIZE], folder[PATH_SIZE], error[MESSAGE_SIZE];
	unsigned char *list = NULL, *signature = NULL;
	unsigned long list_size = 0, signature_size = 0;
	size_t kept_list_size, kept_signature_size;
	int status = 0, kept = 0;

	error[0] = 0;
	categories_load();
	path_join(folder, sizeof(folder), archive.data_root, "map_archive");
	path_join(list_path, sizeof(list_path), folder, "map_torrents.txt");
	path_join(signature_path, sizeof(signature_path), folder, "map_torrents.txt.sig");
	if (file_read(list_path, LIST_MAXIMUM_SIZE, &list, &kept_list_size) &&
		file_read(signature_path, 4096, &signature, &kept_signature_size))
	{
		kept = list_take(list, kept_list_size, (char const *)signature, kept_signature_size, "this device", error,
			(int)sizeof(error));
	}
	free(list);
	free(signature);
	list = signature = NULL;
	if (steam_https_get(LIST_URL, LIST_MAXIMUM_SIZE, &list, &list_size, &status, error, (int)sizeof(error)) &&
		status == 200 &&
		steam_https_get(SIGNATURE_URL, 4096, &signature, &signature_size, &status, error, (int)sizeof(error)) &&
		status == 200)
	{
		if (list_take(list, list_size, (char const *)signature, signature_size, "GitHub", error, (int)sizeof(error)))
		{
			posix_make_directory(folder);
			if (!file_write(list_path, list, list_size) || !file_write(signature_path, signature, signature_size))
				platform_log("map archive: the list could not be kept in %s", folder);
		}
	}
	else if (!error[0])
		snprintf(error, sizeof(error), "GitHub answered %d", status);
	free(list);
	free(signature);
	pthread_mutex_lock(&archive_lock);
	archive.github_done = 1;
	if (archive.list_state != LIST_READY)
	{
		archive.list_state = LIST_FAILED;
		snprintf(archive.list_error, sizeof(archive.list_error), "%s", error[0] ? error : "no list");
		platform_log("map archive: no list: %s", archive.list_error);
	}
	else if (error[0] && !kept)
		platform_log("map archive: kept the list in use (%s)", error);
	pthread_mutex_unlock(&archive_lock);
	/* (the catalog's details and types, the community server's) */
	catalog_load();
}

/* ---------- what is installed: by name, in custom_maps and its folders */

struct installed_names
{
	char (*names)[NAME_SIZE];
	int count;
	int capacity;
};

static int compare_names(void const *a, void const *b)
{
	return name_compare((char const *)a, (char const *)b);
}

/* a map file's name as the list's are matched by: less ".map", and less
the _<8 lowercase digits> a second version's name ends in */
static void installed_add(struct installed_names *names, char const *file_name)
{
	size_t length = strlen(file_name), base, i;
	int hashed = 1;

	if (length < 5 || !names_equal(file_name + length - 4, ".map"))
		return;
	base = length - 4;
	if (base > NAME_DIGITS + 1 && file_name[base - NAME_DIGITS - 1] == '_')
	{
		for (i = base - NAME_DIGITS; i < base && hashed; i++)
			hashed = (file_name[i] >= '0' && file_name[i] <= '9') || (file_name[i] >= 'a' && file_name[i] <= 'f');
		if (hashed)
			base -= NAME_DIGITS + 1;
	}
	if (!base || base >= NAME_SIZE)
		return;
	if (names->count == names->capacity)
	{
		int capacity = names->capacity ? names->capacity * 2 : 256;
		char (*grown)[NAME_SIZE] = realloc(names->names, (size_t)capacity * NAME_SIZE);

		if (!grown)
			return;
		names->names = grown;
		names->capacity = capacity;
	}
	memcpy(names->names[names->count], file_name, base);
	names->names[names->count][base] = 0;
	names->count++;
}

/* (thread) a folder's maps named, and (depth 0) its folders' too */
static void installed_scan_folder(struct installed_names *names, char const *folder, int depth)
{
	char name[260];
	void *directory = posix_directory_open(folder);

	while (directory && posix_directory_next(directory, name, sizeof(name)))
	{
		char path[PATH_SIZE];
		struct posix_file_information information;

		if (name[0] == '.')
			continue;
		path_join(path, sizeof(path), folder, name);
		if (posix_stat(path, &information) != 0)
			continue;
		if (information.flags & _posix_file_is_directory)
		{
			if (!depth)
				installed_scan_folder(names, path, 1);
			continue;
		}
		installed_add(names, name);
	}
	if (directory)
		posix_directory_close(directory);
}

static void installed_scan(void)
{
	struct installed_names names = { NULL, 0, 0 };
	char custom_maps[PATH_SIZE];
	int i;

	pthread_mutex_lock(&archive_lock);
	snprintf(custom_maps, sizeof(custom_maps), "%s", archive.custom_maps);
	pthread_mutex_unlock(&archive_lock);
	installed_scan_folder(&names, custom_maps, 0);
	if (names.count)
		qsort(names.names, (size_t)names.count, NAME_SIZE, compare_names);
	pthread_mutex_lock(&archive_lock);
	for (i = 0; i < archive.map_count; i++)
	{
		char key[NAME_SIZE];

		snprintf(key, sizeof(key), "%.*s", (int)base_length(archive.maps[i].name), archive.maps[i].name);
		archive.maps[i].installed = names.count &&
			bsearch(key, names.names, (size_t)names.count, NAME_SIZE, compare_names) != NULL;
	}
	shown_build();
	pthread_mutex_unlock(&archive_lock);
	free(names.names);
}

/* (thread) the map's file named in custom_maps/downloaded_maps.txt (map
sharing's list: map_share.c's downloaded_maps_mark) */
static void downloaded_mark(char const *custom_maps, char const *name)
{
	char path[PATH_SIZE], line[NAME_SIZE + 8];
	FILE *file;

	path_join(path, sizeof(path), custom_maps, "downloaded_maps.txt");
	file = fopen(path, "rb");
	while (file && fgets(line, sizeof(line), file))
	{
		line[strcspn(line, "\r\n")] = 0;
		if (line[0] && names_equal(line, name))
		{
			fclose(file);
			return;
		}
	}
	if (file)
		fclose(file);
	file = fopen(path, "ab");
	if (!file)
	{
		platform_log("map archive: could not name %s in %s", name, path);
		return;
	}
	fprintf(file, "%s\r\n", name);
	fclose(file);
}

/* ---------- a download */

static int send_all(int socket, char const *data, size_t size)
{
	while (size)
	{
		int sent;

		if (steam_socket_wait(socket, 20000, 1) <= 0)
			return 0;
		sent = posix_socket_send(socket, data, (int)size, 0);
		if (sent <= 0)
			return 0;
		data += sent;
		size -= (size_t)sent;
	}
	return 1;
}

static int receive(int socket, char *buffer, size_t size)
{
	if (steam_socket_wait(socket, 20000, 0) <= 0)
		return -1;
	return posix_socket_recv(socket, buffer, (int)size, 0);
}

/* (thread, unlocked) the bytes so far of the download, and whether it is
to stop */
static int download_progress(int joining, unsigned long long written)
{
	int stop;

	pthread_mutex_lock(&archive_lock);
	if (joining)
	{
		archive.join.done = written;
		archive.join.now = system_milliseconds();
		stop = archive.join.cancel;
	}
	else
	{
		archive.downloaded = written;
		stop = archive.cancel || archive.requeue;
	}
	pthread_mutex_unlock(&archive_lock);
	return stop;
}

/* (thread) the map downloaded into custom_maps/<file_name>: 1, else 0 and
why */
static int download(struct map const *map, char const *custom_maps, char const *file_name, int joining, char *error,
	int error_size)
{
	enum { BLOCK = 64 * 1024 };
	char request[1024], path[PATH_SIZE], part[PATH_SIZE + 8];
	char *buffer = malloc(BLOCK + 1);
	char const *at;
	size_t have = 0, i, used;
	char *end = NULL;
	unsigned long long written = 0;
	crypto_blake2b_ctx hash;
	uint8_t digest[32];
	FILE *file = NULL;
	int socket = -1, good = 0, status;
	long long length = -1;

	if (!buffer)
	{
		snprintf(error, (size_t)error_size, "out of memory");
		return 0;
	}
	used = (size_t)snprintf(request, sizeof(request), "GET " MAPS_PATH);
	for (at = map->name; *at && used + 4 < sizeof(request); at++)
	{
		unsigned char c = (unsigned char)*at;

		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
			c == '.' || c == '~')
			request[used++] = (char)c;
		else
			used += (size_t)snprintf(request + used, sizeof(request) - used, "%%%02X", c);
	}
	snprintf(request + used, sizeof(request) - used, " HTTP/1.1\r\nHost: " MAPS_HOST "\r\nUser-Agent: " USER_AGENT
		"\r\nAccept: */*\r\nConnection: close\r\n\r\n");
	path_join(path, sizeof(path), custom_maps, file_name);
	snprintf(part, sizeof(part), "%s.part", path);
	/* (never over a map there, before a byte is fetched as after) */
	{
		FILE *there = fopen(path, "rb");

		if (there)
		{
			fclose(there);
			snprintf(error, (size_t)error_size, "a file named %s is in custom_maps", file_name);
			free(buffer);
			return 0;
		}
	}
	socket = steam_tcp_connect(MAPS_HOST, 80, 10000);
	if (socket < 0)
	{
		snprintf(error, (size_t)error_size, "the archive (" MAPS_HOST ") could not be reached");
		goto done;
	}
	if (!send_all(socket, request, strlen(request)))
	{
		snprintf(error, (size_t)error_size, "the archive took no request");
		goto done;
	}
	while (!end)
	{
		int got;

		if (have >= BLOCK)
		{
			snprintf(error, (size_t)error_size, "the archive's answer is not HTTP");
			goto done;
		}
		got = receive(socket, buffer + have, BLOCK - have);
		if (got <= 0)
		{
			snprintf(error, (size_t)error_size, "the archive closed the connection");
			goto done;
		}
		have += (size_t)got;
		buffer[have] = 0;
		end = strstr(buffer, "\r\n\r\n");
	}
	status = !strncmp(buffer, "HTTP/1.", 7) && have > 12 ? atoi(buffer + 9) : 0;
	*end = 0;
	for (at = buffer; (at = strchr(at, '\n')) != NULL;)
	{
		static char const name[] = "content-length:";

		at++;
		for (i = 0; i < sizeof(name) - 1 && lower((unsigned char)at[i]) == name[i]; i++)
			;
		if (i == sizeof(name) - 1)
			length = strtoll(at + i, NULL, 10);
	}
	if (status != 200)
	{
		snprintf(error, (size_t)error_size, status == 404 ? "the archive does not have it" : "the archive answered %d",
			status);
		goto done;
	}
	if (length >= 0 && (unsigned long long)length != map->size)
	{
		snprintf(error, (size_t)error_size, "the archive's file is not the list's size");
		goto done;
	}
	file = fopen(part, "wb");
	if (!file)
	{
		snprintf(error, (size_t)error_size, "custom_maps could not be written to");
		goto done;
	}
	crypto_blake2b_init(&hash, 32);
	have -= (size_t)(end + 4 - buffer);
	memmove(buffer, end + 4, have);
	for (;;)
	{
		if (have)
		{
			if (written + have > map->size)
			{
				snprintf(error, (size_t)error_size, "the archive's file is larger than the list's");
				goto done;
			}
			if (fwrite(buffer, 1, have, file) != have)
			{
				snprintf(error, (size_t)error_size, "custom_maps is full, or could not be written to");
				goto done;
			}
			crypto_blake2b_update(&hash, (uint8_t const *)buffer, have);
			written += have;
			have = 0;
		}
		if (download_progress(joining, written))
		{
			snprintf(error, (size_t)error_size, "cancelled");
			goto done;
		}
		if (written == map->size)
			break;
		{
			int got = receive(socket, buffer, BLOCK);

			if (got <= 0)
			{
				snprintf(error, (size_t)error_size, "the download was cut short");
				goto done;
			}
			have = (size_t)got;
		}
	}
	if (fclose(file) != 0)
	{
		file = NULL;
		snprintf(error, (size_t)error_size, "custom_maps is full, or could not be written to");
		goto done;
	}
	file = NULL;
	crypto_blake2b_final(&hash, digest);
	if (crypto_verify32(digest, map->blake2b) != 0)
	{
		snprintf(error, (size_t)error_size, "the file is not the list's (its BLAKE2b differs)");
		goto done;
	}
	/* (never over a map there: a file of its name is another version, and
	the caller chose a name none has) */
	{
		FILE *there = fopen(path, "rb");

		if (there)
		{
			fclose(there);
			snprintf(error, (size_t)error_size, "a file named %s is in custom_maps", file_name);
			goto done;
		}
	}
	if (rename(part, path) != 0)
	{
		snprintf(error, (size_t)error_size, "the map could not be put in custom_maps");
		goto done;
	}
	downloaded_mark(custom_maps, file_name);
	good = 1;
done:
	if (file)
		fclose(file);
	if (!good)
		remove(part);
	if (socket >= 0)
		posix_socket_close(socket);
	free(buffer);
	return good;
}

/* ---------- the thread */

static void *archive_thread(void *unused)
{
	(void)unused;
	pthread_mutex_lock(&archive_lock);
	for (;;)
	{
		char custom_maps[PATH_SIZE];

		snprintf(custom_maps, sizeof(custom_maps), "%s", archive.custom_maps);
		if (archive.want_list)
		{
			archive.want_list = 0;
			archive.github_done = 0;
			if (archive.list_state != LIST_READY)
				archive.list_state = LIST_LOADING;
			pthread_mutex_unlock(&archive_lock);
			list_load();
			pthread_mutex_lock(&archive_lock);
			continue;
		}
		/* (a joiner's map first) */
		if (archive.join.wanted)
		{
			struct map map = archive.join.map;
			char file_name[NAME_SIZE], error[200];
			int good;

			archive.join.wanted = 0;
			archive.join.cancel = 0;
			archive.join.done = 0;
			archive.join.started_at = archive.join.now = system_milliseconds();
			snprintf(file_name, sizeof(file_name), "%s", archive.join.file_name);
			pthread_mutex_unlock(&archive_lock);
			platform_log("map archive: a joiner's map: downloading %s as %s (%llu bytes)", map.name, file_name,
				map.size);
			error[0] = 0;
			good = download(&map, custom_maps, file_name, 1, error, (int)sizeof(error));
			platform_log("map archive: %s: %s", map.name, good ? "installed" : error);
			pthread_mutex_lock(&archive_lock);
			/* (another asked for meanwhile: this one's state is not its) */
			if (!archive.join.wanted && archive.join.state == MAP_ARCHIVE_FETCH_RUNNING)
			{
				archive.join.state = good ? MAP_ARCHIVE_FETCH_DONE : MAP_ARCHIVE_FETCH_FAILED;
				snprintf(archive.join.error, sizeof(archive.join.error), "%s", error);
			}
			if (good)
				archive.rescan = 1;
			archive.want_scan = 1;
			continue;
		}
		if (archive.want_scan)
		{
			archive.want_scan = 0;
			pthread_mutex_unlock(&archive_lock);
			installed_scan();
			pthread_mutex_lock(&archive_lock);
			continue;
		}
		if (archive.queue_count && archive.list_state == LIST_READY)
		{
			struct map map;
			char error[200];
			int good, map_index;

			map_index = archive.downloading = archive.queue[0];
			memmove(archive.queue, archive.queue + 1, (size_t)(--archive.queue_count) * sizeof(int));
			archive.downloaded = 0;
			archive.cancel = 0;
			archive.requeue = 0;
			map = archive.maps[map_index];
			pthread_mutex_unlock(&archive_lock);
			platform_log("map archive: downloading %s (%llu bytes)", map.name, map.size);
			error[0] = 0;
			good = download(&map, custom_maps, map.name, 0, error, (int)sizeof(error));
			pthread_mutex_lock(&archive_lock);
			if (!good && archive.requeue && archive.queue_count < QUEUE_SIZE)
			{
				/* (a joiner's came first: this one again after it) */
				memmove(archive.queue + 1, archive.queue, (size_t)archive.queue_count * sizeof(int));
				archive.queue[0] = map_index;
				archive.queue_count++;
				platform_log("map archive: %s waits for a joiner's map", map.name);
			}
			else
			{
				platform_log("map archive: %s: %s", map.name, good ? "installed" : error);
				if (good)
				{
					snprintf(archive.message, sizeof(archive.message), "%.*s installed.",
						(int)base_length(map.name), map.name);
					archive.rescan = 1;
				}
				else
					snprintf(archive.message, sizeof(archive.message), "%.*s: %s.", (int)base_length(map.name),
						map.name, error);
			}
			archive.downloading = -1;
			archive.requeue = 0;
			archive.want_scan = 1;
			continue;
		}
		pthread_cond_wait(&archive_wake, &archive_lock);
	}
	return NULL;
}

/* ---------- starting */

int map_archive_available(void)
{
#if defined(HALO_WEB)
	return 0;
#else
	return steam_net_available();
#endif
}

/* (locked) the folders, as they are now (custom_maps may have moved:
game.custom_maps.path) */
static void folders_set(void)
{
	char custom_maps[PATH_SIZE];

	platform_translate_path("d:\\custom_maps", custom_maps, sizeof(custom_maps));
	snprintf(archive.custom_maps, sizeof(archive.custom_maps), "%s", custom_maps);
	snprintf(archive.data_root, sizeof(archive.data_root), "%s", platform_data_root());
}

void map_archive_start(void)
{
	pthread_t thread;
	int start;

	if (!map_archive_available())
		return;
	pthread_mutex_lock(&archive_lock);
	start = !archive.started;
	if (start)
	{
		archive.started = 1;
		archive.downloading = -1;
		archive.picture_want = -1;
		{
			int i;

			for (i = 0; i < PICTURES_KEPT; i++)
				archive.pictures[i].entry = -1;
		}
		archive.page = 1;
		archive.want_list = 1;
		archive.list_state = LIST_LOADING;
		folders_set();
	}
	/* (a list that could not be had: tried again) */
	else if (archive.list_state == LIST_FAILED && !archive.want_list)
		archive.want_list = 1;
	pthread_cond_signal(&archive_wake);
	pthread_mutex_unlock(&archive_lock);
	if (start)
	{
		if (pthread_create(&thread, NULL, archive_thread, NULL) == 0)
			pthread_detach(thread);
		else
		{
			pthread_mutex_lock(&archive_lock);
			archive.list_state = LIST_FAILED;
			archive.github_done = 1;
			snprintf(archive.list_error, sizeof(archive.list_error), "no thread for it");
			pthread_mutex_unlock(&archive_lock);
		}
	}
}

/* ---------- a joiner's map */

/* (locked) the list's map of that name and version, else -1 */
static int map_find(char const *name, unsigned long version)
{
	size_t length = strlen(name);
	int i;

	if (length > 4 && names_equal(name + length - 4, ".map"))
		length -= 4;
	for (i = 0; i < archive.map_count; i++)
	{
		struct map const *map = &archive.maps[i];

		if (map->version == version && base_length(map->name) == length && names_equal_length(map->name, name, length))
			return i;
	}
	return -1;
}

enum map_archive_lookup map_archive_lookup(char const *name, unsigned long version, unsigned long long *size)
{
	enum map_archive_lookup result;
	int found;

	*size = 0;
	if (!map_archive_available() || !name || !*name)
		return MAP_ARCHIVE_LOOKUP_MISSING;
	map_archive_start();
	pthread_mutex_lock(&archive_lock);
	found = archive.list_state == LIST_READY ? map_find(name, version) : -1;
	if (found >= 0)
	{
		*size = archive.maps[found].size;
		result = MAP_ARCHIVE_LOOKUP_FOUND;
	}
	/* (not in the kept list: GitHub's may be newer) */
	else if (!archive.github_done)
		result = MAP_ARCHIVE_LOOKUP_WAIT;
	else
		result = MAP_ARCHIVE_LOOKUP_MISSING;
	pthread_mutex_unlock(&archive_lock);
	return result;
}

int map_archive_fetch(char const *name, unsigned long version, char const *file_name)
{
	int found;

	if (!map_archive_available() || !file_name || !*file_name || strlen(file_name) >= NAME_SIZE ||
		strchr(file_name, '/') || strchr(file_name, '\\'))
		return 0;
	map_archive_start();
	pthread_mutex_lock(&archive_lock);
	found = archive.list_state == LIST_READY ? map_find(name, version) : -1;
	if (found >= 0)
	{
		folders_set();
		/* (one under way still: stopped, the thread taking this one next) */
		if (archive.join.state == MAP_ARCHIVE_FETCH_RUNNING && !archive.join.wanted)
			archive.join.cancel = 1;
		archive.join.map = archive.maps[found];
		snprintf(archive.join.file_name, sizeof(archive.join.file_name), "%s", file_name);
		archive.join.state = MAP_ARCHIVE_FETCH_RUNNING;
		archive.join.wanted = 1;
		archive.join.done = 0;
		archive.join.error[0] = 0;
		archive.join.started_at = archive.join.now = system_milliseconds();
		/* (the screen's download under way stopped, and queued again) */
		if (archive.downloading >= 0)
			archive.requeue = 1;
		pthread_cond_signal(&archive_wake);
	}
	pthread_mutex_unlock(&archive_lock);
	return found >= 0;
}

enum map_archive_fetch_state map_archive_fetch_status(unsigned long long *done, unsigned long long *total,
	unsigned long *rate, uint8_t *share_hash, char *error, int error_size)
{
	enum map_archive_fetch_state state;

	pthread_mutex_lock(&archive_lock);
	state = archive.join.state;
	*done = archive.join.done;
	*total = archive.join.map.size;
	{
		unsigned long elapsed = archive.join.now - archive.join.started_at;

		*rate = elapsed > 500 ? (unsigned long)(archive.join.done * 1000 / elapsed) : 0;
	}
	if (share_hash)
		memcpy(share_hash, archive.join.map.share, MAP_ARCHIVE_HASH_BYTES);
	if (error && error_size > 0)
		snprintf(error, (size_t)error_size, "%s", archive.join.error);
	pthread_mutex_unlock(&archive_lock);
	return state;
}

void map_archive_fetch_end(void)
{
	pthread_mutex_lock(&archive_lock);
	if (archive.join.state == MAP_ARCHIVE_FETCH_RUNNING)
		archive.join.cancel = 1;
	archive.join.wanted = 0;
	archive.join.state = MAP_ARCHIVE_FETCH_NONE;
	pthread_mutex_unlock(&archive_lock);
}

/* ---------- the catalog and the pictures */

static int row_kind_of_chosen(int *map);
static enum row_kind row_kind(int place, int *map);

static int compare_entries(void const *a, void const *b)
{
	struct entry const *x = a, *y = b;
	int order = name_compare(x->name, y->name);

	return order ? order : x->version < y->version ? -1 : x->version > y->version;
}

/* (locked) each map's catalog entry, and its type from it */
static void catalog_link(void)
{
	int i;

	for (i = 0; i < archive.map_count; i++)
	{
		struct map *map = &archive.maps[i];
		struct entry key;
		struct entry const *found;

		memset(&key, 0, sizeof(key));
		key.name = map->name;
		key.version = (unsigned long)map->share[0] << 24 | (unsigned long)map->share[1] << 16 |
			(unsigned long)map->share[2] << 8 | map->share[3];
		found = archive.entry_count ? bsearch(&key, archive.entries, (size_t)archive.entry_count, sizeof(key),
			compare_entries) : NULL;
		map->entry = found ? (int)(found - archive.entries) : -1;
		if (found && found->type)
			map->category = found->type;
	}
}

/* the catalog's entries (text's fields cut in place: text is theirs from
then): their count, -1 when it is not a catalog */
static int catalog_parse(char *text, size_t size, struct entry **entries_out, unsigned long long *serial)
{
	size_t lines = 0, i;
	struct entry *entries;
	char *line = text, *end = text + size;
	int count = 0;

	*entries_out = NULL;
	*serial = 0;
	if (size < sizeof(CATALOG_FORMAT) - 1 || memcmp(text, CATALOG_FORMAT, sizeof(CATALOG_FORMAT) - 1))
		return -1;
	for (i = 0; i < size; i++)
		lines += text[i] == '\n';
	entries = calloc(lines + 1, sizeof(*entries));
	if (!entries)
		return -1;
	while (line < end)
	{
		char *stop = memchr(line, '\n', (size_t)(end - line));
		char *fields[11];
		int field = 0;
		char *at = line;

		if (!stop)
			stop = end;
		*stop = 0;
		if (stop > line && stop[-1] == '\r')
			stop[-1] = 0;
		if (!strncmp(line, "# serial ", 9))
			*serial = strtoull(line + 9, NULL, 10);
		if (*line && *line != '#')
		{
			while (field < 11)
			{
				char *tab = strchr(at, '\t');

				fields[field++] = at;
				if (!tab)
					break;
				*tab = 0;
				at = tab + 1;
			}
			if (field >= 10 && fields[0][0] && strlen(fields[1]) == 8)
			{
				struct entry *entry = &entries[count++];

				entry->name = fields[0];
				entry->version = strtoul(fields[1], NULL, 16);
				entry->type = strchr("SMAU", fields[2][0]) && fields[2][0] ? fields[2][0] : 0;
				entry->title = fields[3];
				entry->author = fields[4];
				entry->date = fields[5];
				entry->rating = fields[6];
				entry->category = fields[7];
				entry->picture = fields[8];
				entry->description = fields[9];
				entry->downloads = field > 10 ? fields[10] : "";
			}
		}
		line = stop + 1;
	}
	if (count)
		qsort(entries, (size_t)count, sizeof(*entries), compare_entries);
	*entries_out = entries;
	return count;
}

/* (thread) a catalog's text taken in place of the one in use: 1 if it was */
static int catalog_take(char *text, size_t size, char const *source)
{
	struct entry *entries;
	unsigned long long serial;
	int count = catalog_parse(text, size, &entries, &serial), i;

	if (count <= 0)
	{
		free(entries);
		platform_log("map archive: the catalog from %s is not one", source);
		return 0;
	}
	pthread_mutex_lock(&archive_lock);
	free(archive.catalog_text);
	free(archive.entries);
	archive.catalog_text = text;
	archive.entries = entries;
	archive.entry_count = count;
	archive.catalog_serial = serial;
	/* (the pictures kept are the old entries') */
	for (i = 0; i < PICTURES_KEPT; i++)
		archive.pictures[i].entry = -1;
	archive.picture_want = -1;
	catalog_link();
	shown_build();
	archive.moved = 1;
	pthread_mutex_unlock(&archive_lock);
	platform_log("map archive: the catalog from %s is in use: serial %llu, %d maps", source, serial, count);
	return 1;
}

/* (thread) the catalog kept, then the community server's when its serial
is another */
static void catalog_load(void)
{
	char folder[PATH_SIZE], path[PATH_SIZE], server[512], url[640], error[200];
	unsigned char *data = NULL;
	size_t size = 0;
	unsigned long got = 0;
	int status = 0;
	unsigned long long serial;

	path_join(folder, sizeof(folder), archive.data_root, "map_archive");
	path_join(path, sizeof(path), folder, "catalog.txt");
	if (file_read(path, CATALOG_MAXIMUM, &data, &size) && !catalog_take((char *)data, size, "this device"))
		free(data);
	data = NULL;
	if (!community_net_address(server, (int)sizeof(server)))
		return;
	snprintf(url, sizeof(url), "%s/archive/catalog/serial", server);
	if (!steam_https_get(url, 64, &data, &got, &status, error, (int)sizeof(error)) || status != 200 || !data)
	{
		platform_log("map archive: no catalog serial from %s (%d %s)", server, status, error);
		free(data);
		return;
	}
	serial = strtoull((char const *)data, NULL, 10);
	free(data);
	data = NULL;
	pthread_mutex_lock(&archive_lock);
	if (serial && serial == archive.catalog_serial)
		serial = 0;
	pthread_mutex_unlock(&archive_lock);
	if (!serial)
		return;
	snprintf(url, sizeof(url), "%s/archive/catalog", server);
	if (!steam_https_get(url, CATALOG_MAXIMUM, &data, &got, &status, error, (int)sizeof(error)) || status != 200 || !data)
	{
		platform_log("map archive: no catalog from %s (%d %s)", server, status, error);
		free(data);
		return;
	}
	posix_make_directory(folder);
	if (!file_write(path, data, got))
		platform_log("map archive: the catalog could not be kept in %s", folder);
	if (!catalog_take((char *)data, got, "the community server"))
		free(data);
}

/* a picture's bytes fitted to PICTURE_SIDE square on black (malloc'd), or NULL */
static unsigned long *picture_decode(unsigned char const *data, unsigned long size)
{
	int width, height, components;
	unsigned char *rgba = stbi_load_from_memory(data, (int)size, &width, &height, &components, 4);
	unsigned long *pixels;
	long x, y, fitted_width, fitted_height, left, top;

	if (!rgba)
		return NULL;
	pixels = malloc(sizeof(*pixels) * PICTURE_SIDE * PICTURE_SIDE);
	if (!pixels || width <= 0 || height <= 0)
	{
		stbi_image_free(rgba);
		free(pixels);
		return NULL;
	}
	/* (fitted into a 4:3 frame, which the square is stretched to: the
	picture's widget is 4:3) */
	if ((long)width * 3 >= (long)height * 4)
	{
		fitted_width = PICTURE_SIDE;
		fitted_height = (long)height * 4 * PICTURE_SIDE / ((long)width * 3);
	}
	else
	{
		fitted_height = PICTURE_SIDE;
		fitted_width = (long)width * 3 * PICTURE_SIDE / ((long)height * 4);
	}
	left = (PICTURE_SIDE - fitted_width) / 2;
	top = (PICTURE_SIDE - fitted_height) / 2;
	for (y = 0; y < PICTURE_SIDE; y++)
	{
		for (x = 0; x < PICTURE_SIDE; x++)
		{
			unsigned long pixel = 0xFF000000UL;

			if (x >= left && x < left + fitted_width && y >= top && y < top + fitted_height && fitted_width &&
				fitted_height)
			{
				long source_x = (x - left) * width / fitted_width;
				long source_y = (y - top) * height / fitted_height;
				unsigned char const *texel = rgba + 4 * ((size_t)source_y * (size_t)width + (size_t)source_x);

				pixel = 0xFF000000UL | (unsigned long)texel[0] << 16 | (unsigned long)texel[1] << 8 | texel[2];
			}
			pixels[y * PICTURE_SIDE + x] = pixel;
		}
	}
	stbi_image_free(rgba);
	return pixels;
}

static void picture_sleep(unsigned long milliseconds)
{
	struct timespec wait = { (time_t)(milliseconds / 1000), (long)(milliseconds % 1000) * 1000000L };

	nanosleep(&wait, NULL);
}

/* (locked) the kept picture of an entry, or NULL */
static struct picture *picture_find(int entry)
{
	int i;

	for (i = 0; i < PICTURES_KEPT; i++)
		if (archive.pictures[i].entry == entry && entry >= 0 && archive.pictures[i].pixels)
			return &archive.pictures[i];
	return NULL;
}

/* (locked) the next picture to fetch ahead: the nearest row to the chosen
one, within PICTURES_AHEAD, whose map has a picture not kept; -1 when none
(or the screen is closed) */
static int picture_ahead(void)
{
	int distance;

	if (!archive.opened || archive.list_state != LIST_READY)
		return -1;
	for (distance = 1; distance <= PICTURES_AHEAD; distance++)
	{
		int side;

		for (side = 0; side < 2; side++)
		{
			int map, entry;

			if (row_kind(archive.chosen + (side ? -distance : distance), &map) != ROW_MAP)
				continue;
			entry = archive.maps[map].entry;
			if (entry >= 0 && entry < archive.entry_count && archive.entries[entry].picture[0] && !picture_find(entry))
				return entry;
		}
	}
	return -1;
}

/* the chosen rows' pictures, from FractumSeraph's server over one kept
HTTP connection (a picture is no secret; a TLS handshake each took most
of a second) */
static void *picture_thread(void *unused)
{
	struct steam_http http;

	(void)unused;
	steam_http_init(&http);
	pthread_mutex_lock(&archive_lock);
	for (;;)
	{
		char path[300];
		char const *url;
		int entry, i;
		unsigned char *body = NULL;
		unsigned long size = 0;
		int status = 0;
		char error[200];
		unsigned long *pixels = NULL;
		struct picture *slot;
		int fetched_ahead;

		while (archive.picture_want < 0)
		{
			int ahead = picture_ahead();

			if (ahead >= 0)
			{
				archive.picture_want = ahead;
				archive.picture_want_at = 0;
				break;
			}
			pthread_cond_wait(&picture_wake, &archive_lock);
		}
		if (archive.picture_want_at && system_milliseconds() - archive.picture_want_at < PICTURE_DELAY)
		{
			pthread_mutex_unlock(&archive_lock);
			picture_sleep(30);
			pthread_mutex_lock(&archive_lock);
			continue;
		}
		entry = archive.picture_want;
		fetched_ahead = !archive.picture_want_at;
		archive.picture_want = -1;
		if (entry >= archive.entry_count || picture_find(entry))
			continue;
		/* (its path on his server: the catalog's URLs are his) */
		url = archive.entries[entry].picture;
		path[0] = 0;
		if (!strncmp(url, "https://" PICTURES_HOST "/", sizeof("https://" PICTURES_HOST)))
			snprintf(path, sizeof(path), "%s", url + sizeof("https://" PICTURES_HOST) - 1);
		else if (!strncmp(url, "http://" PICTURES_HOST "/", sizeof("http://" PICTURES_HOST)))
			snprintf(path, sizeof(path), "%s", url + sizeof("http://" PICTURES_HOST) - 1);
		if (!path[0] || strchr(path, ' '))
			continue;
		pthread_mutex_unlock(&archive_lock);
		if (steam_http_get(&http, PICTURES_HOST, path, PICTURE_MAXIMUM, &body, &size, &status, error,
			(int)sizeof(error)) && status == 200 && body)
			pixels = picture_decode(body, size);
		else
			platform_log("map archive: no picture %s (%d %s)", path, status, error);
		free(body);
		pthread_mutex_lock(&archive_lock);
		if (!pixels)
			continue;
		/* (in the place of the one shown longest ago) */
		slot = &archive.pictures[0];
		for (i = 1; i < PICTURES_KEPT; i++)
			if (archive.pictures[i].used_at < slot->used_at)
				slot = &archive.pictures[i];
		free(slot->pixels);
		slot->pixels = pixels;
		slot->entry = entry;
		/* (one fetched ahead counts as shown a moment ago: the chosen row's
		are the last to go) */
		slot->used_at = (system_milliseconds() - (fetched_ahead ? 1000 : 0)) | 1;
		slot->serial = ++archive.picture_serials;
	}
	return NULL;
}

/* (locked) the entry's picture asked for, if it has one not kept; the
thread started the first time */
static void picture_want(int entry)
{
	struct picture *kept;

	if (entry < 0 || entry >= archive.entry_count || !archive.entries[entry].picture[0])
		return;
	kept = picture_find(entry);
	if (kept)
	{
		kept->used_at = system_milliseconds() | 1;
		return;
	}
	if (archive.picture_want == entry)
		return;
	archive.picture_want = entry;
	archive.picture_want_at = system_milliseconds();
	if (!archive.pictures_started)
	{
		pthread_t thread;

		archive.pictures_started = 1;
		if (pthread_create(&thread, NULL, picture_thread, NULL) == 0)
			pthread_detach(thread);
	}
	pthread_cond_signal(&picture_wake);
}

int map_archive_picture(unsigned *serial, unsigned long *pixels)
{
	struct picture const *kept = NULL;
	int map;

	pthread_mutex_lock(&archive_lock);
	if (archive.list_state == LIST_READY && row_kind_of_chosen(&map) && archive.maps[map].entry >= 0)
		kept = picture_find(archive.maps[map].entry);
	*serial = kept ? kept->serial : 0;
	if (kept && pixels)
		memcpy(pixels, kept->pixels, sizeof(*pixels) * PICTURE_SIDE * PICTURE_SIDE);
	pthread_mutex_unlock(&archive_lock);
	return kept ? PICTURE_SIDE : 0;
}

/* ---------- BROWSE MAPS */

void map_archive_open(void)
{
	map_archive_start();
	pthread_mutex_lock(&archive_lock);
	folders_set();
	archive.want_scan = 1;
	archive.message[0] = 0;
	archive.moved = 1;
	archive.opened = 1;
	pthread_cond_signal(&archive_wake);
	pthread_mutex_unlock(&archive_lock);
}

void map_archive_close(void)
{
	pthread_mutex_lock(&archive_lock);
	archive.want_search = 0;
	archive.opened = 0;
	pthread_mutex_unlock(&archive_lock);
}

int map_archive_try_lock(void)
{
	return pthread_mutex_trylock(&archive_lock) == 0;
}

void map_archive_unlock(void)
{
	pthread_mutex_unlock(&archive_lock);
}

static int page_count(void)
{
	int from = (archive.page - 1) * PAGE_MAPS;
	int left = archive.shown_count - from;

	return left < 0 ? 0 : left > PAGE_MAPS ? PAGE_MAPS : left;
}

static int pages(void)
{
	return archive.shown_count ? (archive.shown_count + PAGE_MAPS - 1) / PAGE_MAPS : 1;
}

static enum row_kind row_kind(int place, int *map)
{
	int on_page;

	*map = -1;
	if (place < 0)
		return ROW_NONE;
	if (archive.list_state != LIST_READY)
		return place == 0 ? ROW_LIST : ROW_NONE;
	switch (place)
	{
	case 0:
		return ROW_SEARCH;
	case 1:
		return ROW_TYPE;
	case 2:
		return ROW_SORT;
	case 3:
		return ROW_ORDER;
	case 4:
		return ROW_SHOW;
	}
	place -= FILTER_ROWS;
	on_page = page_count();
	if (place < on_page)
	{
		*map = archive.shown[(archive.page - 1) * PAGE_MAPS + place];
		return ROW_MAP;
	}
	place -= on_page;
	if (archive.page > 1 && place-- == 0)
		return ROW_PREVIOUS;
	if (archive.page * PAGE_MAPS < archive.shown_count && place == 0)
		return ROW_NEXT;
	return ROW_NONE;
}

/* (locked) the chosen row's map, if it is one */
static int row_kind_of_chosen(int *map)
{
	return row_kind(archive.chosen, map) == ROW_MAP;
}

int map_archive_count(void)
{
	if (archive.list_state != LIST_READY)
		return 1;
	return FILTER_ROWS + page_count() + (archive.page > 1) + (archive.page * PAGE_MAPS < archive.shown_count);
}

short *map_archive_first(void)
{
	return &archive.first;
}

short *map_archive_chosen(void)
{
	return &archive.chosen;
}

static int queued_at(int map)
{
	int i;

	for (i = 0; i < archive.queue_count; i++)
		if (archive.queue[i] == map)
			return i;
	return -1;
}

static void percent_text(char *text, int size)
{
	unsigned long long total = archive.downloading >= 0 ? archive.maps[archive.downloading].size : 0;

	snprintf(text, (size_t)size, "%d%%", total ? (int)(archive.downloaded * 100 / total) : 0);
}

void map_archive_row(int place, char *name, int name_size, char *state, int state_size)
{
	int map;

	name[0] = state[0] = 0;
	switch (row_kind(place, &map))
	{
	case ROW_LIST:
		snprintf(name, (size_t)name_size, "MAP LIST");
		snprintf(state, (size_t)state_size, "%s", archive.list_state == LIST_FAILED ? "TRY AGAIN" : "LOADING");
		break;
	case ROW_SEARCH:
		snprintf(name, (size_t)name_size, "SEARCH");
		snprintf(state, (size_t)state_size, "%.15s", archive.search[0] ? archive.search : "ANY");
		break;
	case ROW_TYPE:
		snprintf(name, (size_t)name_size, "TYPE");
		snprintf(state, (size_t)state_size, "%s", type_names[archive.type]);
		break;
	case ROW_SORT:
		snprintf(name, (size_t)name_size, "SORT");
		snprintf(state, (size_t)state_size, "%s", sort_names[archive.sort]);
		break;
	case ROW_ORDER:
		snprintf(name, (size_t)name_size, "ORDER");
		snprintf(state, (size_t)state_size, "%s", archive.descending ? "DESCENDING" : "ASCENDING");
		break;
	case ROW_SHOW:
		snprintf(name, (size_t)name_size, "SHOW");
		snprintf(state, (size_t)state_size, "%s", show_names[archive.show]);
		break;
	case ROW_MAP:
	{
		struct map const *entry = &archive.maps[map];

		display_name(entry, name, name_size);
		if (archive.downloading == map)
			percent_text(state, state_size);
		else if (queued_at(map) >= 0)
			snprintf(state, (size_t)state_size, "QUEUED");
		else if (entry->installed)
			snprintf(state, (size_t)state_size, "INSTALLED");
		else
			size_text(entry->size, state, state_size);
		break;
	}
	case ROW_PREVIOUS:
		snprintf(name, (size_t)name_size, "PREVIOUS PAGE");
		snprintf(state, (size_t)state_size, "%d OF %d", archive.page - 1, pages());
		break;
	case ROW_NEXT:
		snprintf(name, (size_t)name_size, "NEXT PAGE");
		snprintf(state, (size_t)state_size, "%d OF %d", archive.page + 1, pages());
		break;
	default:
		break;
	}
}

void map_archive_details(int place, char *title, int title_size, char *text, int text_size)
{
	char body[600];
	int map;
	size_t used;

	title[0] = body[0] = 0;
	switch (row_kind(place, &map))
	{
	case ROW_LIST:
		snprintf(title, (size_t)title_size, "Map list");
		if (archive.list_state == LIST_FAILED)
			snprintf(body, sizeof(body), "The list of maps could not be had: %s. A tries again.", archive.list_error);
		else
			snprintf(body, sizeof(body), "Getting the list of maps from FractumSeraph's archive...");
		break;
	case ROW_SEARCH:
		snprintf(title, (size_t)title_size, "Search");
		snprintf(body, sizeof(body), "A types a part of a map's name.\n%d of %d maps shown, page %d of %d.",
			archive.shown_count, archive.map_count, archive.page, pages());
		break;
	case ROW_TYPE:
		snprintf(title, (size_t)title_size, "Type");
		snprintf(body, sizeof(body), "A shows every map, or only singleplayer maps, multiplayer maps, or "
			"multiplayer maps with AI.%s", archive.category_count || archive.entry_count ? "" :
			"\nThis build has no categories file: every map shows.");
		break;
	case ROW_SORT:
		snprintf(title, (size_t)title_size, "Sort");
		snprintf(body, sizeof(body), "A sorts the maps by name, rating, downloads, date or size. Maps "
			"their page gives no rating, downloads or date for come last.");
		break;
	case ROW_ORDER:
		snprintf(title, (size_t)title_size, "Order");
		snprintf(body, sizeof(body), "A turns the sort around: ascending (A to Z, lowest, oldest, smallest "
			"first) or descending.");
		break;
	case ROW_SHOW:
		snprintf(title, (size_t)title_size, "Show");
		snprintf(body, sizeof(body), "A shows every map, those you have, or those you have not. A map you have is "
			"one of its name in custom_maps or its folders.");
		break;
	case ROW_MAP:
	{
		struct map const *entry = &archive.maps[map];
		char size[24];

		struct entry const *details = entry->entry >= 0 && entry->entry < archive.entry_count ?
			&archive.entries[entry->entry] : NULL;

		picture_want(entry->entry);
		display_name(entry, title, title_size);
		size_text(entry->size, size, (int)sizeof(size));
		if (details && details->title[0])
		{
			used = (size_t)snprintf(body, sizeof(body), "%s%s%s%s%s\n%s%s%s%s%s%s, %s.\n", details->title,
				details->author[0] ? " by " : "", details->author, details->date[0] ? ", " : "", details->date,
				details->category[0] ? details->category : category_name(entry->category),
				details->rating[0] ? ", rated " : "", details->rating,
				details->downloads[0] ? ", " : "", details->downloads, details->downloads[0] ? " downloads" : "", size);
		}
		else
			used = (size_t)snprintf(body, sizeof(body), "%s\n%s, version %08lX.\n", category_name(entry->category),
				size, entry->version);
		if (archive.downloading == map)
			snprintf(body + used, sizeof(body) - used, "Downloading: A cancels.");
		else if (queued_at(map) >= 0)
			snprintf(body + used, sizeof(body) - used, "Queued: A takes it out.");
		else if (entry->installed)
			snprintf(body + used, sizeof(body) - used, "Installed.");
		else
			snprintf(body + used, sizeof(body) - used, "A downloads it.");
		/* (what its page says of it; its "\\n"s, line breaks) */
		if (details && details->description[0])
		{
			char const *at = details->description;

			used = strlen(body);
			if (used + 2 < sizeof(body))
			{
				body[used++] = '\n';
				while (*at && used + 1 < sizeof(body))
				{
					if (at[0] == '\\' && at[1] == 'n')
					{
						body[used++] = '\n';
						at += 2;
						while (*at == ' ')
							at++;
						continue;
					}
					body[used++] = *at++;
				}
				body[used] = 0;
			}
		}
		break;
	}
	case ROW_PREVIOUS:
	case ROW_NEXT:
		snprintf(title, (size_t)title_size, "%d maps", archive.shown_count);
		snprintf(body, sizeof(body), "Page %d of %d.", archive.page, pages());
		break;
	default:
		break;
	}
	text[0] = 0;
	if (archive.message[0])
		snprintf(text, (size_t)text_size, "%s\n", archive.message);
	used = strlen(text);
	snprintf(text + used, (size_t)text_size - used, "%s", body);
	if (archive.downloading >= 0 && map != archive.downloading)
	{
		char shown[NAME_SIZE], percent[8];

		display_name(&archive.maps[archive.downloading], shown, (int)sizeof(shown));
		percent_text(percent, (int)sizeof(percent));
		used = strlen(text);
		snprintf(text + used, (size_t)text_size - used, "\nDownloading %s: %s%s", shown, percent,
			archive.queue_count ? "" : ".");
		if (archive.queue_count)
		{
			used = strlen(text);
			snprintf(text + used, (size_t)text_size - used, ", %d more queued.", archive.queue_count);
		}
	}
}

int map_archive_take_search(char *initial, int initial_size)
{
	if (!archive.want_search)
		return 0;
	archive.want_search = 0;
	snprintf(initial, (size_t)initial_size, "%s", archive.search);
	return 1;
}

int map_archive_take_rescan(void)
{
	int rescan = archive.rescan;

	archive.rescan = 0;
	return rescan;
}

int map_archive_take_moved(void)
{
	int moved = archive.moved;

	archive.moved = 0;
	return moved;
}

/* (locked) the rows changed: the first map chosen, the list at its top */
static void rows_moved(int chosen)
{
	archive.chosen = (short)chosen;
	archive.first = 0;
	archive.moved = 1;
}

void map_archive_choose(int place)
{
	int map;

	pthread_mutex_lock(&archive_lock);
	archive.message[0] = 0;
	switch (row_kind(place, &map))
	{
	case ROW_LIST:
		if (archive.list_state == LIST_FAILED)
		{
			archive.want_list = 1;
			archive.list_state = LIST_LOADING;
		}
		break;
	case ROW_SEARCH:
		archive.want_search = 1;
		break;
	case ROW_TYPE:
		archive.type = (enum type)((archive.type + 1) % TYPE_COUNT);
		archive.page = 1;
		shown_build();
		break;
	case ROW_SORT:
		archive.sort = (enum sort)((archive.sort + 1) % SORT_COUNT);
		/* (names A to Z; ratings, downloads, dates and sizes highest first) */
		archive.descending = archive.sort != SORT_NAME;
		archive.page = 1;
		shown_build();
		break;
	case ROW_ORDER:
		archive.descending = !archive.descending;
		archive.page = 1;
		shown_build();
		break;
	case ROW_SHOW:
		archive.show = (enum show)((archive.show + 1) % SHOW_COUNT);
		archive.page = 1;
		shown_build();
		break;
	case ROW_MAP:
	{
		int at = queued_at(map);

		if (archive.downloading == map)
			archive.cancel = 1;
		else if (at >= 0)
		{
			memmove(archive.queue + at, archive.queue + at + 1, (size_t)(archive.queue_count - at - 1) * sizeof(int));
			archive.queue_count--;
			snprintf(archive.message, sizeof(archive.message), "Taken out of the queue.");
		}
		else if (archive.maps[map].installed)
			snprintf(archive.message, sizeof(archive.message), "You have this map already.");
		else if (archive.queue_count == QUEUE_SIZE)
			snprintf(archive.message, sizeof(archive.message), "The queue is full: wait for some to finish.");
		else
		{
			archive.queue[archive.queue_count++] = map;
			if (archive.downloading >= 0)
				snprintf(archive.message, sizeof(archive.message), "Queued (%d waiting).", archive.queue_count);
		}
		break;
	}
	case ROW_PREVIOUS:
		archive.page--;
		rows_moved(FILTER_ROWS);
		break;
	case ROW_NEXT:
		archive.page++;
		rows_moved(FILTER_ROWS);
		break;
	default:
		break;
	}
	pthread_cond_signal(&archive_wake);
	pthread_mutex_unlock(&archive_lock);
}

int map_archive_back(void)
{
	int taken = 0;

	pthread_mutex_lock(&archive_lock);
	/* (the screen is not left while maps are coming: the queue would go on
	with no screen to show it) */
	if (archive.downloading >= 0 || archive.queue_count)
	{
		int going = (archive.downloading >= 0) + archive.queue_count;

		snprintf(archive.message, sizeof(archive.message), "%d download%s going: wait, or cancel %s, to leave.",
			going, going == 1 ? "" : "s", going == 1 ? "it" : "them");
		taken = 1;
	}
	pthread_mutex_unlock(&archive_lock);
	return taken;
}

void map_archive_search(char const *text)
{
	pthread_mutex_lock(&archive_lock);
	snprintf(archive.search, sizeof(archive.search), "%s", text);
	archive.page = 1;
	shown_build();
	rows_moved(archive.shown_count ? FILTER_ROWS : 0);
	pthread_mutex_unlock(&archive_lock);
}
