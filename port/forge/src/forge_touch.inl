/*
FORGE_TOUCH.INL (Halo Forge Evolved)

The Forge on the touchscreen, included by forge_3d_mode.c (after the Forge's
own functions, before forge_3d_update), so that it works the Forge as its own
controls do, through the same code: Halo Forge Evolved adds nothing to how an
object is picked up, placed, copied or turned.

While the Monitor flies, the touch overlay (TouchControls.java) shows only its
stick (and Start), and hands every other finger to the Forge
(host_touch_forge_read, port/android/host/host_touch.c). Here a finger:

- on one of the buttons the Forge draws (forge_touch_draw: OBJETOS, DESHACER,
  REHACER, GUARDAR, MAPA, PROBAR, SUBIR, BAJAR, AYUDA; and with an object
  chosen or held, its bar: MOVER, GIRAR, COPIAR, AJUSTES, BORRAR, SOLTAR...)
  presses it;
- tapped on an object, chooses it (its bar shows); tapped on nothing, lets it
  go; tapped while an object is held, puts it down there;
- dragged from an object, picks it up and carries it under the finger, along
  the ground and the walls, and puts it down where the finger is lifted;
- dragged from nothing, looks around;
- with a second finger, while an object is held, turns it (twisting the two).

The Forge's pick, which is the view's middle (the reticle), is the finger's
line through the screen while a finger is on the map (forge_touch_pick); the
Forge's own click, copy and the keys of its controls are pressed as keys are
(forge_touch_key_down, which forge_hooks.c's forge_key_state asks); and what
the click acts on is the object under the finger when it went down
(forge_touch_after_hover).

With one of the Forge's menus open (its palette, an object's settings, the
map's settings, saving), every finger is the menu's: a tap clicks a row as
the mouse does (forge_touch_menu_pointer, for forge_pointer_poll), a drag up
or down scrolls, and VOLVER goes back.

Off Android (the desktop, the web) nothing hands fingers over, and none of
this does anything. */

/* the overlay's scene bits (port/linux/src/touch_input.c's _touch_scene_forge,
_touch_scene_forge_menu) */
#define FORGE_TOUCH_SCENE_FORGE (1 << 6)
#define FORGE_TOUCH_SCENE_MENU (1 << 7)

/* the host's events (host_touch.c's HOST_TOUCH_FORGE_*) */
#define FORGE_TOUCH_DOWN 0
#define FORGE_TOUCH_MOVE 1
#define FORGE_TOUCH_UP 2
#define FORGE_TOUCH_CANCEL 3
#define FORGE_TOUCH_FIELDS 4
#define FORGE_TOUCH_READ 64

#if defined(HALO_ANDROID) && !defined(HALO_SWITCH)
/* port/android/guest/runtime/guest_host.h */
int host_touch_forge_read(int *events, int maximum);
void host_android_export_map(const char *path);
#define FORGE_TOUCH_HOST 1
#else
#define FORGE_TOUCH_HOST 0
#endif

/* save the map (forge_runtime_tags.c): the name asked the first time */
extern void forge_trigger_save_map_action(void);
/* a d:\ path's file on this machine (the port's file layer) */
extern void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size);

/* a finger moves this far (a share of the screen's height) before it is a
drag, not a tap; a tap is at most this long */
#define FORGE_TOUCH_SLOP 0.025f
#define FORGE_TOUCH_TAP_MS 450
/* a menu's drag scrolls a row per this much of the screen's height */
#define FORGE_TOUCH_SCROLL_STEP 0.055f
/* looking: radians per screen width, and per screen height */
#define FORGE_TOUCH_LOOK_YAW 3.2f
#define FORGE_TOUCH_LOOK_PITCH 2.0f
/* a key pressed by a tap is held this many of the Forge's frames: the
Forge acts as a key goes down (one frame), and lets go the next */
#define FORGE_TOUCH_PULSE_FRAMES 2
#define FORGE_TOUCH_MAX_FINGERS 5
#define FORGE_TOUCH_MAX_BUTTONS 64
/* the quick bar (Minecraft's hotbar): its slots */
#define FORGE_TOUCH_HOTBAR_SLOTS 9
/* held this long on one spot over an object, a finger deletes it (as
Minecraft's breaks a block) */
#define FORGE_TOUCH_DELETE_HOLD_MS 650
/* (a finger held this long shows the deleting's ring) */
#define FORGE_TOUCH_DELETE_SHOW_MS 150
/* the Forge's menus on a touchscreen: this many times as large (0.75 becomes
1.1: the palette then fills most of the screen's height) */
#define FORGE_TOUCH_MENU_GROWTH 1.47f

/* the inventory (+ OBJETOS): its columns and rows of objects, and the most it lists of a tab */
#define FORGE_TOUCH_INVENTORY_COLUMNS 4
#define FORGE_TOUCH_INVENTORY_ROWS 5
#define FORGE_TOUCH_INVENTORY_MAX 512

enum
{
	_forge_touch_gesture_none = 0,
	/* down on the map, not yet a tap or a drag */
	_forge_touch_gesture_pending,
	_forge_touch_gesture_look,
	/* carrying what was under the finger (or what was already held) */
	_forge_touch_gesture_carry,
	/* on one of the Forge's buttons */
	_forge_touch_gesture_button,
	/* on the inventory: a tap chooses, a drag scrolls */
	_forge_touch_gesture_inventory
};

/* the Forge's touch buttons: what each does (forge_touch_press) */
enum
{
	_forge_touch_button_palette = 0,
	_forge_touch_button_undo,
	_forge_touch_button_redo,
	_forge_touch_button_save,
	_forge_touch_button_map,
	_forge_touch_button_test,
	_forge_touch_button_up,
	_forge_touch_button_down,
	_forge_touch_button_help,
	_forge_touch_button_move,
	_forge_touch_button_turn_left,
	_forge_touch_button_turn_right,
	_forge_touch_button_copy,
	_forge_touch_button_settings,
	_forge_touch_button_delete,
	_forge_touch_button_done,
	_forge_touch_button_drop,
	_forge_touch_button_cancel,
	_forge_touch_button_floor,
	_forge_touch_button_back,
	_forge_touch_button_snap,
	_forge_touch_button_export,
	_forge_touch_button_clear,
	_forge_touch_button_inventory_close,
	_forge_touch_button_inventory_full,
	NUMBER_OF_FORGE_TOUCH_BUTTONS,
	/* the quick bar's slots: this plus the slot's index */
	_forge_touch_button_hotbar = 100,
	/* the inventory's tabs: this plus the category */
	_forge_touch_button_tab = 200,
	/* the inventory's objects: this plus the place in its list */
	_forge_touch_button_cell = 300
};

struct forge_touch_finger
{
	int used;
	int id;
	/* where, in shares of the screen (0..1) */
	float x, y;
	float down_x, down_y;
	uint32_t down_ms;
	int moved;
};

struct forge_touch_button
{
	short x0, y0, x1, y1;
	int what;
};

static struct
{
	struct forge_touch_finger fingers[FORGE_TOUCH_MAX_FINGERS];
	/* the finger on the map (fingers' index), -1 for none */
	int primary;
	int gesture;
	/* the button the primary finger holds (a gesture_button's), -1 */
	int button;
	/* the Forge's pick through this point of the screen this frame */
	int pick;
	float pick_x, pick_y;
	/* what was under the finger as it went down (forge_touch_pick_at): a
	live instance, else a map object not yet the Forge's, else none */
	int32_t down_live;
	long down_object;
	/* what is chosen (tapped): its bar shows; -1 none */
	int32_t chosen;
	/* what the Forge's click (or copy) acts on this frame, if it is not
	what the pick is on */
	int force_frames;
	int32_t force_live;
	long force_object;
	/* keys pressed by the touch: frames left (pulses) and held */
	unsigned char key_pulse[256];
	unsigned char key_held[256];
	/* the look since the Forge's last read */
	float look_yaw, look_pitch;
	/* two fingers turning what is held: their angle last frame */
	int twist;
	float twist_angle;
	/* the menu's pointer, for forge_pointer_poll */
	int menu_finger;
	float menu_x, menu_y;
	float menu_down_x, menu_down_y;
	float menu_scroll;
	int menu_dragged;
	int menu_moved;
	int menu_clicks;
	float menu_click_x, menu_click_y;
	int menu_wheel;
	/* the HUD's window as last drawn (the Forge's screen units) */
	int window_known;
	short window_x0, window_y0, window_x1, window_y1;
	/* the buttons as last drawn */
	struct forge_touch_button buttons[FORGE_TOUCH_MAX_BUTTONS];
	int button_count;
	/* the help shown */
	int help;
	/* VACIAR's first tap, when (its second, soon after, empties the map) */
	uint32_t clear_armed_ms;
	/* the quick bar: the objects taken from the palette, the latest first,
	and the one in hand (-1: none, and a tap chooses as before) */
	struct
	{
		int used;
		uint32_t group_tag;
		uint8_t source_cache_id;
		char tag_path[FORGE_TAG_PATH_MAX];
		char name[64];
	} hotbar[FORGE_TOUCH_HOTBAR_SLOTS];
	int hand;
	/* for the help's last line, which says how the touch went (to tell
	what fails on a phone from a screenshot): the finger's line's error, and
	what the last finger went down on */
	float last_ray_error;
	int32_t last_down_live;
	long last_down_object;
	/* whether a finger has ever been handed over: the touch HUD shows from
	then on (a desktop's never) */
	int seen;
	/* the inventory (Halo Forge Evolved's own, not the Forge's palette):
	whether it is open, its tab (a category), how far down it is (rows),
	the list of its tab (catalog indices) and how many each tab has */
	int inv_open;
	int inv_tab;
	float inv_scroll;
	float inv_scroll_down;
	int inv_drag;
	int inv_list[FORGE_TOUCH_INVENTORY_MAX];
	int inv_count;
	int inv_list_tab;
	int inv_list_catalog;
	int inv_tab_count[NUMBER_OF_FORGE_PALETTE_CATEGORIES];
	int inv_counts_catalog;
	/* its grid as last drawn, in shares of the screen */
	float inv_grid_x0, inv_grid_y0, inv_grid_x1, inv_grid_y1, inv_row_h;
} s_touch;
/* (s_touch set up: forge_touch_ready) */
static int s_touch_ready;

static void forge_touch_ready(void)
{
	if (s_touch_ready)
		return;
	memset(&s_touch, 0, sizeof(s_touch));
	s_touch.primary = -1;
	s_touch.button = -1;
	s_touch.down_live = -1;
	s_touch.down_object = NONE;
	s_touch.chosen = -1;
	s_touch.force_live = -1;
	s_touch.force_object = NONE;
	s_touch.menu_finger = -1;
	s_touch.hand = -1;
	s_touch.last_ray_error = -1.0f;
	s_touch.last_down_live = -1;
	s_touch.last_down_object = NONE;
	s_touch.inv_tab = _forge_cat_scenery;
	s_touch.inv_list_tab = -1;
	s_touch.inv_list_catalog = -1;
	s_touch.inv_counts_catalog = -1;
	s_touch_ready = 1;
}

/* ---------- small helpers */

/* (FORGE_IN_MATCH_HUD_SCALE) the Forge's menus' scale: larger once a finger
has been handed over, so that a row is a finger high (a desktop's as it was) */
static float forge_touch_menu_scale(float scale)
{
	return s_touch_ready && s_touch.seen ? scale * FORGE_TOUCH_MENU_GROWTH : scale;
}

