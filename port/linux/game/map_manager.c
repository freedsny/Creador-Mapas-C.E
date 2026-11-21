/*
MAP_MANAGER.C

The Map Manager's lists and what their rows do (map_manager.h), of one map.

A map is known by its file name (the level name custom_maps\<name> finds it
wherever it is: custom_edition_maps_display_index), never by its display
index, which a scan of the folders changes. Each change to the folder
(custom_edition_maps_look_again) has the menus scan it again when they next
ask for its maps.

A map moved takes its picture and description with it (<name>.bmp and
<name>.txt beside it); one deleted, them and its shots (map_pictures.h).
Its description is written as the lines shown (each typed on the game's
keyboard, which holds 31 characters), with Windows' line ends.
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/errors.h"
#include "math/real_math.h"
#include "bitmaps/bitmaps.h"
#include "interface/virtual_keyboard.h"
#include "text/unicode.h"
#include "cache_file_formats.h"
#include "custom_edition_cache.h"
#include "custom_edition_maps.h"
#include "map_share.h"
#include "map_pictures.h"
#include "map_manager.h"
#include "network_replay.h"
#include "../src/map_archive.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xtl.h>

/* ---------- constants */

#define MAP_MANAGER_PATH_SIZE 256
/* the replays of a map looked through (the newest) */
#define MAP_MANAGER_REPLAYS 64
/* a description's lines, and a line's characters (the keyboard's room) */
#define DESCRIPTION_LINES 6
#define DESCRIPTION_LINE_LENGTH 31
/* how long the last thing done is said (milliseconds) */
#define MESSAGE_MILLISECONDS 6000
/* ... and what an import did (map_manager_say) */
#define REPORT_MILLISECONDS 20000
/* the rows a list has at most: every map, every folder, and an action */
#define MAXIMUM_ROWS (CUSTOM_EDITION_MAPS_MAXIMUM + 1024 + CUSTOM_EDITION_MAP_FOLDERS_MAXIMUM + 8)
/* the keyboard's caption (virtual_keyboard.c's first: a name) */
#define KEYBOARD_CAPTION 8

/* the lists (map_manager.h) */
enum
{
	_list_map,
	_list_description,
	_list_move,
	_list_picture,
	_list_theater,
	_list_compare,
	/* (Mods > CUSTOM EDITION MAPS: map_manager_open_library) */
	_list_library,
	_list_manage,
	_list_folder,
	NUMBER_OF_LISTS
};

/* the resource maps the library's SHARED FILES looks for, and their files */
static short const shared_file_types[] = { _resource_map_bitmaps, _resource_map_sounds, _resource_map_locale };
static char const *const shared_file_names[] = { "bitmaps.map", "sounds.map", "loc.map" };
#define SHARED_FILE_COUNT ((short)NUMBEROF(shared_file_types))

/* a row's kind; its value is a folder's index, a line's, a shot's or a
replay's */
enum
{
	_row_map_folder,
	_row_map_description,
	_row_map_picture,
	_row_delete_map,
	_row_line,
	_row_clear_description,
	_row_move_none,
	_row_move_folder,
	_row_move_new_folder,
	_row_picture_current,
	_row_picture_shot,
	_row_take_shots,
	_row_take_theater,
	_row_delete_picture,
	_row_explore,
	_row_replay,
	_row_keep_current,
	_row_keep_new,
	/* (the library's and a folder's: a map's value is its display index) */
	_row_shared_files,
	_row_import,
	_row_manage,
	/* (FractumSeraph's map archive: menu_functions.c opens its screen) */
	_row_browse,
	_row_new_folder,
	_row_folder,
	_row_library_map,
	_row_delete_folder,
};

/* what the keyboard was opened for */
enum
{
	_typing_none,
	_typing_move_folder,
	_typing_line,
	/* (the library's NEW FOLDER: made, nothing moved) */
	_typing_new_folder,
};

/* ---------- structures */

struct manager_row
{
	short kind;
	short value;
};

/* ---------- prototypes */

unsigned long system_milliseconds(void);
/* (whether Custom Edition maps can run here at all: platform.h's) */
void *halo_custom_edition_tag_cache(void);

/* ---------- globals */

static struct
{
	short list;
	short first[NUMBER_OF_LISTS];
	short chosen[NUMBER_OF_LISTS];
	/* the map the lists are of (its file name) */
	char map[CUSTOM_EDITION_MAP_NAME_SIZE];
	/* the list shown's rows */
	struct manager_row rows[MAXIMUM_ROWS];
	short count;
	/* a row asked about once (A again does it): its list and kind, NONE for
	none */
	short confirming_list;
	short confirming_kind;
	short confirming_value;
	/* the keyboard: what for, the line, and what is typed */
	short typing;
	short typing_line;
	wchar_t typed[DESCRIPTION_LINE_LENGTH + 1];
	/* the description's lines, as DESCRIPTION shows them */
	char lines[DESCRIPTION_LINES][DESCRIPTION_LINE_LENGTH + 1];
	/* which of the map's shots are there (map_pictures_shot_path's) */
	boolean shots[NUMBER_OF_MAP_PICTURE_SHOTS];
	/* the map's replays (THEATER), the newest first */
	char replays[MAP_MANAGER_REPLAYS][NETWORK_REPLAY_NAME_SIZE];
	struct network_replay_info infos[MAP_MANAGER_REPLAYS];
	short replay_count;
	/* the last thing done, and when, and how long it is said */
	char message[512];
	unsigned long message_time;
	unsigned long message_milliseconds;
	/* the picture over the details: the file wanted, the one read and the
	files' changes it was read at (each change counted) */
	char picture_wanted[MAP_MANAGER_PATH_SIZE];
	char picture_read[MAP_MANAGER_PATH_SIZE];
	unsigned long picture_changes;
	unsigned long picture_read_changes;
	struct bitmap_data *picture;
	/* the map the screen opens at next, and whether at its PICTURE list
	(map_manager_open_at) */
	char open_at[CUSTOM_EDITION_MAP_NAME_SIZE];
	boolean open_at_pictures;
	boolean open_at_choosing;
	/* the shot COMPARE weighs against the map's picture */
	short compare_shot;
	/* opened at the library (map_manager_open_library), and what it has;
	the folder FOLDER lists; the maps of each of custom_maps' folders, as
	the rows were last built */
	boolean library;
	boolean open_library;
	boolean can_import;
	char folder[CUSTOM_EDITION_MAP_CATEGORY_LENGTH];
	short folder_maps[CUSTOM_EDITION_MAP_FOLDERS_MAXIMUM];
	/* which of the shared files are there (looked at as the library opens,
	and at A on SHARED FILES) */
	boolean shared[NUMBEROF(shared_file_types)];
} manager;

