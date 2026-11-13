/**
 * @file cam.c
 * @brief 3D Camera, matrix mathematics, and perspective projections for liblamina.
 */

#include "../include/ttypt/lam-cam.h"
#include "./gl.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static lam_cam_t s_cam = {
	.target = { 0.0f, 0.0f, 0.0f },
	.distance = 20.0f,
	.pitch_deg = 50.0f,
	.yaw_deg = 0.0f,
	.fov_deg = 45.0f,
	.near_z = 0.5f,
	.far_z = 500.0f,
	.active = 0,
};

void lam_mat4_identity(float m[16])
{
	memset(m, 0, sizeof(float) * 16);
	m[0] = 1.0f;
	m[5] = 1.0f;
	m[10] = 1.0f;
	m[15] = 1.0f;
}

void lam_mat4_mul(float out[16], const float a[16], const float b[16])
{
	float res[16];
	for (int c = 0; c < 4; c++) {
		for (int r = 0; r < 4; r++) {
			res[c * 4 + r] =
				a[0 * 4 + r] * b[c * 4 + 0] +
				a[1 * 4 + r] * b[c * 4 + 1] +
				a[2 * 4 + r] * b[c * 4 + 2] +
				a[3 * 4 + r] * b[c * 4 + 3];
		}
	}
	memcpy(out, res, sizeof(float) * 16);
}

void lam_mat4_translate(float m[16], float x, float y, float z)
{
	float t[16];
	lam_mat4_identity(t);
	t[12] = x;
	t[13] = y;
	t[14] = z;
	lam_mat4_mul(m, m, t);
}

void lam_mat4_scale(float m[16], float sx, float sy, float sz)
{
	float s[16];
	lam_mat4_identity(s);
	s[0] = sx;
	s[5] = sy;
	s[10] = sz;
	lam_mat4_mul(m, m, s);
}

void lam_mat4_rotate_x(float m[16], float rad)
{
	float r[16];
	lam_mat4_identity(r);
	float c = cosf(rad);
	float s = sinf(rad);
	r[5] = c;
	r[6] = s;
	r[9] = -s;
	r[10] = c;
	lam_mat4_mul(m, m, r);
}

void lam_mat4_rotate_y(float m[16], float rad)
{
	float r[16];
	lam_mat4_identity(r);
	float c = cosf(rad);
	float s = sinf(rad);
	r[0] = c;
	r[2] = -s;
	r[8] = s;
	r[10] = c;
	lam_mat4_mul(m, m, r);
}

void lam_mat4_rotate_z(float m[16], float rad)
{
	float r[16];
	lam_mat4_identity(r);
	float c = cosf(rad);
	float s = sinf(rad);
	r[0] = c;
	r[1] = s;
	r[4] = -s;
	r[5] = c;
	lam_mat4_mul(m, m, r);
}

void lam_mat4_perspective(float m[16], float fov_rad, float aspect, float near_z, float far_z)
{
	memset(m, 0, sizeof(float) * 16);
	float tan_half = tanf(fov_rad * 0.5f);
	float f = (tan_half != 0.0f) ? (1.0f / tan_half) : 1.0f;
	m[0] = f / aspect;
	m[5] = f;
	m[10] = -(far_z + near_z) / (far_z - near_z);
	m[11] = -1.0f;
	m[14] = -(2.0f * far_z * near_z) / (far_z - near_z);
}

void lam_mat4_ortho(float m[16], float l, float r, float b, float t, float n, float f)
{
	memset(m, 0, sizeof(float) * 16);
	m[0] = 2.0f / (r - l);
	m[5] = 2.0f / (t - b);
	m[10] = -2.0f / (f - n);
	m[12] = -(r + l) / (r - l);
	m[13] = -(t + b) / (t - b);
	m[14] = -(f + n) / (f - n);
	m[15] = 1.0f;
}

