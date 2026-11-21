/*
MCC_TAG_PREVIEW.C

(port) The turning preview of an MCC map's tag (tag_preview.c), read from
the map's file without loading it: Firefight's MAP CONTENT list of an MCC
map's weapons, vehicles and AI (menu_functions.c's map_content_tags_*), and
later Forge's and the Tag Injector's.

mcc_tag_preview_open reads the map's tags as its file has them
(mcc_cache_raw_open: unconverted, at no linked address, each address found
with mcc_cache_raw_pointer); mcc_tag_preview_mesh makes the mesh of a tag of
it, by group and name, as tag_injector.c's preview_mesh_build makes a Custom
Edition map's: an actor variant's unit, an object's model; of each region
its first permutation an object may be given, at its most detailed
geometry; its opaque parts, else all. MCC's mod2 parts are laid out as Halo
PC's gbxmodel's (0x84 bytes; 68-byte vertices in the map's model data, the
strips after the vertices: mcc_geometry.c).

Its textures as tag_preview.c draws a Custom Edition map's: each opaque
shader's base map (its largest level no larger than the preview draws,
decoded to a8r8g8b8 by mcc_bitmaps.c's decoder), and a model's shader's
multipurpose map for the change color it names (its mask in alpha: blue in
the original Xbox order a shader's flag 6 selects). No needs list.
*/

#include "cseries.h"
#include "ai/actor_definitions.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmaps.h"
#include "items/weapon_definitions.h"
#include "models/model_definitions.h"
#include "objects/object_definitions.h"
#include "tag_files/tag_groups.h"

#include "mcc_cache.h"
#include "mcc_tag_preview.h"
#include "tag_injector.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MCC_PREVIEW_MAXIMUM_VERTICES 16384
#define MCC_PREVIEW_MAXIMUM_TRIANGLES 24576
#define MCC_PREVIEW_MAXIMUM_STRIP_TRIANGLES 0x10000
#define MCC_PREVIEW_MAXIMUM_REGIONS 32
#define MCC_PREVIEW_DETAIL_LEVELS 5
#define MCC_PREVIEW_MAXIMUM_COORDINATE 1000.0f
#define MCC_PREVIEW_MAXIMUM_MAP_SCALE 1000.0f

#define MCC_GEOMETRY_BYTES 0x30
#define MCC_GEOMETRY_PARTS_OFFSET 0x24
#define MCC_PART_BYTES 0x84
#define MCC_PART_SHADER_INDEX_OFFSET 0x04
#define MCC_PART_STRIP_TRIANGLE_COUNT_OFFSET 0x48
#define MCC_PART_STRIP_OFFSET_OFFSET 0x4C
#define MCC_PART_VERTEX_COUNT_OFFSET 0x58
#define MCC_PART_VERTEX_OFFSET_OFFSET 0x64
#define MCC_VERTEX_BYTES 0x44
#define MCC_VERTEX_NORMAL_OFFSET 0x0C
#define MCC_VERTEX_TEXCOORD_OFFSET 0x30
/* a model's shader reference (16 bytes) and the shader groups whose parts
are opaque: a model's, an environment's */
#define MCC_MODEL_SHADER_BYTES 0x20
#define MCC_SHADER_MODEL_GROUP 'soso'
#define MCC_SHADER_ENVIRONMENT_GROUP 'senv'
/* a shader's fields read (Halo PC's layout, as tag_injector.c's): a
model's flags, change color source, map scales, base and multipurpose maps;
an environment's base map */
#define MCC_SHADER_MODEL_FLAGS_OFFSET 0x28
#define MCC_SHADER_MODEL_XBOX_MULTIPURPOSE_FLAG 0x40
#define MCC_SHADER_MODEL_CHANGE_COLOR_SOURCE_OFFSET 0x4C
#define MCC_SHADER_MODEL_MAP_U_SCALE_OFFSET 0x9C
#define MCC_SHADER_MODEL_MAP_V_SCALE_OFFSET 0xA0
#define MCC_SHADER_MODEL_BASE_MAP_OFFSET 0xA4
#define MCC_SHADER_MODEL_MULTIPURPOSE_MAP_OFFSET 0xBC
#define MCC_SHADER_ENVIRONMENT_BASE_MAP_OFFSET 0x88
#define MCC_SHADER_BYTES 0x180
#define MCC_PREVIEW_MAXIMUM_SHADERS 64
#define MCC_PREVIEW_MATERIAL_UNMADE (-2)
#define MCC_PREVIEW_CHANGE_COLORS 4
/* the largest texture the preview keeps (tag_injector.c's), the largest
bitmap read, and the most bytes read for one */
#define MCC_PREVIEW_TEXTURE_SIZE 128
#define MCC_PREVIEW_MAXIMUM_BITMAP_SIZE 4096
#define MCC_PREVIEW_MAXIMUM_PIXEL_BYTES (24u * 1024u * 1024u)
#define MCC_BITMAP_EXTERNAL_FLAG 0x100
#define MCC_BITMAP_FORMAT_A8R8G8B8 11