/* ---------- private code */

static void message_set(
	char const *format,
	...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(manager.message, sizeof(manager.message), format, arguments);
	va_end(arguments);
	manager.message_time = system_milliseconds();
	manager.message_milliseconds = MESSAGE_MILLISECONDS;
}

/* the menus' wide text (a map's name: ASCII) as UTF-8's */
static void narrow_text(
	wchar_t const *wide,
	char *text,
	long size)
{
	long index;

	for (index = 0; wide && wide[index] && index < size - 1; index++)
		text[index] = wide[index] < 0x80 ? (char)wide[index] : '?';
	text[index] = 0;
}

/* the display index and files of the map the lists are of; FALSE when it
is not there any more */
static boolean map_get(
	short *display_index,
	struct custom_edition_map_file *file)
{
	char level_name[CUSTOM_EDITION_MAP_NAME_SIZE + 16];

	if (!manager.map[0])
		return FALSE;
	csprintf(level_name, "%s%s", CUSTOM_EDITION_LEVEL_NAME_PREFIX, manager.map);
	*display_index = custom_edition_maps_display_index(level_name);
	return *display_index != NONE && custom_edition_maps_file_get(*display_index, file) && file->managed &&
		!csstrcasecmp(file->name, manager.map);
}

/* <custom_maps><folder\><name><extension> in path; FALSE if it does not fit */
static boolean path_in_folder(
	char const *folder,
	char const *name,
	char const *extension,
	char *path,
	long size)
{
	long length = csstrlen(CUSTOM_EDITION_MAP_DIRECTORY) + csstrlen(name) + csstrlen(extension) +
		(folder[0] ? csstrlen(folder) + 1 : 0);

	if (length >= size)
		return FALSE;
	if (folder[0])
		csprintf(path, "%s%s\\%s%s", CUSTOM_EDITION_MAP_DIRECTORY, folder, name, extension);
	else
		csprintf(path, "%s%s%s", CUSTOM_EDITION_MAP_DIRECTORY, name, extension);
	return TRUE;
}

static boolean file_present(
	char const *path)
{
	FILE *file = fopen(path, "rb");

	if (!file)
		return FALSE;
	fclose(file);
	return TRUE;
}

/* whether the folder's files may be changed: not while a Custom Edition
map is loaded (said) */
static boolean files_free(
	void)
{
	if (custom_edition_cache_loaded_path())
	{
		message_set("Nothing can be changed while a Custom Edition map is loaded.");
		return FALSE;
	}
	return TRUE;
}

/* the folder changed: the menus scan it again, and the picture is read again */
static void files_changed(
	void)
{
	custom_edition_maps_look_again();
	manager.picture_changes++;
}

static void row_add(
	short kind,
	short value)
{
	if (manager.count < MAXIMUM_ROWS)
	{
		manager.rows[manager.count].kind = kind;
		manager.rows[manager.count].value = value;
		manager.count++;
	}
}

/* the managed maps (custom_maps', not a Custom Edition install's) in a
folder of custom_maps ("" its top), multiplayer then campaign, each sorted
by name, as rows */
static void library_maps_add(
	char const *folder)
{
	struct custom_edition_map_file file;
	short campaign;
	short index;

	for (campaign = 0; campaign < 2; campaign++)
	{
		for (index = 0; index < custom_edition_maps_count((boolean)campaign); index++)
		{
			short display_index = custom_edition_maps_display_index_of((boolean)campaign, index);

			if (custom_edition_maps_file_get(display_index, &file) && file.managed &&
				!csstrcasecmp(file.category, folder))
			{
				row_add(_row_library_map, display_index);
			}
		}
	}
}

/* how many managed maps each of custom_maps' folders has */
static void folder_maps_count(
	void)
{
	struct custom_edition_map_file file;
	short folders = (short)MIN(custom_edition_maps_folder_count(), CUSTOM_EDITION_MAP_FOLDERS_MAXIMUM);
	short campaign;
	short index;
	short folder;

	csmemset(manager.folder_maps, 0, sizeof(manager.folder_maps));
	for (campaign = 0; campaign < 2; campaign++)
	{
		for (index = 0; index < custom_edition_maps_count((boolean)campaign); index++)
		{
			if (!custom_edition_maps_file_get(custom_edition_maps_display_index_of((boolean)campaign, index), &file) ||
				!file.managed || !file.category[0])
			{
				continue;
			}
			for (folder = 0; folder < folders; folder++)
			{
				if (!csstrcasecmp(custom_edition_maps_folder_name(folder), file.category))
				{
					manager.folder_maps[folder]++;
					break;
				}
			}
		}
	}
}

/* which of the shared files the maps folders have */
static void shared_look(
	void)
{
	char path[MAP_MANAGER_PATH_SIZE];
	short index;

	for (index = 0; index < SHARED_FILE_COUNT; index++)
		manager.shared[index] = custom_edition_resource_map_file_path(shared_file_types[index], path);
}

static short shared_missing(
	void)
{
	short index;
	short missing = 0;

	for (index = 0; index < SHARED_FILE_COUNT; index++)
		missing += !manager.shared[index];
	return missing;
}

/* the folder FOLDER lists, as the scan has it now (NONE: gone) */
static short folder_index(
	void)
{
	short index;

	for (index = 0; index < custom_edition_maps_folder_count(); index++)
	{
		if (!csstrcasecmp(custom_edition_maps_folder_name(index), manager.folder))
			return index;
	}
	return NONE;
}