static float forge_touch_abs(float v)
{
	return v < 0.0f ? -v : v;
}

static void forge_touch_pulse(int vk)
{
	if (vk > 0 && vk < 256)
		s_touch.key_pulse[vk] = FORGE_TOUCH_PULSE_FRAMES;
}

/* (forge_hooks.c's forge_key_state, on the builds with no Windows to ask) */
int forge_touch_key_down(int vk)
{
	if (!s_touch_ready || vk <= 0 || vk >= 256)
		return 0;
	return s_touch.key_pulse[vk] != 0 || s_touch.key_held[vk] != 0;
}

static int forge_touch_monitor(void)
{
	return g_forge_session_enabled && g_forge.mode == _forge_mode_3d_monitor && !forge_is_main_menu_active() &&
		!ui_widgets_active();
}

/* the overlay's scene (touch_input.c, as it reads the controller) */
int forge_touch_scene(void)
{
	if (!forge_touch_monitor())
		return 0;
	return FORGE_TOUCH_SCENE_FORGE | (g_forge.active_modal != _forge_modal_none ? FORGE_TOUCH_SCENE_MENU : 0);
}

/* a point of the screen (shares) in the Forge's screen units, as it draws.
The HUD's window is the screen less its margins, the same on each side (on a
phone about 8% of the screen each way: the HUD's safe area), in units that
start at the screen's corner: so the whole screen is window_x0 + window_x1
wide and window_y0 + window_y1 high */
static void forge_touch_to_window(float x, float y, short *wx, short *wy)
{
	float width, height;

	if (!s_touch.window_known)
	{
		*wx = (short)(x * 640.0f);
		*wy = (short)(y * 480.0f);
		return;
	}
	width = (float)(s_touch.window_x0 + s_touch.window_x1);
	height = (float)(s_touch.window_y0 + s_touch.window_y1);
	*wx = (short)(x * width + 0.5f);
	*wy = (short)(y * height + 0.5f);
}

/* the button under a point of the screen (shares), as last drawn: its index in
buttons, else -1 */
static int forge_touch_button_at(float x, float y)
{
	short wx, wy;
	int i;

	forge_touch_to_window(x, y, &wx, &wy);
	for (i = s_touch.button_count - 1; i >= 0; i--)
	{
		struct forge_touch_button const *b = &s_touch.buttons[i];

		if (wx >= b->x0 && wx < b->x1 && wy >= b->y0 && wy < b->y1)
			return i;
	}
	return -1;
}

static int forge_touch_finger_of(int id)
{
	int i;

	for (i = 0; i < FORGE_TOUCH_MAX_FINGERS; i++)
	{
		if (s_touch.fingers[i].used && s_touch.fingers[i].id == id)
			return i;
	}
	return -1;
}

/* the other finger down besides the primary one, -1 */
static int forge_touch_second_finger(void)
{
	int i;

	for (i = 0; i < FORGE_TOUCH_MAX_FINGERS; i++)
	{
		if (s_touch.fingers[i].used && i != s_touch.primary)
			return i;
	}
	return -1;
}

static int32_t forge_touch_live_valid(int32_t live)
{
	return live >= 0 && live < g_forge.live_instance_count && g_forge.live_instances[live].active ? live : -1;
}

/* how many degrees a GIRAR turns: the rotation snap's, 15 with none */
static float forge_touch_turn_step(void)
{
	switch (g_forge.snap_mode)
	{
	case _forge_snap_45_deg:
		return 45.0f;
	case _forge_snap_90_deg:
		return 90.0f;
	default:
		return 15.0f;
	}
}

/* an object's live instance taken up to where its object is now */
static void forge_touch_instance_refresh(int32_t live)
{
	struct forge_live_instance *inst = &g_forge.live_instances[live];
	struct object_datum *obj = forge_valid_object_get(inst->object_index);

	if (obj)
	{
		inst->x = obj->object.position.x;
		inst->y = obj->object.position.y;
		inst->z = obj->object.position.z;
		forward_up_to_euler(&obj->object.forward, &obj->object.up, &inst->yaw, &inst->pitch, &inst->roll);
	}
}

/* (below: what is under a point of the screen, and taking it up) */
static void forge_touch_pick_at(float x, float y, int32_t *live, long *object);
static int32_t forge_touch_resolve(int32_t live, long object);
static void forge_touch_grab(int32_t live);
static void forge_touch_place_hand(float x, float y);

/* ---------- the buttons */

static void forge_touch_chosen_turn(float degrees)
{
	int32_t live = forge_touch_live_valid(s_touch.chosen);
	struct forge_live_instance *inst;
	float yaw;

	if (live < 0)
		return;
	inst = &g_forge.live_instances[live];
	forge_touch_instance_refresh(live);
	forge_history_begin(live);
	yaw = inst->yaw + degrees * 0.0174532925f;
	while (yaw > 3.14159265f)
		yaw -= 6.283185307f;
	while (yaw < -3.14159265f)
		yaw += 6.283185307f;
	forge_3d_move_instance_ext(live, inst->x, inst->y, inst->z, yaw, inst->pitch, inst->roll, 0, 1);
	forge_history_end(live);
	forge_set_status("Giro: %d grados", (int)(yaw * 57.2957795f + (yaw >= 0.0f ? 0.5f : -0.5f)));
}

/* what the Forge's click or copy acts on, for a frame: not what the pick is on */
static void forge_touch_force(int32_t live, long object)
{
	s_touch.force_live = live;
	s_touch.force_object = object;
	s_touch.force_frames = FORGE_TOUCH_PULSE_FRAMES;
}

/* EXPORTAR: a copy of the map saved (GUARDAR), where the player chooses
(HaloActivity.exportForgeMap); saved first if it never was */
static void forge_touch_export(void)
{
	static char const *const places[] = {
		"d:\\Faber_data\\forge_maps\\%s\\%s.map",
		"d:\\custom_maps\\%s.map",
		"d:\\OpenCE Forge\\forge_maps\\%s\\%s.map",
		"d:\\forge_maps\\%s\\%s.map",
	};
	char name[128];
	int i;

	if (!g_forge.has_saved_map_once || !g_forge.save_map_name[0])
	{
		forge_set_status("Primero GUARDAR el mapa: luego EXPORTAR");
		forge_trigger_save_map_action();
		return;
	}
	snprintf(name, sizeof(name), "%s", g_forge.save_map_name);
	for (i = 0; i < (int)(sizeof(places) / sizeof(places[0])); i++)
	{
		char xbox_path[512];
		char host_path[1024];
		FILE *file;

		snprintf(xbox_path, sizeof(xbox_path), places[i], name, name);
		platform_translate_path(xbox_path, host_path, sizeof(host_path));
		file = (FILE *)forge_host_fopen(host_path, "rb");
		if (!file)
			continue;
		fclose(file);
		platform_log("Forge: EXPORTAR %s (%s)", xbox_path, host_path);
#if FORGE_TOUCH_HOST
		host_android_export_map(host_path);
		forge_set_status("Elige donde guardar '%s.map'", name);
#else
		forge_set_status("El mapa esta en %s", host_path);
#endif
		return;
	}
	forge_set_status("No se encontro '%s.map': GUARDAR otra vez", name);
}

/* VACIAR: the map's objects taken away (its scenery, vehicles, weapons and
equipment, the map's own and those placed), for a map started from its
ground alone; its devices (doors, lifts, bridges) and the places where
players and objectives appear stay. The map's own are taken away as the
Forge takes them away one by one (taken over, then deleted), so that the map
saved is without them */
static void forge_touch_clear_map(void)
{
	unsigned long mask = _object_mask_scenery | _object_mask_vehicle | _object_mask_weapon | _object_mask_equipment;
	long local_unit = player_control_get_unit_index(0);
	int removed = 0;
	int pass;

	if (g_forge.grabbed_live_index >= 0)
		forge_cancel_or_release_held_item();
	s_touch.chosen = -1;
	/* (twice: deleting can make others, a vehicle's dropped weapons) */
	for (pass = 0; pass < 2; pass++)
	{
		long found[512];
		int count = 0, k;
		struct object_iterator iter;
		struct object_datum *obj;

		object_iterator_new(&iter, mask, 0);
		while ((obj = object_iterator_next(&iter)) != NULL && count < (int)(sizeof(found) / sizeof(found[0])))
		{
			if (iter.index == local_unit || obj->object.parent_object_index != NONE)
				continue;
			found[count++] = iter.index;
		}
		for (k = 0; k < count; k++)
		{
			int32_t live;

			if (!forge_valid_object_get(found[k]))
				continue;
			live = forge_touch_resolve(-1, found[k]);
			if (forge_touch_live_valid(live) < 0)
				continue;
			forge_3d_delete_instance(live, 1);
			removed++;
		}
	}
	forge_set_status("Mapa vaciado: %d objetos quitados (DESHACER no lo devuelve)", removed);
}

/* ---------- the inventory (+ OBJETOS): Halo Forge Evolved's own, in big
squares for fingers, as Minecraft's; what is chosen goes to the quick bar, in
hand */

static void forge_touch_hotbar_note(struct forge_catalog_entry const *entry);

/* its tabs, in order: the categories a tap can put (the rest, as the AI and
the volumes, stay in the Forge's full list) */
static struct
{
	int category;
	char const *name;
} const s_forge_touch_tabs[] = {
	{ _forge_cat_scenery, "ESCENARIO" },
	{ _forge_cat_barriers_devices, "BARRERAS" },
	{ _forge_cat_weapons, "ARMAS" },
	{ _forge_cat_vehicles, "VEHICULOS" },
	{ _forge_cat_equipment, "EQUIPO" },
	{ _forge_cat_spawns, "REAPARICION" },
	{ _forge_cat_objectives, "OBJETIVOS" },
	{ _forge_cat_teleporters, "TELEPORTES" },
	{ _forge_cat_lights, "LUCES" },
	{ _forge_cat_effects, "EFECTOS" },
};
#define FORGE_TOUCH_TAB_COUNT ((int)(sizeof(s_forge_touch_tabs) / sizeof(s_forge_touch_tabs[0])))

/* whether a catalog row is one object a tap can put down as it is (not a
folder, a menu, an AI, a volume or a KOTH hill, which are placed their own way) */
static int forge_touch_entry_placeable(struct forge_catalog_entry const *entry)
{
	int tab;

	if (!entry || !entry->tag_path[0])
		return 0;
	switch (entry->group_tag)
	{
	case FORGE_GROUP_FOLDER:
	case FORGE_GROUP_FF_MENU:
	case FORGE_GROUP_AI_MENU:
	case FORGE_GROUP_AI_SPAWN:
	case FORGE_GROUP_VOLUME:
	case 'ffsp':
		return 0;
	default:
		break;
	}
	if (entry->group_tag == 'flag' && strstr(entry->tag_path, "koth") != NULL)
		return 0;
	if (forge_palette_is_ff_preset(entry))
		return 0;
	for (tab = 0; tab < FORGE_TOUCH_TAB_COUNT; tab++)
	{
		if (s_forge_touch_tabs[tab].category == entry->category)
			return forge_catalog_entry_allowed(entry);
	}
	return 0;
}

