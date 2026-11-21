#ifndef MCC_TAG_PREVIEW_H
#define MCC_TAG_PREVIEW_H

/* The turning preview's mesh of an MCC map's tag, read from the map's file
without loading it (mcc_tag_preview.c). */
struct tag_injector_mesh;

boolean mcc_tag_preview_open(char const *level_name);
void mcc_tag_preview_close(void);
boolean mcc_tag_preview_opened(void);
boolean mcc_tag_preview_mesh(unsigned long group, char const *tag_name, struct tag_injector_mesh *mesh);
/* whether the open map's weapon of that name has what a player's needs (its
model, collision, first-person model and animations, HUD, and a projectile
or a melee weapon's name); TRUE for one not found */
boolean mcc_tag_preview_weapon_usable(char const *tag_name);

#endif