/* the list shown's rows, as things are now */
static void rows_build(
	void)
{
	struct custom_edition_map_file file;
	short display_index;
	short index;

	manager.count = 0;
	switch (manager.list)
	{
	case _list_map:
		row_add(_row_map_folder, 0);
		row_add(_row_map_description, 0);
		row_add(_row_map_picture, 0);
		row_add(_row_delete_map, 0);
		break;
	case _list_description:
		for (index = 0; index < DESCRIPTION_LINES; index++)
			row_add(_row_line, index);
		if (map_get(&display_index, &file) && file.has_description)
			row_add(_row_clear_description, 0);
		break;
	case _list_move:
		row_add(_row_move_none, 0);
		for (index = 0; index < custom_edition_maps_folder_count(); index++)
			row_add(_row_move_folder, index);
		row_add(_row_move_new_folder, 0);
		break;
	case _list_picture:
		row_add(_row_picture_current, 0);
		for (index = 0; index < NUMBER_OF_MAP_PICTURE_SHOTS; index++)
			row_add(_row_picture_shot, index);
		row_add(_row_take_shots, 0);
		row_add(_row_take_theater, 0);
		if (map_get(&display_index, &file) && file.has_picture)
			row_add(_row_delete_picture, 0);
		break;
	case _list_theater:
		row_add(_row_explore, 0);
		for (index = 0; index < manager.replay_count; index++)
			row_add(_row_replay, index);
		break;
	case _list_compare:
		row_add(_row_keep_current, 0);
		row_add(_row_keep_new, manager.compare_shot);
		break;
	case _list_library:
		row_add(_row_shared_files, 0);
		if (manager.can_import)
			row_add(_row_import, 0);
		row_add(_row_manage, 0);
		if (map_archive_available())
			row_add(_row_browse, 0);
		break;
	case _list_manage:
		row_add(_row_new_folder, 0);
		folder_maps_count();
		for (index = 0; index < custom_edition_maps_folder_count() && index < CUSTOM_EDITION_MAP_FOLDERS_MAXIMUM; index++)
			row_add(_row_folder, index);
		library_maps_add("");
		break;
	case _list_folder:
		library_maps_add(manager.folder);
		row_add(_row_delete_folder, 0);
		break;
	}
}

/* the row chosen, or NULL */
static struct manager_row const *row_get(
	short row)
{
	return row >= 0 && row < manager.count ? &manager.rows[row] : NULL;
}

/* which of the map's shots are there */
static void shots_look(
	void)
{
	char path[MAP_MANAGER_PATH_SIZE];
	short shot;

	for (shot = 0; shot < NUMBER_OF_MAP_PICTURE_SHOTS; shot++)
	{
		manager.shots[shot] = manager.map[0] && map_pictures_shot_path(manager.map, shot, path, sizeof(path)) &&
			file_present(path);
	}
}

/* the map's description's lines, from its file (each line of it, cut into
lines the keyboard holds) */
static void description_read(
	void)
{
	struct custom_edition_map_file file;
	char path[MAP_MANAGER_PATH_SIZE];
	char text[1024];
	short display_index;
	short line = 0, length = 0;
	long size = 0, index;
	FILE *stream;

	csmemset(manager.lines, 0, sizeof(manager.lines));
	if (!map_get(&display_index, &file) || !file.has_description ||
		!custom_edition_maps_file_path(display_index, ".txt", path, sizeof(path)))
	{
		return;
	}
	stream = fopen(path, "rb");
	if (!stream)
		return;
	size = (long)fread(text, 1, sizeof(text), stream);
	fclose(stream);
	index = size >= 3 && (byte)text[0] == 0xEF && (byte)text[1] == 0xBB && (byte)text[2] == 0xBF ? 3 : 0;
	for (; index < size && line < DESCRIPTION_LINES; index++)
	{
		char character = text[index];

		if (character == '\n' || length == DESCRIPTION_LINE_LENGTH)
		{
			line++;
			length = 0;
			if (character == '\n' || line == DESCRIPTION_LINES)
				continue;
		}
		if (character == '\t')
			character = ' ';
		if (character >= ' ' && character < 0x7F)
			manager.lines[line][length++] = character;
	}
}

/* the lines written as the map's description (none: its file deleted) */
static void description_write(
	void)
{
	struct custom_edition_map_file file;
	char path[MAP_MANAGER_PATH_SIZE];
	short display_index;
	short last = NONE, line;
	FILE *stream;

	if (!map_get(&display_index, &file) || !custom_edition_maps_file_path(display_index, ".txt", path, sizeof(path)) ||
		!files_free())
	{
		return;
	}
	for (line = 0; line < DESCRIPTION_LINES; line++)
	{
		if (manager.lines[line][0])
			last = line;
	}
	if (last == NONE)
	{
		DeleteFileA(path);
		files_changed();
		message_set("Its description is the default one now.");
		return;
	}
	stream = fopen(path, "wb");
	if (!stream)
	{
		message_set("Its description could not be written.");
		return;
	}
	for (line = 0; line <= last; line++)
		fprintf(stream, "%s%s", manager.lines[line], line < last ? "\r\n" : "");
	if (fclose(stream) != 0)
		message_set("Its description could not be written.");
	else
		message_set("Its description is saved.");
	files_changed();
}

/* the map's replays: those of the replays folder recorded on it */
static void replays_look(
	void)
{
	static char names[MAP_MANAGER_REPLAYS][NETWORK_REPLAY_NAME_SIZE];
	short base_length = map_share_name_base_length(manager.map);
	long count = network_replay_list(names, MAP_MANAGER_REPLAYS);
	long index;

	manager.replay_count = 0;
	for (index = 0; index < count; index++)
	{
		struct network_replay_info *info = &manager.infos[manager.replay_count];

		if (!network_replay_info(names[index], info) || !info->valid)
			continue;
		/* (its level's name, custom_maps\<name>: a version's is the name
		without its hash) */
		if (csstrcasecmp(info->map, manager.map) &&
			((short)csstrlen(info->map) != base_length || _strnicmp(info->map, manager.map, base_length)))
		{
			continue;
		}
		csstrcpy(manager.replays[manager.replay_count], names[index]);
		manager.replay_count++;
	}
}

/* a list opened, at its first row */
static void list_open(
	short list)
{
	manager.list = list;
	manager.first[list] = 0;
	manager.chosen[list] = 0;
	manager.confirming_list = NONE;
	if (list == _list_description)
		description_read();
	if (list == _list_picture || list == _list_compare)
		shots_look();
	if (list == _list_theater)
		replays_look();
	if (list == _list_library)
		shared_look();
	rows_build();
}

/* the row asked about once: TRUE at the second press (it is done), FALSE
at the first (said) */
static boolean confirmed(
	struct manager_row const *row,
	char const *what)
{
	if (manager.confirming_list == manager.list && manager.confirming_kind == row->kind &&
		manager.confirming_value == row->value)
	{
		manager.confirming_list = NONE;
		return TRUE;
	}
	manager.confirming_list = manager.list;
	manager.confirming_kind = row->kind;
	manager.confirming_value = row->value;
	message_set("Press A again to %s.", what);
	return FALSE;
}

