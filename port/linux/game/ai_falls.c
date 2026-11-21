/*
AI_FALLS.C

(port, debug) Why AI walk off cliffs and into death volumes: a trail of
each AI unit's last moments, written to debug.txt when it dies a way no
enemy killed it.

The units followed: those an actor drives (Firefight's waves, a campaign's
or Forge's AI: the host's alone, as the AI runs there) and the offline bots'
(bots.c, the host's too). Every AI_FALLS_SAMPLE_TICKS ticks each one's
place, vertical speed and what it was doing is kept, AI_FALLS_SAMPLES of
them (about three seconds); for an actor, its action, its destination's
kind and point, the point it moves towards, whether it has a path and how
many steps, and whether its movement was thwarted; for a bot, its goal's
kind and point, its path and teleporter use (bots_describe_unit).

When a followed unit dies, or is deleted while alive (fallen out of the
map), its trail is written if:

- its last damage was falling damage (category 1), or
- it dropped AI_FALLS_DROP units or more over the trail, or
- it ended outside the map's clusters, or inside a trigger volume (the
  volumes a map's scripts kill in, by their names).

"ai falls:" lines in debug.txt (not halo.log, which is off unless
debug.halo_log); at the level's end a count of the deaths followed and of
those written. players.c calls ai_falls_update each tick, after the
bots' controls; ai_falls_level_end at the level's end (bots' dispose).
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "ai/actors.h"
#include "game/game.h"
#include "objects/objects.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_groups.h"
#include "units/units.h"

#include "death_volumes.h"

#include <stdio.h>
#include <string.h>

/* bots.c: a bot's goal and path, by its unit; 0 for a unit no bot drives */
int bots_describe_unit(long unit_index, char *text, int size);
/* a Firefight game (firefight.c's) */
boolean multiplayer_firefight_mode(void);

#define AI_FALLS_TRACKED 160
#define AI_FALLS_SAMPLES 32
#define AI_FALLS_SAMPLE_TICKS 3
#define AI_FALLS_DROP 3.0f
#define AI_FALLS_FALLING_CATEGORY 1
#define AI_FALLS_TEXT 160

struct ai_falls_sample
{
	long tick;
	real_point3d position;
	real vertical_speed;
	char what[AI_FALLS_TEXT];
};

struct ai_falls_unit
{
	long unit_index;
	boolean bot;
	long definition_index;
	short count;
	short next;
	struct ai_falls_sample samples[AI_FALLS_SAMPLES];
};

static struct
{
	struct ai_falls_unit units[AI_FALLS_TRACKED];
	long last_tick;
	long deaths;
	long written;
	boolean full_logged;
	boolean initialized;
} ai_falls;

static char const *ai_falls_action_name(short action)
{
	static char const *const names[NUMBER_OF_ACTOR_ACTIONS] =
	{
		"none", "sleep", "alert", "fight", "flee", "uncover", "guard", "search", "wait", "vehicle", "charge",
		"obey", "converse", "avoid",
	};

	return action >= 0 && action < NUMBER_OF_ACTOR_ACTIONS ? names[action] : "?";
}

static char const *ai_falls_destination_name(short type)
{
	static char const *const names[] = { "none", "halt", "raw", "firing-position", "move-position", "prop" };

	return type >= 0 && type < (short)NUMBEROF(names) ? names[type] : "?";
}

/* what an actor is doing, in a line */
static void ai_falls_describe_actor(struct unit_datum const *unit, char *text, int size)
{
	struct actor_datum *actor = actor_try_and_get(unit->unit.actor_index);
	struct actor_control_data const *control;

	if (!actor)
	{
		snprintf(text, (size_t)size, "actor gone");
		return;
	}
	control = &actor->control;
	snprintf(text, (size_t)size,
		"actor %s dest=%s (%.1f %.1f %.1f) towards (%.1f %.1f %.1f) path=%s/%d%s%s%s",
		ai_falls_action_name(actor->state.action),
		ai_falls_destination_name(control->path.destination_orders.destination_type),
		control->path.destination.point.x, control->path.destination.point.y, control->path.destination.point.z,
		control->moving_towards_point.x, control->moving_towards_point.y, control->moving_towards_point.z,
		control->path.path.valid ? "yes" : "no", (int)control->path.path.step_count,
		control->moving ? " moving" : "",
		control->movement_thwarted ? " thwarted" : "",
		control->path.at_destination ? " at-dest" : "");
}