void lam_mat4_look_at(float m[16], const float eye[3], const float target[3], const float up[3])
{
	float f[3] = { target[0] - eye[0], target[1] - eye[1], target[2] - eye[2] };
	float flen = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
	if (flen > 1e-6f) {
		f[0] /= flen; f[1] /= flen; f[2] /= flen;
	} else {
		f[0] = 0.0f; f[1] = -1.0f; f[2] = 0.0f;
	}

	/* r = cross(up, f) so East (+X) is to the right */
	float r[3] = {
		up[1] * f[2] - up[2] * f[1],
		up[2] * f[0] - up[0] * f[2],
		up[0] * f[1] - up[1] * f[0]
	};
	float rlen = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
	if (rlen > 1e-6f) {
		r[0] /= rlen; r[1] /= rlen; r[2] /= rlen;
	} else {
		r[0] = 1.0f; r[1] = 0.0f; r[2] = 0.0f;
	}

	/* u = cross(f, r) */
	float u[3] = {
		f[1] * r[2] - f[2] * r[1],
		f[2] * r[0] - f[0] * r[2],
		f[0] * r[1] - f[1] * r[0]
	};

	memset(m, 0, sizeof(float) * 16);
	m[0] = r[0];
	m[4] = r[1];
	m[8] = r[2];
	m[12] = -(r[0] * eye[0] + r[1] * eye[1] + r[2] * eye[2]);

	m[1] = u[0];
	m[5] = u[1];
	m[9] = u[2];
	m[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);

	m[2] = -f[0];
	m[6] = -f[1];
	m[10] = -f[2];
	m[14] = (f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2]);

	m[15] = 1.0f;
}

bool lam_mat4_invert(float out[16], const float in[16])
{
	float inv[16], det;

	inv[0] = in[5]  * in[10] * in[15] - 
             in[5]  * in[11] * in[14] - 
             in[9]  * in[6]  * in[15] + 
             in[9]  * in[7]  * in[14] +
             in[13] * in[6]  * in[11] - 
             in[13] * in[7]  * in[10];

    inv[4] = -in[4]  * in[10] * in[15] + 
              in[4]  * in[11] * in[14] + 
              in[8]  * in[6]  * in[15] - 
              in[8]  * in[7]  * in[14] - 
              in[12] * in[6]  * in[11] + 
              in[12] * in[7]  * in[10];

    inv[8] = in[4]  * in[9] * in[15] - 
             in[4]  * in[11] * in[13] - 
             in[8]  * in[5] * in[15] + 
             in[8]  * in[7] * in[13] + 
             in[12] * in[5] * in[11] - 
             in[12] * in[7] * in[9];

    inv[12] = -in[4]  * in[9] * in[14] + 
               in[4]  * in[10] * in[13] +
               in[8]  * in[5] * in[14] - 
               in[8]  * in[6] * in[13] - 
               in[12] * in[5] * in[10] + 
               in[12] * in[6] * in[9];

    inv[1] = -in[1]  * in[10] * in[15] + 
              in[1]  * in[11] * in[14] + 
              in[9]  * in[2] * in[15] - 
              in[9]  * in[3] * in[14] - 
              in[13] * in[2] * in[11] + 
              in[13] * in[3] * in[10];

    inv[5] = in[0]  * in[10] * in[15] - 
             in[0]  * in[11] * in[14] - 
             in[8]  * in[2] * in[15] + 
             in[8]  * in[3] * in[14] + 
             in[12] * in[2] * in[11] - 
             in[12] * in[3] * in[10];

    inv[9] = -in[0]  * in[9] * in[15] + 
              in[0]  * in[11] * in[13] + 
              in[8]  * in[1] * in[15] - 
              in[8]  * in[3] * in[13] - 
              in[12] * in[1] * in[11] + 
              in[12] * in[3] * in[9];

    inv[13] = in[0]  * in[9] * in[14] - 
              in[0]  * in[10] * in[13] - 
              in[8]  * in[1] * in[14] + 
              in[8]  * in[2] * in[13] + 
              in[12] * in[1] * in[10] - 
              in[12] * in[2] * in[9];

    inv[2] = in[1]  * in[6] * in[15] - 
             in[1]  * in[7] * in[14] - 
             in[5]  * in[2] * in[15] + 
             in[5]  * in[3] * in[14] + 
             in[13] * in[2] * in[7] - 
             in[13] * in[3] * in[6];

    inv[6] = -in[0]  * in[6] * in[15] + 
              in[0]  * in[7] * in[14] + 
              in[4]  * in[2] * in[15] - 
              in[4]  * in[3] * in[14] - 
              in[12] * in[2] * in[7] + 
              in[12] * in[3] * in[6];

    inv[10] = in[0]  * in[5] * in[15] - 
              in[0]  * in[7] * in[13] - 
              in[4]  * in[1] * in[15] + 
              in[4]  * in[3] * in[13] + 
              in[12] * in[1] * in[7] - 
              in[12] * in[3] * in[5];

    inv[14] = -in[0]  * in[5] * in[14] + 
               in[0]  * in[6] * in[13] + 
               in[4]  * in[1] * in[14] - 
               in[4]  * in[2] * in[13] - 
               in[12] * in[1] * in[6] + 
               in[12] * in[2] * in[5];

    inv[3] = -in[1] * in[6] * in[11] + 
              in[1] * in[7] * in[10] + 
              in[5] * in[2] * in[11] - 
              in[5] * in[3] * in[10] - 
              in[9] * in[2] * in[7] + 
              in[9] * in[3] * in[6];

    inv[7] = in[0] * in[6] * in[11] - 
             in[0] * in[7] * in[10] - 
             in[4] * in[2] * in[11] + 
             in[4] * in[3] * in[10] + 
             in[8] * in[2] * in[7] - 
             in[8] * in[3] * in[6];

    inv[11] = -in[0] * in[5] * in[11] + 
               in[0] * in[7] * in[9] + 
               in[4] * in[1] * in[11] - 
               in[4] * in[3] * in[9] - 
               in[8] * in[1] * in[7] + 
               in[8] * in[3] * in[5];

    inv[15] = in[0] * in[5] * in[10] - 
              in[0] * in[6] * in[9] - 
              in[4] * in[1] * in[10] + 
              in[4] * in[2] * in[9] + 
              in[8] * in[1] * in[6] - 
              in[8] * in[2] * in[5];

    det = in[0] * inv[0] + in[1] * inv[4] + in[2] * inv[8] + in[3] * inv[12];
    if (fabsf(det) < 1e-12f)
        return false;

    det = 1.0f / det;
    for (int i = 0; i < 16; i++)
        out[i] = inv[i] * det;

    return true;
}