/* (mcc_bitmaps.c's) */
int mcc_bitmaps_decode_texels(void const *input, uint32_t input_bytes, uint32_t *output,
	unsigned width, unsigned height, short format);

static struct
{
	struct mcc_raw_map *map;
	char name[128];
} mcc_preview;

/* a mesh's making: its model, the materials made of its shaders, the
textures read (by bitmap tag, and whether as a mask in Xbox's order), and
its object's change colors */
struct mcc_preview_build
{
	struct model const *model;
	short shader_materials[MCC_PREVIEW_MAXIMUM_SHADERS];
	long texture_handles[TAG_INJECTOR_MESH_MAXIMUM_TEXTURES];
	boolean texture_xbox_masks[TAG_INJECTOR_MESH_MAXIMUM_TEXTURES];
	boolean colored[MCC_PREVIEW_CHANGE_COLORS];
	float colors[MCC_PREVIEW_CHANGE_COLORS][3];
};

/* (an actor variant's change color: actor_definitions.c's, 0x20 bytes) */
struct mcc_preview_variant_change_color
{
	real_rgb_color lower;
	real_rgb_color upper;
	unsigned long unused[2];
};

static uint32_t mcc_preview_u32(void const *p)
{
	uint32_t value;

	memcpy(&value, p, sizeof(value));
	return value;
}

boolean mcc_tag_preview_open(char const *level_name)
{
	if (mcc_preview.map && level_name && !strcmp(mcc_preview.name, level_name))
		return TRUE;
	mcc_tag_preview_close();
	if (!level_name || !mcc_level_name(level_name))
		return FALSE;
	mcc_preview.map = mcc_cache_raw_open(level_name);
	if (!mcc_preview.map)
		return FALSE;
	snprintf(mcc_preview.name, sizeof(mcc_preview.name), "%s", level_name);
	return TRUE;
}

void mcc_tag_preview_close(void)
{
	if (mcc_preview.map)
		mcc_cache_raw_close(mcc_preview.map);
	mcc_preview.map = NULL;
	mcc_preview.name[0] = 0;
}

boolean mcc_tag_preview_opened(void)
{
	return mcc_preview.map != NULL;
}

/* a block's elements, NULL for none or one outside the tags */
static byte *mcc_preview_block(struct tag_block const *block, unsigned long stride)
{
	if (block->count <= 0 || (unsigned long)block->count > 0x100000UL / MAX(stride, 1))
		return NULL;
	return mcc_cache_raw_pointer(mcc_preview.map, (unsigned long)(uintptr_t)block->address,
		(unsigned long)block->count * stride);
}

/* the root of the tag a handle names, of at least `bytes`, and its group */
static byte *mcc_preview_tag_of_handle(long handle, unsigned long bytes, unsigned long *group)
{
	unsigned char *entry;

	if (handle == NONE)
		return NULL;
	entry = mcc_cache_raw_entry(mcc_preview.map, (unsigned long)(handle & 0xFFFF));
	if (!entry || (long)mcc_preview_u32(entry + 12) != handle)
		return NULL;
	*group = mcc_preview_u32(entry);
	return mcc_cache_raw_pointer(mcc_preview.map, mcc_preview_u32(entry + 20), bytes);
}