/* a name for a square or a slot: in capitals, its _ as spaces, at most size-1 long */
static void forge_touch_label(char *out, size_t size, char const *name)
{
	size_t n;

	snprintf(out, size, "%s", name ? name : "");
	for (n = 0; out[n]; n++)
	{
		if (out[n] >= 'a' && out[n] <= 'z')
			out[n] = (char)(out[n] - 'a' + 'A');
		else if (out[n] == '_')
			out[n] = ' ';
	}
}

/* how many each tab has (again when the catalog changes) */
static void forge_touch_inventory_counts(void)
{
	int i;

	if (s_touch.inv_counts_catalog == g_forge.catalog_count)
		return;
	memset(s_touch.inv_tab_count, 0, sizeof(s_touch.inv_tab_count));
	for (i = 0; i < g_forge.catalog_count; i++)
	{
		struct forge_catalog_entry const *entry = &g_forge.catalog[i];

		if (entry->category < NUMBER_OF_FORGE_PALETTE_CATEGORIES && forge_touch_entry_placeable(entry))
			s_touch.inv_tab_count[entry->category]++;
	}
	s_touch.inv_counts_catalog = g_forge.catalog_count;
}

/* the tab's list: its objects once each (the same tag from two places once) */
static void forge_touch_inventory_list(void)
{
	int i;

	if (s_touch.inv_list_tab == s_touch.inv_tab && s_touch.inv_list_catalog == g_forge.catalog_count)
		return;
	s_touch.inv_count = 0;
	for (i = 0; i < g_forge.catalog_count && s_touch.inv_count < FORGE_TOUCH_INVENTORY_MAX; i++)
	{
		struct forge_catalog_entry const *entry = &g_forge.catalog[i];
		int j, twice = 0;

		if (entry->category != s_touch.inv_tab || !forge_touch_entry_placeable(entry))
			continue;
		for (j = 0; j < s_touch.inv_count; j++)
		{
			struct forge_catalog_entry const *other = &g_forge.catalog[s_touch.inv_list[j]];

			if (other->group_tag == entry->group_tag && !_stricmp(other->tag_path, entry->tag_path))
			{
				twice = 1;
				break;
			}
		}
		if (!twice)
			s_touch.inv_list[s_touch.inv_count++] = i;
	}
	s_touch.inv_list_tab = s_touch.inv_tab;
	s_touch.inv_list_catalog = g_forge.catalog_count;
}

static int forge_touch_inventory_rows(void)
{
	return (s_touch.inv_count + FORGE_TOUCH_INVENTORY_COLUMNS - 1) / FORGE_TOUCH_INVENTORY_COLUMNS;
}

static void forge_touch_inventory_scroll_to(float rows)
{
	float most = (float)(forge_touch_inventory_rows() - FORGE_TOUCH_INVENTORY_ROWS);

	if (most < 0.0f)
		most = 0.0f;
	if (rows > most)
		rows = most;
	if (rows < 0.0f)
		rows = 0.0f;
	s_touch.inv_scroll = rows;
}

static void forge_touch_inventory_open(void)
{
	int tab;

	/* (made again each time: another map brings its own objects, its new
	weapons too, which the catalog has as _forge_origin_map) */
	s_touch.inv_counts_catalog = -1;
	s_touch.inv_list_tab = -1;
	forge_touch_inventory_counts();
	/* (a tab with something in it, the last one used if it has) */
	if (s_touch.inv_tab < 0 || s_touch.inv_tab >= NUMBER_OF_FORGE_PALETTE_CATEGORIES ||
		!s_touch.inv_tab_count[s_touch.inv_tab])
	{
		for (tab = 0; tab < FORGE_TOUCH_TAB_COUNT; tab++)
		{
			if (s_touch.inv_tab_count[s_forge_touch_tabs[tab].category])
			{
				s_touch.inv_tab = s_forge_touch_tabs[tab].category;
				s_touch.inv_scroll = 0.0f;
				break;
			}
		}
	}
	s_touch.inv_open = 1;
	s_touch.chosen = -1;
}

/* a square of the inventory tapped: that object in hand, the inventory shut */
static void forge_touch_inventory_choose(int place)
{
	struct forge_catalog_entry const *entry;

	forge_touch_inventory_list();
	if (place < 0 || place >= s_touch.inv_count)
		return;
	entry = &g_forge.catalog[s_touch.inv_list[place]];
	if (!forge_touch_entry_placeable(entry))
		return;
	forge_touch_hotbar_note(entry);
	s_touch.inv_open = 0;
	forge_set_status("En la mano: %s. Toca el suelo para ponerlo",
		entry->display_name[0] ? entry->display_name : entry->tag_path);
}

static void forge_touch_press(int what)
{
	int vk;

	/* (a slot of the quick bar: in hand, or out of it if it was) */
	if (what >= _forge_touch_button_hotbar && what < _forge_touch_button_hotbar + FORGE_TOUCH_HOTBAR_SLOTS)
	{
		int slot = what - _forge_touch_button_hotbar;

		if (!s_touch.hotbar[slot].used || s_touch.hand == slot)
		{
			s_touch.hand = -1;
			forge_set_status("Mano vacia: toca un objeto para elegirlo");
		}
		else
		{
			s_touch.hand = slot;
			s_touch.chosen = -1;
			forge_set_status("En la mano: %s. Toca el suelo para ponerlo", s_touch.hotbar[slot].name);
		}
		return;
	}
	/* (the inventory's tabs and squares) */
	if (what >= _forge_touch_button_tab && what < _forge_touch_button_tab + NUMBER_OF_FORGE_PALETTE_CATEGORIES)
	{
		s_touch.inv_tab = what - _forge_touch_button_tab;
		s_touch.inv_scroll = 0.0f;
		return;
	}
	if (what >= _forge_touch_button_cell && what < _forge_touch_button_cell + FORGE_TOUCH_INVENTORY_MAX)
	{
		forge_touch_inventory_choose(what - _forge_touch_button_cell);
		return;
	}
	switch (what)
	{
	case _forge_touch_button_palette:
		forge_touch_inventory_open();
		break;
	case _forge_touch_button_inventory_close:
		s_touch.inv_open = 0;
		break;
	case _forge_touch_button_inventory_full:
		/* (the Forge's own palette, which has all: the AI, the volumes...) */
		s_touch.inv_open = 0;
		vk = g_forge.bind_vk[FORGE_BIND_PALETTE];
		forge_touch_pulse(vk > 0 ? vk : VK_F2);
		s_touch.chosen = -1;
		break;
	case _forge_touch_button_undo:
	case _forge_touch_button_redo:
		s_touch.chosen = -1;
		if (!forge_3d_history_step(what == _forge_touch_button_redo))
			forge_set_status(what == _forge_touch_button_redo ? "Nada que rehacer" : "Nada que deshacer");
		break;
	case _forge_touch_button_save:
		forge_trigger_save_map_action();
		break;
	case _forge_touch_button_map:
		s_touch.chosen = -1;
		forge_map_settings_menu_show();
		break;
	case _forge_touch_button_test:
		vk = g_forge.bind_vk[FORGE_BIND_TOGGLE_MODE];
		forge_touch_pulse(vk > 0 ? vk : VK_F1);
		s_touch.chosen = -1;
		break;
	case _forge_touch_button_up:
		s_touch.key_held[VK_SPACE] = 1;
		break;
	case _forge_touch_button_down:
		s_touch.key_held[VK_CONTROL] = 1;
		break;
	case _forge_touch_button_help:
		s_touch.help = !s_touch.help;
		break;
	case _forge_touch_button_move:
		if (forge_touch_live_valid(s_touch.chosen) >= 0)
			forge_touch_grab(s_touch.chosen);
		s_touch.chosen = -1;
		break;
	case _forge_touch_button_turn_left:
	case _forge_touch_button_turn_right:
	{
		float step = forge_touch_turn_step() * (what == _forge_touch_button_turn_left ? 1.0f : -1.0f);

		if (g_forge.grabbed_live_index >= 0)
		{
			g_forge.grab_yaw += step * 0.0174532925f;
			while (g_forge.grab_yaw > 3.14159265f)
				g_forge.grab_yaw -= 6.283185307f;
			while (g_forge.grab_yaw < -3.14159265f)
				g_forge.grab_yaw += 6.283185307f;
		}
		else
		{
			forge_touch_chosen_turn(step);
		}
		break;
	}
	case _forge_touch_button_copy:
		if (forge_touch_live_valid(s_touch.chosen) >= 0)
		{
			forge_touch_force(s_touch.chosen, NONE);
			vk = g_forge.bind_vk[FORGE_BIND_COPY_OBJECT];
			forge_touch_pulse(vk > 0 ? vk : 'C');
		}
		s_touch.chosen = -1;
		break;
	case _forge_touch_button_settings:
		if (g_forge.grabbed_live_index >= 0)
		{
			forge_props_open();
		}
		else if (forge_touch_live_valid(s_touch.chosen) >= 0)
		{
			forge_select_instance(s_touch.chosen);
			g_forge.grabbed_for_settings = 1;
			forge_props_open();
		}
		s_touch.chosen = -1;
		break;
	case _forge_touch_button_delete:
		if (g_forge.grabbed_live_index >= 0)
		{
			forge_3d_delete_instance(g_forge.grabbed_live_index, 1);
			g_forge.grabbed_live_index = -1;
			g_forge.grabbed_object_index = NONE;
			forge_set_status("Borrado");
		}
		else if (forge_touch_live_valid(s_touch.chosen) >= 0)
		{
			forge_3d_delete_instance(s_touch.chosen, 1);
			forge_set_status("Borrado");
		}
		s_touch.chosen = -1;
		break;
	case _forge_touch_button_done:
		s_touch.chosen = -1;
		break;
	case _forge_touch_button_drop:
		if (g_forge.grabbed_live_index >= 0)
			forge_place_held_item();
		break;
	case _forge_touch_button_cancel:
		if (g_forge.grabbed_live_index >= 0)
			forge_cancel_or_release_held_item();
		break;
	case _forge_touch_button_floor:
		/* (the Forge's DROP TO FLOOR, held while the button is: forge_touch_release) */
		vk = g_forge.bind_vk[FORGE_BIND_SNAP_FLOOR];
		s_touch.key_held[vk > 0 && vk < 256 ? vk : 'Z'] = 1;
		break;
	case _forge_touch_button_snap:
		vk = g_forge.bind_vk[FORGE_BIND_ROT_SNAP];
		forge_touch_pulse(vk > 0 ? vk : 'G');
		break;
	case _forge_touch_button_back:
		forge_touch_pulse(VK_ESCAPE);
		break;
	case _forge_touch_button_export:
		s_touch.chosen = -1;
		forge_touch_export();
		break;
	case _forge_touch_button_clear:
	{
		uint32_t now = (uint32_t)system_milliseconds();

		if (s_touch.clear_armed_ms && now - s_touch.clear_armed_ms < 4000u)
		{
			s_touch.clear_armed_ms = 0;
			forge_touch_clear_map();
		}
		else
		{
			s_touch.clear_armed_ms = now ? now : 1u;
			forge_set_status("Toca VACIAR otra vez para quitar todos los objetos del mapa");
		}
		break;
	}
	}
}

