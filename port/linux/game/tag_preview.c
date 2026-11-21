/*
TAG_PREVIEW.C

The Tag Injector's preview (tag_preview.h): the model of the tag chosen in
its menu (tag_injector.c's tag_injector_*_preview: a weapon's, equipment's,
a vehicle's, a biped's or scenery's own, an actor variant's its unit's), in
its default pose, turning about its up axis and seen from a little above,
drawn by this file into a picture of its own: each part with its shader's
base map (a model's or an environment's shader's; grey without one), lit
from above and in front, its edges toward the eye in the menus' blue, on
nothing (clear), which the menu's tag_preview widget draws (ui_widget.c),
as a Custom Edition map's picture is drawn. Drawn by the processor, the same
on every build, a frame in TAG_PREVIEW_FRAME_MILLISECONDS at most: its
triangles, nearest first by a depth for each texel, each vertex's light and
texture coordinates carried across them (straight across: the view has no
perspective), each texel its texture's nearest.

The textures are decoded from Halo PC's pixels as they are read (DXT1, 3
and 5, and the uncompressed formats). A base map's alpha is not drawn (it is
a mask for its detail maps, not a part's transparency); a multipurpose map's
is its color change mask, where a model's shader's base map is tinted by the
object's change color its shader names (tag_injector_mesh_material: a grunt
major's red, a minor's orange, on the same model).

Its model is read once the same tag has been chosen for
TAG_PREVIEW_SETTLE_MILLISECONDS, so that the rows scrolled past are not, a
profile's tag from its bundle (its tags, and its vertices, strips and
pixels), the map open's from the map's tags already loaded; and kept while
it is chosen, with what it needs to work (tag_preview_needs). It is drawn to
fill the widget's shape whichever way it has turned.

A sky's model (tag_injector_mesh's sky) is seen as the game draws it, from
inside: from its origin, by perspective, turning about its up axis and
looking a little up, only its triangles that face the eye (as most of
them do, which says which way they turn), cut at a near plane. Its opaque
parts are drawn first, the nearest kept; then its transparent shaders'
parts (their first map) over them in its model's order, each by its
shader's framebuffer blend function, where nothing opaque is nearer.
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "bitmaps/bitmaps.h"
#include "bitmaps/bitmap_group.h"
#include "rasterizer/xbox/rasterizer_xbox_hardware_bitmaps.h"
#include "custom_edition_cache.h"
#include "mcc_tag_preview.h"
#include "tag_injector.h"
#include "tag_preview.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

/* the picture: of the widget's shape, more texels than the widget has
units, for screens several times the menus' 640x480 */
#define TAG_PREVIEW_TEXTURE_WIDTH 512
#define TAG_PREVIEW_TEXTURE_HEIGHT 256
/* how far a marked point's square reaches from it, in texels */
#define TAG_PREVIEW_MARKER_SIZE 5
#define TAG_PREVIEW_MARKER_CHOSEN_SIZE 8
/* how long a tag is chosen before its model is read, and how often its
picture is drawn again */
#define TAG_PREVIEW_SETTLE_MILLISECONDS 150
#define TAG_PREVIEW_FRAME_MILLISECONDS 33
/* a turn's time, the view's pitch down onto it (radians), and how much of
the widget it fills at most */
#define TAG_PREVIEW_TURN_MILLISECONDS 8000
#define TAG_PREVIEW_PITCH 0.35f
#define TAG_PREVIEW_FILL 0.9f
/* its light: what every side has, what faces the light has more, and the
blue its edges have; the light's direction, to the right, up and toward the
eye; and how much brighter a texture is drawn than it is (a base map is
made to be lit by more than this light) */
#define TAG_PREVIEW_AMBIENT 0.3f
#define TAG_PREVIEW_DIFFUSE 0.7f
#define TAG_PREVIEW_RIM 0.6f
#define TAG_PREVIEW_LIGHT_RIGHT -0.45f
#define TAG_PREVIEW_LIGHT_UP 0.55f
#define TAG_PREVIEW_LIGHT_TOWARD 0.70f
#define TAG_PREVIEW_TEXTURE_BRIGHTNESS 1.5f
#define TAG_PREVIEW_KEY_LENGTH 384
/* a sky's view, from its model's origin: a turn's time, its pitch up from
the horizon (radians), how wide it sees across the widget (radians), and
the nearest it draws, of its model's farthest vertex */
#define TAG_PREVIEW_SKY_TURN_MILLISECONDS 40000
#define TAG_PREVIEW_SKY_PITCH 0.2f
#define TAG_PREVIEW_SKY_FIELD_OF_VIEW 1.75f
#define TAG_PREVIEW_SKY_NEAR 0.001f
/* the corners a triangle cut at the near plane has at most */
#define TAG_PREVIEW_SKY_CORNERS 4
#define TAG_PREVIEW_PI 3.14159265f
/* an effect's particles played: the most drawn at once, how much their
speed is let go each second (as most particles' physics slow them), and the
pause after the last has gone before they play again, and the longest and
shortest they play before that (seconds) */
#define TAG_PREVIEW_MAXIMUM_PARTICLES 512
#define TAG_PREVIEW_PARTICLE_DRAG 0.8f
#define TAG_PREVIEW_PARTICLE_PAUSE 0.5f
#define TAG_PREVIEW_PARTICLE_LONGEST 10.0f
#define TAG_PREVIEW_PARTICLE_SHORTEST 0.75f
/* the times of a play its particles are found at, to fit them all */
#define TAG_PREVIEW_PARTICLE_FIT_STEPS 32

/* (bitmaps.c's formats: the picture's, and those of the textures decoded) */
enum
{
	_bitmap_format_a8 = 0,
	_bitmap_format_y8 = 1,
	_bitmap_format_ay8 = 2,
	_bitmap_format_a8y8 = 3,
	_bitmap_format_r5g6b5 = 6,
	_bitmap_format_a1r5g5b5 = 8,
	_bitmap_format_a4r4g4b4 = 9,
	_bitmap_format_x8r8g8b8 = 10,
	_bitmap_format_a8r8g8b8 = 11,
	_bitmap_format_dxt1 = 14,
	_bitmap_format_dxt3 = 15,
	_bitmap_format_dxt5 = 16,
};

/* its color without a texture, and its edges' (the menus' #2896FF) */
static float const tag_preview_color[3] = { 214.0f, 222.0f, 232.0f };
static float const tag_preview_rim_color[3] = { 40.0f, 150.0f, 255.0f };

/* what the preview is of */
enum
{
	_tag_preview_none,
	_tag_preview_map_tag,
	_tag_preview_profile_tag,
	/* (an MCC map's tag: mcc_tag_preview.c, its group in first, its name in
	tag_preview_mcc_name) */
	_tag_preview_mcc_tag,
};

static char tag_preview_mcc_name[256];

/* ---------- structures */

/* a texture of the mesh, decoded: its texels, 0xAARRGGBB, the top row
first (NULL: one that cannot be decoded, drawn grey) */
struct tag_preview_texture
{
	long width;
	long height;
	uint32_t *texels;
};

/* ---------- globals */

/* (port_tag_profiles.c's: port_tag_profiles.h) */
long port_tag_profiles_count(void);
long port_tag_profiles_tag_count(long profile);
char const *port_tag_profiles_folder(long profile);
char const *port_tag_profiles_tag_name(long profile, long tag);
unsigned long port_tag_profiles_tag_group(long profile, long tag);

static struct
{
	/* the tag chosen: what it is, by its key ("" for none), and when it was
	chosen; and whether it has been read, and could be (its mesh empty when
	its model cannot be), and what it needs to work */
	char key[TAG_PREVIEW_KEY_LENGTH];
	short kind;
	long first;
	long second;
	unsigned long chosen_time;
	boolean read;
	boolean readable;
	struct tag_injector_mesh mesh;
	struct tag_injector_needs needs;
	struct tag_preview_texture textures[TAG_INJECTOR_MESH_MAXIMUM_TEXTURES];
	/* the mesh's middle, and the widget's units a world unit is drawn at */
	float center[3];
	float scale;
	/* a sky's: which way its triangles turn that face its origin (1, else
	-1: most of them do, being seen from it), and the nearest it draws */
	float sky_winding;
	float sky_near;
	/* each vertex as it is drawn: where in the picture (x, y) and its depth
	toward the eye; and its light (what its color or texture is drawn by)
	and the blue of its edge */
	float (*projected)[3];
	float (*shading)[2];
	/* an effect's: its particles' picture as it is drawn (premultiplied
	red, green, blue and how much is covered, each texel), and how long a
	play of them is (seconds) */
	float *particle_buffer;
	float particle_period;
	/* its box, and the points marked on it (tag_preview_markers) */
	float minimum[3];
	float maximum[3];
	float markers[TAG_PREVIEW_MAXIMUM_MARKERS][3];
	short marker_count;
	short marker_chosen;
	/* the picture and its texels' depths (failed: they could not be made,
	and are not tried again until the menu closes), and when it was drawn
	(drawn: since its model was read) */
	struct bitmap_data *bitmap;
	float *depths;
	boolean failed;
	unsigned long drawn_time;
	boolean drawn;
} tag_preview;

/* ---------- private code */

static void tag_preview_forget(
	void)
{
	short index;

	tag_injector_mesh_free(&tag_preview.mesh);
	/* (the game's free does not take NULL) */
	for (index = 0; index < TAG_INJECTOR_MESH_MAXIMUM_TEXTURES; index++)
	{
		if (tag_preview.textures[index].texels)
			free(tag_preview.textures[index].texels);
		tag_preview.textures[index].texels = NULL;
	}
	if (tag_preview.projected)
		free(tag_preview.projected);
	if (tag_preview.shading)
		free(tag_preview.shading);
	tag_preview.projected = NULL;
	tag_preview.shading = NULL;
	if (tag_preview.particle_buffer)
		free(tag_preview.particle_buffer);
	tag_preview.particle_buffer = NULL;
	memset(&tag_preview.needs, 0, sizeof(tag_preview.needs));
	tag_preview.read = FALSE;
	tag_preview.readable = FALSE;
	tag_preview.drawn = FALSE;
	tag_preview.marker_count = 0;
}

/* a 16-bit color of 5, 6 and 5 bits as 0xFFRRGGBB */
static uint32_t tag_preview_r5g6b5(
	unsigned long color)
{
	unsigned long red = (color >> 11) & 0x1F;
	unsigned long green = (color >> 5) & 0x3F;
	unsigned long blue = color & 0x1F;

	return 0xFF000000u | (uint32_t)((red << 3 | red >> 2) << 16 | (green << 2 | green >> 4) << 8 | (blue << 3 | blue >> 2));
}