/* the root of the tag of a group and name */
static byte *mcc_preview_tag_named(unsigned long group, char const *name, unsigned long bytes)
{
	unsigned long count = mcc_cache_raw_tag_count(mcc_preview.map);
	unsigned long index;

	for (index = 0; index < count; index++)
	{
		unsigned char *entry = mcc_cache_raw_entry(mcc_preview.map, index);
		char const *tag_name;

		if (!entry || mcc_preview_u32(entry) != group)
			continue;
		tag_name = mcc_cache_raw_pointer(mcc_preview.map, mcc_preview_u32(entry + 16), 1);
		if (tag_name && !_stricmp(tag_name, name))
			return mcc_cache_raw_pointer(mcc_preview.map, mcc_preview_u32(entry + 20), bytes);
	}
	return NULL;
}

static boolean mcc_preview_shader_opaque(struct model const *model, short shader_index)
{
	byte *shaders = mcc_preview_block(&model->shaders, MCC_MODEL_SHADER_BYTES);
	unsigned long group = 0;
	struct tag_reference const *reference;

	if (!shaders || shader_index < 0 || shader_index >= model->shaders.count)
		return FALSE;
	reference = (struct tag_reference const *)(shaders + shader_index * MCC_MODEL_SHADER_BYTES);
	if (!mcc_preview_tag_of_handle(reference->index, 4, &group))
		return FALSE;
	return group == MCC_SHADER_MODEL_GROUP || group == MCC_SHADER_ENVIRONMENT_GROUP;
}

/* the bytes of a level of a bitmap of a format, 0 for one not decoded */
static unsigned long mcc_preview_level_size(short format, long width, long height)
{
	long blocks = MAX(1, (width + 3) / 4) * MAX(1, (height + 3) / 4);

	switch (format)
	{
	case 0: case 1: case 2: case 17:
		return (unsigned long)(width * height);
	case 3: case 6: case 8: case 9:
		return (unsigned long)(width * height * 2);
	case 10: case 11:
		return (unsigned long)(width * height * 4);
	case 14:
		return (unsigned long)(blocks * 8);
	case 15: case 16: case 18:
		return (unsigned long)(blocks * 16);
	default:
		return 0;
	}
}

