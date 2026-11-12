/**
 * @file particle.c
 * @brief Generic 3D Ballistic & Fluid-Advected Particle System for liblamina.
 *
 * Pure C, zero-GL-dependency 3D particle simulation engine compatible with
 * both native SSR/OpenGL and browser WebAssembly builds.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../include/ttypt/lam.h"

struct lam_particle_sys {
	lam_particle_t *pool;
	size_t          capacity;
	size_t          active_count;
	size_t          cursor;
	float           gravity_z;
	uint32_t        rng_state;
};

static inline uint32_t lam_particle_rng_next(uint32_t *state)
{
	uint32_t x = *state;
	if (x == 0)
		x = 0x9E3779B9u;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

static inline float lam_particle_rng_f01(uint32_t *state)
{
	return (float)(lam_particle_rng_next(state) & 0x00FFFFFFu) * (1.0f / 16777215.0f);
}

static inline float lam_particle_rng_range(uint32_t *state, float lo, float hi)
{
	return lo + (hi - lo) * lam_particle_rng_f01(state);
}

lam_particle_sys_t *lam_particle_sys_create(size_t capacity, float default_gravity_z)
{
	if (capacity == 0)
		capacity = 256;

	size_t total_bytes = sizeof(lam_particle_sys_t) + capacity * sizeof(lam_particle_t);
	uint8_t *raw = (uint8_t *)calloc(1, total_bytes);
	if (!raw)
		return NULL;

	lam_particle_sys_t *ps = (lam_particle_sys_t *)raw;
	ps->pool = (lam_particle_t *)(raw + sizeof(lam_particle_sys_t));
	ps->capacity = capacity;
	ps->active_count = 0;
	ps->cursor = 0;
	ps->gravity_z = default_gravity_z;
	ps->rng_state = 0xA341316Cu;
	return ps;
}

void lam_particle_sys_destroy(lam_particle_sys_t *ps)
{
	if (!ps)
		return;
	free(ps);
}

void lam_particle_sys_clear(lam_particle_sys_t *ps)
{
	if (!ps)
		return;
	memset(ps->pool, 0, ps->capacity * sizeof(lam_particle_t));
	ps->active_count = 0;
	ps->cursor = 0;
}

size_t lam_particle_emit(lam_particle_sys_t *ps, const lam_particle_emitter_t *em)
{
	if (!ps || !em || em->count == 0 || ps->capacity == 0)
		return 0;

	float dx = em->dir_x, dy = em->dir_y, dz = em->dir_z;
	float dlen = sqrtf(dx * dx + dy * dy + dz * dz);
	if (dlen > 1e-5f) {
		dx /= dlen;
		dy /= dlen;
		dz /= dlen;
	} else {
		dx = 0.0f;
		dy = 0.0f;
		dz = 1.0f;
	}

	size_t spawned = 0;
	for (uint32_t i = 0; i < em->count; i++) {
		size_t idx = ps->cursor;
		for (size_t probe = 0; probe < ps->capacity; probe++) {
			size_t cand = (ps->cursor + probe) % ps->capacity;
			if ((ps->pool[cand].flags & 1u) == 0) {
				idx = cand;
				break;
			}
		}
		ps->cursor = (idx + 1) % ps->capacity;

		lam_particle_t *p = &ps->pool[idx];
		if ((p->flags & 1u) == 0)
			ps->active_count++;

		p->x = em->x + lam_particle_rng_range(&ps->rng_state, -em->spread_x, em->spread_x);
		p->y = em->y + lam_particle_rng_range(&ps->rng_state, -em->spread_y, em->spread_y);
		p->z = em->z + lam_particle_rng_range(&ps->rng_state, -em->spread_z, em->spread_z);

		float rx = lam_particle_rng_range(&ps->rng_state, -1.0f, 1.0f) * em->cone_spread;
		float ry = lam_particle_rng_range(&ps->rng_state, -1.0f, 1.0f) * em->cone_spread;
		float rz = lam_particle_rng_range(&ps->rng_state, -0.5f, 1.0f) * em->cone_spread;
		float vx = dx + rx;
		float vy = dy + ry;
		float vz = dz + rz;
		float vlen = sqrtf(vx * vx + vy * vy + vz * vz);
		if (vlen > 1e-5f) {
			vx /= vlen;
			vy /= vlen;
			vz /= vlen;
		}
		float spd = lam_particle_rng_range(&ps->rng_state, em->speed_min, em->speed_max);
		p->vx = vx * spd;
		p->vy = vy * spd;
		p->vz = vz * spd;

		p->size0 = lam_particle_rng_range(&ps->rng_state, em->size0_min, em->size0_max);
		p->size1 = lam_particle_rng_range(&ps->rng_state, em->size1_min, em->size1_max);
		p->age = 0.0f;
		float life = lam_particle_rng_range(&ps->rng_state, em->life_min, em->life_max);
		p->max_age = (life > 0.02f) ? life : 0.02f;
		p->drag = (em->drag >= 0.0f) ? em->drag : 0.0f;
		p->gravity_scale = em->gravity_scale;
		p->restitution = em->restitution;
		p->rgba0 = em->rgba0;
		p->rgba1 = em->rgba1;
		p->kind = em->kind;
		p->flags = 1u;
		p->seed = (uint16_t)(lam_particle_rng_next(&ps->rng_state) & 0xFFFFu);
		spawned++;
	}
	return spawned;
}

void lam_particle_step(lam_particle_sys_t *ps, float dt,
                       float wind_vx, float wind_vy, float wind_vz,
                       lam_particle_surface_fn surface_fn, void *user_ctx)
{
	if (!ps || dt <= 0.0f)
		return;
	if (dt > 0.25f)
		dt = 0.25f;

	size_t live = 0;
	for (size_t i = 0; i < ps->capacity; i++) {
		lam_particle_t *p = &ps->pool[i];
		if ((p->flags & 1u) == 0)
			continue;

		p->age += dt;
		if (p->age >= p->max_age) {
			p->flags = 0;
			continue;
		}

		/* Exponential fluid/air drag toward ambient wind velocity + gravitational acceleration */
		if (p->drag > 0.0f) {
			float damp = expf(-p->drag * dt);
			p->vx = wind_vx + (p->vx - wind_vx) * damp;
			p->vy = wind_vy + (p->vy - wind_vy) * damp;
			p->vz = wind_vz + (p->vz - wind_vz) * damp;
		}
		p->vz -= (ps->gravity_z * p->gravity_scale) * dt;

		p->x += p->vx * dt;
		p->y += p->vy * dt;
		p->z += p->vz * dt;

		if (surface_fn) {
			float surf_z = surface_fn(user_ctx, p->x, p->y);
			if (p->z <= surf_z) {
				if (p->restitution < 0.0f) {
					p->flags = 0;
					continue;
				} else if (p->restitution == 0.0f) {
					p->z = surf_z;
					if (p->vz < 0.0f)
						p->vz = 0.0f;
					p->vx *= (1.0f - fminf(1.0f, dt * 4.5f));
					p->vy *= (1.0f - fminf(1.0f, dt * 4.5f));
				} else {
					p->z = surf_z;
					if (p->vz < 0.0f)
						p->vz = -p->vz * p->restitution;
					p->vx *= 0.78f;
					p->vy *= 0.78f;
				}
			}
		}
		live++;
	}
	ps->active_count = live;
}