/* the keyboard opened for a folder's name, or for a line (its text first) */
static boolean keyboard_open(
	short typing,
	short line)
{
	short index;

	csmemset(manager.typed, 0, sizeof(manager.typed));
	if (typing == _typing_line)
	{
		for (index = 0; manager.lines[line][index] && index < DESCRIPTION_LINE_LENGTH; index++)
			manager.typed[index] = (wchar_t)(unsigned char)manager.lines[line][index];
	}
	virtual_keyboard_allow_any_name();
	if (!virtual_keyboard_launch(manager.typed, (word)sizeof(manager.typed), KEYBOARD_CAPTION))
	{
		message_set("The keyboard did not open.");
		return FALSE;
	}
	manager.typing = typing;
	manager.typing_line = line;
	return TRUE;
}

/* the map moved into the folder ("" for custom_maps' own), its picture and
description with it */
static void map_move(
	char const *folder)
{
	static char const *const beside[] = { ".bmp", ".txt" };
	struct custom_edition_map_file file;
	char from[MAP_MANAGER_PATH_SIZE];
	char to[MAP_MANAGER_PATH_SIZE];
	short display_index;
	short index;

	if (!map_get(&display_index, &file))
		return;
	if (!csstrcasecmp(file.category, folder))
	{
		message_set("It is in that folder already.");
		return;
	}
	if (!files_free())
		return;
	if (!custom_edition_maps_file_path(display_index, ".map", from, sizeof(from)) ||
		!path_in_folder(folder, file.name, ".map", to, sizeof(to)))
	{
		message_set("Its name is too long to move it.");
		return;
	}
	if (!MoveFileA(from, to))
	{
		message_set("It could not be moved: that folder has a map of its name, or the file is in use.");
		return;
	}
	for (index = 0; index < (short)NUMBEROF(beside); index++)
	{
		if (custom_edition_maps_file_path(display_index, beside[index], from, sizeof(from)) &&
			path_in_folder(folder, file.name, beside[index], to, sizeof(to)) && file_present(from))
		{
			MoveFileA(from, to);
		}
	}
	files_changed();
	error(_error_silent, "map manager: %s moved to custom_maps\\%s", file.name, folder);
	message_set("Moved to %s.", folder[0] ? folder : "custom_maps itself");
}

/* the map deleted, with its picture, description and shots */
static boolean map_delete(
	void)
{
	static char const *const beside[] = { ".bmp", ".txt" };
	struct custom_edition_map_file file;
	char path[MAP_MANAGER_PATH_SIZE];
	char shown[CUSTOM_EDITION_MAP_NAME_SIZE];
	short display_index;
	short index;

	if (!map_get(&display_index, &file) || !files_free())
		return FALSE;
	narrow_text(custom_edition_maps_name(display_index), shown, sizeof(shown));
	if (!custom_edition_maps_file_path(display_index, ".map", path, sizeof(path)) || !DeleteFileA(path))
	{
		message_set("It could not be deleted (the file is in use, or read-only).");
		return FALSE;
	}
	for (index = 0; index < (short)NUMBEROF(beside); index++)
	{
		if (custom_edition_maps_file_path(display_index, beside[index], path, sizeof(path)))
			DeleteFileA(path);
	}
	for (index = 0; index < NUMBER_OF_MAP_PICTURE_SHOTS; index++)
	{
		if (map_pictures_shot_path(file.name, index, path, sizeof(path)))
			DeleteFileA(path);
	}
	files_changed();
	error(_error_silent, "map manager: %s deleted", file.name);
	message_set("%s is deleted.", shown);
	return TRUE;
}

/* the folder FOLDER lists deleted, with every map in it (the first map of
its rows, by which custom_edition_maps_delete finds it; none, the folder
empty) */
static boolean folder_delete(
	void)
{
	char path[MAP_MANAGER_PATH_SIZE];
	short index;
	boolean deleted;

	if (!files_free())
		return FALSE;
	for (index = 0; index < manager.count && manager.rows[index].kind != _row_library_map; index++)
	{
	}
	if (index < manager.count)
		deleted = custom_edition_maps_delete(manager.rows[index].value, TRUE);
	else
	{
		if (csstrlen(CUSTOM_EDITION_MAP_DIRECTORY) + csstrlen(manager.folder) >= (long)sizeof(path))
			return FALSE;
		csprintf(path, "%s%s", CUSTOM_EDITION_MAP_DIRECTORY, manager.folder);
		deleted = RemoveDirectoryA(path) != 0;
	}
	if (!deleted)
	{
		message_set("The folder could not be deleted whole (a file in it is in use, or read-only).");
		files_changed();
		return FALSE;
	}
	files_changed();
	error(_error_silent, "map manager: the folder custom_maps\\%s deleted", manager.folder);
	message_set("%s is deleted.", manager.folder);
	return TRUE;
}

/* the typed name as a folder's (TRUE: made, or there already, in folder) */
static boolean folder_make(
	wchar_t const *typed,
	char *folder,
	long size)
{
	char path[MAP_MANAGER_PATH_SIZE];
	short index;
	short start = 0;
	short length;

	narrow_text(typed, folder, size);
	/* (no spaces at either end) */
	while (folder[start] == ' ')
		start++;
	length = (short)csstrlen(folder + start);
	csmemmove(folder, folder + start, length + 1);
	while (length > 0 && folder[length - 1] == ' ')
		folder[--length] = 0;
	if (!length)
		return FALSE;
	if (length >= CUSTOM_EDITION_MAP_CATEGORY_LENGTH || !map_share_name_safe(folder))
	{
		message_set("A folder's name can't have \\ / : * ? \" < > | in it, or a dot at its end.");
		return FALSE;
	}
	for (index = 0; index < custom_edition_maps_folder_count(); index++)
	{
		if (!csstrcasecmp(custom_edition_maps_folder_name(index), folder))
		{
			csstrcpy(folder, custom_edition_maps_folder_name(index));
			return TRUE;
		}
	}
	if (!files_free())
		return FALSE;
	csprintf(path, "%s%s", CUSTOM_EDITION_MAP_DIRECTORY, folder);
	if (!CreateDirectoryA(path, NULL))
	{
		message_set("The folder could not be made.");
		return FALSE;
	}
	files_changed();
	error(_error_silent, "map manager: the folder custom_maps\\%s made", folder);
	message_set("Made the folder %s.", folder);
	return TRUE;
}

/* the shot set as the map's picture */
static void picture_set(
	short shot)
{
	struct custom_edition_map_file file;
	char from[MAP_MANAGER_PATH_SIZE];
	char to[MAP_MANAGER_PATH_SIZE];
	short display_index;

	if (!map_get(&display_index, &file) || !files_free())
		return;
	if (!manager.shots[shot] || !map_pictures_shot_path(file.name, shot, from, sizeof(from)))
	{
		message_set("That shot has not been taken.");
		return;
	}
	if (!custom_edition_maps_file_path(display_index, ".bmp", to, sizeof(to)) || !CopyFileA(from, to, FALSE))
	{
		message_set("The picture could not be written beside the map.");
		return;
	}
	files_changed();
	message_set("It is the map's picture now.");
}