/* The texture of a bitmap tag a shader names (its index in the mesh's
textures, read the first time): its first bitmap's largest level no larger
than the preview draws (its levels one after another, the largest first:
read from their start, as bitmaps.map gives a bitmap's pixels only whole),
decoded to a8r8g8b8 and, larger still (no smaller level), sampled down to
it; a mask in Xbox's order has its blue (the change color) moved to alpha.
NONE for none. */
static short mcc_preview_texture(struct mcc_preview_build *build, struct tag_reference const *reference,
	boolean xbox_mask, struct tag_injector_mesh *mesh)
{
	unsigned long group = 0;
	struct bitmap_group const *bitmap_group;
	struct bitmap_data const *bitmaps;
	struct bitmap_data const *bitmap;
	struct tag_injector_mesh_texture *texture;
	long width;
	long height;
	long out_width;
	long out_height;
	unsigned long offset = 0;
	unsigned long size;
	unsigned char *raw;
	uint32_t *texels;
	uint32_t *out;
	long x;
	long y;
	short index;

	if (reference->index == NONE)
		return NONE;
	for (index = 0; index < mesh->texture_count; index++)
	{
		if (build->texture_handles[index] == reference->index && build->texture_xbox_masks[index] == xbox_mask)
			return index;
	}
	if (mesh->texture_count >= TAG_INJECTOR_MESH_MAXIMUM_TEXTURES)
		return NONE;
	bitmap_group = (struct bitmap_group const *)mcc_preview_tag_of_handle(reference->index, sizeof(struct bitmap_group),
		&group);
	if (!bitmap_group || group != BITMAP_GROUP_TAG)
		return NONE;
	bitmaps = (struct bitmap_data const *)mcc_preview_block(&bitmap_group->bitmaps, sizeof(struct bitmap_data));
	if (!bitmaps)
		return NONE;
	bitmap = &bitmaps[0];
	width = bitmap->width;
	height = bitmap->height;
	if (bitmap->type != 0 || width <= 0 || height <= 0 || width > MCC_PREVIEW_MAXIMUM_BITMAP_SIZE ||
		height > MCC_PREVIEW_MAXIMUM_BITMAP_SIZE || !mcc_preview_level_size(bitmap->format, width, height) ||
		bitmap->pixels_size <= 0)
	{
		return NONE;
	}
	/* (its levels while one smaller is there) */
	while (MAX(width, height) > MCC_PREVIEW_TEXTURE_SIZE && (width > 1 || height > 1))
	{
		unsigned long next = offset + mcc_preview_level_size(bitmap->format, width, height);
		long next_width = MAX(1, width >> 1);
		long next_height = MAX(1, height >> 1);

		if (next + mcc_preview_level_size(bitmap->format, next_width, next_height) > (unsigned long)bitmap->pixels_size)
			break;
		offset = next;
		width = next_width;
		height = next_height;
	}
	size = mcc_preview_level_size(bitmap->format, width, height);
	if (offset + size > (unsigned long)bitmap->pixels_size || offset + size > MCC_PREVIEW_MAXIMUM_PIXEL_BYTES)
		return NONE;
	out_width = width;
	out_height = height;
	while (MAX(out_width, out_height) > MCC_PREVIEW_TEXTURE_SIZE)
	{
		out_width = MAX(1, out_width >> 1);
		out_height = MAX(1, out_height >> 1);
	}
	raw = malloc(offset + size);
	texels = malloc((unsigned long)(width * height) * sizeof(uint32_t));
	out = malloc((unsigned long)(out_width * out_height) * sizeof(uint32_t));
	if (!raw || !texels || !out ||
		!mcc_cache_raw_bitmap_read(mcc_preview.map, (unsigned long)reference->index, 0, bitmap, offset + size, raw) ||
		!mcc_bitmaps_decode_texels(raw + offset, (uint32_t)size, texels, (unsigned)width, (unsigned)height,
			bitmap->format))
	{
		if (raw)
			free(raw);
		if (texels)
			free(texels);
		if (out)
			free(out);
		return NONE;
	}
	for (y = 0; y < out_height; y++)
	{
		for (x = 0; x < out_width; x++)
		{
			uint32_t color = texels[(y * height / out_height) * width + x * width / out_width];

			if (xbox_mask)
				color = (color & 0x00FFFFFFu) | (color & 0xFFu) << 24;
			out[y * out_width + x] = color;
		}
	}
	free(raw);
	free(texels);
	texture = &mesh->textures[mesh->texture_count];
	texture->format = MCC_BITMAP_FORMAT_A8R8G8B8;
	texture->width = (short)out_width;
	texture->height = (short)out_height;
	texture->size = (unsigned long)(out_width * out_height) * sizeof(uint32_t);
	texture->pixels = (unsigned char *)out;
	build->texture_handles[mesh->texture_count] = reference->index;
	build->texture_xbox_masks[mesh->texture_count] = xbox_mask;
	return mesh->texture_count++;
}

/* The material of a model's shader (made the first time a part names it),
and its map scales into `scale`; NONE for a shader not a model's or an
environment's (its parts drawn grey). As tag_injector.c's
preview_material. */
static short mcc_preview_material(struct mcc_preview_build *build, short shader_index,
	struct tag_injector_mesh *mesh, float *scale)
{
	byte *shaders = mcc_preview_block(&build->model->shaders, MCC_MODEL_SHADER_BYTES);
	struct tag_reference const *reference;
	struct tag_injector_mesh_material *material;
	unsigned long group = 0;
	byte *shader;
	short source = 0;
	unsigned short flags = 0;
	short index;