/* the colors of a DXT block's 4x4 texels (at `block`, its 8 bytes of
colors), into the texture at (x, y); four colors (DXT3's and DXT5's), else
three and black where c0 is not more than c1 (DXT1's) */
static void tag_preview_dxt_colors(
	unsigned char const *block,
	boolean four,
	struct tag_preview_texture *texture,
	long x,
	long y)
{
	unsigned long c0 = (unsigned long)block[0] | (unsigned long)block[1] << 8;
	unsigned long c1 = (unsigned long)block[2] | (unsigned long)block[3] << 8;
	uint32_t colors[4];
	short channel;
	long row;
	long column;

	colors[0] = tag_preview_r5g6b5(c0);
	colors[1] = tag_preview_r5g6b5(c1);
	colors[2] = 0xFF000000u;
	/* (DXT1's fourth in three colors: clear) */
	colors[3] = four || c0 > c1 ? 0xFF000000u : 0;
	for (channel = 0; channel < 3; channel++)
	{
		unsigned long a = (colors[0] >> (8 * channel)) & 0xFF;
		unsigned long b = (colors[1] >> (8 * channel)) & 0xFF;

		if (four || c0 > c1)
		{
			colors[2] |= (uint32_t)((2 * a + b) / 3) << (8 * channel);
			colors[3] |= (uint32_t)((a + 2 * b) / 3) << (8 * channel);
		}
		else
		{
			colors[2] |= (uint32_t)((a + b) / 2) << (8 * channel);
		}
	}
	for (row = 0; row < 4 && y + row < texture->height; row++)
	{
		unsigned long bits = block[4 + row];

		for (column = 0; column < 4 && x + column < texture->width; column++)
			texture->texels[(y + row) * texture->width + x + column] = colors[(bits >> (2 * column)) & 3];
	}
}

/* the alpha of a DXT3 or DXT5 block's 4x4 texels (at `block`, its 8 bytes
of alpha) into the texture at (x, y), over the colors there */
static void tag_preview_dxt_alpha(
	unsigned char const *block,
	boolean interpolated,
	struct tag_preview_texture *texture,
	long x,
	long y)
{
	unsigned long alphas[8];
	unsigned long long bits = 0;
	short index;
	long row;
	long column;

	if (interpolated)
	{
		/* (DXT5's: two alphas, six or four between them, and 3 bits a texel) */
		alphas[0] = block[0];
		alphas[1] = block[1];
		for (index = 2; index < 8; index++)
		{
			alphas[index] = alphas[0] > alphas[1] ?
				((8 - index) * alphas[0] + (index - 1) * alphas[1]) / 7 :
				index < 6 ? ((6 - index) * alphas[0] + (index - 1) * alphas[1]) / 5 : index == 6 ? 0 : 255;
		}
		for (index = 0; index < 6; index++)
			bits |= (unsigned long long)block[2 + index] << (8 * index);
	}
	for (row = 0; row < 4 && y + row < texture->height; row++)
	{
		for (column = 0; column < 4 && x + column < texture->width; column++)
		{
			long texel = row * 4 + column;
			uint32_t *out = &texture->texels[(y + row) * texture->width + x + column];
			/* (DXT3's: 4 bits a texel) */
			unsigned long alpha = interpolated ? alphas[(bits >> (3 * texel)) & 7] :
				((unsigned long)(block[texel / 2] >> (4 * (texel & 1))) & 0xF) * 0x11;

			*out = (*out & 0x00FFFFFFu) | (uint32_t)alpha << 24;
		}
	}
}

/* a texture of the mesh's decoded from its pixels; FALSE for a format it is
not, or pixels too few for its size */
static boolean tag_preview_texture_decode(
	struct tag_injector_mesh_texture const *in,
	struct tag_preview_texture *out)
{
	unsigned char const *pixels = in->pixels;
	long count = (long)in->width * in->height;
	long blocks_across = (in->width + 3) / 4;
	long blocks_down = (in->height + 3) / 4;
	long index;

	out->width = in->width;
	out->height = in->height;
	if (out->width <= 0 || out->height <= 0 || !pixels)
		return FALSE;
	switch (in->format)
	{
	case _bitmap_format_dxt1:
		if (in->size < (unsigned long)(blocks_across * blocks_down * 8))
			return FALSE;
		break;
	case _bitmap_format_dxt3:
	case _bitmap_format_dxt5:
		if (in->size < (unsigned long)(blocks_across * blocks_down * 16))
			return FALSE;
		break;
	case _bitmap_format_a8:
	case _bitmap_format_y8:
	case _bitmap_format_ay8:
		if (in->size < (unsigned long)count)
			return FALSE;
		break;
	case _bitmap_format_a8y8:
	case _bitmap_format_r5g6b5:
	case _bitmap_format_a1r5g5b5:
	case _bitmap_format_a4r4g4b4:
		if (in->size < (unsigned long)count * 2)
			return FALSE;
		break;
	case _bitmap_format_x8r8g8b8:
	case _bitmap_format_a8r8g8b8:
		if (in->size < (unsigned long)count * 4)
			return FALSE;
		break;
	default:
		return FALSE;
	}
	out->texels = malloc((unsigned long)count * sizeof(uint32_t));
	if (!out->texels)
		return FALSE;
	if (in->format >= _bitmap_format_dxt1)
	{
		long block_bytes = in->format == _bitmap_format_dxt1 ? 8 : 16;
		long block_y;
		long block_x;

		for (block_y = 0; block_y < blocks_down; block_y++)
		{
			for (block_x = 0; block_x < blocks_across; block_x++)
			{
				unsigned char const *block = pixels + (block_y * blocks_across + block_x) * block_bytes;

				/* (DXT3's and DXT5's colors after their 8 bytes of alpha) */
				tag_preview_dxt_colors(block + block_bytes - 8, in->format != _bitmap_format_dxt1, out, block_x * 4,
					block_y * 4);
				if (in->format != _bitmap_format_dxt1)
					tag_preview_dxt_alpha(block, in->format == _bitmap_format_dxt5, out, block_x * 4, block_y * 4);
			}
		}
		return TRUE;
	}
	for (index = 0; index < count; index++)
	{
		uint32_t color;

		switch (in->format)
		{
		case _bitmap_format_a8:
			color = (uint32_t)pixels[index] << 24 | 0xFFFFFFu;
			break;
		case _bitmap_format_y8:
			color = 0xFF000000u | (uint32_t)pixels[index] * 0x010101u;
			break;
		case _bitmap_format_ay8:
			color = (uint32_t)pixels[index] * 0x01010101u;
			break;
		case _bitmap_format_a8y8:
			/* (its low byte the brightness, its high byte alpha) */
			color = (uint32_t)pixels[index * 2 + 1] << 24 | (uint32_t)pixels[index * 2] * 0x010101u;
			break;
		case _bitmap_format_r5g6b5:
			color = tag_preview_r5g6b5((unsigned long)pixels[index * 2] | (unsigned long)pixels[index * 2 + 1] << 8);
			break;
		case _bitmap_format_a1r5g5b5:
		{
			unsigned long value = (unsigned long)pixels[index * 2] | (unsigned long)pixels[index * 2 + 1] << 8;
			unsigned long red = (value >> 10) & 0x1F;
			unsigned long green = (value >> 5) & 0x1F;
			unsigned long blue = value & 0x1F;

			color = (value & 0x8000 ? 0xFF000000u : 0) | (uint32_t)((red << 3 | red >> 2) << 16 |
				(green << 3 | green >> 2) << 8 | (blue << 3 | blue >> 2));
			break;
		}
		case _bitmap_format_a4r4g4b4:
		{
			unsigned long value = (unsigned long)pixels[index * 2] | (unsigned long)pixels[index * 2 + 1] << 8;

			color = (uint32_t)((value >> 12) & 0xF) * 0x11 << 24 | (uint32_t)(((value >> 8) & 0xF) * 0x11 << 16 |
				((value >> 4) & 0xF) * 0x11 << 8 | (value & 0xF) * 0x11);
			break;
		}
		default:
			/* (blue, green, red and alpha, in that order: x8r8g8b8's alpha
			opaque) */
			color = (in->format == _bitmap_format_a8r8g8b8 ? (uint32_t)pixels[index * 4 + 3] << 24 : 0xFF000000u) |
				(uint32_t)pixels[index * 4 + 2] << 16 | (uint32_t)pixels[index * 4 + 1] << 8 | (uint32_t)pixels[index * 4];
			break;
		}
		out->texels[index] = color;
	}

	return TRUE;
}

