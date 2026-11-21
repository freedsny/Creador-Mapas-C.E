/* The dynamic lighting's model variant under the desktop OpenGL renderer
   (d3d8_gl.c): from GulchCE's plasma_occlusion_gl.c, with the draw's choice as
   the Vulkan renderer's (d3d8_vk.c plasma_draw_prepare).

   A draw of a model lighting program with both point lights, with a light of
   the feature's in one of them, uses a pair of shaders of its own: the
   selected lights' squared falloff, and their visibility fields (3D textures,
   plasma_occlusion.c, written straight to their GL textures:
   xbox_textures.c halo_field_texture_write). The fields are bound to units
   XGPU_PLASMA_TEXTURE_UNIT and the next, which the game's four texture stages
   never use, with a sampler of their own (xgpu_gl_bind_extra_textures: GL
   4.4's multi-bind on desktop, so the active unit never changes; under WebGL
   the active unit is left at 0 again). Every other draw's shaders are as they
   were.

   The Linux, Windows and web builds have it (HALO_GL_DYNAMIC_LIGHTING);
   Android's OpenGL ES image links dynamic_lighting_disabled.c. */
#ifdef HALO_GL_DYNAMIC_LIGHTING

#include "xgpu.h"
#include "plasma_model_shader.h"
#include "plasma_occlusion.h"
#include "plasma_occlusion_gl.h"

#include <string.h>

const char xgpu_plasma_model_declarations[] =
	/* (precisions spelled out: an ES vertex shader has no default for
	sampler3D, and desktop GLSL takes and ignores them) */
	"uniform highp vec4 plasma_model_parameters;\n"
	"uniform highp vec4 plasma_occ_transform[2];\n"
	"uniform highp sampler3D plasma_occ_texture0;\n"
	"uniform highp sampler3D plasma_occ_texture1;\n";

/* the draw at hand's: its variant (XGPU_PLASMA_MODEL_*; 0 for stock), the
lights selected (x, y) and the fields they have (z, w), the fields' boxes, and
their textures */
static struct
{
	unsigned int bits;
	float parameters[4], transforms[2][4];
	GLuint textures[2];
} plasma_draw;

unsigned int plasma_gl_draw_prepare(BOOL immediate, unsigned long lights, BOOL failed)
{
	unsigned int mask;
	int slot;

	plasma_draw.bits = 0;
	if (immediate || lights != 2 || failed)
		return 0;
	mask = plasma_model_lighting_mask();
	if (!mask)
		return 0;
	memset(&plasma_draw, 0, sizeof(plasma_draw));
	/* (the samplers are always declared: a slot without a field reads 1,
	plasma_model_emit_visibility, and its unit has a texture all the same) */
	plasma_draw.bits = XGPU_PLASMA_MODEL_FALLOFF | XGPU_PLASMA_MODEL_OCCLUSION;
	for (slot = 0; slot < 2; slot++)
	{
		const DWORD *field;

		plasma_draw.parameters[slot] = (mask & (1u << slot)) ? 1.0f : 0.0f;
		field = (const DWORD *)plasma_occlusion_model_texture(slot, plasma_draw.transforms[slot]);
		if (field)
		{
			GLuint texture = xgpu_field_texture(field);

			if (texture)
			{
				plasma_draw.textures[slot] = texture;
				plasma_draw.parameters[2 + slot] = 1.0f;
			}
		}
	}
	return plasma_draw.bits;
}

void plasma_gl_draw_cancel(void)
{
	plasma_draw.bits = 0;
}

void plasma_gl_program_initialize(GLuint program, struct plasma_gl_uniforms *uniforms)
{
	int slot;

	/* (the program is current: program_finish) */
	uniforms->parameters = glGetUniformLocation(program, "plasma_model_parameters");
	uniforms->transforms = glGetUniformLocation(program, "plasma_occ_transform");
	memset(uniforms->last, 0xff, sizeof(uniforms->last));
	for (slot = 0; slot < 2; slot++)
	{
		GLint location = glGetUniformLocation(program, slot ? "plasma_occ_texture1" : "plasma_occ_texture0");

		if (location >= 0)
			glUniform1i(location, XGPU_PLASMA_TEXTURE_UNIT + slot);
	}
}

/* a 1x1x1 white volume, for a unit whose slot has no field (never read, but a
sampler3D has to have a 3D texture on its unit) */
static GLuint white_texture(void)
{
	static GLuint white;

	if (!white)
	{
		static const unsigned char pixel[4] = { 255, 255, 255, 255 };

		glGenTextures(1, &white);
		glBindTexture(GL_TEXTURE_3D, white);
		xgpu_gl_state_invalidate();
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_BASE_LEVEL, 0);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAX_LEVEL, 0);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
	}
	return white;
}

/* the fields' linear, clamped sampler, the context's for good */
static GLuint field_sampler(void)
{
	static GLuint sampler;

	if (!sampler)
	{
		glGenSamplers(1, &sampler);
		if (sampler)
		{
			glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glSamplerParameteri(sampler, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		}
	}
	return sampler;
}

/* what units XGPU_PLASMA_TEXTURE_UNIT and the next were last given (only
this module binds them; a texture the cache deletes leaves its unit, so a name
GL gives again is bound again: plasma_gl_texture_deleted) */
static GLuint bound_textures[2], bound_sampler;

void plasma_gl_texture_deleted(GLuint texture)
{
	int slot;

	for (slot = 0; slot < 2; slot++)
	{
		if (bound_textures[slot] == texture)
			bound_textures[slot] = 0;
	}
}

void plasma_gl_draw_bind(struct plasma_gl_uniforms *uniforms)
{
	GLuint textures[2], sampler;
	int slot;

	if (!plasma_draw.bits)
		return;
	for (slot = 0; slot < 2; slot++)
		textures[slot] = plasma_draw.textures[slot] ? plasma_draw.textures[slot] : white_texture();
	if (memcmp(bound_textures, textures, sizeof(textures)))
	{
		xgpu_gl_bind_extra_textures(XGPU_PLASMA_TEXTURE_UNIT, GL_TEXTURE_3D, textures, 2);
		memcpy(bound_textures, textures, sizeof(textures));
	}
	sampler = field_sampler();
	if (sampler != bound_sampler)
	{
		for (slot = 0; slot < 2; slot++)
			glBindSampler(XGPU_PLASMA_TEXTURE_UNIT + slot, sampler);
		bound_sampler = sampler;
	}
	if (uniforms->parameters >= 0 && memcmp(uniforms->last[0], plasma_draw.parameters, sizeof(plasma_draw.parameters)))
	{
		glUniform4fv(uniforms->parameters, 1, plasma_draw.parameters);
		memcpy(uniforms->last[0], plasma_draw.parameters, sizeof(plasma_draw.parameters));
	}
	if (uniforms->transforms >= 0 && memcmp(&uniforms->last[1], plasma_draw.transforms, sizeof(plasma_draw.transforms)))
	{
		glUniform4fv(uniforms->transforms, 2, plasma_draw.transforms[0]);
		memcpy(&uniforms->last[1], plasma_draw.transforms, sizeof(plasma_draw.transforms));
	}
}

#endif
