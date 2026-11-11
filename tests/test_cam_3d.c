/**
 * test_cam_3d.c - 3D Camera and Perspective Pipeline unit tests for liblamina
 */

#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <ttypt/lam.h>
#include <ttypt/lam-tm.h>
#include <ttypt/lam-cam.h>

static void test_mat4_identity(void) {
	lam_mat4_t m;
	lam_mat4_identity(m);

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			float expected = (i == j) ? 1.0f : 0.0f;
			assert(fabsf(m[i * 4 + j] - expected) < 1e-6f);
		}
	}
	printf("  test_mat4_identity: PASS\n");
}

static void test_mat4_mul_and_invert(void) {
	lam_mat4_t a, b, c, inv, ident;
	lam_mat4_identity(a);
	lam_mat4_translate(a, 10.0f, -5.0f, 2.5f);
	lam_mat4_rotate_x(a, 0.785398f);
	lam_mat4_scale(a, 2.0f, 1.5f, 0.8f);

	lam_mat4_identity(b);
	lam_mat4_translate(b, -3.0f, 4.0f, 1.0f);

	lam_mat4_mul(c, a, b);

	/* Inversion test: c * inv(c) should be identity */
	bool ok = lam_mat4_invert(inv, c);
	assert(ok);

	lam_mat4_mul(ident, c, inv);
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			float expected = (i == j) ? 1.0f : 0.0f;
			assert(fabsf(ident[i * 4 + j] - expected) < 1e-4f);
		}
	}
	printf("  test_mat4_mul_and_invert: PASS\n");
}

static void test_mat4_perspective(void) {
	lam_mat4_t proj;
	float fov = 45.0f * (float)M_PI / 180.0f;
	float aspect = 1.333333f;
	float near_z = 0.5f;
	float far_z = 500.0f;

	lam_mat4_perspective(proj, fov, aspect, near_z, far_z);
	assert(proj[0] > 0.0f); /* X scale */
	assert(proj[5] > 0.0f); /* Y scale */
	assert(proj[11] == -1.0f); /* Perspective W factor */
	assert(proj[10] < 0.0f); /* Depth mapping */

	printf("  test_mat4_perspective: PASS\n");
}

static void test_cam_setup_and_toggle(void) {
	assert(!lam_cam_is_3d());

	lam_cam_set_3d(100.0f, 200.0f, 25.0f, 50.0f, 0.0f, 45.0f);
	assert(lam_cam_is_3d());

	const lam_cam_t *cam = lam_cam_get();
	assert(fabsf(cam->target[0] - 100.0f) < 1e-4f);
	assert(fabsf(cam->target[1] - 200.0f) < 1e-4f);
	assert(fabsf(cam->distance - 25.0f) < 1e-4f);
	assert(fabsf(cam->pitch_deg - 50.0f) < 1e-4f);

	float r[3];
	lam_cam_get_right(r);
	/* For yaw 0, right should be along +X */
	assert(fabsf(r[0] - 1.0f) < 1e-4f);
	assert(fabsf(r[1]) < 1e-4f);
	assert(fabsf(r[2]) < 1e-4f);

	lam_cam_set_2d();
	assert(!lam_cam_is_3d());

	printf("  test_cam_setup_and_toggle: PASS\n");
}

static void test_unproject_ground(void) {
	uint32_t w = 0, h = 0;
	lam_size(&w, &h);

	float target_x = 35.0f;
	float target_y = 42.0f;
	float distance = 18.0f;
	float pitch = 55.0f;

	lam_cam_set_3d(target_x, target_y, distance, pitch, 0.0f, 45.0f);

	/* Ray through exact screen center must hit the camera target on ground! */
	float hit_x = 0.0f, hit_y = 0.0f;
	int ok = lam_unproject_ground((int32_t)w / 2, (int32_t)h / 2, &hit_x, &hit_y);
	assert(ok == 1);
	assert(fabsf(hit_x - target_x) < 0.1f);
	assert(fabsf(hit_y - target_y) < 0.1f);

	/* Points higher on screen (smaller screen_y) should hit further North (smaller world Y) */
	float hit_north_x = 0.0f, hit_north_y = 0.0f;
	ok = lam_unproject_ground((int32_t)w / 2, (int32_t)h / 4, &hit_north_x, &hit_north_y);
	assert(ok == 1);
	assert(hit_north_y < target_y);

	/* Points lower on screen (larger screen_y) should hit further South (larger world Y) */
	float hit_south_x = 0.0f, hit_south_y = 0.0f;
	ok = lam_unproject_ground((int32_t)w / 2, (int32_t)(3 * h) / 4, &hit_south_x, &hit_south_y);
	assert(ok == 1);
	assert(hit_south_y > target_y);

	lam_cam_set_2d();
	printf("  test_unproject_ground: PASS (center hit=(%.2f, %.2f) expected=(%.2f, %.2f))\n",
	       hit_x, hit_y, target_x, target_y);
}

static void test_3d_tile_and_billboard_drawing(void) {
	uint32_t img_ref = lam_tex_load("tests/fixtures/test_tilemap.png");
	uint32_t tm_ref = lam_tm_new(img_ref, 16, 16);

	lam_cam_set_3d(5.0f, 5.0f, 15.0f, 50.0f, 0.0f, 45.0f);

	/* Draw ground tiles */
	for (int y = 0; y < 10; y++) {
		for (int x = 0; x < 10; x++) {
			lam_tile_draw_ground_3d(tm_ref, (x + y) % 16, (float)x, (float)y, 0.0f, 1.0f, 1.0f);
		}
	}

	/* Draw billboard sprites */
	lam_tile_draw_billboard_3d(tm_ref, 0, 5.0f, 5.0f, 0.0f, 1.0f, 1.5f, 0.5f, 0.0f);
	lam_tile_draw_billboard_3d(tm_ref, 1, 3.0f, 7.0f, 0.0f, 1.0f, 1.5f, 0.5f, 0.0f);

	lam_flush();
	lam_cam_set_2d();

	printf("  test_3d_tile_and_billboard_drawing: PASS\n");
}

int main(void) {
	lam_init();
	printf("test_cam_3d:\n");
	test_mat4_identity();
	test_mat4_mul_and_invert();
	test_mat4_perspective();
	test_cam_setup_and_toggle();
	test_unproject_ground();
	test_3d_tile_and_billboard_drawing();
	printf("test_cam_3d: ALL TESTS PASSED\n");
	return 0;
}