	scale[0] = scale[1] = 1.0f;
	if (!shaders || shader_index < 0 || shader_index >= build->model->shaders.count ||
		shader_index >= MCC_PREVIEW_MAXIMUM_SHADERS)
	{
		return NONE;
	}
	reference = (struct tag_reference const *)(shaders + shader_index * MCC_MODEL_SHADER_BYTES);
	shader = mcc_preview_tag_of_handle(reference->index, MCC_SHADER_BYTES, &group);
	if (!shader || (group != MCC_SHADER_MODEL_GROUP && group != MCC_SHADER_ENVIRONMENT_GROUP))
		return NONE;
	if (group == MCC_SHADER_MODEL_GROUP)
	{
		float u_scale;
		float v_scale;

		memcpy(&u_scale, shader + MCC_SHADER_MODEL_MAP_U_SCALE_OFFSET, sizeof(u_scale));
		memcpy(&v_scale, shader + MCC_SHADER_MODEL_MAP_V_SCALE_OFFSET, sizeof(v_scale));
		if (u_scale > 0.0f && u_scale < MCC_PREVIEW_MAXIMUM_MAP_SCALE)
			scale[0] = u_scale;
		if (v_scale > 0.0f && v_scale < MCC_PREVIEW_MAXIMUM_MAP_SCALE)
			scale[1] = v_scale;
	}
	if (build->shader_materials[shader_index] != MCC_PREVIEW_MATERIAL_UNMADE)
		return build->shader_materials[shader_index];
	build->shader_materials[shader_index] = NONE;
	if (mesh->material_count >= TAG_INJECTOR_MESH_MAXIMUM_MATERIALS)
		return NONE;
	material = &mesh->materials[mesh->material_count];
	material->mask_texture = NONE;
	material->tinted = FALSE;
	material->blend = _tag_injector_blend_opaque;
	material->layer_count = 0;
	if (group == MCC_SHADER_MODEL_GROUP)
	{
		material->base_texture = mcc_preview_texture(build,
			(struct tag_reference const *)(shader + MCC_SHADER_MODEL_BASE_MAP_OFFSET), FALSE, mesh);
		memcpy(&source, shader + MCC_SHADER_MODEL_CHANGE_COLOR_SOURCE_OFFSET, sizeof(source));
		memcpy(&flags, shader + MCC_SHADER_MODEL_FLAGS_OFFSET, sizeof(flags));
		if (source >= 1 && source <= MCC_PREVIEW_CHANGE_COLORS && build->colored[source - 1])
		{
			material->mask_texture = mcc_preview_texture(build,
				(struct tag_reference const *)(shader + MCC_SHADER_MODEL_MULTIPURPOSE_MAP_OFFSET),
				(flags & MCC_SHADER_MODEL_XBOX_MULTIPURPOSE_FLAG) != 0, mesh);
			material->tinted = material->mask_texture != NONE;
			for (index = 0; index < 3; index++)
				material->tint[index] = build->colors[source - 1][index];
		}
	}
	else
	{
		material->base_texture = mcc_preview_texture(build,
			(struct tag_reference const *)(shader + MCC_SHADER_ENVIRONMENT_BASE_MAP_OFFSET), FALSE, mesh);
	}
	build->shader_materials[shader_index] = mesh->material_count++;
	return build->shader_materials[shader_index];
}

/* the change colors of the object: an actor variant's own, else its
unit's first permutation's, each the middle of its bounds */
static void mcc_preview_change_colors(struct object_definition const *object,
	struct actor_variant_definition const *variant, struct mcc_preview_build *build)
{
	byte *elements;
	short index;
	short channel;

	elements = object ? mcc_preview_block(&object->object.change_colors, sizeof(struct object_change_color_definition)) :
		NULL;
	for (index = 0; elements && index < MIN(object->object.change_colors.count, MCC_PREVIEW_CHANGE_COLORS); index++)
	{
		struct object_change_color_definition const *change_color = (struct object_change_color_definition const *)
			(elements + index * sizeof(struct object_change_color_definition));
		struct object_change_color_permutation const *permutation = (struct object_change_color_permutation const *)
			mcc_preview_block(&change_color->permutations, sizeof(struct object_change_color_permutation));

		if (!permutation)
			continue;
		build->colors[index][0] = (permutation->color_lower_bound.red + permutation->color_upper_bound.red) * 0.5f;
		build->colors[index][1] = (permutation->color_lower_bound.green + permutation->color_upper_bound.green) * 0.5f;
		build->colors[index][2] = (permutation->color_lower_bound.blue + permutation->color_upper_bound.blue) * 0.5f;
		build->colored[index] = TRUE;
	}
	elements = variant ? mcc_preview_block(&variant->change_colors, sizeof(struct mcc_preview_variant_change_color)) :
		NULL;
	for (index = 0; elements && index < MIN(variant->change_colors.count, MCC_PREVIEW_CHANGE_COLORS); index++)
	{
		struct mcc_preview_variant_change_color const *color = (struct mcc_preview_variant_change_color const *)
			(elements + index * sizeof(struct mcc_preview_variant_change_color));

		build->colors[index][0] = (color->lower.red + color->upper.red) * 0.5f;
		build->colors[index][1] = (color->lower.green + color->upper.green) * 0.5f;
		build->colors[index][2] = (color->lower.blue + color->upper.blue) * 0.5f;
		build->colored[index] = TRUE;
	}
	for (index = 0; index < MCC_PREVIEW_CHANGE_COLORS; index++)
	{
		/* (a NaN fails every comparison) */
		for (channel = 0; channel < 3; channel++)
		{
			if (!(build->colors[index][channel] >= 0.0f))
				build->colors[index][channel] = 0.0f;
			build->colors[index][channel] = MIN(build->colors[index][channel], 1.0f);
		}
	}
}