/* the picture file for a row of the lists that show one, else none */
static void picture_choose(
	struct manager_row const *row)
{
	struct custom_edition_map_file file;
	short display_index = NONE;
	boolean have = FALSE;

	manager.picture_wanted[0] = 0;
	if (!row)
		return;
	/* (the library's: a map's own picture, else none) */
	if (row->kind == _row_library_map)
	{
		if (custom_edition_maps_file_get(row->value, &file) && file.has_picture)
			custom_edition_maps_file_path(row->value, ".bmp", manager.picture_wanted, sizeof(manager.picture_wanted));
		return;
	}
	if (manager.list == _list_library || manager.list == _list_manage || manager.list == _list_folder)
		return;
	have = map_get(&display_index, &file);
	if (!have)
		display_index = NONE;
	if (row->kind == _row_picture_shot || row->kind == _row_keep_new)
	{
		if (manager.shots[row->value])
			map_pictures_shot_path(manager.map, row->value, manager.picture_wanted, sizeof(manager.picture_wanted));
		return;
	}
	if (display_index != NONE && custom_edition_maps_file_get(display_index, &file) && file.has_picture)
		custom_edition_maps_file_path(display_index, ".bmp", manager.picture_wanted, sizeof(manager.picture_wanted));
}

/* what the library's IMPORT MAPS does here */
static char const import_help[] =
#if defined(HALO_ANDROID) && !defined(HALO_SWITCH)
	"A picks .map files, or .zip files of maps, with the file picker. A .zip of a set goes into a folder of its own.";
#elif defined(HALO_SWITCH) || defined(HALO_PS5)
	"A shows a page to upload .map files, or .zip files of maps, from a phone or a computer on the same network.";
#else
	"A picks a .map file, or a .zip file of maps, to copy into custom_maps. A .zip of a set goes into a folder of "
	"its own.";
#endif

/* ---------- public code */

void map_manager_say(
	char const *text)
{
	message_set("%s", text);
	manager.message_milliseconds = REPORT_MILLISECONDS;
}

void map_manager_open_library(
	boolean can_import)
{
	manager.open_library = TRUE;
	manager.can_import = can_import;
	manager.open_at[0] = 0;
}

void map_manager_open(
	void)
{
	short display_index;
	struct custom_edition_map_file file;
	short list;

	for (list = 0; list < NUMBER_OF_LISTS; list++)
	{
		manager.first[list] = 0;
		manager.chosen[list] = 0;
	}
	manager.map[0] = 0;
	manager.typing = _typing_none;
	manager.confirming_list = NONE;
	manager.message[0] = 0;
	manager.replay_count = 0;
	manager.picture_changes++;
	/* (the folder looked at again: maps added since) */
	custom_edition_maps_look_again();
	manager.library = manager.open_library;
	manager.open_library = FALSE;
	manager.folder[0] = 0;
	if (manager.library)
	{
		manager.open_at[0] = 0;
		list_open(_list_library);
		return;
	}
	csstrcpy(manager.map, manager.open_at);
	manager.open_at[0] = 0;
	list_open(manager.open_at_pictures && map_get(&display_index, &file) ? _list_picture : _list_map);
	/* (a Theater shot of a map with a picture: which to keep) */
	if (manager.open_at_choosing && map_get(&display_index, &file) && file.has_picture &&
		manager.shots[_map_picture_shot_theater])
	{
		manager.compare_shot = _map_picture_shot_theater;
		list_open(_list_compare);
	}
	if (manager.open_at_pictures && map_get(&display_index, &file) && manager.message[0] == 0 &&
		!manager.shots[_map_picture_shot_spawn] && map_pictures_shots_failed())
	{
		message_set("The shots were not taken: the map did not load, or the Theater was left before they were "
			"(the log says why).");
	}
}

boolean map_manager_present(
	void)
{
	short display_index;
	struct custom_edition_map_file file;

	return map_get(&display_index, &file);
}

void map_manager_close(
	void)
{
	if (manager.picture)
		bitmap_delete(manager.picture);
	manager.picture = NULL;
	manager.picture_read[0] = 0;
	manager.picture_wanted[0] = 0;
}

void map_manager_open_at(
	char const *map_name,
	boolean pictures,
	boolean choosing)
{
	csstrncpy(manager.open_at, map_name, sizeof(manager.open_at) - 1);
	manager.open_at[sizeof(manager.open_at) - 1] = 0;
	manager.open_at_pictures = pictures || choosing;
	manager.open_at_choosing = choosing;
}

void map_manager_poll(
	void)
{
	short display_index;
	struct custom_edition_map_file file;
	char folder[CUSTOM_EDITION_MAP_CATEGORY_LENGTH];
	short typing = manager.typing;

	/* (what was typed, once the keyboard has closed) */
	if (typing != _typing_none && !virtual_keyboard_active())
	{
		manager.typing = _typing_none;
		if (virtual_keyboard_last_exit_saved_text())
		{
			if (typing == _typing_line)
			{
				narrow_text(manager.typed, manager.lines[manager.typing_line], DESCRIPTION_LINE_LENGTH + 1);
				description_write();
			}
			else if (folder_make(manager.typed, folder, sizeof(folder)) && typing == _typing_move_folder)
			{
				map_move(folder);
				list_open(_list_map);
			}
		}
	}
	if (manager.list == _list_library || manager.list == _list_manage || manager.list == _list_folder)
	{
		/* (a folder gone: MANAGE MAPS) */
		if (manager.list == _list_folder && folder_index() == NONE)
		{
			manager.folder[0] = 0;
			manager.list = _list_manage;
		}
	}
	else if (!map_get(&display_index, &file))
	{
		/* (the map gone: back to the library's list it was chosen in) */
		if (manager.library)
		{
			manager.map[0] = 0;
			manager.list = manager.folder[0] && folder_index() != NONE ? _list_folder : _list_manage;
		}
		/* (else no rows, the screen's B leaves) */
		else
		{
			manager.list = _list_map;
			manager.count = 0;
			picture_choose(NULL);
			return;
		}
	}
	if (manager.message[0] && system_milliseconds() - manager.message_time > manager.message_milliseconds)
		manager.message[0] = 0;
	rows_build();
	if (manager.chosen[manager.list] >= manager.count)
		manager.chosen[manager.list] = (short)MAX(0, manager.count - 1);
	picture_choose(row_get(manager.chosen[manager.list]));
}

