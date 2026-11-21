/*
MAP_ARCHIVE.H

FractumSeraph's map archive (docs/map_archive.md): the Custom Edition maps of
his approved map list, browsed and downloaded into custom_maps (Mods > CE
MAPS > BROWSE MAPS), and the map a host's game is on fetched from it before
the host is asked (map_share.c). Ported from SlipstreamCE's
fractum_archive.c (thelinkin3000), which ran in the Android host alone: here
it is the game's, on every native build. The browser build has no way to
its plain-HTTP web seed's downloads yet: map_archive_available says so.

- The list: map_torrents.txt from the map-index branch of his fork
  (github.com/FractumSeraph/OpenCE), taken only when its signature
  (map_torrents.txt.sig: Ed25519 by his release or recovery key, over
  "fractum-halo-map-index 1\n" and the list's bytes) checks. A good one is
  kept in <data root>/map_archive and used at the next start until GitHub's
  is newer (its "# serial"). Each map's share: hash is the one map sharing
  makes (map_share_files.c), so its first 32 bits are the version a host's
  game record says: a joiner finds the host's map in it by name and version.
- The categories: map_categories.txt beside config.toml (a game data file,
  port/assets/map_archive, written by tools/map_archive_categories.py), a
  letter for each map's name and version: S solo, M multiplayer, A
  multiplayer with AI, U the user interface's.
- Installed: a map whose name (less ".map", or less a second version's
  _<8 digits>) is a file's in custom_maps or one of its folders.
- A download: http://halomaps.fractumseraph.net/maps/<name>, written to
  custom_maps/<file>.part while its BLAKE2b is taken, kept only when its size
  and BLAKE2b are the list's, renamed into place and named in
  custom_maps/downloaded_maps.txt as map sharing's downloads are (their
  scripts held to tighter rules). One at a time: a joiner's goes first.

A thread of its own does the work; every call here takes the lock briefly
and never waits on the network or the disk.
*/

#ifndef MAP_ARCHIVE_H
#define MAP_ARCHIVE_H

#include <stdint.h>

enum
{
	MAP_ARCHIVE_HASH_BYTES = 32,
	/* a map's file name: the game's 31 characters, a second version's
	_<8 digits> and ".map" */
	MAP_ARCHIVE_NAME_SIZE = 48,
	/* the chosen map's picture: a square of this side (map_archive_picture) */
	MAP_ARCHIVE_PICTURE_SIDE = 256
};

/* whether this build can reach the archive at all */
int map_archive_available(void);

/* the thread started and the list asked for (the kept copy, then GitHub's),
if they are not: the screen's opening, and a joiner's lookup, start it */
void map_archive_start(void);

/* ---------- a joiner's map (map_share.c, the game's thread) */

enum map_archive_lookup
{
	/* the list is not read yet (or GitHub's is still coming) */
	MAP_ARCHIVE_LOOKUP_WAIT,
	MAP_ARCHIVE_LOOKUP_MISSING,
	MAP_ARCHIVE_LOOKUP_FOUND
};

/* the map of that name (a file name less ".map", whatever its case) and
version (map_share_hash_version's) in the list: FOUND with its size */
enum map_archive_lookup map_archive_lookup(char const *name, unsigned long version, unsigned long long *size);

/* its download started ahead of any other (one of the screen's stopped and
queued again), into custom_maps/<file_name>: 1 if it is */
int map_archive_fetch(char const *name, unsigned long version, char const *file_name);

enum map_archive_fetch_state
{
	MAP_ARCHIVE_FETCH_NONE,
	MAP_ARCHIVE_FETCH_RUNNING,
	MAP_ARCHIVE_FETCH_DONE,
	MAP_ARCHIVE_FETCH_FAILED
};

/* the joiner's download: its bytes so far and in all, bytes a second, and
once DONE its share hash (map sharing's, so it is not read again), once
FAILED why */
enum map_archive_fetch_state map_archive_fetch_status(unsigned long long *done, unsigned long long *total,
	unsigned long *rate, uint8_t *share_hash, char *error, int error_size);
/* ... let go: stopped if it runs, forgotten if it is over */
void map_archive_fetch_end(void);

/* ---------- BROWSE MAPS (menu_functions.c's "port archive" functions, the
game's thread) */

void map_archive_open(void);
void map_archive_close(void);

/* takes the lock for the frame's rows: 0 if a thread holds it (the last
frame's then stand) */
int map_archive_try_lock(void);
void map_archive_unlock(void);
/* (locked) */
int map_archive_count(void);
void map_archive_row(int place, char *name, int name_size, char *state, int state_size);
void map_archive_details(int place, char *title, int title_size, char *text, int text_size);
/* (locked) the first row shown and the row chosen, the screen's to move */
short *map_archive_first(void);
short *map_archive_chosen(void);
/* (locked) the search's text once, when the keyboard is wanted: 1 then */
int map_archive_take_search(char *initial, int initial_size);
/* (locked) whether a map was put in custom_maps since the last call (the
map lists look again) */
int map_archive_take_rescan(void);
/* (locked) whether the rows changed under the screen (a page turned, a
filter): it focuses the chosen row again */
int map_archive_take_moved(void);

/* (any thread; takes the lock) the chosen map's first screenshot, as its
archive page names it (the community server's /archive/map), fitted on
black: its side, copied into pixels (MAP_ARCHIVE_PICTURE_SIDE squared, ARGB)
if not NULL, else 0 while there is none; serial changes with each picture */
int map_archive_picture(unsigned *serial, unsigned long *pixels);

/* A on a row; B (1 if it was taken: the screen stays); the search typed */
void map_archive_choose(int place);
int map_archive_back(void);
void map_archive_search(char const *text);

#endif