/* a held button let go */
static void forge_touch_release(int what)
{
	int vk;

	if (what == _forge_touch_button_up)
		s_touch.key_held[VK_SPACE] = 0;
	else if (what == _forge_touch_button_down)
		s_touch.key_held[VK_CONTROL] = 0;
	else if (what == _forge_touch_button_floor)
	{
		vk = g_forge.bind_vk[FORGE_BIND_SNAP_FLOOR];
		s_touch.key_held[vk > 0 && vk < 256 ? vk : 'Z'] = 0;
	}
}

/* ---------- the fingers */

static void forge_touch_let_go_all(void)
{
	int i;

	for (i = 0; i < FORGE_TOUCH_MAX_FINGERS; i++)
		s_touch.fingers[i].used = 0;
	if (s_touch.gesture == _forge_touch_gesture_button && s_touch.button >= 0)
		forge_touch_release(s_touch.button);
	/* (what was being carried is put down where it is) */
	if (s_touch.gesture == _forge_touch_gesture_carry && g_forge.grabbed_live_index >= 0)
		forge_place_held_item();
	memset(s_touch.key_held, 0, sizeof(s_touch.key_held));
	s_touch.primary = -1;
	s_touch.gesture = _forge_touch_gesture_none;
	s_touch.button = -1;
	s_touch.twist = 0;
	s_touch.menu_finger = -1;
}

/* a finger on one of the Forge's menus */
static void forge_touch_menu_event(int action, int id, float x, float y)
{
	switch (action)
	{
	case FORGE_TOUCH_DOWN:
		if (s_touch.menu_finger >= 0)
			return;
		s_touch.menu_finger = id;
		s_touch.menu_down_x = s_touch.menu_x = x;
		s_touch.menu_down_y = s_touch.menu_y = y;
		s_touch.menu_scroll = 0.0f;
		s_touch.menu_dragged = 0;
		s_touch.menu_moved = 1;
		break;
	case FORGE_TOUCH_MOVE:
		if (id != s_touch.menu_finger)
			return;
		if (!s_touch.menu_dragged && forge_touch_abs(y - s_touch.menu_down_y) > FORGE_TOUCH_SLOP)
			s_touch.menu_dragged = 1;
		if (s_touch.menu_dragged)
		{
			/* (the finger down shows what is above: the wheel's away notches) */
			s_touch.menu_scroll += y - s_touch.menu_y;
			while (s_touch.menu_scroll >= FORGE_TOUCH_SCROLL_STEP)
			{
				s_touch.menu_wheel++;
				s_touch.menu_scroll -= FORGE_TOUCH_SCROLL_STEP;
			}
			while (s_touch.menu_scroll <= -FORGE_TOUCH_SCROLL_STEP)
			{
				s_touch.menu_wheel--;
				s_touch.menu_scroll += FORGE_TOUCH_SCROLL_STEP;
			}
		}
		s_touch.menu_x = x;
		s_touch.menu_y = y;
		break;
	case FORGE_TOUCH_UP:
		if (id != s_touch.menu_finger)
			return;
		s_touch.menu_finger = -1;
		if (!s_touch.menu_dragged)
		{
			int button = forge_touch_button_at(x, y);

			if (button >= 0)
			{
				forge_touch_press(s_touch.buttons[button].what);
				forge_touch_release(s_touch.buttons[button].what);
			}
			else
			{
				s_touch.menu_x = s_touch.menu_click_x = x;
				s_touch.menu_y = s_touch.menu_click_y = y;
				s_touch.menu_moved = 1;
				s_touch.menu_clicks++;
			}
		}
		break;
	case FORGE_TOUCH_CANCEL:
		s_touch.menu_finger = -1;
		break;
	}
}

/* a finger on the map */
static void forge_touch_map_event(int action, int id, float x, float y)
{
	int index = forge_touch_finger_of(id);
	struct forge_touch_finger *finger;

	switch (action)
	{
	case FORGE_TOUCH_DOWN:
		if (index < 0)
		{
			for (index = 0; index < FORGE_TOUCH_MAX_FINGERS && s_touch.fingers[index].used; index++)
				;
			if (index == FORGE_TOUCH_MAX_FINGERS)
				return;
		}
		finger = &s_touch.fingers[index];
		finger->used = 1;
		finger->id = id;
		finger->x = finger->down_x = x;
		finger->y = finger->down_y = y;
		finger->down_ms = (uint32_t)system_milliseconds();
		finger->moved = 0;
		if (s_touch.primary < 0 && s_touch.inv_open)
		{
			/* (the inventory: what is tapped is chosen as the finger lifts, a
			drag on its squares scrolls them) */
			s_touch.primary = index;
			s_touch.gesture = _forge_touch_gesture_inventory;
			s_touch.button = -1;
			s_touch.inv_scroll_down = s_touch.inv_scroll;
			s_touch.inv_drag = x >= s_touch.inv_grid_x0 && x < s_touch.inv_grid_x1 && y >= s_touch.inv_grid_y0 &&
				y < s_touch.inv_grid_y1;
		}
		else if (s_touch.primary < 0)
		{
			int button = forge_touch_button_at(x, y);

			s_touch.primary = index;
			if (button >= 0)
			{
				s_touch.gesture = _forge_touch_gesture_button;
				s_touch.button = s_touch.buttons[button].what;
				forge_touch_press(s_touch.button);
			}
			else if (g_forge.grabbed_live_index >= 0)
			{
				/* (what is held follows the finger along the ground at once, and
				goes down where it is lifted: a tap puts it there) */
				s_touch.gesture = _forge_touch_gesture_carry;
				s_touch.button = -1;
			}
			else
			{
				s_touch.gesture = _forge_touch_gesture_pending;
				s_touch.button = -1;
				forge_touch_pick_at(x, y, &s_touch.down_live, &s_touch.down_object);
				s_touch.last_down_live = s_touch.down_live;
				s_touch.last_down_object = s_touch.down_object;
			}
		}
		else if (s_touch.gesture != _forge_touch_gesture_button && s_touch.gesture != _forge_touch_gesture_inventory)
		{
			/* (a second finger: the two turn what is held) */
			struct forge_touch_finger const *first = &s_touch.fingers[s_touch.primary];

			s_touch.twist = 1;
			s_touch.twist_angle = (float)atan2((double)(y - first->y), (double)(x - first->x));
			/* (and what the first was doing becomes carrying, or nothing) */
			if (s_touch.gesture == _forge_touch_gesture_pending || s_touch.gesture == _forge_touch_gesture_look)
				s_touch.gesture = g_forge.grabbed_live_index >= 0 ? _forge_touch_gesture_carry : _forge_touch_gesture_none;
		}
		break;
	case FORGE_TOUCH_MOVE:
		if (index < 0)
			return;
		finger = &s_touch.fingers[index];
		if (index == s_touch.primary && s_touch.gesture == _forge_touch_gesture_look && !s_touch.twist)
		{
			/* (the view follows the finger: right turns right, up looks up) */
			s_touch.look_yaw -= (x - finger->x) * FORGE_TOUCH_LOOK_YAW;
			s_touch.look_pitch -= (y - finger->y) * FORGE_TOUCH_LOOK_PITCH;
		}
		finger->x = x;
		finger->y = y;
		if (forge_touch_abs(x - finger->down_x) > FORGE_TOUCH_SLOP || forge_touch_abs(y - finger->down_y) > FORGE_TOUCH_SLOP)
			finger->moved = 1;
		if (index == s_touch.primary && s_touch.gesture == _forge_touch_gesture_inventory && s_touch.inv_drag &&
			finger->moved && s_touch.inv_row_h > 0.0f)
		{
			/* (the squares follow the finger: up shows the ones below) */
			forge_touch_inventory_scroll_to(s_touch.inv_scroll_down - (y - finger->down_y) / s_touch.inv_row_h);
		}
		if (s_touch.twist && g_forge.grabbed_live_index >= 0)
		{
			int other = index == s_touch.primary ? forge_touch_second_finger() : s_touch.primary;

			if (other >= 0)
			{
				struct forge_touch_finger const *a = &s_touch.fingers[s_touch.primary == other ? other : index];
				struct forge_touch_finger const *b = &s_touch.fingers[s_touch.primary == other ? index : other];
				float angle = (float)atan2((double)(b->y - a->y), (double)(b->x - a->x));
				float turn = angle - s_touch.twist_angle;

				while (turn > 3.14159265f)
					turn -= 6.283185307f;
				while (turn < -3.14159265f)
					turn += 6.283185307f;
				/* (clockwise on the screen turns it clockwise seen from above) */
				g_forge.grab_yaw -= turn;
				while (g_forge.grab_yaw > 3.14159265f)
					g_forge.grab_yaw -= 6.283185307f;
				while (g_forge.grab_yaw < -3.14159265f)
					g_forge.grab_yaw += 6.283185307f;
				s_touch.twist_angle = angle;
			}
		}
		break;
	case FORGE_TOUCH_UP:
		if (index < 0)
			return;
		finger = &s_touch.fingers[index];
		finger->x = x;
		finger->y = y;
		finger->used = 0;
		if (index != s_touch.primary)
		{
			s_touch.twist = 0;
			break;
		}
		switch (s_touch.gesture)
		{
		case _forge_touch_gesture_button:
			if (s_touch.button >= 0)
				forge_touch_release(s_touch.button);
			break;
		case _forge_touch_gesture_inventory:
			if (!finger->moved)
			{
				int button = forge_touch_button_at(x, y);

				if (button >= 0)
					forge_touch_press(s_touch.buttons[button].what);
			}
			break;
		case _forge_touch_gesture_pending:
			if (!finger->moved && (uint32_t)system_milliseconds() - finger->down_ms <= FORGE_TOUCH_TAP_MS)
			{
				if (g_forge.grabbed_live_index >= 0)
				{
					forge_place_held_item();
				}
				else if (s_touch.hand >= 0 && s_touch.hotbar[s_touch.hand].used)
				{
					/* (with something in hand, a tap puts one there, as a block) */
					forge_touch_place_hand(x, y);
				}
				else
				{
					/* (what was under the finger is chosen; nothing lets go) */
					s_touch.chosen = forge_touch_live_valid(forge_touch_resolve(s_touch.down_live, s_touch.down_object));
					if (s_touch.chosen >= 0)
					{
						char const *slash = strrchr(g_forge.live_instances[s_touch.chosen].tag_path, '\\');

						forge_set_status("Elegido: %s", slash ? slash + 1 : g_forge.live_instances[s_touch.chosen].tag_path);
					}
				}
			}
			break;
		case _forge_touch_gesture_carry:
			/* (put down where it is: under the finger, which it has followed) */
			if (g_forge.grabbed_live_index >= 0)
				forge_place_held_item();
			break;
		default:
			break;
		}
		s_touch.primary = forge_touch_second_finger();
		s_touch.gesture = _forge_touch_gesture_none;
		s_touch.button = -1;
		s_touch.twist = 0;
		/* (a finger left behind after two only looks) */
		if (s_touch.primary >= 0)
			s_touch.gesture = s_touch.inv_open ? _forge_touch_gesture_none :
				g_forge.grabbed_live_index >= 0 ? _forge_touch_gesture_carry : _forge_touch_gesture_look;
		break;
	case FORGE_TOUCH_CANCEL:
		forge_touch_let_go_all();
		break;
	}
}