static void cam_update_matrices(void)
{
	float pitch_rad = s_cam.pitch_deg * (float)M_PI / 180.0f;
	float yaw_rad = s_cam.yaw_deg * (float)M_PI / 180.0f;
	float fov_rad = s_cam.fov_deg * (float)M_PI / 180.0f;

	float cos_p = cosf(pitch_rad);
	float sin_p = sinf(pitch_rad);
	float cos_y = cosf(yaw_rad);
	float sin_y = sinf(yaw_rad);

	/* Camera eye sits South (+Y) and above (+Z) the target point, looking North (-Y) */
	s_cam.eye[0] = s_cam.target[0] + s_cam.distance * cos_p * sin_y;
	s_cam.eye[1] = s_cam.target[1] + s_cam.distance * cos_p * cos_y;
	s_cam.eye[2] = s_cam.target[2] + s_cam.distance * sin_p;

	/* Camera Up vector tilted backwards from vertical: (0, -sin_p, cos_p) rotated by yaw */
	s_cam.up[0] = -sin_p * sin_y;
	s_cam.up[1] = -sin_p * cos_y;
	s_cam.up[2] = cos_p;

	/* View matrix */
	lam_mat4_look_at(s_cam.view, s_cam.eye, s_cam.target, s_cam.up);

	/* Camera Right vector in world coordinates (extracted from view matrix row 0) */
	s_cam.right[0] = s_cam.view[0];
	s_cam.right[1] = s_cam.view[4];
	s_cam.right[2] = s_cam.view[8];

	/* Perspective Projection matrix */
	extern uint32_t lam_width, lam_height;
	float aspect = (lam_height > 0) ? ((float)lam_width / (float)lam_height) : 1.333333f;
	lam_mat4_perspective(s_cam.proj, fov_rad, aspect, s_cam.near_z, s_cam.far_z);

	/* Combined View-Projection */
	lam_mat4_mul(s_cam.vp, s_cam.proj, s_cam.view);

	/* Inverted View-Projection for unprojecting */
	lam_mat4_invert(s_cam.inv_vp, s_cam.vp);
}