static inline uint32_t lam_particle_lerp_rgba(uint32_t c0, uint32_t c1, float t, float env_alpha)
{
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	float r0 = (float)((c0 >> 24) & 0xFFu);
	float g0 = (float)((c0 >> 16) & 0xFFu);
	float b0 = (float)((c0 >>  8) & 0xFFu);
	float a0 = (float)( c0        & 0xFFu);

	float r1 = (float)((c1 >> 24) & 0xFFu);
	float g1 = (float)((c1 >> 16) & 0xFFu);
	float b1 = (float)((c1 >>  8) & 0xFFu);
	float a1 = (float)( c1        & 0xFFu);

	int r = (int)(r0 + (r1 - r0) * t + 0.5f);
	int g = (int)(g0 + (g1 - g0) * t + 0.5f);
	int b = (int)(b0 + (b1 - b0) * t + 0.5f);
	int a = (int)((a0 + (a1 - a0) * t) * env_alpha + 0.5f);

	if (r < 0) r = 0; else if (r > 255) r = 255;
	if (g < 0) g = 0; else if (g > 255) g = 255;
	if (b < 0) b = 0; else if (b > 255) b = 255;
	if (a < 0) a = 0; else if (a > 255) a = 255;

	return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (uint32_t)a;
}

size_t lam_particle_gather(const lam_particle_sys_t *ps,
                           lam_particle_draw_t *out_draw, size_t max_out)
{
	if (!ps || !out_draw || max_out == 0)
		return 0;

	size_t written = 0;
	for (size_t i = 0; i < ps->capacity && written < max_out; i++) {
		const lam_particle_t *p = &ps->pool[i];
		if ((p->flags & 1u) == 0)
			continue;

		float t = (p->max_age > 1e-5f) ? (p->age / p->max_age) : 1.0f;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;

		/* Smooth attack (0..0.12) and parabolic dissolve (0.35..1.0) envelope */
		float fade_in = (t < 0.12f) ? (t / 0.12f) : 1.0f;
		float fade_out = (t > 0.35f) ? ((1.0f - t) / 0.65f) : 1.0f;
		fade_out = fade_out * fade_out * (3.0f - 2.0f * fade_out);
		float env_alpha = fade_in * fade_out;

		uint32_t rgba = lam_particle_lerp_rgba(p->rgba0, p->rgba1, t, env_alpha);
		if ((rgba & 0xFFu) == 0)
			continue;

		lam_particle_draw_t *d = &out_draw[written++];
		d->x = p->x;
		d->y = p->y;
		d->z = p->z;
		d->size = p->size0 + (p->size1 - p->size0) * t;
		d->norm_age = t;
		d->rgba = rgba;
		d->kind = p->kind;
		d->pad[0] = (uint8_t)(p->seed & 0xFFu);
		d->pad[1] = (uint8_t)((p->seed >> 8) & 0xFFu);
		d->pad[2] = 0;
	}
	return written;
}

size_t lam_particle_active_count(const lam_particle_sys_t *ps)
{
	return ps ? ps->active_count : 0;
}

size_t lam_particle_capacity(const lam_particle_sys_t *ps)
{
	return ps ? ps->capacity : 0;
}