/* the middle of the mesh, and the scale that fits it in the widget however
it turns: across, as far as its farthest vertex from its up axis; down,
as high as it is, tipped by the pitch */
static void tag_preview_fit(
	void)
{
	struct tag_injector_mesh const *mesh = &tag_preview.mesh;
	float minimum[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
	float maximum[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
	float across = 0.0f;
	float high = 0.0f;
	float down;
	long index;
	short axis;

	for (index = 0; index < mesh->vertex_count; index++)
	{
		for (axis = 0; axis < 3; axis++)
		{
			minimum[axis] = MIN(minimum[axis], mesh->positions[index][axis]);
			maximum[axis] = MAX(maximum[axis], mesh->positions[index][axis]);
		}
	}
	for (axis = 0; axis < 3; axis++)
	{
		tag_preview.center[axis] = (minimum[axis] + maximum[axis]) * 0.5f;
		tag_preview.minimum[axis] = minimum[axis];
		tag_preview.maximum[axis] = maximum[axis];
	}
	for (index = 0; index < mesh->vertex_count; index++)
	{
		float x = mesh->positions[index][0] - tag_preview.center[0];
		float y = mesh->positions[index][1] - tag_preview.center[1];

		across = MAX(across, x * x + y * y);
	}
	across = MAX(sqrtf(across), 0.001f);
	high = (maximum[2] - minimum[2]) * 0.5f;
	down = high * cosf(TAG_PREVIEW_PITCH) + across * sinf(TAG_PREVIEW_PITCH);
	tag_preview.scale = TAG_PREVIEW_FILL *
		MIN(TAG_PREVIEW_WIDTH * 0.5f / across, TAG_PREVIEW_HEIGHT * 0.5f / MAX(down, 0.001f));
}

/* a sky's: which way the triangles facing its origin turn, by the area of
those that turn each way, and how near it draws */
static void tag_preview_sky_fit(
	void)
{
	struct tag_injector_mesh const *mesh = &tag_preview.mesh;
	double facing = 0.0;
	float farthest = 0.0f;
	long index;

	for (index = 0; index < mesh->vertex_count; index++)
	{
		float const *p = mesh->positions[index];

		farthest = MAX(farthest, p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
	}
	for (index = 0; index < mesh->triangle_count; index++)
	{
		float const *a = mesh->positions[mesh->triangles[index][0]];
		float const *b = mesh->positions[mesh->triangles[index][1]];
		float const *c = mesh->positions[mesh->triangles[index][2]];
		float ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
		float ac[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
		float normal[3] = { ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0] };
		float middle[3] = { (a[0] + b[0] + c[0]) / 3.0f, (a[1] + b[1] + c[1]) / 3.0f, (a[2] + b[2] + c[2]) / 3.0f };
		float length = sqrtf(middle[0] * middle[0] + middle[1] * middle[1] + middle[2] * middle[2]);

		/* (toward the origin, its area's worth: a triangle's normal its area
		long) */
		if (length > 0.0001f)
			facing -= (normal[0] * middle[0] + normal[1] * middle[1] + normal[2] * middle[2]) / length;
	}
	tag_preview.sky_winding = facing >= 0.0 ? 1.0f : -1.0f;
	tag_preview.sky_near = MAX(sqrtf(farthest) * TAG_PREVIEW_SKY_NEAR, 0.0001f);
}

static void tag_preview_particles_fit(
	void);

/* the tag chosen read: what it needs, and its model and textures */
static void tag_preview_read(
	void)
{
	short index;

	tag_preview.read = TRUE;
	if (tag_preview.kind == _tag_preview_map_tag)
	{
		tag_preview.readable = tag_injector_map_tag_preview((short)tag_preview.first, tag_preview.second,
			&tag_preview.mesh, &tag_preview.needs);
	}
	else if (tag_preview.kind == _tag_preview_profile_tag)
	{
		tag_preview.readable = tag_injector_profile_tag_preview(tag_preview.first, tag_preview.second,
			&tag_preview.mesh, &tag_preview.needs);
	}
	else if (tag_preview.kind == _tag_preview_mcc_tag)
	{
		/* (no needs: mcc_tag_preview.c reads a model alone) */
		memset(&tag_preview.needs, 0, sizeof(tag_preview.needs));
		tag_preview.readable = mcc_tag_preview_mesh((unsigned long)tag_preview.first, tag_preview_mcc_name,
			&tag_preview.mesh);
	}
	if (tag_preview.mesh.triangle_count <= 0 && tag_preview.mesh.emitter_count <= 0)
		return;
	for (index = 0; index < tag_preview.mesh.texture_count; index++)
	{
		if (!tag_preview_texture_decode(&tag_preview.mesh.textures[index], &tag_preview.textures[index]) &&
			tag_preview.textures[index].texels)
		{
			free(tag_preview.textures[index].texels);
			tag_preview.textures[index].texels = NULL;
		}
	}
	/* (an effect's particles: no triangles, but their picture's buffer) */
	if (tag_preview.mesh.emitter_count > 0)
	{
		tag_preview.particle_buffer = malloc(TAG_PREVIEW_TEXTURE_WIDTH * TAG_PREVIEW_TEXTURE_HEIGHT * 4 * sizeof(float));
		if (tag_preview.particle_buffer)
			tag_preview_particles_fit();
		return;
	}
	tag_preview.projected = malloc((unsigned long)tag_preview.mesh.vertex_count * sizeof(*tag_preview.projected));
	tag_preview.shading = malloc((unsigned long)tag_preview.mesh.vertex_count * sizeof(*tag_preview.shading));
	if (!tag_preview.projected || !tag_preview.shading)
	{
		struct tag_injector_needs needs = tag_preview.needs;
		boolean readable = tag_preview.readable;

		/* (what it needs kept: only its picture is gone) */
		tag_preview_forget();
		tag_preview.needs = needs;
		tag_preview.readable = readable;
		tag_preview.read = TRUE;
		return;
	}
	tag_preview_fit();
	if (tag_preview.mesh.sky)
		tag_preview_sky_fit();
}

/* the tag chosen now: its key, and what it is */
static void tag_preview_choose(
	char const *key,
	short kind,
	long first,
	long second)
{
	unsigned long now = system_milliseconds();

	if (strcmp(key, tag_preview.key))
	{
		tag_preview_forget();
		snprintf(tag_preview.key, sizeof(tag_preview.key), "%s", key);
		tag_preview.kind = kind;
		tag_preview.first = first;
		tag_preview.second = second;
		tag_preview.chosen_time = now;
	}
	if (!tag_preview.read && tag_preview.kind != _tag_preview_none &&
		now - tag_preview.chosen_time >= TAG_PREVIEW_SETTLE_MILLISECONDS)
	{
		tag_preview_read();
	}
}

/* the picture made, the first time it is wanted; FALSE when it cannot be */
static boolean tag_preview_picture_new(
	void)
{
	if (tag_preview.bitmap)
		return TRUE;
	if (tag_preview.failed)
		return FALSE;
	tag_preview.failed = TRUE;
	tag_preview.depths = malloc(TAG_PREVIEW_TEXTURE_WIDTH * TAG_PREVIEW_TEXTURE_HEIGHT * sizeof(float));
	if (!tag_preview.depths)
		return FALSE;
	tag_preview.bitmap = bitmap_2d_new(TAG_PREVIEW_TEXTURE_WIDTH, TAG_PREVIEW_TEXTURE_HEIGHT, 0,
		_bitmap_format_a8r8g8b8);
	if (tag_preview.bitmap && tag_preview.bitmap->base_address)
	{
		memset(XBOX_POINTER(uint32_t, tag_preview.bitmap->base_address), 0,
			TAG_PREVIEW_TEXTURE_WIDTH * TAG_PREVIEW_TEXTURE_HEIGHT * sizeof(uint32_t));
		bitmap_rebuild(tag_preview.bitmap);
	}
	if (tag_preview.bitmap && (!tag_preview.bitmap->base_address || !tag_preview.bitmap->hardware_format))
	{
		bitmap_delete(tag_preview.bitmap);
		tag_preview.bitmap = NULL;
	}
	if (!tag_preview.bitmap)
	{
		free(tag_preview.depths);
		tag_preview.depths = NULL;
		return FALSE;
	}
	tag_preview.failed = FALSE;
	return TRUE;
}

/* each vertex where it is drawn as the model has turned by `turn`, and its
light */
static void tag_preview_project(
	float turn)
{
	struct tag_injector_mesh const *mesh = &tag_preview.mesh;
	float turn_cosine = cosf(turn);
	float turn_sine = sinf(turn);
	float pitch_cosine = cosf(TAG_PREVIEW_PITCH);
	float pitch_sine = sinf(TAG_PREVIEW_PITCH);
	float across = tag_preview.scale * TAG_PREVIEW_TEXTURE_WIDTH / TAG_PREVIEW_WIDTH;
	float down = tag_preview.scale * TAG_PREVIEW_TEXTURE_HEIGHT / TAG_PREVIEW_HEIGHT;
	float light[3] = { TAG_PREVIEW_LIGHT_RIGHT, TAG_PREVIEW_LIGHT_UP, TAG_PREVIEW_LIGHT_TOWARD };
	float length = sqrtf(light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
	long index;
	short channel;

	for (channel = 0; channel < 3; channel++)
		light[channel] /= length;
	for (index = 0; index < mesh->vertex_count; index++)
	{
		float const *position = mesh->positions[index];
		float const *normal = mesh->normals[index];
		float x = position[0] - tag_preview.center[0];
		float y = position[1] - tag_preview.center[1];
		float z = position[2] - tag_preview.center[2];
		/* (the eye on the model's forward axis once turned, looking back
		along it: its right the model's left, +y) */
		float toward = x * turn_cosine - y * turn_sine;
		float right = x * turn_sine + y * turn_cosine;
		float up = z * pitch_cosine - toward * pitch_sine;
		float normal_toward = normal[0] * turn_cosine - normal[1] * turn_sine;
		float normal_right = normal[0] * turn_sine + normal[1] * turn_cosine;
		float normal_up = normal[2] * pitch_cosine - normal_toward * pitch_sine;
		float facing;
		float lit;
		float rim;

		normal_toward = normal_toward * pitch_cosine + normal[2] * pitch_sine;
		tag_preview.projected[index][0] = TAG_PREVIEW_TEXTURE_WIDTH * 0.5f + right * across;
		tag_preview.projected[index][1] = TAG_PREVIEW_TEXTURE_HEIGHT * 0.5f - up * down;
		tag_preview.projected[index][2] = toward * pitch_cosine + z * pitch_sine;
		length = sqrtf(normal_right * normal_right + normal_up * normal_up + normal_toward * normal_toward);
		if (length > 0.001f)
		{
			/* (both sides of a triangle drawn: the side seen is the one lit) */
			if (normal_toward < 0.0f)
				length = -length;
			normal_right /= length;
			normal_up /= length;
			normal_toward /= length;
			facing = normal_right * light[0] + normal_up * light[1] + normal_toward * light[2];
			lit = TAG_PREVIEW_AMBIENT + TAG_PREVIEW_DIFFUSE * MAX(facing, 0.0f);
			rim = 1.0f - normal_toward;
			rim = rim * rim * rim * TAG_PREVIEW_RIM;
		}
		else
		{
			lit = TAG_PREVIEW_AMBIENT + TAG_PREVIEW_DIFFUSE * 0.5f;
			rim = 0.0f;
		}
		tag_preview.shading[index][0] = lit;
		tag_preview.shading[index][1] = rim;
	}
}

/* a texture of the mesh's decoded, or NULL (none, or one that could not be) */
static struct tag_preview_texture const *tag_preview_texture(
	short index)
{
	return index >= 0 && index < tag_preview.mesh.texture_count && tag_preview.textures[index].texels ?
		&tag_preview.textures[index] : NULL;
}

/* a texture's nearest texel to (u, v), the texture repeating */
static uint32_t tag_preview_sample(
	struct tag_preview_texture const *texture,
	float u,
	float v)
{
	long x = (long)floorf((u - floorf(u)) * (float)texture->width);
	long y = (long)floorf((v - floorf(v)) * (float)texture->height);

	x = PIN(x, 0, texture->width - 1);
	y = PIN(y, 0, texture->height - 1);
	return texture->texels[y * texture->width + x];
}

/* a triangle into the picture: each texel whose middle it covers and is
nearer than what is there, its material's base map (tinted by its change
color where its mask says) or its color, by its light, and its edge's blue */
static void tag_preview_triangle(
	uint32_t *texels,
	long triangle_index)
{
	unsigned short const *triangle = tag_preview.mesh.triangles[triangle_index];
	short material_index = tag_preview.mesh.triangle_materials[triangle_index];
	struct tag_injector_mesh_material const *material =
		material_index >= 0 && material_index < tag_preview.mesh.material_count ?
		&tag_preview.mesh.materials[material_index] : NULL;
	struct tag_preview_texture const *texture = material ? tag_preview_texture(material->base_texture) : NULL;
	struct tag_preview_texture const *mask = material && material->tinted ?
		tag_preview_texture(material->mask_texture) : NULL;
	float const *a = tag_preview.projected[triangle[0]];
	float const *b = tag_preview.projected[triangle[1]];
	float const *c = tag_preview.projected[triangle[2]];
	float const *a_shading = tag_preview.shading[triangle[0]];
	float const *b_shading = tag_preview.shading[triangle[1]];
	float const *c_shading = tag_preview.shading[triangle[2]];
	float const *a_texcoord = tag_preview.mesh.texcoords[triangle[0]];
	float const *b_texcoord = tag_preview.mesh.texcoords[triangle[1]];
	float const *c_texcoord = tag_preview.mesh.texcoords[triangle[2]];
	float area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
	long left;
	long right;
	long top;
	long bottom;
	long x;
	long y;

	if (fabsf(area) < 0.0001f)
		return;
	left = (long)MAX(floorf(MIN(a[0], MIN(b[0], c[0]))), 0.0f);
	right = (long)MIN(ceilf(MAX(a[0], MAX(b[0], c[0]))), (float)(TAG_PREVIEW_TEXTURE_WIDTH - 1));
	top = (long)MAX(floorf(MIN(a[1], MIN(b[1], c[1]))), 0.0f);
	bottom = (long)MIN(ceilf(MAX(a[1], MAX(b[1], c[1]))), (float)(TAG_PREVIEW_TEXTURE_HEIGHT - 1));
	area = 1.0f / area;
	for (y = top; y <= bottom; y++)
	{
		float middle_y = (float)y + 0.5f;

		for (x = left; x <= right; x++)
		{
			float middle_x = (float)x + 0.5f;
			/* (each corner's share: the area across from it, of the whole) */
			float a_share = ((b[0] - middle_x) * (c[1] - middle_y) - (b[1] - middle_y) * (c[0] - middle_x)) * area;
			float b_share = ((c[0] - middle_x) * (a[1] - middle_y) - (c[1] - middle_y) * (a[0] - middle_x)) * area;
			float c_share = 1.0f - a_share - b_share;
			long texel = y * TAG_PREVIEW_TEXTURE_WIDTH + x;
			float depth;
			float lit;
			float rim;
			float base[3];
			uint32_t color = 0xFF000000u;
			short channel;

			if (a_share < 0.0f || b_share < 0.0f || c_share < 0.0f)
				continue;
			depth = a_share * a[2] + b_share * b[2] + c_share * c[2];
			if (depth <= tag_preview.depths[texel])
				continue;
			tag_preview.depths[texel] = depth;
			lit = a_share * a_shading[0] + b_share * b_shading[0] + c_share * c_shading[0];
			rim = a_share * a_shading[1] + b_share * b_shading[1] + c_share * c_shading[1];
			if (texture || mask)
			{
				float u = a_share * a_texcoord[0] + b_share * b_texcoord[0] + c_share * c_texcoord[0];
				float v = a_share * a_texcoord[1] + b_share * b_texcoord[1] + c_share * c_texcoord[1];
				uint32_t sample = texture ? tag_preview_sample(texture, u, v) : 0xFFFFFFFFu;

				lit *= TAG_PREVIEW_TEXTURE_BRIGHTNESS;
				base[0] = (float)((sample >> 16) & 0xFF);
				base[1] = (float)((sample >> 8) & 0xFF);
				base[2] = (float)(sample & 0xFF);
				/* (the base map times the change color, where the mask is) */
				if (mask)
				{
					float amount = (float)(tag_preview_sample(mask, u, v) >> 24) * (1.0f / 255.0f);

					for (channel = 0; channel < 3; channel++)
						base[channel] *= 1.0f - amount + amount * material->tint[channel];
				}
			}
			else
			{
				base[0] = tag_preview_color[0];
				base[1] = tag_preview_color[1];
				base[2] = tag_preview_color[2];
			}
			for (channel = 0; channel < 3; channel++)
			{
				float value = base[channel] * lit + tag_preview_rim_color[channel] * rim;

				color |= (uint32_t)PIN(value, 0.0f, 255.0f) << (16 - 8 * channel);
			}
			texels[texel] = color;
		}
	}
}

/* the points marked, over the model wherever they are: squares, the one
chosen larger and white */
static void tag_preview_markers_draw(
	uint32_t *texels,
	float turn)
{
	float across = tag_preview.scale * TAG_PREVIEW_TEXTURE_WIDTH / TAG_PREVIEW_WIDTH;
	float down = tag_preview.scale * TAG_PREVIEW_TEXTURE_HEIGHT / TAG_PREVIEW_HEIGHT;
	float turn_cosine = cosf(turn);
	float turn_sine = sinf(turn);
	float pitch_cosine = cosf(TAG_PREVIEW_PITCH);
	float pitch_sine = sinf(TAG_PREVIEW_PITCH);
	short index;

	for (index = 0; index < tag_preview.marker_count; index++)
	{
		boolean chosen = index == tag_preview.marker_chosen;
		short size = chosen ? TAG_PREVIEW_MARKER_CHOSEN_SIZE : TAG_PREVIEW_MARKER_SIZE;
		uint32_t color = chosen ? 0xFFFFFFFFu : 0xFF28D8FFu;
		float x = tag_preview.markers[index][0] - tag_preview.center[0];
		float y = tag_preview.markers[index][1] - tag_preview.center[1];
		float z = tag_preview.markers[index][2] - tag_preview.center[2];
		/* (as tag_preview_project) */
		float toward = x * turn_cosine - y * turn_sine;
		float right = x * turn_sine + y * turn_cosine;
		float up = z * pitch_cosine - toward * pitch_sine;
		long middle_x = (long)(TAG_PREVIEW_TEXTURE_WIDTH * 0.5f + right * across);
		long middle_y = (long)(TAG_PREVIEW_TEXTURE_HEIGHT * 0.5f - up * down);
		long texel_x;
		long texel_y;

		for (texel_y = middle_y - size; texel_y <= middle_y + size; texel_y++)
		{
			for (texel_x = middle_x - size; texel_x <= middle_x + size; texel_x++)
			{
				if (texel_x >= 0 && texel_x < TAG_PREVIEW_TEXTURE_WIDTH &&
					texel_y >= 0 && texel_y < TAG_PREVIEW_TEXTURE_HEIGHT)
				{
					texels[texel_y * TAG_PREVIEW_TEXTURE_WIDTH + texel_x] = color;
				}
			}
		}
	}
}

/* a corner of a sky's triangle as it is drawn: where it is seen (to the
right, up and ahead of the eye), and its texture coordinates */
struct tag_preview_sky_corner
{
	float eye[3];
	float texcoord[2];
};

/* each vertex of a sky where the eye sees it, at its origin, turned by
`turn` and looking up by the pitch: (right, up, ahead) */
static void tag_preview_sky_project(
	float turn)
{
	struct tag_injector_mesh const *mesh = &tag_preview.mesh;
	float turn_cosine = cosf(turn);
	float turn_sine = sinf(turn);
	float pitch_cosine = cosf(TAG_PREVIEW_SKY_PITCH);
	float pitch_sine = sinf(TAG_PREVIEW_SKY_PITCH);
	long index;

	for (index = 0; index < mesh->vertex_count; index++)
	{
		float const *position = mesh->positions[index];
		float ahead = position[0] * turn_cosine + position[1] * turn_sine;
		/* (the model's +y to the eye's left, as the game's) */
		float right = position[0] * turn_sine - position[1] * turn_cosine;

		tag_preview.projected[index][0] = right;
		tag_preview.projected[index][1] = position[2] * pitch_cosine - ahead * pitch_sine;
		tag_preview.projected[index][2] = ahead * pitch_cosine + position[2] * pitch_sine;
	}
}

/* (a chicago shader's function: shader_transparent_chicago_preprocessor.c's
thirteen, in their order) */
enum
{
	_tag_preview_function_current,
	_tag_preview_function_next_map,
	_tag_preview_function_multiply,
	_tag_preview_function_double_multiply,
	_tag_preview_function_add,
	_tag_preview_function_add_signed_current,
	_tag_preview_function_add_signed_next_map,
	_tag_preview_function_subtract_current,
	_tag_preview_function_subtract_next_map,
	_tag_preview_function_blend_current_alpha,
	_tag_preview_function_blend_current_alpha_inverse,
	_tag_preview_function_blend_next_map_alpha,
	_tag_preview_function_blend_next_map_alpha_inverse,
};

/* what a function makes of what the maps before made (current) and the
next map's (0 to 1), by their alphas */
static float tag_preview_function(
	short function,
	float current,
	float current_alpha,
	float next,
	float next_alpha)
{
	float value;

	switch (function)
	{
	case _tag_preview_function_next_map:
		value = next;
		break;
	case _tag_preview_function_multiply:
		value = current * next;
		break;
	case _tag_preview_function_double_multiply:
		value = 2.0f * current * next;
		break;
	case _tag_preview_function_add:
		value = current + next;
		break;
	case _tag_preview_function_add_signed_current:
	case _tag_preview_function_add_signed_next_map:
		value = current + next - 0.5f;
		break;
	case _tag_preview_function_subtract_current:
		value = current - next;
		break;
	case _tag_preview_function_subtract_next_map:
		value = next - current;
		break;
	case _tag_preview_function_blend_current_alpha:
		value = current * current_alpha + next * (1.0f - current_alpha);
		break;
	case _tag_preview_function_blend_current_alpha_inverse:
		value = next * current_alpha + current * (1.0f - current_alpha);
		break;
	case _tag_preview_function_blend_next_map_alpha:
		value = next * next_alpha + current * (1.0f - next_alpha);
		break;
	case _tag_preview_function_blend_next_map_alpha_inverse:
		value = current * next_alpha + next * (1.0f - next_alpha);
		break;
	default:
		value = current;
		break;
	}
	return PIN(value, 0.0f, 1.0f);
}

/* a material's color at (u, v): its chicago shader's maps folded together,
each repeated as it says, else its base map's (white without one) */
static uint32_t tag_preview_material_sample(
	struct tag_injector_mesh_material const *material,
	struct tag_preview_texture const *texture,
	float u,
	float v)
{
	float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	short index;
	short channel;

	if (!material || material->layer_count <= 0)
		return texture ? tag_preview_sample(texture, u, v) : 0xFFFFFFFFu;
	for (index = 0; index < material->layer_count; index++)
	{
		struct tag_injector_mesh_layer const *layer = &material->layers[index];
		struct tag_preview_texture const *layer_texture = tag_preview_texture(layer->texture);
		uint32_t sample = layer_texture ?
			tag_preview_sample(layer_texture, u * layer->scale[0], v * layer->scale[1]) : 0xFFFFFFFFu;
		float next[4];

		next[3] = (float)(sample >> 24) * (1.0f / 255.0f);
		for (channel = 0; channel < 3; channel++)
		{
			next[channel] = layer->alpha_replicate ? next[3] :
				(float)((sample >> (16 - 8 * channel)) & 0xFF) * (1.0f / 255.0f);
		}
		if (index == 0)
		{
			memcpy(color, next, sizeof(color));
			continue;
		}
		{
			struct tag_injector_mesh_layer const *before = &material->layers[index - 1];
			float alpha = color[3];

			for (channel = 0; channel < 3; channel++)
				color[channel] = tag_preview_function(before->color_function, color[channel], alpha, next[channel], next[3]);
			color[3] = tag_preview_function(before->alpha_function, alpha, alpha, next[3], next[3]);
		}
	}
	return (uint32_t)(color[3] * 255.0f + 0.5f) << 24 | (uint32_t)(color[0] * 255.0f + 0.5f) << 16 |
		(uint32_t)(color[1] * 255.0f + 0.5f) << 8 | (uint32_t)(color[2] * 255.0f + 0.5f);
}

/* what a texel of a sky's material is over what is there */
static uint32_t tag_preview_sky_blend(
	short blend,
	uint32_t source,
	uint32_t target)
{
	float alpha = (float)(source >> 24) * (1.0f / 255.0f);
	uint32_t color = 0xFF000000u;
	short channel;

	for (channel = 0; channel < 3; channel++)
	{
		short shift = (short)(16 - 8 * channel);
		float in = (float)((source >> shift) & 0xFF);
		float there = (float)((target >> shift) & 0xFF);
		float value;

		switch (blend)
		{
		case _tag_injector_blend_multiply:
			value = in * there * (1.0f / 255.0f);
			break;
		case _tag_injector_blend_double_multiply:
			value = in * there * (2.0f / 255.0f);
			break;
		case _tag_injector_blend_add:
			value = there + in;
			break;
		case _tag_injector_blend_subtract:
			value = there - in;
			break;
		case _tag_injector_blend_minimum:
			value = MIN(in, there);
			break;
		case _tag_injector_blend_maximum:
			value = MAX(in, there);
			break;
		case _tag_injector_blend_alpha_multiply_add:
			value = there + in * alpha;
			break;
		case _tag_injector_blend_alpha:
			value = in * alpha + there * (1.0f - alpha);
			break;
		default:
			value = in;
			break;
		}
		color |= (uint32_t)PIN(value, 0.0f, 255.0f) << shift;
	}
	return color;
}

/* a part of a sky's triangle in front of the eye into the picture, by its
perspective: each texel whose middle it covers, its material's base map (its
texture coordinates true to the perspective) or grey, opaque where it is the
nearest, else blended over what is there where nothing opaque is nearer */
static void tag_preview_sky_fill(
	uint32_t *texels,
	struct tag_preview_sky_corner const corners[3],
	struct tag_injector_mesh_material const *material,
	struct tag_preview_texture const *texture)
{
	/* (the widget's units of a unit's view ahead, in texels) */
	float focal = TAG_PREVIEW_WIDTH * 0.5f / tanf(TAG_PREVIEW_SKY_FIELD_OF_VIEW * 0.5f);
	float across = focal * TAG_PREVIEW_TEXTURE_WIDTH / TAG_PREVIEW_WIDTH;
	float down = focal * TAG_PREVIEW_TEXTURE_HEIGHT / TAG_PREVIEW_HEIGHT;
	boolean opaque = !material || material->blend == _tag_injector_blend_opaque;
	float screen[3][5];
	float area;
	long left;
	long right;
	long top;
	long bottom;
	long x;
	long y;
	short corner;

	/* (where it is seen, and its inverse distance ahead and its texture
	coordinates by it: those are carried straight across the picture) */
	for (corner = 0; corner < 3; corner++)
	{
		float inverse = 1.0f / corners[corner].eye[2];

		screen[corner][0] = TAG_PREVIEW_TEXTURE_WIDTH * 0.5f + corners[corner].eye[0] * inverse * across;
		screen[corner][1] = TAG_PREVIEW_TEXTURE_HEIGHT * 0.5f - corners[corner].eye[1] * inverse * down;
		screen[corner][2] = inverse;
		screen[corner][3] = corners[corner].texcoord[0] * inverse;
		screen[corner][4] = corners[corner].texcoord[1] * inverse;
	}
	area = (screen[1][0] - screen[0][0]) * (screen[2][1] - screen[0][1]) -
		(screen[1][1] - screen[0][1]) * (screen[2][0] - screen[0][0]);
	if (fabsf(area) < 0.0001f)
		return;
	left = (long)MAX(floorf(MIN(screen[0][0], MIN(screen[1][0], screen[2][0]))), 0.0f);
	right = (long)MIN(ceilf(MAX(screen[0][0], MAX(screen[1][0], screen[2][0]))), (float)(TAG_PREVIEW_TEXTURE_WIDTH - 1));
	top = (long)MAX(floorf(MIN(screen[0][1], MIN(screen[1][1], screen[2][1]))), 0.0f);
	bottom = (long)MIN(ceilf(MAX(screen[0][1], MAX(screen[1][1], screen[2][1]))), (float)(TAG_PREVIEW_TEXTURE_HEIGHT - 1));
	area = 1.0f / area;
	for (y = top; y <= bottom; y++)
	{
		float middle_y = (float)y + 0.5f;

		for (x = left; x <= right; x++)
		{
			float middle_x = (float)x + 0.5f;
			float a_share = ((screen[1][0] - middle_x) * (screen[2][1] - middle_y) -
				(screen[1][1] - middle_y) * (screen[2][0] - middle_x)) * area;
			float b_share = ((screen[2][0] - middle_x) * (screen[0][1] - middle_y) -
				(screen[2][1] - middle_y) * (screen[0][0] - middle_x)) * area;
			float c_share = 1.0f - a_share - b_share;
			long texel = y * TAG_PREVIEW_TEXTURE_WIDTH + x;
			float inverse;
			uint32_t sample;

			if (a_share < 0.0f || b_share < 0.0f || c_share < 0.0f)
				continue;
			/* (nearer: a larger inverse distance) */
			inverse = a_share * screen[0][2] + b_share * screen[1][2] + c_share * screen[2][2];
			if (opaque ? inverse <= tag_preview.depths[texel] : inverse < tag_preview.depths[texel])
				continue;
			if (texture || (material && material->layer_count > 0))
			{
				float u = (a_share * screen[0][3] + b_share * screen[1][3] + c_share * screen[2][3]) / inverse;
				float v = (a_share * screen[0][4] + b_share * screen[1][4] + c_share * screen[2][4]) / inverse;

				sample = tag_preview_material_sample(material, texture, u, v);
			}
			else
				sample = 0xFF000000u | (uint32_t)tag_preview_color[0] << 16 | (uint32_t)tag_preview_color[1] << 8 |
					(uint32_t)tag_preview_color[2];
			if (opaque)
			{
				tag_preview.depths[texel] = inverse;
				texels[texel] = sample | 0xFF000000u;
			}
			else
				texels[texel] = tag_preview_sky_blend(material->blend, sample, texels[texel]);
		}
	}
}

/* a sky's triangle into the picture (opaque: those of opaque materials,
else those of transparent ones) when it faces the eye: cut where it passes
behind the near plane */
static void tag_preview_sky_triangle(
	uint32_t *texels,
	long triangle_index,
	boolean opaque_pass)
{
	unsigned short const *triangle = tag_preview.mesh.triangles[triangle_index];
	short material_index = tag_preview.mesh.triangle_materials[triangle_index];
	struct tag_injector_mesh_material const *material =
		material_index >= 0 && material_index < tag_preview.mesh.material_count ?
		&tag_preview.mesh.materials[material_index] : NULL;
	boolean opaque = !material || material->blend == _tag_injector_blend_opaque;
	struct tag_preview_texture const *texture = material ? tag_preview_texture(material->base_texture) : NULL;
	struct tag_preview_sky_corner in[3];
	struct tag_preview_sky_corner out[TAG_PREVIEW_SKY_CORNERS];
	float ab[3];
	float ac[3];
	float normal[3];
	short count = 0;
	short corner;
	short axis;

	if (opaque != opaque_pass || (!opaque && !texture && !(material && material->layer_count > 0)))
		return;
	for (corner = 0; corner < 3; corner++)
	{
		memcpy(in[corner].eye, tag_preview.projected[triangle[corner]], sizeof(in[corner].eye));
		memcpy(in[corner].texcoord, tag_preview.mesh.texcoords[triangle[corner]], sizeof(in[corner].texcoord));
	}
	/* (facing the eye, at the origin, as most of the sky's do: in the
	model's space, as tag_preview_sky_fit, the eye's turning none of it) */
	{
		float const *a = tag_preview.mesh.positions[triangle[0]];
		float const *b = tag_preview.mesh.positions[triangle[1]];
		float const *c = tag_preview.mesh.positions[triangle[2]];

		for (axis = 0; axis < 3; axis++)
		{
			ab[axis] = b[axis] - a[axis];
			ac[axis] = c[axis] - a[axis];
		}
		normal[0] = ab[1] * ac[2] - ab[2] * ac[1];
		normal[1] = ab[2] * ac[0] - ab[0] * ac[2];
		normal[2] = ab[0] * ac[1] - ab[1] * ac[0];
		if (-(normal[0] * a[0] + normal[1] * a[1] + normal[2] * a[2]) * tag_preview.sky_winding <= 0.0f)
			return;
	}
	/* (its part ahead of the near plane: each corner in front of it, and
	where each edge crosses it) */
	for (corner = 0; corner < 3; corner++)
	{
		struct tag_preview_sky_corner const *from = &in[corner];
		struct tag_preview_sky_corner const *to = &in[(corner + 1) % 3];
		boolean from_in = from->eye[2] >= tag_preview.sky_near;
		boolean to_in = to->eye[2] >= tag_preview.sky_near;

		if (from_in)
			out[count++] = *from;
		if (from_in != to_in)
		{
			float share = (tag_preview.sky_near - from->eye[2]) / (to->eye[2] - from->eye[2]);
			struct tag_preview_sky_corner *cut = &out[count++];

			for (axis = 0; axis < 3; axis++)
				cut->eye[axis] = from->eye[axis] + (to->eye[axis] - from->eye[axis]) * share;
			for (axis = 0; axis < 2; axis++)
				cut->texcoord[axis] = from->texcoord[axis] + (to->texcoord[axis] - from->texcoord[axis]) * share;
			cut->eye[2] = tag_preview.sky_near;
		}
	}
	for (corner = 1; corner + 1 < count; corner++)
	{
		struct tag_preview_sky_corner fan[3];

		fan[0] = out[0];
		fan[1] = out[corner];
		fan[2] = out[corner + 1];
		tag_preview_sky_fill(texels, fan, material, texture);
	}
}

/* a sky's picture drawn again, as it is seen from its origin turned as it
is now: black where it has nothing, its opaque parts, then its transparent
ones over them in its model's order */
static void tag_preview_sky_draw(
	uint32_t *texels,
	unsigned long now)
{
	float turn = (float)(now % TAG_PREVIEW_SKY_TURN_MILLISECONDS) / TAG_PREVIEW_SKY_TURN_MILLISECONDS * 2.0f *
		TAG_PREVIEW_PI;
	long index;
	short pass;

	tag_preview_sky_project(turn);
	for (index = 0; index < TAG_PREVIEW_TEXTURE_WIDTH * TAG_PREVIEW_TEXTURE_HEIGHT; index++)
	{
		texels[index] = 0xFF000000u;
		tag_preview.depths[index] = 0.0f;
	}
	for (pass = 0; pass < 2; pass++)
	{
		for (index = 0; index < tag_preview.mesh.triangle_count; index++)
			tag_preview_sky_triangle(texels, index, pass == 0);
	}
}

/* a card's texel as it is seen over what is behind it in a game, as a
color and how much of it covers (the picture is drawn over the menu by its
alpha): alpha blended by its alpha; added (and the like) by its brightness;
multiplied (and the like) by its darkness */
static uint32_t tag_preview_card_texel(
	uint32_t sample,
	short blend)
{
	float color[3] = { (float)((sample >> 16) & 0xFF), (float)((sample >> 8) & 0xFF), (float)(sample & 0xFF) };
	float alpha = (float)(sample >> 24);
	float most = MAX(color[0], MAX(color[1], color[2]));
	float least = MIN(color[0], MIN(color[1], color[2]));
	short channel;

	switch (blend)
	{
	case _tag_injector_blend_add:
	case _tag_injector_blend_maximum:
	case _tag_injector_blend_alpha_multiply_add:
		/* (black adds nothing: its brightness covers, its color as bright
		as it can be) */
		if (blend == _tag_injector_blend_alpha_multiply_add)
			most *= alpha * (1.0f / 255.0f);
		alpha = most;
		for (channel = 0; channel < 3 && most > 0.0f; channel++)
			color[channel] = color[channel] * 255.0f / MAX(most, 1.0f);
		break;
	case _tag_injector_blend_double_multiply:
		/* (grey changes nothing: its darkness below it covers, twice) */
		for (channel = 0; channel < 3; channel++)
			color[channel] = MIN(color[channel] * 2.0f, 255.0f);
		least = MIN(color[0], MIN(color[1], color[2]));
		/* (falls through) */
	case _tag_injector_blend_multiply:
	case _tag_injector_blend_minimum:
		/* (white changes nothing: its darkness covers) */
		alpha = 255.0f - least;
		break;
	case _tag_injector_blend_subtract:
		/* (what it takes away, black) */
		alpha = most;
		color[0] = color[1] = color[2] = 0.0f;
		break;
	case _tag_injector_blend_opaque:
		alpha = 255.0f;
		break;
	default:
		break;
	}
	if (alpha < 1.0f)
		return 0;
	return (uint32_t)PIN(alpha, 0.0f, 255.0f) << 24 | (uint32_t)PIN(color[0], 0.0f, 255.0f) << 16 |
		(uint32_t)PIN(color[1], 0.0f, 255.0f) << 8 | (uint32_t)PIN(color[2], 0.0f, 255.0f);
}

/* a texture's color at (u, v), between its four nearest texels, kept
inside the rectangle (u0, u1, v0, v1) of it (a sprite's on its sheet) */
static uint32_t tag_preview_sample_smooth(
	struct tag_preview_texture const *texture,
	float u,
	float v,
	float const *rectangle)
{
	float x = u * (float)texture->width - 0.5f;
	float y = v * (float)texture->height - 0.5f;
	long left = (long)floorf(rectangle[0] * (float)texture->width);
	long right = (long)ceilf(rectangle[1] * (float)texture->width) - 1;
	long top = (long)floorf(rectangle[2] * (float)texture->height);
	long bottom = (long)ceilf(rectangle[3] * (float)texture->height) - 1;
	long x0;
	long y0;
	float fx;
	float fy;
	uint32_t corners[4];
	uint32_t result = 0;
	short channel;

	left = PIN(left, 0, texture->width - 1);
	right = PIN(right, left, texture->width - 1);
	top = PIN(top, 0, texture->height - 1);
	bottom = PIN(bottom, top, texture->height - 1);
	x0 = (long)floorf(x);
	y0 = (long)floorf(y);
	fx = x - (float)x0;
	fy = y - (float)y0;
	corners[0] = texture->texels[PIN(y0, top, bottom) * texture->width + PIN(x0, left, right)];
	corners[1] = texture->texels[PIN(y0, top, bottom) * texture->width + PIN(x0 + 1, left, right)];
	corners[2] = texture->texels[PIN(y0 + 1, top, bottom) * texture->width + PIN(x0, left, right)];
	corners[3] = texture->texels[PIN(y0 + 1, top, bottom) * texture->width + PIN(x0 + 1, left, right)];
	for (channel = 0; channel < 4; channel++)
	{
		short shift = (short)(8 * channel);
		float a = (float)((corners[0] >> shift) & 0xFF);
		float b = (float)((corners[1] >> shift) & 0xFF);
		float c = (float)((corners[2] >> shift) & 0xFF);
		float d = (float)((corners[3] >> shift) & 0xFF);
		float value = (a + (b - a) * fx) * (1.0f - fy) + (c + (d - c) * fx) * fy;

		result |= (uint32_t)PIN(value + 0.5f, 0.0f, 255.0f) << shift;
	}
	return result;
}

/* a card (a decal's, or an effect's sprite) into the picture: flat, facing
the eye, as large as the widget holds it (its shape kept: the picture's
texels are not square in the widget), by its blend */
static void tag_preview_card_draw(
	uint32_t *texels)
{
	struct tag_injector_mesh const *mesh = &tag_preview.mesh;
	struct tag_injector_mesh_material const *material = mesh->material_count > 0 ? &mesh->materials[0] : NULL;
	struct tag_preview_texture const *texture = material ? tag_preview_texture(material->base_texture) : NULL;
	float width = MAX(fabsf(mesh->positions[2][1] - mesh->positions[0][1]), 0.001f);
	float height = MAX(fabsf(mesh->positions[2][2] - mesh->positions[0][2]), 0.001f);
	float rectangle[4] = { mesh->texcoords[0][0], mesh->texcoords[2][0], mesh->texcoords[0][1], mesh->texcoords[2][1] };
	float scale = TAG_PREVIEW_FILL * MIN(TAG_PREVIEW_WIDTH / width, TAG_PREVIEW_HEIGHT / height);
	long across = PIN((long)(width * scale * TAG_PREVIEW_TEXTURE_WIDTH / TAG_PREVIEW_WIDTH), 1,
		TAG_PREVIEW_TEXTURE_WIDTH);
	long down = PIN((long)(height * scale * TAG_PREVIEW_TEXTURE_HEIGHT / TAG_PREVIEW_HEIGHT), 1,
		TAG_PREVIEW_TEXTURE_HEIGHT);
	long left = (TAG_PREVIEW_TEXTURE_WIDTH - across) / 2;
	long top = (TAG_PREVIEW_TEXTURE_HEIGHT - down) / 2;
	long x;
	long y;

	memset(texels, 0, TAG_PREVIEW_TEXTURE_WIDTH * TAG_PREVIEW_TEXTURE_HEIGHT * sizeof(uint32_t));
	for (y = 0; y < down; y++)
	{
		float v = rectangle[2] + ((float)y + 0.5f) / (float)down * (rectangle[3] - rectangle[2]);

		for (x = 0; x < across; x++)
		{
			float u = rectangle[0] + ((float)x + 0.5f) / (float)across * (rectangle[1] - rectangle[0]);
			uint32_t sample = texture ? tag_preview_sample_smooth(texture, u, v, rectangle) : 0xFFFFFFFFu;

			texels[(top + y) * TAG_PREVIEW_TEXTURE_WIDTH + left + x] =
				tag_preview_card_texel(sample, material ? material->blend : _tag_injector_blend_alpha);
		}
	}
}

/* ---------- an effect's particles */

/* a particle as it is at a time of a play: where (in the effect's space),
how large (its radius), how much it shows (its fade times its tint's
alpha), its tint, its frame, its turn (radians), and its emitter */
struct tag_preview_particle
{
	float position[3];
	float radius;
	float alpha;
	float tint[3];
	float angle;
	float depth;
	float screen[2];
	short frame;
	short emitter;
};

/* the same number between 0 and 1 for the same four, a different one for
any other: what each particle chooses between its bounds, again each play */
static float tag_preview_random(
	uint32_t a,
	uint32_t b,
	uint32_t c,
	uint32_t d)
{
	uint32_t hash = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du ^
		(d + 0x27D4EB2Fu) * 0x2C1B3C6Du;

	hash ^= hash >> 15;
	hash *= 0x2C1B3C6Du;
	hash ^= hash >> 12;
	hash *= 0x297A2D39u;
	hash ^= hash >> 15;
	return (float)(hash & 0xFFFFFF) / 16777216.0f;
}

static float tag_preview_between(
	float const *bounds,
	float amount)
{
	return bounds[0] + (bounds[1] - bounds[0]) * amount;
}

/* how long a play of the effect's particles is: until the last of them
has gone, and a pause */
static float tag_preview_particles_period(
	void)
{
	float longest = 0.0f;
	short index;

	for (index = 0; index < tag_preview.mesh.emitter_count; index++)
	{
		struct tag_injector_mesh_emitter const *emitter = &tag_preview.mesh.emitters[index];

		longest = MAX(longest, emitter->delay[1] + emitter->duration[1] + emitter->life[1]);
	}
	return PIN(longest + TAG_PREVIEW_PARTICLE_PAUSE, TAG_PREVIEW_PARTICLE_SHORTEST, TAG_PREVIEW_PARTICLE_LONGEST);
}

/* the particles alive `time` seconds into a play (the play's number makes
each play's different); how many, at most `maximum` */
static short tag_preview_particles_at(
	float time,
	uint32_t play,
	struct tag_preview_particle *particles,
	short maximum)
{
	short count = 0;
	short emitter_index;

	for (emitter_index = 0; emitter_index < tag_preview.mesh.emitter_count && count < maximum; emitter_index++)
	{
		struct tag_injector_mesh_emitter const *emitter = &tag_preview.mesh.emitters[emitter_index];
		uint32_t e = (uint32_t)emitter_index;
		float delay = tag_preview_between(emitter->delay, tag_preview_random(play, e, 0xFFFF, 1));
		float duration = tag_preview_between(emitter->duration, tag_preview_random(play, e, 0xFFFF, 2));
		short number = (short)(emitter->count[0] + (emitter->count[1] - emitter->count[0] + 1) *
			tag_preview_random(play, e, 0xFFFF, 3));
		float const *forward = emitter->direction;
		/* (two directions across the one they go: about it, the cone) */
		float side[3];
		float other[3];
		float length;
		short index;

		number = (short)PIN(number, emitter->count[0], emitter->count[1]);
		if (fabsf(forward[2]) < 0.9f)
		{
			side[0] = -forward[1];
			side[1] = forward[0];
			side[2] = 0.0f;
		}
		else
		{
			side[0] = 0.0f;
			side[1] = -forward[2];
			side[2] = forward[1];
		}
		length = sqrtf(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
		length = MAX(length, 0.0001f);
		side[0] /= length;
		side[1] /= length;
		side[2] /= length;
		other[0] = forward[1] * side[2] - forward[2] * side[1];
		other[1] = forward[2] * side[0] - forward[0] * side[2];
		other[2] = forward[0] * side[1] - forward[1] * side[0];
		for (index = 0; index < number && count < maximum; index++)
		{
			uint32_t i = (uint32_t)index;
			struct tag_preview_particle *particle = &particles[count];
			float r = tag_preview_random(play, e, i, 0);
			float born;
			float age;
			float life = tag_preview_between(emitter->life, tag_preview_random(play, e, i, 1));
			float fade = 1.0f;
			float spread;
			float cone_cosine;
			float cone_sine;
			float around;
			float travel;
			float speed;
			float drag;
			float sphere[3];
			short axis;

			/* (when it comes: the effect's distribution function's start,
			end, constant, buildup, falloff, or both) */
			switch (emitter->distribution)
			{
			case 0: born = 0.0f; break;
			case 1: born = duration; break;
			case 3: born = sqrtf(r) * duration; break;
			case 4: born = (1.0f - sqrtf(r)) * duration; break;
			case 5: born = (r + tag_preview_random(play, e, i, 2)) * 0.5f * duration; break;
			default: born = r * duration; break;
			}
			age = time - delay - born;
			if (age < 0.0f || age > life)
				continue;
			if (emitter->fade_in > 0.0f && age < emitter->fade_in)
				fade = age / emitter->fade_in;
			if (emitter->fade_out > 0.0f && life - age < emitter->fade_out)
				fade = MIN(fade, (life - age) / emitter->fade_out);
			/* (where it starts: in a ball of the distribution radius) */
			spread = tag_preview_between(emitter->distribution_radius, tag_preview_random(play, e, i, 3));
			{
				float z = tag_preview_random(play, e, i, 4) * 2.0f - 1.0f;
				float angle = tag_preview_random(play, e, i, 5) * 2.0f * TAG_PREVIEW_PI;
				float ring = sqrtf(MAX(1.0f - z * z, 0.0f));
				float reach = spread * cbrtf(tag_preview_random(play, e, i, 6));

				sphere[0] = ring * cosf(angle) * reach;
				sphere[1] = ring * sinf(angle) * reach;
				sphere[2] = z * reach;
			}
			/* (which way it goes: in the cone about its direction) */
			cone_cosine = 1.0f - tag_preview_random(play, e, i, 7) * (1.0f - cosf(MIN(emitter->cone, TAG_PREVIEW_PI)));
			cone_sine = sqrtf(MAX(1.0f - cone_cosine * cone_cosine, 0.0f));
			around = tag_preview_random(play, e, i, 8) * 2.0f * TAG_PREVIEW_PI;
			speed = tag_preview_between(emitter->velocity, tag_preview_random(play, e, i, 9));
			drag = TAG_PREVIEW_PARTICLE_DRAG;
			travel = speed * (1.0f - expf(-drag * age)) / drag;
			for (axis = 0; axis < 3; axis++)
			{
				float way = forward[axis] * cone_cosine +
					(side[axis] * cosf(around) + other[axis] * sinf(around)) * cone_sine;

				particle->position[axis] = emitter->offset[axis] + sphere[axis] + way * travel;
			}
			particle->radius = MAX(tag_preview_between(emitter->radius, tag_preview_random(play, e, i, 10)), 0.001f);
			{
				float tint = tag_preview_random(play, e, i, 11);

				particle->alpha = fade * (emitter->tint[0][0] + (emitter->tint[1][0] - emitter->tint[0][0]) * tint);
				for (axis = 0; axis < 3; axis++)
				{
					particle->tint[axis] = emitter->tint[0][axis + 1] +
						(emitter->tint[1][axis + 1] - emitter->tint[0][axis + 1]) * tint;
				}
			}
			particle->angle = tag_preview_random(play, e, i, 12) * 2.0f * TAG_PREVIEW_PI +
				tag_preview_between(emitter->angular_velocity, tag_preview_random(play, e, i, 13)) * age;
			{
				float rate = tag_preview_between(emitter->frames_per_second, tag_preview_random(play, e, i, 14));

				particle->frame = rate > 0.0f ? (short)((long)(age * rate) % MAX(emitter->frame_count, 1)) :
					(short)(tag_preview_random(play, e, i, 15) * (float)MAX(emitter->frame_count, 1));
				particle->frame = (short)PIN(particle->frame, 0, MAX(emitter->frame_count, 1) - 1);
			}
			particle->emitter = emitter_index;
			if (particle->alpha > 0.001f)
				count++;
		}
	}
	return count;
}

/* the middle of the particles over a play, and the scale that fits them
however the eye turns (as tag_preview_fit's a model) */
static void tag_preview_particles_fit(
	void)
{
	static struct tag_preview_particle particles[TAG_PREVIEW_MAXIMUM_PARTICLES];
	float minimum[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
	float maximum[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
	float across = 0.0f;
	float high;
	float down;
	short step;
	short pass;
	short axis;

	tag_preview.particle_period = tag_preview_particles_period();
	/* (the box of them, then how far across they reach from its middle) */
	for (pass = 0; pass < 2; pass++)
	{
		for (step = 0; step < TAG_PREVIEW_PARTICLE_FIT_STEPS; step++)
		{
			float time = tag_preview.particle_period * ((float)step + 0.5f) / TAG_PREVIEW_PARTICLE_FIT_STEPS;
			short count = tag_preview_particles_at(time, (uint32_t)(step & 3), particles, TAG_PREVIEW_MAXIMUM_PARTICLES);
			short index;

			for (index = 0; index < count; index++)
			{
				float const *p = particles[index].position;
				float r = particles[index].radius;

				if (pass == 0)
				{
					for (axis = 0; axis < 3; axis++)
					{
						minimum[axis] = MIN(minimum[axis], p[axis] - r);
						maximum[axis] = MAX(maximum[axis], p[axis] + r);
					}
				}
				else
				{
					float x = p[0] - tag_preview.center[0];
					float y = p[1] - tag_preview.center[1];

					across = MAX(across, sqrtf(x * x + y * y) + r);
				}
			}
		}
		if (pass == 0)
		{
			if (minimum[0] > maximum[0])
			{
				for (axis = 0; axis < 3; axis++)
				{
					minimum[axis] = -1.0f;
					maximum[axis] = 1.0f;
				}
			}
			for (axis = 0; axis < 3; axis++)
			{
				tag_preview.center[axis] = (minimum[axis] + maximum[axis]) * 0.5f;
				tag_preview.minimum[axis] = minimum[axis];
				tag_preview.maximum[axis] = maximum[axis];
			}
		}
	}
	across = MAX(across, 0.001f);
	high = (maximum[2] - minimum[2]) * 0.5f;
	down = high * cosf(TAG_PREVIEW_PITCH) + across * sinf(TAG_PREVIEW_PITCH);
	tag_preview.scale = TAG_PREVIEW_FILL *
		MIN(TAG_PREVIEW_WIDTH * 0.5f / across, TAG_PREVIEW_HEIGHT * 0.5f / MAX(down, 0.001f));
}

static int tag_preview_particle_order(
	void const *a,
	void const *b)
{
	float first = ((struct tag_preview_particle const *)a)->depth;
	float second = ((struct tag_preview_particle const *)b)->depth;

	return first < second ? -1 : first > second ? 1 : 0;
}

/* the effect's particles as they are now, the eye turning about them: each
a sprite facing the eye, the farthest first, by its blend (added ones
brighten what is behind them, the others cover it) */
static void tag_preview_particles_draw(
	uint32_t *texels,
	unsigned long now)
{
	static struct tag_preview_particle particles[TAG_PREVIEW_MAXIMUM_PARTICLES];
	float *buffer = tag_preview.particle_buffer;
	float seconds = (float)(now % 3600000u) * 0.001f;
	float period = MAX(tag_preview.particle_period, 0.1f);
	uint32_t play = (uint32_t)(seconds / period);
	float time = seconds - (float)play * period;
	float turn = (float)(now % TAG_PREVIEW_TURN_MILLISECONDS) / TAG_PREVIEW_TURN_MILLISECONDS * 2.0f * TAG_PREVIEW_PI +
		TAG_PREVIEW_PI * 0.5f;
	float turn_cosine = cosf(turn);
	float turn_sine = sinf(turn);
	float pitch_cosine = cosf(TAG_PREVIEW_PITCH);
	float pitch_sine = sinf(TAG_PREVIEW_PITCH);
	float across = tag_preview.scale * TAG_PREVIEW_TEXTURE_WIDTH / TAG_PREVIEW_WIDTH;
	float down = tag_preview.scale * TAG_PREVIEW_TEXTURE_HEIGHT / TAG_PREVIEW_HEIGHT;
	short count = tag_preview_particles_at(time, play, particles, TAG_PREVIEW_MAXIMUM_PARTICLES);
	short index;
	long texel;

	memset(buffer, 0, TAG_PREVIEW_TEXTURE_WIDTH * TAG_PREVIEW_TEXTURE_HEIGHT * 4 * sizeof(float));
	for (index = 0; index < count; index++)
	{
		struct tag_preview_particle *particle = &particles[index];
		float x = particle->position[0] - tag_preview.center[0];
		float y = particle->position[1] - tag_preview.center[1];
		float z = particle->position[2] - tag_preview.center[2];
		float toward = x * turn_cosine - y * turn_sine;
		float right = x * turn_sine + y * turn_cosine;
		float up = z * pitch_cosine - toward * pitch_sine;

		particle->screen[0] = TAG_PREVIEW_TEXTURE_WIDTH * 0.5f + right * across;
		particle->screen[1] = TAG_PREVIEW_TEXTURE_HEIGHT * 0.5f - up * down;
		particle->depth = toward * pitch_cosine + z * pitch_sine;
	}
	qsort(particles, (size_t)count, sizeof(particles[0]), tag_preview_particle_order);
	for (index = 0; index < count; index++)
	{
		struct tag_preview_particle const *particle = &particles[index];
		struct tag_injector_mesh_emitter const *emitter = &tag_preview.mesh.emitters[particle->emitter];
		struct tag_injector_mesh_material const *material =
			emitter->material >= 0 && emitter->material < tag_preview.mesh.material_count ?
			&tag_preview.mesh.materials[emitter->material] : NULL;
		struct tag_preview_texture const *texture = material ? tag_preview_texture(material->base_texture) : NULL;
		short blend = material ? material->blend : _tag_injector_blend_alpha;
		boolean added = blend == _tag_injector_blend_add || blend == _tag_injector_blend_maximum ||
			blend == _tag_injector_blend_alpha_multiply_add;
		float const *rectangle = emitter->frames[PIN(particle->frame, 0, MAX(emitter->frame_count, 1) - 1)];
		float stretch = sqrtf(MAX(emitter->aspect, 0.01f));
		float half_width = particle->radius * stretch;
		float half_height = particle->radius / stretch;
		float reach = sqrtf(half_width * half_width + half_height * half_height);
		float angle_cosine = cosf(particle->angle);
		float angle_sine = sinf(particle->angle);
		long left = (long)floorf(particle->screen[0] - reach * across);
		long right = (long)ceilf(particle->screen[0] + reach * across);
		long top = (long)floorf(particle->screen[1] - reach * down);
		long bottom = (long)ceilf(particle->screen[1] + reach * down);
		long px;
		long py;

		if (!texture)
			continue;
		left = MAX(left, 0);
		right = MIN(right, TAG_PREVIEW_TEXTURE_WIDTH - 1);
		top = MAX(top, 0);
		bottom = MIN(bottom, TAG_PREVIEW_TEXTURE_HEIGHT - 1);
		for (py = top; py <= bottom; py++)
		{
			float dy = ((float)py + 0.5f - particle->screen[1]) / down;

			for (px = left; px <= right; px++)
			{
				float dx = ((float)px + 0.5f - particle->screen[0]) / across;
				/* (the sprite turned: where in it this texel is, -1 to 1) */
				float sx = (dx * angle_cosine + dy * angle_sine) / half_width;
				float sy = (dy * angle_cosine - dx * angle_sine) / half_height;
				uint32_t sample;
				uint32_t seen;
				float alpha;
				float *out;
				short channel;

				if (sx < -1.0f || sx > 1.0f || sy < -1.0f || sy > 1.0f)
					continue;
				sample = tag_preview_sample_smooth(texture,
					rectangle[0] + (sx * 0.5f + 0.5f) * (rectangle[1] - rectangle[0]),
					rectangle[2] + (sy * 0.5f + 0.5f) * (rectangle[3] - rectangle[2]), rectangle);
				seen = tag_preview_card_texel(sample, blend);
				alpha = (float)(seen >> 24) * (1.0f / 255.0f) * particle->alpha;
				if (alpha <= 0.002f)
					continue;
				out = &buffer[(py * TAG_PREVIEW_TEXTURE_WIDTH + px) * 4];
				for (channel = 0; channel < 3; channel++)
				{
					float color = (float)((seen >> (16 - 8 * channel)) & 0xFF) * (1.0f / 255.0f) *
						particle->tint[channel];

					out[channel] = added ? out[channel] + color * alpha : out[channel] * (1.0f - alpha) + color * alpha;
				}
				out[3] = out[3] + alpha * (1.0f - out[3]);
			}
		}
	}
	for (texel = 0; texel < TAG_PREVIEW_TEXTURE_WIDTH * TAG_PREVIEW_TEXTURE_HEIGHT; texel++)
	{
		float const *in = &buffer[texel * 4];
		uint32_t color = 0;
		short channel;

		if (in[3] > 0.004f)
		{
			color = (uint32_t)PIN(in[3] * 255.0f, 0.0f, 255.0f) << 24;
			for (channel = 0; channel < 3; channel++)
				color |= (uint32_t)PIN(in[channel] / in[3] * 255.0f, 0.0f, 255.0f) << (16 - 8 * channel);
		}
		texels[texel] = color;
	}
}

/* the picture drawn again, the model turned as it is now */
static void tag_preview_draw(
	unsigned long now)
{
	uint32_t *texels = XBOX_POINTER(uint32_t, tag_preview.bitmap->base_address);
	/* (from its side: a weapon's length across) */
	float turn = (float)(now % TAG_PREVIEW_TURN_MILLISECONDS) / TAG_PREVIEW_TURN_MILLISECONDS * 2.0f * TAG_PREVIEW_PI +
		TAG_PREVIEW_PI * 0.5f;
	long index;

	if (tag_preview.mesh.emitter_count > 0)
	{
		tag_preview_particles_draw(texels, now);
		rasterizer_bitmap_changed(tag_preview.bitmap);
		tag_preview.drawn_time = now;
		tag_preview.drawn = TRUE;
		return;
	}
	if (tag_preview.mesh.card)
	{
		tag_preview_card_draw(texels);
		rasterizer_bitmap_changed(tag_preview.bitmap);
		tag_preview.drawn_time = now;
		tag_preview.drawn = TRUE;
		return;
	}
	if (tag_preview.mesh.sky)
	{
		tag_preview_sky_draw(texels, now);
		rasterizer_bitmap_changed(tag_preview.bitmap);
		tag_preview.drawn_time = now;
		tag_preview.drawn = TRUE;
		return;
	}
	tag_preview_project(turn);
	memset(texels, 0, TAG_PREVIEW_TEXTURE_WIDTH * TAG_PREVIEW_TEXTURE_HEIGHT * sizeof(uint32_t));
	for (index = 0; index < TAG_PREVIEW_TEXTURE_WIDTH * TAG_PREVIEW_TEXTURE_HEIGHT; index++)
		tag_preview.depths[index] = -FLT_MAX;
	for (index = 0; index < tag_preview.mesh.triangle_count; index++)
		tag_preview_triangle(texels, index);
	tag_preview_markers_draw(texels, turn);
	rasterizer_bitmap_changed(tag_preview.bitmap);
	tag_preview.drawn_time = now;
	tag_preview.drawn = TRUE;
}

/* ---------- public code */

void tag_preview_map_tag(
	short category,
	long index)
{
	char key[TAG_PREVIEW_KEY_LENGTH];

	snprintf(key, sizeof(key), "map %d %lu %s", tag_injector_map_opened(), tag_injector_category_group(category),
		tag_injector_category_tag(category, index));
	tag_preview_choose(key, _tag_preview_map_tag, category, index);
}

void tag_preview_profile_tag(
	long profile,
	long tag)
{
	char key[TAG_PREVIEW_KEY_LENGTH];

	if (profile < 0 || profile >= port_tag_profiles_count() || tag < 0 || tag >= port_tag_profiles_tag_count(profile))
	{
		tag_preview_none();
		return;
	}
	snprintf(key, sizeof(key), "profile %s %lu %s", port_tag_profiles_folder(profile),
		port_tag_profiles_tag_group(profile, tag), port_tag_profiles_tag_name(profile, tag));
	tag_preview_choose(key, _tag_preview_profile_tag, profile, tag);
}

void tag_preview_mcc_tag(
	unsigned long group,
	char const *tag_name)
{
	char key[TAG_PREVIEW_KEY_LENGTH];

	if (!tag_name || !mcc_tag_preview_opened())
	{
		tag_preview_none();
		return;
	}
	snprintf(key, sizeof(key), "mcc %lu %s", group, tag_name);
	/* (the name a new choice reads, kept while it is chosen) */
	if (strcmp(key, tag_preview.key))
		snprintf(tag_preview_mcc_name, sizeof(tag_preview_mcc_name), "%s", tag_name);
	tag_preview_choose(key, _tag_preview_mcc_tag, (long)group, NONE);
}

void tag_preview_none(
	void)
{
	tag_preview_choose("", _tag_preview_none, NONE, NONE);
}

struct bitmap_data *tag_preview_picture(
	void)
{
	unsigned long now = system_milliseconds();

	boolean drawable = tag_preview.mesh.emitter_count > 0 ? tag_preview.particle_buffer != NULL :
		tag_preview.mesh.triangle_count > 0 && tag_preview.projected;

	if (!tag_preview.read || !drawable || !tag_preview_picture_new())
		return NULL;
	/* (a card does not turn: drawn once) */
	if (!tag_preview.drawn || (!tag_preview.mesh.card && now - tag_preview.drawn_time >= TAG_PREVIEW_FRAME_MILLISECONDS))
		tag_preview_draw(now);

	return tag_preview.bitmap;
}

short tag_preview_needs(
	struct tag_injector_needs *needs)
{
	if (!tag_preview.read || tag_preview.kind == _tag_preview_none)
		return _tag_preview_needs_waiting;
	if (!tag_preview.readable)
		return _tag_preview_needs_unreadable;
	*needs = tag_preview.needs;
	return _tag_preview_needs_read;
}

boolean tag_preview_bounds(
	float minimum[3],
	float maximum[3])
{
	if (!tag_preview.read || tag_preview.mesh.vertex_count <= 0 || !tag_preview.projected)
		return FALSE;
	memcpy(minimum, tag_preview.minimum, sizeof(tag_preview.minimum));
	memcpy(maximum, tag_preview.maximum, sizeof(tag_preview.maximum));
	return TRUE;
}

void tag_preview_markers(
	float const (*points)[3],
	short count,
	short chosen)
{
	count = (short)MIN(count, TAG_PREVIEW_MAXIMUM_MARKERS);
	if (count > 0)
		memcpy(tag_preview.markers, points, (size_t)count * sizeof(tag_preview.markers[0]));
	tag_preview.marker_count = count;
	tag_preview.marker_chosen = chosen;
}

void tag_preview_dispose(
	void)
{
	tag_preview_forget();
	tag_preview.key[0] = 0;
	tag_preview.kind = _tag_preview_none;
	if (tag_preview.bitmap)
		bitmap_delete(tag_preview.bitmap);
	tag_preview.bitmap = NULL;
	if (tag_preview.depths)
		free(tag_preview.depths);
	tag_preview.depths = NULL;
	tag_preview.failed = FALSE;
}