void lam_cam_set_3d(float target_x, float target_y, float distance,
                    float pitch_deg, float yaw_deg, float fov_deg)
{
	s_cam.target[0] = target_x;
	s_cam.target[1] = target_y;
	s_cam.target[2] = 0.0f;
	s_cam.distance = (distance > 0.1f) ? distance : 10.0f;
	s_cam.pitch_deg = pitch_deg;
	s_cam.yaw_deg = yaw_deg;
	s_cam.fov_deg = (fov_deg > 5.0f && fov_deg < 170.0f) ? fov_deg : 45.0f;
	s_cam.active = 1;

	cam_update_matrices();

	/* Enable GL depth testing for 3D drawing */
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
}

void lam_cam_set_2d(void)
{
	s_cam.active = 0;
	glDisable(GL_DEPTH_TEST);
}

int lam_cam_is_3d(void)
{
	return s_cam.active;
}

const lam_cam_t *lam_cam_get(void)
{
	return &s_cam;
}

void lam_cam_get_vp(float out[16])
{
	memcpy(out, s_cam.vp, sizeof(float) * 16);
}

void lam_cam_get_right(float r[3])
{
	r[0] = s_cam.right[0];
	r[1] = s_cam.right[1];
	r[2] = s_cam.right[2];
}

int lam_unproject_ground(int32_t screen_x, int32_t screen_y, float *out_x, float *out_y)
{
	extern uint32_t lam_width, lam_height;
	if (!s_cam.active || lam_width == 0 || lam_height == 0)
		return 0;

	float ndc_x = (2.0f * (float)screen_x) / (float)lam_width - 1.0f;
	float ndc_y = 1.0f - (2.0f * (float)screen_y) / (float)lam_height;

	/* Near point in homogeneous clip space (Z = -1) */
	float p_near[4] = { ndc_x, ndc_y, -1.0f, 1.0f };
	float p_far[4]  = { ndc_x, ndc_y,  1.0f, 1.0f };

	float w_near[4], w_far[4];
	for (int i = 0; i < 4; i++) {
		w_near[i] = s_cam.inv_vp[0 * 4 + i] * p_near[0] +
		            s_cam.inv_vp[1 * 4 + i] * p_near[1] +
		            s_cam.inv_vp[2 * 4 + i] * p_near[2] +
		            s_cam.inv_vp[3 * 4 + i] * p_near[3];

		w_far[i]  = s_cam.inv_vp[0 * 4 + i] * p_far[0] +
		            s_cam.inv_vp[1 * 4 + i] * p_far[1] +
		            s_cam.inv_vp[2 * 4 + i] * p_far[2] +
		            s_cam.inv_vp[3 * 4 + i] * p_far[3];
	}

	if (fabsf(w_near[3]) < 1e-9f || fabsf(w_far[3]) < 1e-9f)
		return 0;

	float p0[3] = { w_near[0] / w_near[3], w_near[1] / w_near[3], w_near[2] / w_near[3] };
	float p1[3] = { w_far[0]  / w_far[3],  w_far[1]  / w_far[3],  w_far[2]  / w_far[3]  };

	float ray_dir[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };

	/* Ray-plane intersection with ground plane Z = 0: p0[2] + t * ray_dir[2] = 0 */
	if (fabsf(ray_dir[2]) < 1e-6f)
		return 0; /* Ray parallel to ground */

	float t = -p0[2] / ray_dir[2];
	if (t < 0.0f)
		return 0; /* Plane behind camera */

	if (out_x) *out_x = p0[0] + t * ray_dir[0];
	if (out_y) *out_y = p0[1] + t * ray_dir[1];
	return 1;
}
