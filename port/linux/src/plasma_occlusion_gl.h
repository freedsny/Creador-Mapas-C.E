/* The dynamic lighting's model variant under the desktop OpenGL renderer
   (plasma_occlusion_gl.c; HALO_GL_DYNAMIC_LIGHTING only: Linux, Windows
   and the web). */
#ifndef PLASMA_OCCLUSION_GL_H
#define PLASMA_OCCLUSION_GL_H

#ifdef HALO_GL_DYNAMIC_LIGHTING

/* a program's locations of the variant's uniforms (-1: a stock program's),
and what they last held: the parameters, then the two transforms */
struct plasma_gl_uniforms
{
	GLint parameters, transforms;
	float last[3][4];
};

/* the variant a draw of a program with these lights would use
(XGPU_PLASMA_MODEL_*; 0: stock), and its fields found; failed: the program's
variant failed to compile or link before */
unsigned int plasma_gl_draw_prepare(BOOL immediate, unsigned long lights, BOOL failed);
/* the draw uses the stock shaders after all */
void plasma_gl_draw_cancel(void);
/* a linked program's locations, and its samplers' units (it is current) */
void plasma_gl_program_initialize(GLuint program, struct plasma_gl_uniforms *uniforms);
/* the prepared draw's fields bound and uniforms set (its program current) */
void plasma_gl_draw_bind(struct plasma_gl_uniforms *uniforms);
/* a written field's GL texture, 0 for none, without a GL call
(xbox_textures.c) */
GLuint xgpu_field_texture(const DWORD *resource);
/* a GL texture was deleted: a unit this module thought had it no longer does */
void plasma_gl_texture_deleted(GLuint texture);

#endif

#endif