static void mcc_preview_part(struct mcc_preview_build *build, byte const *part, boolean opaque_only,
	struct tag_injector_mesh *mesh)
{
	struct model const *model = build->model;
	float scale[2];
	short material;
	long strip_triangle_count = (long)mcc_preview_u32(part + MCC_PART_STRIP_TRIANGLE_COUNT_OFFSET);
	long vertex_count = (long)mcc_preview_u32(part + MCC_PART_VERTEX_COUNT_OFFSET);
	long first_vertex = mesh->vertex_count;
	long index_count = strip_triangle_count + 2;
	short shader_index;
	byte *vertices;
	unsigned short *strip;
	long index;

	memcpy(&shader_index, part + MCC_PART_SHADER_INDEX_OFFSET, sizeof(shader_index));
	if ((opaque_only && !mcc_preview_shader_opaque(model, shader_index)) ||
		vertex_count <= 0 || vertex_count > MCC_PREVIEW_MAXIMUM_VERTICES - mesh->vertex_count ||
		strip_triangle_count <= 0 || strip_triangle_count > MCC_PREVIEW_MAXIMUM_STRIP_TRIANGLES)
	{
		return;
	}
	material = mcc_preview_material(build, shader_index, mesh, scale);
	/* (the model's base map scale, none of it 1) */
	if (model->base_map_scale.i > 0.0f && model->base_map_scale.i < MCC_PREVIEW_MAXIMUM_MAP_SCALE)
		scale[0] *= model->base_map_scale.i;
	if (model->base_map_scale.j > 0.0f && model->base_map_scale.j < MCC_PREVIEW_MAXIMUM_MAP_SCALE)
		scale[1] *= model->base_map_scale.j;
	vertices = malloc((unsigned long)vertex_count * MCC_VERTEX_BYTES);
	strip = malloc((unsigned long)index_count * sizeof(unsigned short));
	if (vertices && strip &&
		mcc_cache_raw_model_read(mcc_preview.map, FALSE, mcc_preview_u32(part + MCC_PART_VERTEX_OFFSET_OFFSET),
			(unsigned long)vertex_count * MCC_VERTEX_BYTES, vertices) &&
		mcc_cache_raw_model_read(mcc_preview.map, TRUE, mcc_preview_u32(part + MCC_PART_STRIP_OFFSET_OFFSET),
			(unsigned long)index_count * sizeof(unsigned short), strip))
	{
		for (index = 0; index < vertex_count; index++)
		{
			byte const *vertex = vertices + index * MCC_VERTEX_BYTES;
			float *position = mesh->positions[first_vertex + index];
			float *normal = mesh->normals[first_vertex + index];
			float *texcoord = mesh->texcoords[first_vertex + index];
			short axis;

			memcpy(position, vertex, 3 * sizeof(float));
			memcpy(normal, vertex + MCC_VERTEX_NORMAL_OFFSET, 3 * sizeof(float));
			memcpy(texcoord, vertex + MCC_VERTEX_TEXCOORD_OFFSET, 2 * sizeof(float));
			/* (a NaN fails every comparison) */
			for (axis = 0; axis < 3; axis++)
			{
				if (!(position[axis] >= -MCC_PREVIEW_MAXIMUM_COORDINATE && position[axis] <= MCC_PREVIEW_MAXIMUM_COORDINATE))
					position[axis] = 0.0f;
				if (!(normal[axis] >= -2.0f && normal[axis] <= 2.0f))
					normal[axis] = 0.0f;
			}
			for (axis = 0; axis < 2; axis++)
			{
				texcoord[axis] *= scale[axis];
				if (!(texcoord[axis] >= -MCC_PREVIEW_MAXIMUM_COORDINATE && texcoord[axis] <= MCC_PREVIEW_MAXIMUM_COORDINATE))
					texcoord[axis] = 0.0f;
			}
		}
		mesh->vertex_count += vertex_count;
		for (index = 0; index + 2 < index_count && mesh->triangle_count < MCC_PREVIEW_MAXIMUM_TRIANGLES; index++)
		{
			unsigned short a = strip[index];
			unsigned short b = strip[index + 1];
			unsigned short c = strip[index + 2];
			unsigned short *out = mesh->triangles[mesh->triangle_count];

			if (a == b || b == c || a == c || a >= vertex_count || b >= vertex_count || c >= vertex_count)
				continue;
			/* (a strip's odd triangles turn the other way) */
			if (index & 1)
			{
				unsigned short swap = b;

				b = c;
				c = swap;
			}
			out[0] = (unsigned short)(first_vertex + a);
			out[1] = (unsigned short)(first_vertex + b);
			out[2] = (unsigned short)(first_vertex + c);
			mesh->triangle_materials[mesh->triangle_count] = material;
			mesh->triangle_count++;
		}
	}
	if (vertices)
		free(vertices);
	if (strip)
		free(strip);
}