boolean map_manager_library_shown(
	void)
{
	return manager.library && manager.list == _list_library;
}

short map_manager_list(
	void)
{
	return manager.list;
}

short map_manager_count(
	void)
{
	return manager.count;
}

short *map_manager_first(
	void)
{
	return &manager.first[manager.list];
}

short *map_manager_chosen(
	void)
{
	return &manager.chosen[manager.list];
}

void map_manager_row(
	short row_index,
	char *name,
	long name_size,
	char *state,
	long state_size)
{
	struct manager_row const *row = row_get(row_index);
	struct custom_edition_map_file file;
	short display_index;
	boolean confirming = row && manager.confirming_list == manager.list && manager.confirming_kind == row->kind &&
		manager.confirming_value == row->value;

	name[0] = 0;
	state[0] = 0;
	if (!row)
		return;
	switch (row->kind)
	{
	case _row_map_folder:
		snprintf(name, (size_t)name_size, "FOLDER");
		if (map_get(&display_index, &file))
			snprintf(state, (size_t)state_size, "%s", file.category[0] ? file.category : "NONE");
		break;
	case _row_map_description:
		snprintf(name, (size_t)name_size, "DESCRIPTION");
		if (map_get(&display_index, &file))
			snprintf(state, (size_t)state_size, "%s", file.has_description ? "ITS OWN" : "DEFAULT");
		break;
	case _row_map_picture:
		snprintf(name, (size_t)name_size, "PICTURE");
		if (map_get(&display_index, &file))
			snprintf(state, (size_t)state_size, "%s", file.has_picture ? "SET" : "NONE");
		break;
	case _row_delete_map:
		snprintf(name, (size_t)name_size, "DELETE MAP");
		break;
	case _row_move_new_folder:
		snprintf(name, (size_t)name_size, "NEW FOLDER");
		break;
	case _row_line:
		snprintf(name, (size_t)name_size, "%s", manager.lines[row->value][0] ? manager.lines[row->value] : "-");
		snprintf(state, (size_t)state_size, "LINE %d", row->value + 1);
		break;
	case _row_clear_description:
		snprintf(name, (size_t)name_size, "CLEAR DESCRIPTION");
		break;
	case _row_move_none:
		snprintf(name, (size_t)name_size, "NO FOLDER");
		if (map_get(&display_index, &file) && !file.category[0])
			snprintf(state, (size_t)state_size, "HERE NOW");
		break;
	case _row_move_folder:
		snprintf(name, (size_t)name_size, "%s", custom_edition_maps_folder_name(row->value));
		if (map_get(&display_index, &file) && !csstrcasecmp(file.category, custom_edition_maps_folder_name(row->value)))
			snprintf(state, (size_t)state_size, "HERE NOW");
		break;
	case _row_picture_current:
		snprintf(name, (size_t)name_size, "CURRENT PICTURE");
		if (map_get(&display_index, &file))
			snprintf(state, (size_t)state_size, "%s", file.has_picture ? "SET" : "NONE");
		break;
	case _row_picture_shot:
		if (row->value == _map_picture_shot_theater)
			snprintf(name, (size_t)name_size, "THEATER SHOT");
		else
			snprintf(name, (size_t)name_size, "SHOT %d: %s", row->value + 1,
				row->value == _map_picture_shot_spawn ? "START" : "CORNER");
		snprintf(state, (size_t)state_size, "%s", manager.shots[row->value] ? "TAKEN" : "NONE");
		break;
	case _row_take_shots:
		snprintf(name, (size_t)name_size, "%s", manager.shots[0] ? "TAKE NEW SHOTS" : "TAKE SHOTS");
		break;
	case _row_take_theater:
		snprintf(name, (size_t)name_size, "TAKE IN THEATER");
		break;
	case _row_delete_picture:
		snprintf(name, (size_t)name_size, "DELETE PICTURE");
		break;
	case _row_explore:
		snprintf(name, (size_t)name_size, "EXPLORE THE MAP");
		snprintf(state, (size_t)state_size, "EMPTY");
		break;
	case _row_keep_current:
		snprintf(name, (size_t)name_size, "KEEP ITS PICTURE");
		snprintf(state, (size_t)state_size, "NOW");
		break;
	case _row_keep_new:
		snprintf(name, (size_t)name_size, "USE THE NEW ONE");
		if (row->value == _map_picture_shot_theater)
			snprintf(state, (size_t)state_size, "THEATER SHOT");
		else
			snprintf(state, (size_t)state_size, "SHOT %d", row->value + 1);
		break;
	case _row_replay:
	{
		struct network_replay_info const *info = &manager.infos[row->value];

		snprintf(name, (size_t)name_size, "%s  %s", info->short_date, info->variant);
		if (info->seconds)
			snprintf(state, (size_t)state_size, "%ld:%02ld", info->seconds / 60, info->seconds % 60);
		break;
	}
	case _row_shared_files:
		snprintf(name, (size_t)name_size, "REQUIRED FILES");
		if (shared_missing())
			snprintf(state, (size_t)state_size, "%d MISSING", shared_missing());
		else
			snprintf(state, (size_t)state_size, "ALL FOUND");
		break;
	case _row_import:
		snprintf(name, (size_t)name_size, "IMPORT CE MAP");
		break;
	case _row_manage:
		snprintf(name, (size_t)name_size, "MANAGE MAPS");
		break;
	case _row_browse:
		snprintf(name, (size_t)name_size, "BROWSE MAPS");
		break;
	case _row_new_folder:
		snprintf(name, (size_t)name_size, "NEW FOLDER");
		break;
	case _row_folder:
		snprintf(name, (size_t)name_size, "%s/", custom_edition_maps_folder_name(row->value));
		if (manager.folder_maps[row->value])
			snprintf(state, (size_t)state_size, "%d MAP%s", manager.folder_maps[row->value],
				manager.folder_maps[row->value] == 1 ? "" : "S");
		else
			snprintf(state, (size_t)state_size, "EMPTY");
		break;
	case _row_library_map:
		narrow_text(custom_edition_maps_name(row->value), name, name_size);
		if (custom_edition_maps_file_get(row->value, &file))
			snprintf(state, (size_t)state_size, "%s", file.campaign ? "CAMPAIGN" : "MULTIPLAYER");
		break;
	case _row_delete_folder:
		snprintf(name, (size_t)name_size, "DELETE FOLDER");
		break;
	}
	if (confirming)
		snprintf(state, (size_t)state_size, "A AGAIN");
}