/* (forge_3d_update, first thing each frame, before forge_pointer_poll) */
static void forge_touch_update(void)
{
	int events[FORGE_TOUCH_READ * FORGE_TOUCH_FIELDS];
	int count = 0;
	int i;

	forge_touch_ready();
	for (i = 0; i < 256; i++)
	{
		if (s_touch.key_pulse[i])
			s_touch.key_pulse[i]--;
	}
	if (s_touch.force_frames > 0 && --s_touch.force_frames == 0)
	{
		s_touch.force_live = -1;
		s_touch.force_object = NONE;
	}
	s_touch.pick = 0;
#if FORGE_TOUCH_HOST
	count = host_touch_forge_read(events, FORGE_TOUCH_READ);
#else
	(void)events;
#endif
	if (count > 0)
		s_touch.seen = 1;
	if (!forge_touch_monitor())
	{
		if (s_touch.primary >= 0 || s_touch.menu_finger >= 0)
			forge_touch_let_go_all();
		s_touch.chosen = -1;
		s_touch.inv_open = 0;
		return;
	}
	/* (a menu opened or closed under the fingers: they start again) */
	{
		static int s_was_menu;
		int menu = g_forge.active_modal != _forge_modal_none;

		if (menu != s_was_menu)
		{
			forge_touch_let_go_all();
			s_touch.chosen = -1;
			s_touch.inv_open = 0;
			s_was_menu = menu;
		}
	}
	for (i = 0; i < count; i++)
	{
		int action = events[i * FORGE_TOUCH_FIELDS];
		int id = events[i * FORGE_TOUCH_FIELDS + 1];
		float x = (float)events[i * FORGE_TOUCH_FIELDS + 2] / 10000.0f;
		float y = (float)events[i * FORGE_TOUCH_FIELDS + 3] / 10000.0f;

		if (g_forge.active_modal != _forge_modal_none)
			forge_touch_menu_event(action, id, x, y);
		else
			forge_touch_map_event(action, id, x, y);
	}
	if (g_forge.active_modal != _forge_modal_none)
		return;
	/* (what is chosen is let go once gone, picked up, or out of reach) */
	if (forge_touch_live_valid(s_touch.chosen) < 0 || g_forge.grabbed_live_index >= 0)
		s_touch.chosen = -1;
	/* (the finger on the map: a drag becomes carrying or looking, as what
	was under it when it went down says) */
	if (s_touch.primary >= 0 && s_touch.gesture == _forge_touch_gesture_pending &&
		s_touch.fingers[s_touch.primary].moved)
	{
		if (g_forge.grabbed_live_index >= 0)
		{
			s_touch.gesture = _forge_touch_gesture_carry;
		}
		else
		{
			int32_t live = forge_touch_live_valid(forge_touch_resolve(s_touch.down_live, s_touch.down_object));

			s_touch.chosen = -1;
			if (live >= 0)
			{
				forge_touch_grab(live);
				s_touch.gesture = g_forge.grabbed_live_index >= 0 ? _forge_touch_gesture_carry : _forge_touch_gesture_look;
			}
			else
			{
				s_touch.gesture = _forge_touch_gesture_look;
			}
		}
	}
	/* (a finger held still on an object deletes it, as Minecraft breaks a
	block: then it does nothing more until it is lifted) */
	if (s_touch.primary >= 0 && s_touch.gesture == _forge_touch_gesture_pending &&
		!s_touch.fingers[s_touch.primary].moved && g_forge.grabbed_live_index < 0 &&
		(s_touch.down_live >= 0 || s_touch.down_object != NONE) &&
		(uint32_t)system_milliseconds() - s_touch.fingers[s_touch.primary].down_ms >= FORGE_TOUCH_DELETE_HOLD_MS)
	{
		int32_t live = forge_touch_live_valid(forge_touch_resolve(s_touch.down_live, s_touch.down_object));

		if (live >= 0)
		{
			char name[64];
			char const *slash = strrchr(g_forge.live_instances[live].tag_path, '\\');

			snprintf(name, sizeof(name), "%s", slash ? slash + 1 : g_forge.live_instances[live].tag_path);
			forge_3d_delete_instance(live, 1);
			forge_set_status("Borrado: %s", name);
		}
		s_touch.chosen = -1;
		s_touch.down_live = -1;
		s_touch.down_object = NONE;
		s_touch.gesture = _forge_touch_gesture_none;
	}
	/* (the pick through the finger while it is on the map, but not while
	it looks) */
	if (s_touch.primary >= 0 && s_touch.gesture != _forge_touch_gesture_look &&
		s_touch.gesture != _forge_touch_gesture_button && s_touch.gesture != _forge_touch_gesture_none)
	{
		s_touch.pick = 1;
		s_touch.pick_x = s_touch.fingers[s_touch.primary].x;
		s_touch.pick_y = s_touch.fingers[s_touch.primary].y;
	}
}

/* (forge_pointer_poll, where a desktop's mouse is read) the menus' pointer:
the latest tap, the finger's place and its scrolling; 0 with none */
static int forge_touch_menu_pointer(int wanted, struct halo_ui_pointer *pointer)
{
	short x, y;

	forge_touch_ready();
	if (!wanted || (!s_touch.menu_moved && !s_touch.menu_clicks && !s_touch.menu_wheel))
		return 0;
	memset(pointer, 0, sizeof(*pointer));
	forge_touch_to_window(s_touch.menu_x, s_touch.menu_y, &x, &y);
	pointer->x = x;
	pointer->y = y;
	forge_touch_to_window(s_touch.menu_click_x, s_touch.menu_click_y, &x, &y);
	pointer->click_x = x;
	pointer->click_y = y;
	pointer->moved = (unsigned char)(s_touch.menu_moved != 0);
	pointer->left_clicks = (unsigned char)(s_touch.menu_clicks > 0 ? 1 : 0);
	pointer->wheel_steps = (signed char)(s_touch.menu_wheel > 8 ? 8 : s_touch.menu_wheel < -8 ? -8 : s_touch.menu_wheel);
	pointer->touch = 1;
	s_touch.menu_moved = 0;
	if (s_touch.menu_clicks > 0)
		s_touch.menu_clicks--;
	s_touch.menu_wheel -= pointer->wheel_steps;
	return 1;
}

/* ---------- the pick through the finger */

/* ---------- the Forge's camera, as the screen shows it

The touch works out where things are on the screen, and the line through a
point of it, from the Forge's own camera (the Monitor's pose, as the observer
is given it: forge_compute_live_monitor_camera_pose) and the field of view the
game draws it with (main.c's: the observer's 80 degrees, made vertical, and
display.fov's, view_fov.c), never from the renderer's globals: when the Forge
runs, those are the menus' and the HUD's pass, not the world's, and nothing was
ever found under a finger. The screen's shape is the HUD's (its window and
margins, forge_touch_draw). */

extern real render_fov_vertical(short local_player_index, real native_vertical_field_of_view);
static void forge_compute_live_monitor_camera_pose(real_point3d *out_pos, real_vector3d *out_fwd, real_vector3d *out_up);

struct forge_touch_camera
{
	real_point3d position;
	real_vector3d forward, right, up;
	/* the tangents of half the view, up and across */
	float tangent_y, tangent_x;
};

static boolean forge_touch_normalize(real_vector3d *v)
{
	float length = (float)sqrt((double)(v->i * v->i + v->j * v->j + v->k * v->k));

	if (length < 0.000001f)
		return FALSE;
	v->i /= length;
	v->j /= length;
	v->k /= length;
	return TRUE;
}

/* the screen's width over its height */
static float forge_touch_aspect(void)
{
	if (s_touch.window_known && s_touch.window_y0 + s_touch.window_y1 > 0)
	{
		float aspect = (float)(s_touch.window_x0 + s_touch.window_x1) / (float)(s_touch.window_y0 + s_touch.window_y1);

		if (aspect > 0.5f && aspect < 4.0f)
			return aspect;
	}
	return 16.0f / 9.0f;
}

static boolean forge_touch_camera_get(struct forge_touch_camera *camera)
{
	/* (main.c: 2 atan(0.75 tan(fov / 2) 0.85), the observer's fov the Monitor's 80 degrees) */
	float native = 2.0f * (float)atan(0.75 * 0.85 * tan(1.3962634 * 0.5));
	float vertical = render_fov_vertical(0, native);

	forge_compute_live_monitor_camera_pose(&camera->position, &camera->forward, &camera->up);
	if (!forge_touch_normalize(&camera->forward) || !forge_touch_normalize(&camera->up))
		return FALSE;
	/* (Halo's world: x ahead, y left, z up; ahead across up is the right) */
	camera->right.i = camera->forward.j * camera->up.k - camera->forward.k * camera->up.j;
	camera->right.j = camera->forward.k * camera->up.i - camera->forward.i * camera->up.k;
	camera->right.k = camera->forward.i * camera->up.j - camera->forward.j * camera->up.i;
	if (!forge_touch_normalize(&camera->right))
		return FALSE;
	if (!(vertical > 0.1f && vertical < 3.0f))
		vertical = native;
	camera->tangent_y = (float)tan((double)(vertical * 0.5f));
	camera->tangent_x = camera->tangent_y * forge_touch_aspect();
	return camera->tangent_y > 0.0f;
}

/* where a point of the world is on the screen (shares, 0..1 across and down);
FALSE when it is behind the camera. depth: how far ahead it is */
static boolean forge_touch_on_screen(real_point3d const *point, float *x, float *y, float *depth)
{
	struct forge_touch_camera camera;
	float vx, vy, vz, ahead, across, up;

	if (!forge_touch_camera_get(&camera))
		return FALSE;
	vx = point->x - camera.position.x;
	vy = point->y - camera.position.y;
	vz = point->z - camera.position.z;
	ahead = vx * camera.forward.i + vy * camera.forward.j + vz * camera.forward.k;
	if (ahead < 0.1f)
		return FALSE;
	across = vx * camera.right.i + vy * camera.right.j + vz * camera.right.k;
	up = vx * camera.up.i + vy * camera.up.j + vz * camera.up.k;
	*x = 0.5f + 0.5f * across / (ahead * camera.tangent_x);
	*y = 0.5f - 0.5f * up / (ahead * camera.tangent_y);
	if (depth)
		*depth = ahead;
	return TRUE;
}

/* the line from the camera through a point of the screen (shares) */
static boolean forge_touch_screen_ray(float x, float y, real_vector3d *direction)
{
	struct forge_touch_camera camera;
	float across, up;

	if (!forge_touch_camera_get(&camera))
		return FALSE;
	across = (2.0f * x - 1.0f) * camera.tangent_x;
	up = (1.0f - 2.0f * y) * camera.tangent_y;
	direction->i = camera.forward.i + camera.right.i * across + camera.up.i * up;
	direction->j = camera.forward.j + camera.right.j * across + camera.up.j * up;
	direction->k = camera.forward.k + camera.right.k * across + camera.up.k * up;
	s_touch.last_ray_error = 0.0f;
	return forge_touch_normalize(direction);
}

/* ---------- what is under the finger

What is nearest the finger on the screen, of what the Forge edits that can be
seen from the camera: the Forge's own (placed, and the map's objects it has
taken over, spawns, flags, teleporters, AI spawns) and the map's objects it has
not yet. Not the Forge's reticle's way (a straight line, 10 world units at
most): anything seen, as far as FORGE_TOUCH_PICK_RANGE, by how near the
finger it shows, a little more for what shows big. */
#define FORGE_TOUCH_PICK_RANGE 80.0f
/* how far from the finger (a share of the screen's height) a point may show */
#define FORGE_TOUCH_PICK_REACH 0.07f