boolean mcc_tag_preview_mesh(unsigned long group, char const *tag_name, struct tag_injector_mesh *mesh)
{
	byte *root;
	unsigned long root_group = group;
	struct model const *model;
	unsigned long model_group = 0;
	struct actor_variant_definition const *variant = NULL;
	struct mcc_preview_build build;
	short pass;
	short index;

	memset(mesh, 0, sizeof(*mesh));
	memset(&build, 0, sizeof(build));
	if (!mcc_preview.map || !tag_name)
		return FALSE;
	/* (an object's or an actor variant's model alone: another tag's fields
	are not an object's) */
	if (group != ACTOR_VARIANT_DEFINITION_TAG && group != 'weap' && group != 'eqip' && group != 'vehi' &&
		group != 'bipd' && group != 'scen' && group != 'mach' && group != 'ctrl' && group != 'lifi' &&
		group != 'garb' && group != 'proj')
	{
		return FALSE;
	}
	root = mcc_preview_tag_named(group, tag_name, sizeof(struct object_definition));
	if (root && group == ACTOR_VARIANT_DEFINITION_TAG)
	{
		variant = (struct actor_variant_definition const *)root;
		root = mcc_preview_tag_of_handle(variant->unit_reference.index, sizeof(struct object_definition), &root_group);
	}
	if (!root)
		return FALSE;
	model = (struct model const *)mcc_preview_tag_of_handle(((struct object_definition const *)root)->object.model.index,
		sizeof(struct model), &model_group);
	if (!model || model_group != 'mod2' || model->regions.count <= 0 || model->geometries.count <= 0)
		return FALSE;
	build.model = model;
	for (index = 0; index < MCC_PREVIEW_MAXIMUM_SHADERS; index++)
		build.shader_materials[index] = MCC_PREVIEW_MATERIAL_UNMADE;
	mcc_preview_change_colors((struct object_definition const *)root, variant, &build);
	mesh->positions = malloc(MCC_PREVIEW_MAXIMUM_VERTICES * sizeof(*mesh->positions));
	mesh->normals = malloc(MCC_PREVIEW_MAXIMUM_VERTICES * sizeof(*mesh->normals));
	mesh->texcoords = malloc(MCC_PREVIEW_MAXIMUM_VERTICES * sizeof(*mesh->texcoords));
	mesh->triangles = malloc(MCC_PREVIEW_MAXIMUM_TRIANGLES * sizeof(*mesh->triangles));
	mesh->triangle_materials = malloc(MCC_PREVIEW_MAXIMUM_TRIANGLES * sizeof(*mesh->triangle_materials));
	if (!mesh->positions || !mesh->normals || !mesh->texcoords || !mesh->triangles || !mesh->triangle_materials)
	{
		tag_injector_mesh_free(mesh);
		return FALSE;
	}
	/* (its opaque parts, else all of them) */
	for (pass = 0; pass < 2 && !mesh->triangle_count; pass++)
	{
		byte *regions = mcc_preview_block(&model->regions, sizeof(struct model_region));
		byte *geometries = mcc_preview_block(&model->geometries, MCC_GEOMETRY_BYTES);
		short region_index;

		mesh->vertex_count = 0;
		if (!regions || !geometries)
			break;
		for (region_index = 0; region_index < MIN(model->regions.count, MCC_PREVIEW_MAXIMUM_REGIONS); region_index++)
		{
			struct model_region const *region = (struct model_region const *)(regions +
				region_index * sizeof(struct model_region));
			byte *permutations = mcc_preview_block(&region->permutations, sizeof(struct model_region_permutation));
			struct model_region_permutation const *permutation = NULL;
			short permutation_index;
			short geometry_index = NONE;
			short level;
			struct tag_block const *parts_block;
			byte *parts;
			long part_index;

			for (permutation_index = 0; permutations && permutation_index < region->permutations.count; permutation_index++)
			{
				struct model_region_permutation const *candidate = (struct model_region_permutation const *)(permutations +
					permutation_index * sizeof(struct model_region_permutation));

				if (!permutation)
					permutation = candidate;
				if (!TEST_FLAG(candidate->flags, _model_region_permutation_cannot_be_chosen_randomly_bit))
				{
					permutation = candidate;
					break;
				}
			}
			if (!permutation)
				continue;
			for (level = MCC_PREVIEW_DETAIL_LEVELS - 1;
				level >= 0 && (geometry_index < 0 || geometry_index >= model->geometries.count); level--)
			{
				geometry_index = permutation->geometry_indices[level];
			}
			if (geometry_index < 0 || geometry_index >= model->geometries.count)
				continue;
			parts_block = (struct tag_block const *)(geometries + geometry_index * MCC_GEOMETRY_BYTES +
				MCC_GEOMETRY_PARTS_OFFSET);
			parts = mcc_preview_block(parts_block, MCC_PART_BYTES);
			for (part_index = 0; parts && part_index < parts_block->count; part_index++)
				mcc_preview_part(&build, parts + part_index * MCC_PART_BYTES, pass == 0, mesh);
		}
	}
	if (!mesh->triangle_count)
	{
		tag_injector_mesh_free(mesh);
		return FALSE;
	}
	return TRUE;
}

