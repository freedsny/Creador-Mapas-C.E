/* MCC's weapon-authored camera uses the engine's existing following camera.
 * Inventory replication supplies the equipped weapon on both peers. */
#include "cseries.h"
#include "cseries/errors.h"
#include "units/units.h"
#include "items/weapons.h"
#include "items/weapon_definitions.h"
#include "cache/cache_files.h"
#include "scenario/scenario.h"
#include "mcc_cache.h"
#include "mcc_camera.h"
#include <string.h>

/* A map whose every hand-held weapon of its own has the flag (GTA's
 * top-down view) is played with that camera whatever is held: a weapon
 * copied in (Firefight's loadout, its pods' campaign weapons) has no such
 * flag, and the first-person view it would give is not the map's (its
 * biped looks only down, its HUD is laid out for the camera above). A map
 * with some weapons flagged and some not (Digsite) keeps it per weapon.
 * Worked out once a map: the scenario and the map's checksum name it. */
static boolean mcc_camera_map_wide(void)
{
    static void const *scenario_seen;
    static unsigned long checksum_seen;
    static boolean wide;
    struct tag_iterator iterator;
    long tag_index;
    long flagged=0,plain=0;
    void const *scenario=global_scenario_try_and_get();

    if (scenario==scenario_seen && cache_files_get_checksum()==checksum_seen) return wide;
    scenario_seen=scenario;
    checksum_seen=cache_files_get_checksum();
    tag_iterator_new(&iterator,WEAPON_DEFINITION_TAG);
    while ((tag_index=tag_iterator_next(&iterator))!=NONE) {
        struct weapon_definition *definition=weapon_definition_get(tag_index);
        char const *name=tag_get_name(tag_index);
        /* (the map's own, carried: not a vehicle's gun, nor a copied one) */
        if (!definition || !mcc_cache_contains(definition,4) || !name ||
            strstr(name,"vehicles\\") || strstr(name,"turret")) continue;
        if (definition->weapon.flags & 0x20000UL) flagged++;
        else plain++;
    }
    wide=flagged>0 && !plain;
    error(_error_silent,"mcc camera: %ld of the map's weapons have the weapon camera, %ld do not%s",
        flagged,plain,wide ? ": every weapon held uses it" : "");
    return wide;
}

boolean mcc_camera_weapon_following(long unit_index)
{
    struct unit_datum *unit;
    struct weapon_datum *weapon;
    struct weapon_definition *definition;
    short slot;
    long weapon_index;

    if (!mcc_cache_tags_loaded() || unit_index == NONE) return FALSE;
    unit = unit_try_and_get(unit_index);
    if (!unit || unit->object.parent_object_index != NONE ||
        TEST_FLAG(unit->object.damage_flags, _object_dead_bit) ||
        unit->unit.current_zoom_level != NONE) return FALSE;
    if (mcc_camera_map_wide()) return TRUE;
    slot = unit->unit.current_weapon_index;
    if (slot < 0 || slot >= MAXIMUM_WEAPONS_PER_UNIT) return FALSE;
    weapon_index = unit->unit.weapon_object_indices[slot];
    if (weapon_index == NONE) return FALSE;
    weapon = weapon_try_and_get(weapon_index);
    if (!weapon) return FALSE;
    definition = weapon_definition_get(weapon->definition_index);
    /* H1A weapon flags bit 17; independently documented by c20's weapon
     * format reference and present on Spasm Playground's Digsite weapons. */
    return (definition->weapon.flags & 0x20000UL) != 0;
}