static float forge_touch_screen_distance(real_point3d const *point, float radius, float x, float y)
{
	float sx, sy, dx, dy, depth, size;
	struct forge_touch_camera camera;

	if (!forge_touch_on_screen(point, &sx, &sy, &depth) || !forge_touch_camera_get(&camera))
		return 1000.0f;
	dx = (sx - x) * forge_touch_aspect();
	dy = sy - y;
	/* (how big it shows: its radius over its depth, in shares of the height) */
	size = radius / (depth * camera.tangent_y * 2.0f);
	if (size > 0.2f)
		size = 0.2f;
	return (float)sqrt((double)(dx * dx + dy * dy)) - size;
}

static void forge_touch_pick_at(float x, float y, int32_t *live, long *object)
{
	float best = FORGE_TOUCH_PICK_REACH;
	long local_unit = player_control_get_unit_index(0);
	int32_t i;

	*live = -1;
	*object = NONE;
	/* (the Forge's own: markers by where they stand, objects by their middle) */
	for (i = 0; i < g_forge.live_instance_count; i++)
	{
		struct forge_live_instance const *inst = &g_forge.live_instances[i];
		struct object_datum *obj;
		real_point3d point;
		float radius = 0.3f, distance, dx, dy, dz;

		if (!inst->active)
			continue;
		obj = inst->object_index != NONE ? forge_valid_object_get(inst->object_index) : NULL;
		if (obj)
		{
			point.x = obj->object.bounding_sphere_center.x;
			point.y = obj->object.bounding_sphere_center.y;
			point.z = obj->object.bounding_sphere_center.z;
			radius = obj->object.bounding_sphere_radius;
		}
		else
		{
			point.x = inst->x;
			point.y = inst->y;
			point.z = inst->z + 0.15f;
		}
		dx = point.x - g_forge.cam_x;
		dy = point.y - g_forge.cam_y;
		dz = point.z - g_forge.cam_z;
		if (dx * dx + dy * dy + dz * dz > FORGE_TOUCH_PICK_RANGE * FORGE_TOUCH_PICK_RANGE)
			continue;
		distance = forge_touch_screen_distance(&point, radius, x, y);
		if (distance < best && forge_point_in_sight(point.x, point.y, point.z))
		{
			best = distance;
			*live = i;
			*object = NONE;
		}
	}
	/* (the map's objects the Forge has not taken over yet) */
	{
		struct object_iterator iter;
		struct object_datum *obj;
		unsigned long edit_mask = _object_mask_scenery | _object_mask_vehicle | _object_mask_weapon | _object_mask_equipment |
			_object_mask_biped | _object_mask_device;

		object_iterator_new(&iter, edit_mask, 0);
		while ((obj = object_iterator_next(&iter)) != NULL)
		{
			long index = iter.index;
			struct unit_datum *unit;
			real_point3d point;
			float distance, dx, dy, dz;
			int32_t k, owned = 0;

			if (index == local_unit || obj->object.parent_object_index != NONE)
				continue;
			unit = (struct unit_datum *)object_try_and_get_and_verify_type(index, _object_mask_biped);
			if (unit && unit->unit.player_index != NONE)
				continue;
			point.x = obj->object.bounding_sphere_center.x;
			point.y = obj->object.bounding_sphere_center.y;
			point.z = obj->object.bounding_sphere_center.z;
			dx = point.x - g_forge.cam_x;
			dy = point.y - g_forge.cam_y;
			dz = point.z - g_forge.cam_z;
			if (dx * dx + dy * dy + dz * dz > FORGE_TOUCH_PICK_RANGE * FORGE_TOUCH_PICK_RANGE)
				continue;
			distance = forge_touch_screen_distance(&point, obj->object.bounding_sphere_radius, x, y);
			if (distance >= best || !forge_point_in_sight(point.x, point.y, point.z))
				continue;
			/* (one the Forge has is its live instance, found above) */
			for (k = 0; k < g_forge.live_instance_count && !owned; k++)
				owned = g_forge.live_instances[k].active && g_forge.live_instances[k].object_index == index;
			if (owned)
				continue;
			best = distance;
			*live = -1;
			*object = index;
		}
	}
}

/* the Forge's live instance of what forge_touch_pick_at found (a map object
taken over now, as the Forge's click takes it), -1 for none */
static int32_t forge_touch_resolve(int32_t live, long object)
{
	if (forge_touch_live_valid(live) >= 0)
		return live;
	if (object != NONE && forge_valid_object_get(object))
		return adopt_default_scenario_object(object);
	return -1;
}

/* what is held from now on, as the Forge's click picks it up */
static void forge_touch_grab(int32_t live)
{
	struct forge_live_instance *inst;
	struct object_datum *obj;

	if (forge_touch_live_valid(live) < 0)
		return;
	if (g_forge.grabbed_live_index >= 0)
		forge_place_held_item();
	inst = &g_forge.live_instances[live];
	obj = forge_valid_object_get(inst->object_index);
	forge_select_instance(live);
	if (obj && obj->object.type == _object_type_vehicle)
	{
		forge_eject_all_passengers_from_vehicle(inst->object_index);
		if (forge_is_multiplayer_session())
		{
			forge_network_send_transform_ext(live, inst->object_index, inst->x, inst->y, inst->z, inst->yaw, inst->pitch,
				inst->roll, 1, 1);
		}
	}
	{
		char const *slash = strrchr(inst->tag_path, '\\');

		forge_set_status("Llevando: %s", slash ? slash + 1 : inst->tag_path);
	}
}

/* (forge_3d_update, as the palette puts an object in hand) the palette's
object goes first in the quick bar, and in hand: Markers, volumes and the
palette's folders are not for it (only what is placed as an object) */
static void forge_touch_hotbar_note(struct forge_catalog_entry const *entry)
{
	int slot, found = FORGE_TOUCH_HOTBAR_SLOTS - 1;

	forge_touch_ready();
	if (!entry || !s_touch.seen || !forge_touch_entry_placeable(entry))
		return;
	for (slot = 0; slot < FORGE_TOUCH_HOTBAR_SLOTS; slot++)
	{
		if (s_touch.hotbar[slot].used && s_touch.hotbar[slot].group_tag == entry->group_tag &&
			!_stricmp(s_touch.hotbar[slot].tag_path, entry->tag_path))
		{
			found = slot;
			break;
		}
	}
	for (slot = found; slot > 0; slot--)
		s_touch.hotbar[slot] = s_touch.hotbar[slot - 1];
	memset(&s_touch.hotbar[0], 0, sizeof(s_touch.hotbar[0]));
	s_touch.hotbar[0].used = 1;
	s_touch.hotbar[0].group_tag = entry->group_tag;
	s_touch.hotbar[0].source_cache_id = entry->source_cache_id;
	snprintf(s_touch.hotbar[0].tag_path, sizeof(s_touch.hotbar[0].tag_path), "%s", entry->tag_path);
	snprintf(s_touch.hotbar[0].name, sizeof(s_touch.hotbar[0].name), "%s",
		entry->display_name[0] ? entry->display_name : entry->tag_path);
	s_touch.hand = 0;
}

/* a tap with something in hand: one of it where the finger shows, on what is
there (the ground, a wall, another object), turned as the camera looks */
static void forge_touch_place_hand(float x, float y)
{
	real_vector3d ray;
	real_point3d origin;
	real_vector3d vector;
	struct collision_result collision;
	long local_unit = player_control_get_unit_index(0);
	int32_t live;
	float px, py, pz;

	if (s_touch.hand < 0 || !s_touch.hotbar[s_touch.hand].used)
		return;
	if (!forge_touch_screen_ray(x, y, &ray))
	{
		forge_set_status("No se pudo saber donde tocaste: prueba otra vez");
		return;
	}
	origin.x = g_forge.cam_x;
	origin.y = g_forge.cam_y;
	origin.z = g_forge.cam_z;
	vector.i = ray.i * FORGE_TOUCH_PICK_RANGE;
	vector.j = ray.j * FORGE_TOUCH_PICK_RANGE;
	vector.k = ray.k * FORGE_TOUCH_PICK_RANGE;
	memset(&collision, 0, sizeof(collision));
	if (!forge_collision_test_vector(
			FLAG(_collision_test_structure_bit) | FLAG(_collision_test_objects_bit) |
			FLAG(_collision_test_front_facing_surfaces_bit),
			&origin, &vector, local_unit, &collision))
	{
		forge_set_status("Toca el suelo o una pared para poner el objeto");
		return;
	}
	px = collision.point.x + collision.plane.n.i * 0.05f;
	py = collision.point.y + collision.plane.n.j * 0.05f;
	pz = collision.point.z + collision.plane.n.k * 0.05f;
	live = forge_3d_spawn_object_at(s_touch.hotbar[s_touch.hand].group_tag, s_touch.hotbar[s_touch.hand].tag_path,
		s_touch.hotbar[s_touch.hand].source_cache_id, px, py, pz, g_forge.cam_yaw, 0.0f, 0.0f, 0, 1);
	if (forge_touch_live_valid(live) < 0)
	{
		forge_set_status("No se pudo poner %s aqui", s_touch.hotbar[s_touch.hand].name);
		return;
	}
	/* (one step of DESHACER) */
	forge_history_note_placed(live, 1);
	forge_set_status("Puesto: %s", s_touch.hotbar[s_touch.hand].name);
}

/* (forge_3d_update, before the Forge looks along fwd for what it points at)
fwd becomes the finger's line while a finger is on the map */
static void forge_touch_pick(real_vector3d *fwd)
{
	real_vector3d ray;

	if (!s_touch.pick || g_forge.active_modal != _forge_modal_none)
		return;
	if (!forge_touch_screen_ray(s_touch.pick_x, s_touch.pick_y, &ray))
		return;
	/* (never behind the camera: a frame drawn from elsewhere) */
	if (ray.i * fwd->i + ray.j * fwd->j + ray.k * fwd->k <= 0.05f)
		return;
	*fwd = ray;
}

/* (forge_3d_update, after the pick's hover, before the Forge's label) what
the finger went down on, what a tap chooses, and what a click or copy acts on */
static void forge_touch_after_hover(void)
{
	if (s_touch.force_frames > 0)
	{
		if (forge_touch_live_valid(s_touch.force_live) >= 0)
		{
			g_forge.hovered_live_index = s_touch.force_live;
			g_forge.hovered_object_index = NONE;
		}
		else if (s_touch.force_object != NONE && forge_valid_object_get(s_touch.force_object))
		{
			g_forge.hovered_live_index = -1;
			g_forge.hovered_object_index = s_touch.force_object;
		}
	}
}

/* (forge_3d_update, before what is held is put along fwd) carried by a
finger, what is held goes as far as the first surface under the finger, so
that it slides along the ground and the walls */
static void forge_touch_hold_distance(real_vector3d const *fwd)
{
	real_point3d origin;
	real_vector3d vector;
	struct collision_result collision;
	long ignore = forge_valid_object_get(g_forge.grabbed_object_index) ? g_forge.grabbed_object_index : NONE;

	if (!s_touch.pick || s_touch.primary < 0 || s_touch.gesture != _forge_touch_gesture_carry)
		return;
	origin.x = g_forge.cam_x;
	origin.y = g_forge.cam_y;
	origin.z = g_forge.cam_z;
	vector.i = fwd->i * 30.0f;
	vector.j = fwd->j * 30.0f;
	vector.k = fwd->k * 30.0f;
	memset(&collision, 0, sizeof(collision));
	if (forge_collision_test_vector(
			FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
			&origin, &vector, ignore, &collision))
	{
		float distance = collision.t * 30.0f + 0.05f;

		g_forge.grab_distance = distance < 1.0f ? 1.0f : distance > 30.0f ? 30.0f : distance;
	}
}