/* (a reference's tag there: the map has it) */
static boolean mcc_preview_has(struct tag_reference const *reference)
{
	unsigned long group = 0;

	return reference->index != NONE && mcc_preview_tag_of_handle(reference->index, 4, &group) != NULL;
}

boolean mcc_tag_preview_weapon_usable(char const *tag_name)
{
	struct weapon_definition const *weapon;
	struct weapon_interface_definition const *interface;
	byte *triggers;
	boolean fires = FALSE;
	char lowered[256];
	long index;

	if (!mcc_preview.map || !tag_name)
		return TRUE;
	weapon = (struct weapon_definition const *)mcc_preview_tag_named(WEAPON_DEFINITION_TAG, tag_name,
		sizeof(struct weapon_definition));
	if (!weapon)
		return TRUE;
	interface = &weapon->weapon.interface_definition;
	if (!mcc_preview_has(&weapon->object.model) || !mcc_preview_has(&weapon->object.collision_model) ||
		!mcc_preview_has(&interface->first_person_model) || !mcc_preview_has(&interface->first_person_animations) ||
		!mcc_preview_has(&interface->hud_interface))
	{
		return FALSE;
	}
	triggers = mcc_preview_block(&weapon->weapon.triggers, sizeof(struct weapon_trigger_definition));
	for (index = 0; triggers && index < weapon->weapon.triggers.count; index++)
	{
		struct weapon_trigger_definition const *trigger = (struct weapon_trigger_definition const *)(triggers +
			index * sizeof(struct weapon_trigger_definition));

		if (mcc_preview_has(&trigger->projectile))
			fires = TRUE;
	}
	for (index = 0; tag_name[index] && index < (long)sizeof(lowered) - 1; index++)
		lowered[index] = (char)tolower((unsigned char)tag_name[index]);
	lowered[index] = 0;
	return fires || strstr(lowered, "sword") || strstr(lowered, "hammer") || strstr(lowered, "melee");
}