void map_manager_details(
	short row_index,
	char *title,
	long title_size,
	char *text,
	long text_size)
{
	struct manager_row const *row = row_get(row_index);
	struct custom_edition_map_file file;
	char about[512];
	char shown[CUSTOM_EDITION_MAP_NAME_SIZE];
	short display_index = NONE;
	boolean have_map = map_get(&display_index, &file);

	title[0] = 0;
	about[0] = 0;
	if (have_map)
		narrow_text(custom_edition_maps_name(display_index), shown, sizeof(shown));
	else
		shown[0] = 0;
	snprintf(title, (size_t)title_size, "%s", shown);
	if (row)
	{
		switch (row->kind)
		{
		case _row_map_folder:
			snprintf(about, sizeof(about), "The folder it is in. A moves it to another, or to a new one.");
			break;
		case _row_map_description:
			snprintf(about, sizeof(about), "The words beside its picture in the map lists. A writes them, a line at "
				"a time.");
			break;
		case _row_map_picture:
			snprintf(about, sizeof(about), "Its picture in the map lists. A takes one in the game, or chooses one of "
				"its shots.");
			break;
		case _row_delete_map:
			snprintf(about, sizeof(about), "A twice deletes the map's file, its picture and its description. They "
				"cannot be brought back.");
			break;
		case _row_line:
			snprintf(about, sizeof(about), "A line of the description (the map lists show about 20 characters of "
				"each). A types it.");
			break;
		case _row_clear_description:
			snprintf(about, sizeof(about), "A twice deletes its description: the default one is shown instead.");
			break;
		case _row_move_none:
			snprintf(about, sizeof(about), "A moves the map to custom_maps itself, in no folder.");
			break;
		case _row_move_folder:
			snprintf(about, sizeof(about), "A moves the map, its picture and its description to this folder.");
			break;
		case _row_move_new_folder:
			snprintf(about, sizeof(about), "A names a new folder and moves the map into it.");
			break;
		case _row_picture_current:
			snprintf(about, sizeof(about), "%s", have_map && file.has_picture ? "The picture the map lists show." :
				"It has no picture: the map lists show the unknown level's.");
			break;
		case _row_picture_shot:
			if (row->value == _map_picture_shot_theater)
				snprintf(about, sizeof(about), "The last picture taken in the Theater (it is the map's picture since)."
					"%s", manager.shots[row->value] ? " A sets it again." : "");
			else
				snprintf(about, sizeof(about), "%s%s", row->value == _map_picture_shot_spawn ?
					"From one of its players' starting places." : "From a corner of the map, toward its middle.",
					manager.shots[row->value] ? " A sets it as the map's picture." : " It is not taken yet.");
			break;
		case _row_take_shots:
			snprintf(about, sizeof(about), "A loads the map, takes three shots (a starting place and two corners) "
				"and comes back here. Each time, other places.");
			break;
		case _row_take_theater:
			snprintf(about, sizeof(about), "A lists its replays, and the map to walk round with nobody in it. In "
				"one, X takes the picture (in a replay, from its pause menu).");
			break;
		case _row_delete_picture:
			snprintf(about, sizeof(about), "A twice deletes its picture.");
			break;
		case _row_explore:
			snprintf(about, sizeof(about), "A loads the map with nobody in it and its clock held. Fly round it with "
				"the free camera; X takes the picture, Start then B leaves.");
			break;
		case _row_keep_current:
			snprintf(about, sizeof(about), "The map's picture now (below). A keeps it; the new one is kept among "
				"its shots. Up and down show each.");
			break;
		case _row_keep_new:
			snprintf(about, sizeof(about), "The new one (below). A makes it the map's picture. Up and down show "
				"each.");
			break;
		case _row_replay:
			snprintf(about, sizeof(about), "A plays the replay. Pause it where the picture is (Start), then X takes "
				"it.");
			break;
		case _row_shared_files:
		{
			short index;
			long length;

			snprintf(title, (size_t)title_size, "Required files");
			about[0] = 0;
			for (index = 0; index < SHARED_FILE_COUNT; index++)
			{
				length = (long)strlen(about);
				snprintf(about + length, sizeof(about) - (size_t)length, "%s: %s\n", shared_file_names[index],
					manager.shared[index] ? "found" : "MISSING");
			}
			length = (long)strlen(about);
			snprintf(about + length, sizeof(about) - (size_t)length, " \n%s",
				!halo_custom_edition_tag_cache() ? "Custom Edition maps can't run on this device (the log says why)." :
				shared_missing() ? "Copy them from a Custom Edition install's maps folder into maps, then press A." :
				"Every Custom Edition map needs these. A checks again.");
			break;
		}
		case _row_import:
			snprintf(title, (size_t)title_size, "Import CE map");
			snprintf(about, sizeof(about), "%s", import_help);
			break;
		case _row_manage:
			snprintf(title, (size_t)title_size, "Manage maps");
			snprintf(about, sizeof(about), "custom_maps' folders and maps: A lists them, to move, describe, picture "
				"or delete a map, and to make or delete folders.");
			break;
		case _row_browse:
			snprintf(title, (size_t)title_size, "Browse maps");
			snprintf(about, sizeof(about), "The Custom Edition maps of FractumSeraph's map archive: search them, "
				"filter them by type, and download them into custom_maps. A map you have already says INSTALLED.");
			break;
		case _row_new_folder:
			snprintf(title, (size_t)title_size, "New folder");
			snprintf(about, sizeof(about), "A names a new folder in custom_maps. A map's FOLDER row moves it in; "
				"the map lists group maps by folder.");
			break;
		case _row_folder:
			snprintf(title, (size_t)title_size, "%s", custom_edition_maps_folder_name(row->value));
			snprintf(about, sizeof(about), "A folder of custom_maps, with %d map%s. A lists them.",
				manager.folder_maps[row->value], manager.folder_maps[row->value] == 1 ? "" : "s");
			break;
		case _row_library_map:
		{
			struct custom_edition_map_file chosen;
			char description[256];
			short index;

			narrow_text(custom_edition_maps_name(row->value), title, title_size);
			narrow_text(custom_edition_maps_description(row->value), description, sizeof(description));
			for (index = 0; description[index]; index++)
			{
				if (description[index] == '\r' || description[index] == '\n')
					description[index] = ' ';
			}
			if (custom_edition_maps_file_get(row->value, &chosen))
				snprintf(about, sizeof(about), "%s.map%s%s\n%s\n \nA moves it, describes it, gives it a picture "
					"or deletes it.", chosen.name, chosen.category[0] ? " in " : "", chosen.category, description);
			break;
		}
		case _row_delete_folder:
			snprintf(title, (size_t)title_size, "%s", manager.folder);
			snprintf(about, sizeof(about), "A twice deletes the folder and every map in it, with their pictures and "
				"descriptions. They cannot be brought back.");
			break;
		}
	}
	text[0] = 0;
	if (manager.message[0])
		snprintf(text, (size_t)text_size, "%s\n \n%s", manager.message, about);
	else
		snprintf(text, (size_t)text_size, "%s", about);
}

