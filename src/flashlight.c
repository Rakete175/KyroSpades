
/*
	Copyright (c) 2017-2020 ByteBit

	This file is part of KyroSpades.

	KyroSpades is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	KyroSpades is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with KyroSpades.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <math.h>
#include <string.h>

#include "common.h"
#include "camera.h"
#include "cameracontroller.h"
#include "config.h"
#include "flashlight.h"
#include "glx.h"
#include "map.h"
#include "matrix.h"
#include "player.h"
#include "tesselator.h"
#include "texture.h"
#include "window.h"

#define FLASHLIGHT_RANGE 32.0F
#define FLASHLIGHT_BEAM_SLICES 10
#define FLASHLIGHT_TANGENT 1.0F

int flashlight_on = 0;

static float flashlight_on_time = 0.0F;
static struct Orientation flashlight_orientation[PLAYERS_MAX];
static struct tesselator flashlight_tesselator;
static int flashlight_ready = 0;

void flashlight_init(void) {
	tesselator_create(&flashlight_tesselator, VERTEX_FLOAT, 0, 1);
	flashlight_ready = 1;
	flashlight_reset();
}

void flashlight_reset(void) {
	flashlight_on = 0;
	flashlight_on_time = 0.0F;
	memset(flashlight_orientation, 0, sizeof(flashlight_orientation));
}

void flashlight_toggle(void) {
	flashlight_on = !flashlight_on;
	if(flashlight_on)
		flashlight_on_time = window_time();
}

static int flashlight_active(int id) {
	if(!players[id].connected || !players[id].alive || players[id].team == TEAM_SPECTATOR)
		return 0;
	if(id == local_player_id)
		return flashlight_on;
	return settings.everyone_flashlight;
}

static float flashlight_brightness(int id) {
	if(id != local_player_id)
		return 1.0F;
	return 1.0F - expf(-(window_time() - flashlight_on_time) * 5.0F);
}

void flashlight_update(float dt) {
	if(dt <= 0.0F || dt > 0.25F)
		dt = 0.016F;

	float blend = 1.0F - powf(1.0E-6F, dt);

	for(int k = 0; k < PLAYERS_MAX; k++) {
		if(!players[k].connected) {
			flashlight_orientation[k] = players[k].orientation;
			continue;
		}

		struct Orientation front = players[k].orientation;
		struct Orientation* o = flashlight_orientation + k;

		if(len3D(o->x, o->y, o->z) < 0.01F) {
			*o = front;
			continue;
		}

		float dx = front.x - o->x, dy = front.y - o->y, dz = front.z - o->z;
		float dist = len3D(dx, dy, dz);
		if(dist > 0.1F) {
			float s = (dist - 0.1F) / dist;
			o->x += dx * s;
			o->y += dy * s;
			o->z += dz * s;
		}

		o->x += (front.x - o->x) * blend;
		o->y += (front.y - o->y) * blend;
		o->z += (front.z - o->z) * blend;

		float l = len3D(o->x, o->y, o->z);
		if(l > 0.0001F) {
			o->x /= l;
			o->y /= l;
			o->z /= l;
		} else {
			*o = front;
		}
	}
}

static void flashlight_quad(float cx, float cy, float cz, float ax, float ay, float az, float bx, float by, float bz,
							float radius, uint32_t color) {
	ax *= radius;
	ay *= radius;
	az *= radius;
	bx *= radius;
	by *= radius;
	bz *= radius;

	float coords[12] = {
		cx - ax - bx, cy - ay - by, cz - az - bz,
		cx + ax - bx, cy + ay - by, cz + az - bz,
		cx + ax + bx, cy + ay + by, cz + az + bz,
		cx - ax + bx, cy - ay + by, cz - az + bz,
	};
	float uvs[8] = {0.002F, 0.002F, 0.998F, 0.002F, 0.998F, 0.998F, 0.002F, 0.998F};

	tesselator_set_color(&flashlight_tesselator, color);
	tesselator_addf_uv(&flashlight_tesselator, coords, uvs);
}

static uint32_t flashlight_color(float intensity) {
	if(intensity > 1.0F)
		intensity = 1.0F;
	int a = (int)(intensity * 255.0F);
	if(a < 1)
		a = 1;
	return rgba(255, 179, 128, a);
}

static float flashlight_trace(float ox, float oy, float oz, float dx, float dy, float dz, float* nx, float* ny,
							  float* nz) {
	*nx = -dx;
	*ny = -dy;
	*nz = -dz;

	int* pos = camera_terrain_pickEx(1, ox, oy, oz, dx, dy, dz);
	if(pos == NULL)
		return FLASHLIGHT_RANGE;

	float fnx = (float)(pos[3] - pos[0]);
	float fny = (float)(pos[4] - pos[1]);
	float fnz = (float)(pos[5] - pos[2]);

	float plane, along, origin;
	if(fnx != 0.0F) {
		plane = (fnx > 0.0F) ? (float)(pos[0] + 1) : (float)pos[0];
		along = dx;
		origin = ox;
	} else if(fny != 0.0F) {
		plane = (fny > 0.0F) ? (float)(pos[1] + 1) : (float)pos[1];
		along = dy;
		origin = oy;
	} else if(fnz != 0.0F) {
		plane = (fnz > 0.0F) ? (float)(pos[2] + 1) : (float)pos[2];
		along = dz;
		origin = oz;
	} else {
		return FLASHLIGHT_RANGE;
	}

	if(fabsf(along) < 0.0001F)
		return FLASHLIGHT_RANGE;

	float t = (plane - origin) / along;
	if(t <= 0.05F || t >= FLASHLIGHT_RANGE)
		return FLASHLIGHT_RANGE;

	*nx = fnx;
	*ny = fny;
	*nz = fnz;
	return t;
}

static void flashlight_emit(int id, float rx, float ry, float rz, float ux, float uy, float uz) {
	struct Player* p = players + id;
	struct Orientation o = flashlight_orientation[id];

	if(len3D(o.x, o.y, o.z) < 0.01F)
		o = p->orientation;

	float ox = p->physics.eye.x;
	float oy = p->physics.eye.y + player_height(p) - 0.1F;
	float oz = p->physics.eye.z;

	if(id == local_player_id && camera_mode == CAMERAMODE_FPS) {
		ox = camera_x;
		oy = camera_y;
		oz = camera_z;
	}

	ox += o.x * 0.2F;
	oy += o.y * 0.2F;
	oz += o.z * 0.2F;

	float brightness = flashlight_brightness(id);
	if(brightness <= 0.01F)
		return;

	float nx, ny, nz;
	float dist = flashlight_trace(ox, oy, oz, o.x, o.y, o.z, &nx, &ny, &nz);

	float hx = ox + o.x * dist;
	float hy = oy + o.y * dist;
	float hz = oz + o.z * dist;

	float falloff = 1.0F - dist / FLASHLIGHT_RANGE;
	falloff = falloff * falloff;

	float incidence = fabsf(o.x * nx + o.y * ny + o.z * nz);
	if(incidence < 0.15F)
		incidence = 0.15F;

	float radius = dist * FLASHLIGHT_TANGENT * 0.55F;
	if(radius < 0.4F)
		radius = 0.4F;
	if(radius > 9.0F)
		radius = 9.0F;
	radius /= incidence < 0.4F ? 0.4F : incidence;

	float ax, ay, az, bx, by, bz;
	if(fabsf(ny) > 0.5F) {
		ax = 1.0F;
		ay = 0.0F;
		az = 0.0F;
	} else {
		ax = 0.0F;
		ay = 1.0F;
		az = 0.0F;
	}
	bx = ny * az - nz * ay;
	by = nz * ax - nx * az;
	bz = nx * ay - ny * ax;
	float bl = len3D(bx, by, bz);
	bx /= bl;
	by /= bl;
	bz /= bl;
	ax = by * nz - bz * ny;
	ay = bz * nx - bx * nz;
	az = bx * ny - by * nx;
	float al = len3D(ax, ay, az);
	ax /= al;
	ay /= al;
	az /= al;

	flashlight_quad(hx + nx * 0.02F, hy + ny * 0.02F, hz + nz * 0.02F, ax, ay, az, bx, by, bz, radius,
					flashlight_color(brightness * falloff * (0.35F + 0.65F * incidence)));

	for(int i = 0; i < FLASHLIGHT_BEAM_SLICES; i++) {
		float t = dist * ((float)i + 0.5F) / (float)FLASHLIGHT_BEAM_SLICES;
		if(t < 0.5F)
			continue;

		float sx = ox + o.x * t;
		float sy = oy + o.y * t;
		float sz = oz + o.z * t;

		if(distance3D(sx, sy, sz, camera_x, camera_y, camera_z) < 1.44F)
			continue;

		float slice = 1.0F - t / FLASHLIGHT_RANGE;
		flashlight_quad(sx, sy, sz, rx, ry, rz, ux, uy, uz, 0.2F + t * FLASHLIGHT_TANGENT * 0.5F,
						flashlight_color(brightness * slice * slice * 0.045F));
	}

	if(id != local_player_id || camera_mode != CAMERAMODE_FPS)
		flashlight_quad(ox, oy, oz, rx, ry, rz, ux, uy, uz, 0.28F, flashlight_color(brightness * 0.7F));
}

void flashlight_render(void) {
	if(!flashlight_ready || !settings.flashlight)
		return;

	tesselator_clear(&flashlight_tesselator);

	float fx = sinf(camera_rot_x) * sinf(camera_rot_y);
	float fy = cosf(camera_rot_y);
	float fz = cosf(camera_rot_x) * sinf(camera_rot_y);

	float rx = fz, ry = 0.0F, rz = -fx;
	float rl = len3D(rx, ry, rz);
	if(rl < 0.0001F) {
		rx = 1.0F;
		ry = 0.0F;
		rz = 0.0F;
	} else {
		rx /= rl;
		rz /= rl;
	}

	float ux = ry * fz - rz * fy;
	float uy = rz * fx - rx * fz;
	float uz = rx * fy - ry * fx;
	float ul = len3D(ux, uy, uz);
	ux /= ul;
	uy /= ul;
	uz /= ul;

	float cull_sq = (settings.render_distance + 2.0F) * (settings.render_distance + 2.0F);

	for(int k = 0; k < PLAYERS_MAX; k++) {
		if(!flashlight_active(k))
			continue;
		if(k != local_player_id && distance2D(players[k].pos.x, players[k].pos.z, camera_x, camera_z) > cull_sq)
			continue;
		flashlight_emit(k, rx, ry, rz, ux, uy, uz);
	}

	if(flashlight_tesselator.quad_count == 0)
		return;

	glDisable(GL_FOG);
	glDepthMask(GL_FALSE);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	glActiveTexture(GL_TEXTURE0);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, texture_spotlight.texture_id);

	matrix_push(matrix_model);
	matrix_identity(matrix_model);
	matrix_upload();

	tesselator_draw(&flashlight_tesselator, 1);

	matrix_pop(matrix_model);
	matrix_upload();

	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_TEXTURE_2D);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_BLEND);
	glEnable(GL_CULL_FACE);
	glDepthMask(GL_TRUE);
}
