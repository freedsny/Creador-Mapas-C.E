#ifndef MCC_CACHE_H
#define MCC_CACHE_H

struct cache_file_tag_header;
struct scenario_structure_bsp_reference;
struct bitmap_data;

/* Path ownership is decided before stripping a filename. MCC never searches
 * maps/, custom_maps/ or the optional Custom Edition installation. */
boolean mcc_level_name(char const *name);
void mcc_cache_validation_clear(void);
boolean mcc_cache_available(char const *name);
boolean mcc_cache_present(char const *name, unsigned long checksum,
    char *message, long message_size);
boolean mcc_cache_require(char const *name, unsigned long checksum);
boolean mcc_cache_precache_ready(char const *name);
void mcc_cache_precache_clear(void);
unsigned long mcc_cache_checksum(char const *name);
struct cache_file_tag_header *mcc_cache_tags_load(char const *name, void *header);
boolean mcc_cache_tags_loaded(void);
/* the canonical level loaded (mcc_maps\<package>\<name>), or NULL */
char const *mcc_cache_level_name(void);
short mcc_cache_scenario_type(void);
void mcc_cache_tags_unload(void);
boolean mcc_cache_contains(void const *address, long bytes);
boolean mcc_cache_read(long tag, long offset, long bytes, void *out);
boolean mcc_cache_bsp_load(struct scenario_structure_bsp_reference const *reference,
    void **header, void **structure);
void mcc_cache_bsp_unload(void);
short mcc_cache_part_palette(void const *buffer, byte const **nodes);
boolean mcc_cache_bitmap_valid(struct bitmap_data *bitmap);
void const *mcc_cache_bitmap_pixels(struct bitmap_data const *bitmap, unsigned long *bytes, int *coverage);

/* (port) A map's file opened read-only as the loader opens a level's, for the
Tag Injector to take its tags out (mcc_tag_bundle.c): a handle for
mcc_cache_file_close, its source and identity filled; NULL when it cannot be */
struct mcc_cache_source;
struct mcc_cache_identity;
void *mcc_cache_file_open(char const *name, struct mcc_cache_source *source, struct mcc_cache_identity *identity);
void mcc_cache_file_close(void *file);

/* A map's tags read without loading it (mcc_cache.c's menus' readers). */
boolean mcc_cache_tag_names_walk(char const *name,
    int (*visit)(void *context, unsigned long group_tag, char const *tag_name), void *context);
struct mcc_raw_map;
struct mcc_raw_map *mcc_cache_raw_open(char const *name);
void mcc_cache_raw_close(struct mcc_raw_map *map);
void *mcc_cache_raw_pointer(struct mcc_raw_map *map, unsigned long address, unsigned long bytes);
unsigned long mcc_cache_raw_tag_count(struct mcc_raw_map const *map);
unsigned char *mcc_cache_raw_entry(struct mcc_raw_map *map, unsigned long index);
boolean mcc_cache_raw_model_read(struct mcc_raw_map *map, boolean strip, unsigned long offset,
    unsigned long bytes, void *out);
/* The first `bytes` of a bitmap's pixels (a bitmap of the bitmap tag of that
handle, at that index of it): the map's own, or MCC's bitmaps.map's */
struct bitmap_data;
boolean mcc_cache_raw_bitmap_read(struct mcc_raw_map *map, unsigned long bitmap_handle, short bitmap_index,
    struct bitmap_data const *bitmap, unsigned long bytes, void *out);

#endif