short map_manager_choose(
	short row_index)
{
	struct manager_row const *row = row_get(row_index);
	struct custom_edition_map_file file;
	char why[160];
	char path[MAP_MANAGER_PATH_SIZE + NETWORK_REPLAY_NAME_SIZE];
	short display_index;

	if (!row)
		return _map_manager_stays;
	switch (row->kind)
	{
	case _row_move_new_folder:
		return keyboard_open(_typing_move_folder, 0) ? _map_manager_typing : _map_manager_stays;
	case _row_map_folder:
		list_open(_list_move);
		break;
	case _row_map_description:
		list_open(_list_description);
		break;
	case _row_map_picture:
		list_open(_list_picture);
		break;
	case _row_delete_map:
		if (confirmed(row, "delete the map") && map_delete())
		{
			/* (the library: back to the list it was chosen in) */
			if (!manager.library)
				return _map_manager_closes;
			manager.map[0] = 0;
			manager.list = manager.folder[0] ? _list_folder : _list_manage;
			rows_build();
		}
		break;
	case _row_shared_files:
		shared_look();
		message_set("%s", shared_missing() ? "Still missing: see below." : "All of them are found.");
		break;
	case _row_import:
		return _map_manager_import;
	case _row_manage:
		manager.folder[0] = 0;
		list_open(_list_manage);
		break;
	case _row_browse:
		return _map_manager_browse;
	case _row_new_folder:
		return keyboard_open(_typing_new_folder, 0) ? _map_manager_typing : _map_manager_stays;
	case _row_folder:
		csstrncpy(manager.folder, custom_edition_maps_folder_name(row->value), sizeof(manager.folder) - 1);
		manager.folder[sizeof(manager.folder) - 1] = 0;
		list_open(_list_folder);
		break;
	case _row_library_map:
		if (!custom_edition_maps_file_get(row->value, &file) || !file.managed)
			break;
		csstrcpy(manager.map, file.name);
		list_open(_list_map);
		break;
	case _row_delete_folder:
		if (confirmed(row, "delete the folder and its maps") && folder_delete())
		{
			manager.folder[0] = 0;
			manager.list = _list_manage;
			rows_build();
		}
		break;
	case _row_line:
		return keyboard_open(_typing_line, row->value) ? _map_manager_typing : _map_manager_stays;
	case _row_clear_description:
		if (confirmed(row, "delete its description"))
		{
			csmemset(manager.lines, 0, sizeof(manager.lines));
			description_write();
		}
		break;
	case _row_move_none:
		map_move("");
		list_open(_list_map);
		break;
	case _row_move_folder:
		map_move(custom_edition_maps_folder_name(row->value));
		list_open(_list_map);
		break;
	case _row_picture_current:
		break;
	case _row_picture_shot:
		/* (the map with a picture: the two weighed first) */
		if (map_get(&display_index, &file) && file.has_picture && manager.shots[row->value])
		{
			manager.compare_shot = row->value;
			list_open(_list_compare);
			manager.chosen[_list_compare] = 1;
		}
		else
			picture_set(row->value);
		break;
	case _row_keep_current:
		list_open(_list_picture);
		message_set("Kept its picture.");
		break;
	case _row_keep_new:
		picture_set(row->value);
		list_open(_list_picture);
		break;
	case _row_take_shots:
		if (!map_get(&display_index, &file) || !files_free())
			break;
		if (map_pictures_generate(display_index, why, sizeof(why)))
			return _map_manager_leaving;
		message_set("%s", why);
		break;
	case _row_take_theater:
		list_open(_list_theater);
		break;
	case _row_delete_picture:
		if (!confirmed(row, "delete its picture") || !map_get(&display_index, &file) || !files_free())
			break;
		if (custom_edition_maps_file_path(display_index, ".bmp", path, sizeof(path)) && DeleteFileA(path))
		{
			files_changed();
			message_set("Its picture is deleted.");
		}
		else
			message_set("Its picture could not be deleted.");
		break;
	case _row_explore:
		if (!map_get(&display_index, &file) || !files_free())
			break;
		if (map_pictures_explore(display_index, why, sizeof(why)))
			return _map_manager_leaving;
		message_set("%s", why);
		break;
	case _row_replay:
		if (!map_get(&display_index, &file) || !files_free())
			break;
		network_replay_folder(path, MAP_MANAGER_PATH_SIZE);
		snprintf(path + strlen(path), sizeof(path) - strlen(path), "/%s", manager.replays[row->value]);
		if (map_pictures_watch(display_index, path, why, sizeof(why)))
			return _map_manager_leaving;
		message_set("%s", why);
		break;
	}
	return _map_manager_stays;
}

boolean map_manager_back(
	void)
{
	short up;

	manager.confirming_list = NONE;
	switch (manager.list)
	{
	case _list_description:
	case _list_move:
	case _list_picture: up = _list_map; break;
	case _list_theater:
	case _list_compare: up = _list_picture; break;
	/* (the library's: a map back to the list it was chosen in) */
	case _list_map:
		if (!manager.library)
			return FALSE;
		manager.map[0] = 0;
		up = manager.folder[0] && folder_index() != NONE ? _list_folder : _list_manage;
		break;
	case _list_folder:
		manager.folder[0] = 0;
		up = _list_manage;
		break;
	case _list_manage:
		if (!manager.library)
			return FALSE;
		up = _list_library;
		break;
	default: return FALSE;
	}
	/* (the list gone back to as it was left: its row kept) */
	manager.list = up;
	if (up == _list_picture)
		shots_look();
	rows_build();
	return TRUE;
}

struct bitmap_data *map_manager_menu_picture(
	void)
{
	if (strcmp(manager.picture_wanted, manager.picture_read) || manager.picture_changes != manager.picture_read_changes)
	{
		if (manager.picture)
			bitmap_delete(manager.picture);
		manager.picture = manager.picture_wanted[0] ? custom_edition_maps_picture_file_read(manager.picture_wanted) :
			NULL;
		csstrcpy(manager.picture_read, manager.picture_wanted);
		manager.picture_read_changes = manager.picture_changes;
	}
	return manager.picture;
}