static struct ai_falls_unit *ai_falls_find(long unit_index, boolean add, boolean bot, long definition_index)
{
	struct ai_falls_unit *free_slot = NULL;
	long index;

	for (index = 0; index < AI_FALLS_TRACKED; index++)
	{
		struct ai_falls_unit *entry = &ai_falls.units[index];

		if (entry->unit_index == unit_index)
			return entry;
		if (!free_slot && entry->unit_index == NONE)
			free_slot = entry;
	}
	if (!add)
		return NULL;
	if (!free_slot)
	{
		if (!ai_falls.full_logged)
			error(_error_silent, "ai falls: more than %d AI units at once: the rest not followed", AI_FALLS_TRACKED);
		ai_falls.full_logged = TRUE;
		return NULL;
	}
	csmemset(free_slot, 0, sizeof(*free_slot));
	free_slot->unit_index = unit_index;
	free_slot->bot = bot;
	free_slot->definition_index = definition_index;
	return free_slot;
}

/* the trigger volumes holding a point, by name; whether it is outside the
map's clusters */
static void ai_falls_where(real_point3d const *point, char *text, int size, short *first_volume)
{
	struct scenario *scenario = global_scenario_try_and_get();
	struct location location;
	int used = 0;
	short index;

	*first_volume = NONE;
	scenario_location_from_point(&location, point);
	used += snprintf(text, (size_t)size, "%s", location.cluster_index == NONE ? "OUTSIDE THE MAP" : "in the map");
	if (!scenario)
		return;
	for (index = 0; index < scenario->trigger_volumes.count && used < size - 40; index++)
	{
		struct scenario_trigger_volume const *volume =
			TAG_BLOCK_GET_ELEMENT(&scenario->trigger_volumes, index, struct scenario_trigger_volume);

		if (scenario_trigger_volume_test_point(index, point) && *first_volume == NONE)
			*first_volume = index;
		if (scenario_trigger_volume_test_point(index, point))
			used += snprintf(text + used, (size_t)(size - used), ", in trigger volume \"%s\"", volume->name);
	}
}

static void ai_falls_finish(struct ai_falls_unit *entry, struct unit_datum const *unit, char const *how)
{
	struct ai_falls_sample const *last;
	real highest = -REAL_MAX;
	short index;
	short category = unit ? unit->unit.last_damage_category : NONE;
	char where[512];
	boolean outside, in_volume, write;
	short volume_index;
	char const *name;

	if (entry->count == 0)
	{
		entry->unit_index = NONE;
		return;
	}
	ai_falls.deaths++;
	last = &entry->samples[(entry->next + AI_FALLS_SAMPLES - 1) % AI_FALLS_SAMPLES];
	for (index = 0; index < entry->count; index++)
		highest = MAX(highest, entry->samples[(entry->next + AI_FALLS_SAMPLES - entry->count + index) % AI_FALLS_SAMPLES].position.z);
	ai_falls_where(&last->position, where, sizeof(where), &volume_index);
	/* (a volume a unit died of falling damage in without a fall: one that
	kills, learnt) */
	if (volume_index != NONE && category == AI_FALLS_FALLING_CATEGORY)
		death_volume_learn(volume_index, entry->bot ? "a bot died in it" : "an actor died in it");
	outside = strstr(where, "OUTSIDE") != NULL;
	in_volume = strstr(where, "trigger volume") != NULL;
	write = category == AI_FALLS_FALLING_CATEGORY || highest - last->position.z >= AI_FALLS_DROP || outside || in_volume;
	if (write)
	{
		ai_falls.written++;
		name = entry->definition_index != NONE ? tag_get_name(entry->definition_index) : NULL;
		error(_error_silent, "ai falls: %s %s %s (%s): damage category %d, dropped %.1f in the trail, at (%.1f %.1f %.1f) %s",
			entry->bot ? "bot" : "actor", name ? name : "?", how, multiplayer_firefight_mode() ? "firefight" : "game",
			(int)category, highest - last->position.z, last->position.x, last->position.y, last->position.z, where);
		for (index = 0; index < entry->count; index++)
		{
			struct ai_falls_sample const *sample =
				&entry->samples[(entry->next + AI_FALLS_SAMPLES - entry->count + index) % AI_FALLS_SAMPLES];

			error(_error_silent, "ai falls:   t-%ld (%.1f %.1f %.1f) vz %.1f %s", last->tick - sample->tick,
				sample->position.x, sample->position.y, sample->position.z, sample->vertical_speed, sample->what);
		}
	}
	entry->unit_index = NONE;
}