/* (forge_3d_update, with the mouse's and the swipe's look) the finger's look */
static void forge_touch_look(float *yaw, float *pitch)
{
	*yaw += s_touch.look_yaw;
	*pitch += s_touch.look_pitch;
	s_touch.look_yaw = 0.0f;
	s_touch.look_pitch = 0.0f;
}

/* ---------- drawing (forge_3d_draw_hud, last) */

static void forge_touch_button_draw(
	long font_index,
	short x0, short y0, short x1, short y1,
	char const *text,
	int what,
	int lit,
	uint32_t color)
{
	rectangle2d r;

	if (s_touch.button_count < FORGE_TOUCH_MAX_BUTTONS)
	{
		struct forge_touch_button *b = &s_touch.buttons[s_touch.button_count++];

		b->x0 = x0;
		b->y0 = y0;
		b->x1 = x1;
		b->y1 = y1;
		b->what = what;
	}
	r.x0 = x0;
	r.y0 = y0;
	r.x1 = x1;
	r.y1 = y1;
	draw_quad(&r, lit ? 0xE01E5A8Cu : 0xB4081828u);
	draw_box_border(x0, y0, x1, y1, 1, lit ? 0xFFFFFFFFu : 0xFF2896FFu);
	draw_ce_menu_str_box(0, font_index, x0, (short)(y0 + (y1 - y0) / 2 - 7), x1, y1, 2, text, color);
}

/* a row of buttons from x, each width wide and gap apart; returns where it ends */
static short forge_touch_row(
	long font_index,
	short x, short y0, short y1, short width, short gap,
	char const *const *texts, int const *whats, int count)
{
	int i;

	for (i = 0; i < count; i++)
	{
		int lit = s_touch.primary >= 0 && s_touch.gesture == _forge_touch_gesture_button && s_touch.button == whats[i];

		forge_touch_button_draw(font_index, x, y0, (short)(x + width), y1, texts[i], whats[i], lit,
			whats[i] == _forge_touch_button_delete ? 0xFFFF8080u : 0xFFFFFFFFu);
		x = (short)(x + width + gap);
	}
	return x;
}

/* the inventory: its tabs at the left, its objects in big squares (the map's
own, new ones in yellow), CERRAR and LISTA COMPLETA at the top */
static void forge_touch_inventory_draw(
	long font,
	short window_x0, short window_y0, short window_x1, short window_y1,
	short bh, short gap)
{
	short w = (short)(window_x1 - window_x0);
	short h = (short)(window_y1 - window_y0);
	float screen_w = (float)(window_x0 + window_x1);
	float screen_h = (float)(window_y0 + window_y1);
	short px0 = (short)(window_x0 + w * 0.015f);
	short px1 = (short)(window_x1 - w * 0.015f);
	short py0 = (short)(window_y0 + h * 0.02f);
	short py1 = (short)(window_y0 + h * 0.80f);
	short top1 = (short)(py0 + bh);
	short tabs_x1 = (short)(px0 + w * 0.18f);
	short gx0 = (short)(tabs_x1 + gap * 2);
	short gy0 = (short)(top1 + gap * 2);
	short gy1 = py1;
	short cell_w, cell_h;
	int first, i, tab, rows;
	char label[32];
	rectangle2d r;

	forge_touch_inventory_counts();
	forge_touch_inventory_list();
	forge_touch_inventory_scroll_to(s_touch.inv_scroll);
	r.x0 = px0;
	r.y0 = py0;
	r.x1 = px1;
	r.y1 = py1;
	draw_quad(&r, 0xE6040C1Cu);
	draw_box_border(r.x0, r.y0, r.x1, r.y1, 1, 0xFF2896FFu);
	/* (the top: its name, how many, and its two buttons) */
	snprintf(label, sizeof(label), "INVENTARIO  %d", s_touch.inv_count);
	draw_ce_menu_str_box(0, font, (short)(px0 + 10), (short)(py0 + bh / 2 - 7), (short)(px0 + w * 0.4f), top1, 0, label,
		0xFFFFFFFFu);
	forge_touch_button_draw(font, (short)(px1 - w * 0.13f - gap), py0, (short)(px1 - gap), top1, "CERRAR",
		_forge_touch_button_inventory_close, 0, 0xFFFFFFFFu);
	forge_touch_button_draw(font, (short)(px1 - w * 0.36f - gap * 2), py0, (short)(px1 - w * 0.13f - gap * 2), top1,
		"LISTA COMPLETA", _forge_touch_button_inventory_full, 0, 0xFF9FD8FFu);
	/* (the tabs: those with something) */
	{
		short y = gy0;
		short tab_h = (short)((gy1 - gy0) / FORGE_TOUCH_TAB_COUNT - gap);

		if (tab_h > bh)
			tab_h = bh;
		for (tab = 0; tab < FORGE_TOUCH_TAB_COUNT; tab++)
		{
			int category = s_forge_touch_tabs[tab].category;

			if (!s_touch.inv_tab_count[category])
				continue;
			forge_touch_button_draw(font, (short)(px0 + gap), y, tabs_x1, (short)(y + tab_h), s_forge_touch_tabs[tab].name,
				_forge_touch_button_tab + category, category == s_touch.inv_tab, 0xFFFFFFFFu);
			y = (short)(y + tab_h + gap);
		}
	}
	/* (the squares) */
	cell_w = (short)((px1 - gap - gx0 - (FORGE_TOUCH_INVENTORY_COLUMNS - 1) * gap) / FORGE_TOUCH_INVENTORY_COLUMNS);
	cell_h = (short)((gy1 - gap - gy0 - (FORGE_TOUCH_INVENTORY_ROWS - 1) * gap) / FORGE_TOUCH_INVENTORY_ROWS);
	if (screen_w > 0.0f && screen_h > 0.0f)
	{
		s_touch.inv_grid_x0 = (float)gx0 / screen_w;
		s_touch.inv_grid_x1 = (float)(px1 - gap) / screen_w;
		s_touch.inv_grid_y0 = (float)gy0 / screen_h;
		s_touch.inv_grid_y1 = (float)(gy1 - gap) / screen_h;
		s_touch.inv_row_h = (float)(cell_h + gap) / screen_h;
	}
	if (!s_touch.inv_count)
	{
		draw_ce_menu_str_box(0, font, gx0, (short)(gy0 + bh), (short)(px1 - gap), (short)(gy0 + bh * 2), 2,
			"Este mapa no tiene objetos de este tipo", 0xFF9FD8FFu);
		return;
	}
	first = (int)(s_touch.inv_scroll + 0.5f) * FORGE_TOUCH_INVENTORY_COLUMNS;
	for (i = 0; i < FORGE_TOUCH_INVENTORY_COLUMNS * FORGE_TOUCH_INVENTORY_ROWS && first + i < s_touch.inv_count; i++)
	{
		struct forge_catalog_entry const *entry = &g_forge.catalog[s_touch.inv_list[first + i]];
		short x = (short)(gx0 + (i % FORGE_TOUCH_INVENTORY_COLUMNS) * (cell_w + gap));
		short y = (short)(gy0 + (i / FORGE_TOUCH_INVENTORY_COLUMNS) * (cell_h + gap));
		char const *name = entry->display_name[0] ? entry->display_name : entry->tag_path;
		char const *slash = entry->display_name[0] ? NULL : strrchr(entry->tag_path, '\\');

		forge_touch_label(label, 20, slash ? slash + 1 : name);
		forge_touch_button_draw(font, x, y, (short)(x + cell_w), (short)(y + cell_h), label, _forge_touch_button_cell + first + i,
			0, entry->origin == _forge_origin_map ? 0xFFFFE680u : 0xFFFFFFFFu);
	}
	/* (where in the list: a bar at the right when there are more rows) */
	rows = forge_touch_inventory_rows();
	if (rows > FORGE_TOUCH_INVENTORY_ROWS)
	{
		short track = (short)(gy1 - gap - gy0);
		short bar = (short)(track * FORGE_TOUCH_INVENTORY_ROWS / rows);
		short at = (short)(gy0 + (track - bar) * s_touch.inv_scroll / (float)(rows - FORGE_TOUCH_INVENTORY_ROWS));

		r.x0 = (short)(px1 - gap + 1);
		r.x1 = (short)(px1 - 1);
		r.y0 = at;
		r.y1 = (short)(at + bar);
		draw_quad(&r, 0xFF2896FFu);
	}
}

