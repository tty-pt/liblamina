/**
 * test_particle.c - Standalone 3D Ballistic & Fluid-Advected Particle System unit tests for liblamina
 */

#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "../include/ttypt/lam.h"

static float flat_surface_fn(void *user_ctx, float wx, float wy)
{
	(void)wx;
	(void)wy;
	float *floor_z = (float *)user_ctx;
	return floor_z ? *floor_z : 0.0f;
}

int main(void)
{
	printf("test_particle:\n");

	lam_particle_sys_t *ps = lam_particle_sys_create(64, 32.0f);
	assert(ps != NULL);
	assert(lam_particle_capacity(ps) == 64);
	assert(lam_particle_active_count(ps) == 0);

	lam_particle_emitter_t em;
	memset(&em, 0, sizeof(em));
	em.x = 100.0f;
	em.y = 200.0f;
	em.z = 5.0f;
	em.spread_x = 1.0f;
	em.spread_y = 1.0f;
	em.spread_z = 0.2f;
	em.dir_x = 0.0f;
	em.dir_y = 0.0f;
	em.dir_z = 1.0f;
	em.speed_min = 12.0f;
	em.speed_max = 16.0f;
	em.cone_spread = 0.25f;
	em.size0_min = 2.0f;
	em.size0_max = 2.0f;
	em.size1_min = 4.0f;
	em.size1_max = 4.0f;
	em.life_min = 1.0f;
	em.life_max = 1.0f;
	em.drag = 2.0f;
	em.gravity_scale = 1.0f;
	em.restitution = -1.0f;
	em.rgba0 = 0xFFFFFFFFu;
	em.rgba1 = 0xFFFFFF80u;
	em.count = 16;
	em.kind = 1;

	size_t spawned = lam_particle_emit(ps, &em);
	assert(spawned == 16);
	assert(lam_particle_active_count(ps) == 16);

	/* Step forward 0.1s with Eastward wind (+10.0, 0, 0) */
	lam_particle_step(ps, 0.1f, 10.0f, 0.0f, 0.0f, NULL, NULL);
	assert(lam_particle_active_count(ps) == 16);

	lam_particle_draw_t draw[64];
	size_t nd = lam_particle_gather(ps, draw, 64);
	assert(nd == 16);
	for (size_t i = 0; i < nd; i++) {
		assert(draw[i].z > 5.0f);
		assert(draw[i].size > 2.0f && draw[i].size < 4.0f);
		assert((draw[i].rgba & 0xFFu) > 0);
		assert(draw[i].kind == 1);
	}

	/* Step past lifetime (1.0s) -> all particles expire cleanly */
	for (int s = 0; s < 10; s++)
		lam_particle_step(ps, 0.15f, 10.0f, 0.0f, 0.0f, NULL, NULL);
	assert(lam_particle_active_count(ps) == 0);

	/* Test surface collision with restitution = -1 (kill on water/ground contact) */
	float floor_z = 4.0f;
	em.z = 4.2f;
	em.dir_z = -1.0f;
	em.cone_spread = 0.05f;
	em.speed_min = 10.0f;
	em.speed_max = 10.0f;
	em.restitution = -1.0f;
	em.count = 8;
	lam_particle_emit(ps, &em);
	assert(lam_particle_active_count(ps) == 8);
	lam_particle_step(ps, 0.1f, 0.0f, 0.0f, 0.0f, flat_surface_fn, &floor_z);
	assert(lam_particle_active_count(ps) == 0);

	lam_particle_sys_destroy(ps);
	printf("  test_particle: ALL TESTS PASSED\n");
	return 0;
}
