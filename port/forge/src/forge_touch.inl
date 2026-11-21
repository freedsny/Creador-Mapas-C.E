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
#define FORGE_TOUCH_MAX_BUTTONS 32
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

enum
{
	_forge_touch_gesture_none = 0,
	/* down on the map, not yet a tap or a drag */
	_forge_touch_gesture_pending,
	_forge_touch_gesture_look,
	/* carrying what was under the finger (or what was already held) */
	_forge_touch_gesture_carry,
	/* on one of the Forge's buttons */
	_forge_touch_gesture_button
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
	NUMBER_OF_FORGE_TOUCH_BUTTONS,
	/* the quick bar's slots: this plus the slot's index */
	_forge_touch_button_hotbar = 100
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
	switch (what)
	{
	case _forge_touch_button_palette:
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
		if (s_touch.primary < 0)
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
		else if (s_touch.gesture != _forge_touch_gesture_button)
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
			s_touch.gesture = g_forge.grabbed_live_index >= 0 ? _forge_touch_gesture_carry : _forge_touch_gesture_look;
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

/* where a point of the world is on the screen (shares of the frame last
drawn's viewport, which is the whole screen in a game alone); FALSE when it
is behind the camera or off the screen */
static boolean forge_touch_on_screen(real_point3d const *point, float *x, float *y)
{
	struct render_camera const *camera = &render.camera;
	float view_w = (float)(camera->viewport_bounds.x1 - camera->viewport_bounds.x0);
	float view_h = (float)(camera->viewport_bounds.y1 - camera->viewport_bounds.y0);
	real_point2d screen;

	/* (not render.frustum.projection_valid: the frame's projection is still
	there once it is drawn, when the flag no longer says so; the Forge's own
	labels project this way too) */
	if (view_w <= 0.0f || view_h <= 0.0f || render.frustum.projection_matrix[0][0] == 0.0f ||
		!render_camera_world_to_screen(camera, &render.frustum, point, &screen))
	{
		return FALSE;
	}
	*x = (screen.x - (float)camera->viewport_bounds.x0) / view_w;
	*y = (screen.y - (float)camera->viewport_bounds.y0) / view_h;
	return TRUE;
}

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

/* how far from (x, y) a line from the camera along direction comes back on the
screen, in shares of its height (large when it cannot be told) */
static float forge_touch_ray_error(real_vector3d const *direction, float x, float y)
{
	struct render_camera const *camera = &render.camera;
	float view_w = (float)(camera->viewport_bounds.x1 - camera->viewport_bounds.x0);
	float view_h = (float)(camera->viewport_bounds.y1 - camera->viewport_bounds.y0);
	real_point3d point;
	float sx, sy, dx, dy;

	point.x = camera->position.x + direction->i * 10.0f;
	point.y = camera->position.y + direction->j * 10.0f;
	point.z = camera->position.z + direction->k * 10.0f;
	if (view_h <= 0.0f || !forge_touch_on_screen(&point, &sx, &sy))
		return 1000.0f;
	dx = (sx - x) * view_w / view_h;
	dy = sy - y;
	return (float)sqrt((double)(dx * dx + dy * dy));
}

/* the line from the camera through a point of the screen (shares), from the
frame last drawn; FALSE if it cannot be told. Three ways (the projection
undone, and the camera's axes with its field of view taken as vertical or as
horizontal), and the one that comes back on the screen nearest the point */
static boolean forge_touch_screen_ray(float x, float y, real_vector3d *direction)
{
	struct render_camera const *camera = &render.camera;
	struct render_frustum const *frustum = &render.frustum;
	float view_w = (float)(camera->viewport_bounds.x1 - camera->viewport_bounds.x0);
	float view_h = (float)(camera->viewport_bounds.y1 - camera->viewport_bounds.y0);
	float ndc_x = 2.0f * x - 1.0f;
	float ndc_y = 2.0f * y - 1.0f;
	float best_error = 1000.0f;
	real_vector3d candidates[3];
	int valid[3];
	int i;

	if (view_w <= 0.0f || view_h <= 0.0f)
		return FALSE;
	memset(valid, 0, sizeof(valid));
	/* (render_camera_view_to_screen, the other way: the view's -z is ahead) */
	if (frustum->projection_matrix[0][0] != 0.0f && frustum->projection_matrix[1][1] != 0.0f)
	{
		real_vector3d view;

		view.i = (ndc_x + frustum->projection_matrix[2][0]) / frustum->projection_matrix[0][0];
		view.j = (frustum->projection_matrix[2][1] - ndc_y) / frustum->projection_matrix[1][1];
		view.k = -1.0f;
		matrix4x3_transform_vector(&frustum->view_to_world, &view, &candidates[0]);
		valid[0] = forge_touch_normalize(&candidates[0]);
	}
	/* (the camera's forward, its right, its up) */
	{
		real_vector3d forward = camera->forward, up = camera->up, right;
		float half = camera->vertical_field_of_view * 0.5f;
		float aspect = view_w / view_h;
		float t = (float)tan((double)half);
		int k;

		right.i = forward.j * up.k - forward.k * up.j;
		right.j = forward.k * up.i - forward.i * up.k;
		right.k = forward.i * up.j - forward.j * up.i;
		if (half > 0.01f && half < 1.5f && forge_touch_normalize(&forward) && forge_touch_normalize(&up) &&
			forge_touch_normalize(&right))
		{
			for (k = 0; k < 2; k++)
			{
				float sx = k == 0 ? t * aspect : t;
				float sy = k == 0 ? t : t / aspect;

				candidates[1 + k].i = forward.i + right.i * ndc_x * sx - up.i * ndc_y * sy;
				candidates[1 + k].j = forward.j + right.j * ndc_x * sx - up.j * ndc_y * sy;
				candidates[1 + k].k = forward.k + right.k * ndc_x * sx - up.k * ndc_y * sy;
				valid[1 + k] = forge_touch_normalize(&candidates[1 + k]);
			}
		}
	}
	for (i = 0; i < 3; i++)
	{
		float error;

		if (!valid[i])
			continue;
		error = forge_touch_ray_error(&candidates[i], x, y);
		if (error < best_error)
		{
			best_error = error;
			*direction = candidates[i];
		}
	}
	/* (none comes back near the finger: the frame is not this camera's) */
	s_touch.last_ray_error = best_error;
	return best_error < 0.08f;
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
	struct render_camera const *camera = &render.camera;
	float view_w = (float)(camera->viewport_bounds.x1 - camera->viewport_bounds.x0);
	float view_h = (float)(camera->viewport_bounds.y1 - camera->viewport_bounds.y0);
	float sx, sy, dx, dy, depth, size;

	if (view_h <= 0.0f || !forge_touch_on_screen(point, &sx, &sy))
		return 1000.0f;
	dx = (sx - x) * view_w / view_h;
	dy = sy - y;
	/* (how big it shows: its radius over its depth, the field of view about 70 degrees) */
	depth = (point->x - camera->position.x) * camera->forward.i + (point->y - camera->position.y) * camera->forward.j +
		(point->z - camera->position.z) * camera->forward.k;
	size = depth > 0.5f ? radius / (depth * 1.4f) : 0.0f;
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
	if (!entry || !s_touch.seen)
		return;
	switch (entry->group_tag)
	{
	case 'scen':
	case 'vehi':
	case 'weap':
	case 'eqip':
	case 'mach':
	case 'ctrl':
	case 'lifi':
		break;
	default:
		return;
	}
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
	/* the top: what is done to the map */
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
			if (g_forge.grabbed_live_index < 0 && render_camera_world_to_screen(&render.camera, &render.frustum, &point, &screen))
			{
				short sx = (short)(window_x0 + (short)(screen.x + 0.5f));
				short sy = (short)(window_y0 + (short)(screen.y + 0.5f));

				draw_box_border((short)(sx - 18), (short)(sy - 18), (short)(sx + 18), (short)(sy + 18), 2, 0xFFFFC824u);
			}
		}
	}
	/* the help */
	if (s_touch.help)
	{
		static char const *const lines[] = {
			"+ OBJETOS: lo que eliges queda en la barra de abajo",
			"Con algo en la mano (barra de abajo): toca el suelo y lo pones",
			"Manten el dedo quieto sobre un objeto: lo borras",
			"Toca un objeto: lo eliges (salen sus botones)",
			"Arrastra un objeto: lo llevas por el suelo y lo sueltas al levantar el dedo",
			"Arrastra donde no hay nada: miras alrededor",
			"Con un objeto agarrado, gira dos dedos: lo giras",
			"Toca donde quieras con un objeto agarrado: lo pones ahi",
			"Palanca: volar.  SUBIR / BAJAR: altura.  START: menu y guardar el mapa",
			"+ OBJETOS: armas, vehiculos, escenario, spawns, banderas...",
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
		snprintf(buf, sizeof(buf), "Prueba tactil: linea %.3f  dedo en %d/%ld  agarrado %d  pantalla %dx%d",
			s_touch.last_ray_error, (int)s_touch.last_down_live, s_touch.last_down_object,
			(int)g_forge.grabbed_live_index,
			(int)(render.camera.viewport_bounds.x1 - render.camera.viewport_bounds.x0),
			(int)(render.camera.viewport_bounds.y1 - render.camera.viewport_bounds.y0));
		draw_ce_menu_str_box(0, font, (short)(x0 + 10), (short)(y0 + line / 2 + i * line), (short)(x1 - 10),
			(short)(y0 + line / 2 + (i + 1) * line), 0, buf, 0xFF9FD8FFu);
	}
}