static void forge_touch_draw(short window_x0, short window_y0, short window_x1, short window_y1, long font_index)
{
	short w = (short)(window_x1 - window_x0);
	short h = (short)(window_y1 - window_y0);
	short bh = (short)(h * 0.085f);
	short gap = (short)(w * 0.008f);
	char buf[160];
	long font;

	forge_touch_ready();
	s_touch.window_known = 1;
	s_touch.window_x0 = window_x0;
	s_touch.window_y0 = window_y0;
	s_touch.window_x1 = window_x1;
	s_touch.window_y1 = window_y1;
	s_touch.button_count = 0;
	/* (only on a touchscreen: once a finger has been handed over) */
	if (!s_touch.seen || !g_forge_session_enabled || w <= 0 || h <= 0)
		return;
	set_forge_hud_ui_scale(1.0f, 0.0f, 0.0f);
	font = get_small_forge_font(font_index);
	if (g_forge.mode != _forge_mode_3d_monitor)
	{
		/* (the Spartan: back to the Monitor with the touch layout's D-pad Right) */
		draw_ce_menu_str_box(0, font, window_x0, (short)(window_y0 + h * 0.13f), window_x1, (short)(window_y0 + h * 0.2f), 2,
			"PRUEBA: vuelve a la Forja con la cruceta DERECHA", 0xFFFFC824u);
		return;
	}
	if (!forge_touch_monitor())
		return;
	if (g_forge.active_modal != _forge_modal_none)
	{
		static char const *const texts[] = { "VOLVER" };
		static int const whats[] = { _forge_touch_button_back };

		/* (the save box has its own SAVE and CANCEL) */
		if (g_forge.active_modal != _forge_modal_save_copy_as)
		{
			forge_touch_row(font, (short)(window_x1 - w * 0.15f), (short)(window_y0 + h * 0.02f),
				(short)(window_y0 + h * 0.02f + bh), (short)(w * 0.13f), gap, texts, whats, 1);
		}
		return;
	}
	/* the top: what is done to the map (not under the inventory) */
	if (!s_touch.inv_open)
	{
		static char const *const texts[] = { "+ OBJETOS", "DESHACER", "REHACER", "GUARDAR", "EXPORTAR" };
		static int const whats[] = {
			_forge_touch_button_palette, _forge_touch_button_undo, _forge_touch_button_redo, _forge_touch_button_save,
			_forge_touch_button_export };
		short y0 = (short)(window_y0 + h * 0.02f);

		forge_touch_row(font, (short)(window_x0 + w * 0.015f), y0, (short)(y0 + bh), (short)(w * 0.082f), gap, texts, whats, 5);
	}
	{
		static char const *const texts[] = { "MAPA", "VACIAR", "PROBAR", "?" };
		static int const whats[] = {
			_forge_touch_button_map, _forge_touch_button_clear, _forge_touch_button_test, _forge_touch_button_help };
		short y0 = (short)(window_y0 + h * 0.02f);
		short widths = (short)(w * 0.07f);

		/* (right of the overlay's Pause, which is at about 0.6 of the screen) */
		forge_touch_row(font, (short)(window_x0 + w * 0.65f), y0, (short)(y0 + bh), widths, gap, texts, whats, 4);
	}
	/* the right: up and down */
	if (!s_touch.inv_open)
	{
		short x0 = (short)(window_x1 - w * 0.1f);
		short x1 = (short)(window_x1 - w * 0.015f);
		short y = (short)(window_y0 + h * 0.36f);
		int lit_up = s_touch.key_held[VK_SPACE] != 0;
		int lit_down = s_touch.key_held[VK_CONTROL] != 0;

		forge_touch_button_draw(font, x0, y, x1, (short)(y + bh * 1.3f), "SUBIR", _forge_touch_button_up, lit_up, 0xFFFFFFFFu);
		y = (short)(y + bh * 1.3f + gap * 2);
		forge_touch_button_draw(font, x0, y, x1, (short)(y + bh * 1.3f), "BAJAR", _forge_touch_button_down, lit_down, 0xFFFFFFFFu);
	}
	/* the quick bar, at the bottom (Minecraft's): the objects taken from
	the palette, the one in hand lit */
	{
		short slot_w = (short)(w * 0.078f);
		short total = (short)(FORGE_TOUCH_HOTBAR_SLOTS * slot_w + (FORGE_TOUCH_HOTBAR_SLOTS - 1) * gap);
		short x = (short)(window_x0 + (w - total) / 2);
		short y0 = (short)(window_y0 + h * 0.83f);
		short y1 = (short)(y0 + bh);
		int slot;

		for (slot = 0; slot < FORGE_TOUCH_HOTBAR_SLOTS; slot++)
		{
			char label[16];
			int lit = slot == s_touch.hand;

			if (s_touch.hotbar[slot].used)
			{
				/* (its name's start: the slot is small) */
				size_t n;

				snprintf(label, sizeof(label), "%s", s_touch.hotbar[slot].name);
				for (n = 0; label[n]; n++)
				{
					if (label[n] >= 'a' && label[n] <= 'z')
						label[n] = (char)(label[n] - 'a' + 'A');
					else if (label[n] == '_')
						label[n] = ' ';
				}
				if (strlen(label) > 9)
					label[9] = 0;
			}
			else
			{
				label[0] = 0;
			}
			forge_touch_button_draw(font, x, y0, (short)(x + slot_w), y1, label, _forge_touch_button_hotbar + slot, lit,
				lit ? 0xFFFFE680u : 0xFFFFFFFFu);
			x = (short)(x + slot_w + gap);
		}
	}
	/* the inventory, over the rest but the quick bar */
	if (s_touch.inv_open)
	{
		forge_touch_inventory_draw(font, window_x0, window_y0, window_x1, window_y1, bh, gap);
		return;
	}
	/* (a finger held still on an object: how near it is to deleting it) */
	if (s_touch.primary >= 0 && s_touch.gesture == _forge_touch_gesture_pending &&
		!s_touch.fingers[s_touch.primary].moved && g_forge.grabbed_live_index < 0 &&
		(s_touch.down_live >= 0 || s_touch.down_object != NONE))
	{
		uint32_t held = (uint32_t)system_milliseconds() - s_touch.fingers[s_touch.primary].down_ms;

		if (held >= FORGE_TOUCH_DELETE_SHOW_MS)
		{
			short fx, fy;
			float part = (float)held / (float)FORGE_TOUCH_DELETE_HOLD_MS;
			short size = (short)(bh * 0.9f);
			rectangle2d r;

			forge_touch_to_window(s_touch.fingers[s_touch.primary].x, s_touch.fingers[s_touch.primary].y, &fx, &fy);
			if (part > 1.0f)
				part = 1.0f;
			fy = (short)(fy - size * 2);
			r.x0 = (short)(fx - size);
			r.y0 = fy;
			r.x1 = (short)(fx + size);
			r.y1 = (short)(fy + size / 3);
			draw_quad(&r, 0xB4081828u);
			r.x1 = (short)(r.x0 + (short)(2 * size * part));
			draw_quad(&r, 0xFFFF5050u);
			draw_box_border((short)(fx - size), fy, (short)(fx + size), (short)(fy + size / 3), 1, 0xFFFFFFFFu);
		}
	}
	/* the bar of what is held, or of what is chosen */
	{
		short y0 = (short)(window_y0 + h * 0.70f);
		short y1 = (short)(y0 + bh);
		short width = (short)(w * 0.088f);

		if (g_forge.grabbed_live_index >= 0)
		{
			static char const *const texts[] = { "< GIRAR", "GIRAR >", "AL SUELO", "AJUSTES", "BORRAR", "CANCELAR", "SOLTAR" };
			static int const whats[] = {
				_forge_touch_button_turn_left, _forge_touch_button_turn_right, _forge_touch_button_floor,
				_forge_touch_button_settings, _forge_touch_button_delete, _forge_touch_button_cancel, _forge_touch_button_drop };
			short total = (short)(7 * width + 6 * gap);

			forge_touch_row(font, (short)(window_x0 + (w - total) / 2), y0, y1, width, gap, texts, whats, 7);
		}
		else if (forge_touch_live_valid(s_touch.chosen) >= 0)
		{
			static char const *const texts[] = { "MOVER", "< GIRAR", "GIRAR >", "COPIAR", "AJUSTES", "BORRAR", "LISTO" };
			static int const whats[] = {
				_forge_touch_button_move, _forge_touch_button_turn_left, _forge_touch_button_turn_right,
				_forge_touch_button_copy, _forge_touch_button_settings, _forge_touch_button_delete, _forge_touch_button_done };
			short total = (short)(7 * width + 6 * gap);

			forge_touch_row(font, (short)(window_x0 + (w - total) / 2), y0, y1, width, gap, texts, whats, 7);
		}
	}
	/* what is chosen or held: its name and where it is (the Map Creation
	Studio's panel), and a mark on it */
	{
		int32_t live = g_forge.grabbed_live_index >= 0 ? forge_touch_live_valid(g_forge.grabbed_live_index) :
			forge_touch_live_valid(s_touch.chosen);

		if (live >= 0)
		{
			struct forge_live_instance const *inst = &g_forge.live_instances[live];
			char const *slash = strrchr(inst->tag_path, '\\');
			float yaw = inst->yaw * 57.2957795f;
			rectangle2d r;
			real_point3d point;
			real_point2d screen;

			r.x0 = (short)(window_x0 + w * 0.015f);
			r.y0 = (short)(window_y0 + h * 0.02f + bh + gap * 2);
			r.x1 = (short)(window_x0 + w * 0.43f);
			r.y1 = (short)(r.y0 + bh * 1.2f);
			draw_quad(&r, 0xB4081828u);
			draw_box_border(r.x0, r.y0, r.x1, r.y1, 1, 0xFF2896FFu);
			snprintf(buf, sizeof(buf), "%s", slash ? slash + 1 : inst->tag_path);
			draw_ce_menu_str_box(0, font, (short)(r.x0 + 6), (short)(r.y0 + 2), (short)(r.x1 - 6), r.y1, 0, buf, 0xFFFFFFFFu);
			snprintf(buf, sizeof(buf), "X %.1f  Y %.1f  Z %.1f  GIRO %d", inst->x, inst->y, inst->z,
				(int)(yaw + (yaw >= 0.0f ? 0.5f : -0.5f)));
			draw_ce_menu_str_box(0, font, (short)(r.x0 + 6), (short)(r.y0 + (r.y1 - r.y0) / 2), (short)(r.x1 - 6), r.y1, 0, buf,
				0xFF9FD8FFu);
			point.x = inst->x;
			point.y = inst->y;
			point.z = inst->z + 0.2f;
			if (g_forge.grabbed_live_index < 0 && forge_touch_on_screen(&point, &screen.x, &screen.y, NULL))
			{
				short sx, sy;

				forge_touch_to_window(screen.x, screen.y, &sx, &sy);
				draw_box_border((short)(sx - 18), (short)(sy - 18), (short)(sx + 18), (short)(sy + 18), 2, 0xFFFFC824u);
			}
		}
	}
	/* the help */
	if (s_touch.help)
	{
		static char const *const lines[] = {
			"+ OBJETOS: el inventario. Toca un objeto y queda en tu mano (amarillo: del mapa)",
			"Con algo en la mano (barra de abajo): toca el suelo y lo pones",
			"Manten el dedo quieto sobre un objeto: lo borras",
			"Toca un objeto: lo eliges (salen sus botones)",
			"Arrastra un objeto: lo llevas por el suelo y lo sueltas al levantar el dedo",
			"Arrastra donde no hay nada: miras alrededor",
			"Con un objeto agarrado, gira dos dedos: lo giras",
			"Toca donde quieras con un objeto agarrado: lo pones ahi",
			"Palanca: volar.  SUBIR / BAJAR: altura.  START: menu y guardar el mapa",
			"LISTA COMPLETA (en el inventario): IA, volumenes y lo demas de la Forja",
		};
		short x0 = (short)(window_x0 + w * 0.18f);
		short x1 = (short)(window_x1 - w * 0.18f);
		short y0 = (short)(window_y0 + h * 0.2f);
		short line = (short)(h * 0.055f);
		rectangle2d r;
		int i;

		r.x0 = x0;
		r.y0 = y0;
		r.x1 = x1;
		r.y1 = (short)(y0 + line * (short)(sizeof(lines) / sizeof(lines[0])) + line * 2);
		draw_quad(&r, 0xE0040C1Cu);
		draw_box_border(r.x0, r.y0, r.x1, r.y1, 1, 0xFF2896FFu);
		for (i = 0; i < (int)(sizeof(lines) / sizeof(lines[0])); i++)
		{
			draw_ce_menu_str_box(0, font, (short)(x0 + 10), (short)(y0 + line / 2 + i * line), (short)(x1 - 10),
				(short)(y0 + line / 2 + (i + 1) * line), 0, lines[i], 0xFFFFFFFFu);
		}
		/* (how the touch went: for a screenshot when something fails) */
		{
			struct forge_touch_camera camera;
			int known = forge_touch_camera_get(&camera);

			snprintf(buf, sizeof(buf), "Prueba tactil: camara %s  vista %.2f x %.2f  dedo en %d/%ld  agarrado %d",
				known ? "si" : "no", known ? camera.tangent_x : 0.0f, known ? camera.tangent_y : 0.0f,
				(int)s_touch.last_down_live, s_touch.last_down_object, (int)g_forge.grabbed_live_index);
		}
		draw_ce_menu_str_box(0, font, (short)(x0 + 10), (short)(y0 + line / 2 + i * line), (short)(x1 - 10),
			(short)(y0 + line / 2 + (i + 1) * line), 0, buf, 0xFF9FD8FFu);
	}
}