void ai_falls_level_end(void);

void ai_falls_update(void)
{
	struct object_iterator iterator;
	long tick = game_time_get();
	long index;

	if (!ai_falls.initialized)
		ai_falls_level_end();
	if (tick - ai_falls.last_tick < AI_FALLS_SAMPLE_TICKS && tick >= ai_falls.last_tick)
		return;
	ai_falls.last_tick = tick;

	/* (the deaths and deletions of those followed) */
	for (index = 0; index < AI_FALLS_TRACKED; index++)
	{
		struct ai_falls_unit *entry = &ai_falls.units[index];
		struct unit_datum *unit;

		if (entry->unit_index == NONE)
			continue;
		unit = unit_try_and_get(entry->unit_index);
		if (!unit)
			ai_falls_finish(entry, NULL, "deleted while alive");
		else if (TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
			ai_falls_finish(entry, unit, "died");
	}

	/* (a sample of each living AI unit) */
	object_iterator_new(&iterator, _object_mask_biped, 0);
	while (object_iterator_next(&iterator))
	{
		struct unit_datum *unit = unit_try_and_get(iterator.index);
		struct ai_falls_unit *entry;
		struct ai_falls_sample *sample;
		char bot_text[AI_FALLS_TEXT];
		boolean bot;

		if (!unit || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
			continue;
		bot = unit->unit.actor_index == NONE && unit->unit.player_index != NONE &&
			bots_describe_unit(iterator.index, bot_text, sizeof(bot_text));
		if (unit->unit.actor_index == NONE && !bot)
			continue;
		entry = ai_falls_find(iterator.index, TRUE, bot, object_get(iterator.index)->definition_index);
		if (!entry)
			continue;
		sample = &entry->samples[entry->next];
		entry->next = (short)((entry->next + 1) % AI_FALLS_SAMPLES);
		entry->count = (short)MIN(entry->count + 1, AI_FALLS_SAMPLES);
		sample->tick = tick;
		object_get_origin(iterator.index, &sample->position);
		sample->vertical_speed = unit->object.translational_velocity.k;
		if (bot)
			snprintf(sample->what, sizeof(sample->what), "%s", bot_text);
		else
			ai_falls_describe_actor(unit, sample->what, sizeof(sample->what));
	}
}

void ai_falls_level_end(void)
{
	long index;

	if (ai_falls.deaths)
		error(_error_silent, "ai falls: the level's end: %ld AI deaths followed, %ld written", ai_falls.deaths, ai_falls.written);
	for (index = 0; index < AI_FALLS_TRACKED; index++)
		ai_falls.units[index].unit_index = NONE;
	ai_falls.last_tick = 0;
	ai_falls.deaths = 0;
	ai_falls.written = 0;
	ai_falls.full_logged = FALSE;
	ai_falls.initialized = TRUE;
	death_volumes_level_end();
}
