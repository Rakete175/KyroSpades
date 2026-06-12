#pragma once
/*
 * gles_immediate_stubs.h  —  Android / OpenGL ES 2.0 compatibility shim
 *
 * OpenGL ES 2.0 removed the entire fixed-function / immediate-mode pipeline.
 * The functions below are no-ops so that code using them compiles; the visual
 * features that rely on them are simply not drawn on Android until properly
 * ported to VBOs + shaders.
 */
#ifdef OPENGL_ES
#  ifndef GL_QUADS
#    define GL_QUADS 0x0007   /* no GLES equivalent; glBegin is a no-op anyway */
#  endif
static inline void glBegin(int m)                                          { (void)m; }
static inline void glEnd(void)                                             {}
static inline void glVertex2f(float x, float y)                           { (void)x; (void)y; }
static inline void glTexCoord2f(float s, float t)                         { (void)s; (void)t; }
/* NOTE: glColor4f / glColor3ub are intentionally NOT stubbed. OpenGL ES 1.x
 * provides real implementations via <SDL2/SDL_opengles.h>, and the HUD relies
 * on them to tint textures. */
#endif /* OPENGL_ES */
