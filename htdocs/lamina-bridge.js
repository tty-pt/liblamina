/*
 * lamina-bridge.js — Browser WebAssembly bridge for liblamina 2D graphics.
 * Provides the "lamina" WASM import module.
 *
 * Automatically mounts onto the active [data-bud-canvas].
 * When WebGL is supported by the canvas, frames flushed via lam_canvas_flush
 * are uploaded as a texture and rendered as a hardware-accelerated textured quad.
 * If WebGL is unavailable or unsupported, falls back cleanly to 2D putImageData.
 */

function getExport(exports, names) {
	if (!exports) return null;
	for (const name of names) {
		if (typeof exports[name] === 'function') return exports[name];
	}
	return null;
}

const VS_QUAD = `attribute vec2 a_pos;
attribute vec2 a_uv;
varying vec2 v_uv;
void main() {
	v_uv = a_uv;
	gl_Position = vec4(a_pos, 0.0, 1.0);
}`;

const FS_QUAD = `precision mediump float;
varying vec2 v_uv;
uniform sampler2D u_tex;
void main() {
	gl_FragColor = texture2D(u_tex, v_uv);
}`;

/* --- 3D WebGL Shaders (WebGL2 / GLSL 300 es) --- */

const VS_TERRAIN_3D = `#version 300 es
precision highp float;
precision highp int;
precision highp isampler2D;
precision highp sampler2D;
layout (location = 0) in vec3 a_grid_uv;
uniform mat4 uMVP;
uniform isampler2D uHeightTex;
uniform vec2 uChunkOrigin;
uniform vec2 uChunkSpan;
uniform float uHeightScale;
uniform float uSeaLevel;
uniform vec3 uCameraPos;
uniform vec3 uPlanetCenter;
uniform vec2 uTorusRadii;
uniform vec2 uWorldDim;
out vec2 vUV;
out float vZ;
out float vLighting;
out float vDistance;
out vec2 vWorldPos;
out vec3 vViewDir;
out float vWaterDepth;
out vec3 vNormal;
out vec3 vTorusNormal;
out vec3 vTangentU;
out vec3 vTangentV;

float getH(ivec2 p, ivec2 maxC) {
	return float(texelFetch(uHeightTex, clamp(p, ivec2(0), maxC), 0).r);
}

vec2 getGrad(ivec2 p, ivec2 maxC) {
	float hL = getH(p - ivec2(1, 0), maxC);
	float hR = getH(p + ivec2(1, 0), maxC);
	float hD = getH(p - ivec2(0, 1), maxC);
	float hU = getH(p + ivec2(0, 1), maxC);
	float dx = (p.x <= 0) ? (hR - getH(p, maxC)) : ((p.x >= maxC.x) ? (getH(p, maxC) - hL) : (hR - hL) * 0.5);
	float dy = (p.y <= 0) ? (hU - getH(p, maxC)) : ((p.y >= maxC.y) ? (getH(p, maxC) - hD) : (hU - hD) * 0.5);
	return vec2(dx, dy);
}

// 4-tap manual bilinear reconstruction on integer height texture
// Evaluates continuous height and seamless finite-difference surface gradient
float sampleHeightSmooth(vec2 uv, out vec2 out_grad) {
	vec2 texSize = vec2(textureSize(uHeightTex, 0));
	vec2 maxCoordF = max(vec2(1.0), texSize - 1.0);
	vec2 pixelCoord = uv * maxCoordF;
	ivec2 iCoord = ivec2(floor(pixelCoord));
	vec2 f = fract(pixelCoord);
	vec2 w = f * f * (3.0 - 2.0 * f);

	ivec2 maxCoord = ivec2(maxCoordF);
	ivec2 c00 = clamp(iCoord, ivec2(0), maxCoord);
	ivec2 c10 = clamp(iCoord + ivec2(1, 0), ivec2(0), maxCoord);
	ivec2 c01 = clamp(iCoord + ivec2(0, 1), ivec2(0), maxCoord);
	ivec2 c11 = clamp(iCoord + ivec2(1, 1), ivec2(0), maxCoord);

	float h00 = getH(c00, maxCoord);
	float h10 = getH(c10, maxCoord);
	float h01 = getH(c01, maxCoord);
	float h11 = getH(c11, maxCoord);

	float b0 = mix(h00, h10, w.x);
	float b1 = mix(h01, h11, w.x);
	float height = mix(b0, b1, w.y);

	vec2 g00 = getGrad(c00, maxCoord);
	vec2 g10 = getGrad(c10, maxCoord);
	vec2 g01 = getGrad(c01, maxCoord);
	vec2 g11 = getGrad(c11, maxCoord);

	vec2 gx0 = mix(g00, g10, w.x);
	vec2 gx1 = mix(g01, g11, w.x);
	vec2 smoothGrad = mix(gx0, gx1, w.y);

	out_grad.x = smoothGrad.x * (maxCoordF.x / uChunkSpan.x);
	out_grad.y = smoothGrad.y * (maxCoordF.y / uChunkSpan.y);

	return height;
}

void main() {
	vUV = a_grid_uv.xy;
	float scale = (uHeightScale != 0.0) ? uHeightScale : 1.0;
	// Unpack Q12.4 fixed-point elevation (16 sub-units per pixel = 0.0625)
	float unpackScale = 0.0625;
	float effectiveScale = scale * unpackScale;

	vec2 grad;
	float raw_h = sampleHeightSmooth(a_grid_uv.xy, grad);
	float wz = raw_h * effectiveScale - a_grid_uv.z;
	vZ = wz;

	vec2 worldXY = vec2(
		uChunkOrigin.x + a_grid_uv.x * uChunkSpan.x,
		uChunkOrigin.y + a_grid_uv.y * uChunkSpan.y
	);

	const float TWO_PI = 6.28318530718;
	float R = (uTorusRadii.x > 0.0) ? uTorusRadii.x : 2000000.0;
	float r0 = (uTorusRadii.y > 0.0) ? uTorusRadii.y : 1335000.0;
	vec2 wdim = (uWorldDim.x > 0.0 && uWorldDim.y > 0.0) ? uWorldDim : vec2(10485760.0, 8388608.0);

	float u = TWO_PI * fract(worldXY.x / wdim.x);
	float v = TWO_PI * fract(worldXY.y / wdim.y);

	float cu = cos(u), su = sin(u);
	float cv = cos(v), sv = sin(v);
	float r = r0 + wz;

	vec3 worldPos = uPlanetCenter + vec3(
		(R + r * cv) * cu,
		(R + r * cv) * su,
		r * sv
	);

	// Analytical surface normal & tangent frame
	vec3 torusN = vec3(cv * cu, cv * su, sv);
	vec3 tangentU = vec3(-su, cu, 0.0);
	vec3 tangentV = vec3(-sv * cu, -sv * su, cv);
	vec3 N = normalize(torusN - grad.x * effectiveScale * tangentU - grad.y * effectiveScale * tangentV);

	// Sunlight from Northwest slanted down
	vec3 L = normalize(vec3(-0.577, -0.577, 0.816));
	float diff = max(0.0, dot(N, L));
	vLighting = (a_grid_uv.z > 0.0) ? (0.45 * 0.7) : (0.45 + 0.55 * diff);

	vWorldPos = worldXY;
	vWaterDepth = (uSeaLevel > -900.0) ? max(0.0, uSeaLevel - wz) : 0.0;
	vDistance = length(worldPos - uCameraPos);
	vViewDir = uCameraPos - worldPos;
	vNormal = N;
	vTorusNormal = torusN;
	vTangentU = tangentU;
	vTangentV = tangentV;
	gl_Position = uMVP * vec4(worldPos, 1.0);
}`;

const FS_TERRAIN_3D = `#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;
in vec2 vUV;
in float vZ;
in float vLighting;
in float vDistance;
in vec2 vWorldPos;
in vec3 vViewDir;
in float vWaterDepth;
in vec3 vNormal;
in vec3 vTorusNormal;
in vec3 vTangentU;
in vec3 vTangentV;
uniform sampler2D uSubstrateTex;
uniform float uSeaLevel;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uAnimTime;
uniform vec3 uSkyColor;
uniform float uAtmoIntensity;
out vec4 FragColor;

uint chaff_rotl32(uint x, int r) {
	return (x << uint(r)) | (x >> uint(32 - r));
}

uint chaff_hash3(ivec3 p, uint seed) {
	uint v1 = (uint(p.y) & 0xFFFFu) | ((uint(p.x) & 0xFFFFu) << 16u);
	uint v2 = (uint(p.z) & 0xFFFFu);
	uint h = seed + 0x165667B1u + 6u;
	h += v1 * 0xC2B2AE3Du;
	h = chaff_rotl32(h, 17) * 0x27D4EB2Fu;
	h += v2 * 0x85EBCA77u;
	h = chaff_rotl32(h, 13) * 0xC2B2AE3Du;
	h ^= h >> 15u;
	h *= 0x85EBCA77u;
	h ^= h >> 16u;
	return h;
}

float chaff_sample_3d(vec3 pos, uint shift, uint seed) {
	float cell_size = float(1u << shift);
	vec3 p = pos / cell_size;
	ivec3 i0 = ivec3(floor(p));
	vec3 f = fract(p);
	vec2 u = f.xy * f.xy * (3.0 - 2.0 * f.xy);
	float uz = f.z * f.z * (3.0 - 2.0 * f.z);
	int step_px = int(1u << shift);
	ivec3 cell = i0 * step_px;

	uint v000 = chaff_hash3(cell + ivec3(0, 0, 0), seed) >> 24u;
	uint v100 = chaff_hash3(cell + ivec3(step_px, 0, 0), seed) >> 24u;
	uint v010 = chaff_hash3(cell + ivec3(0, step_px, 0), seed) >> 24u;
	uint v110 = chaff_hash3(cell + ivec3(step_px, step_px, 0), seed) >> 24u;
	uint v001 = chaff_hash3(cell + ivec3(0, 0, step_px), seed) >> 24u;
	uint v101 = chaff_hash3(cell + ivec3(step_px, 0, step_px), seed) >> 24u;
	uint v011 = chaff_hash3(cell + ivec3(0, step_px, step_px), seed) >> 24u;
	uint v111 = chaff_hash3(cell + ivec3(step_px, step_px, step_px), seed) >> 24u;

	float b00 = mix(float(v000), float(v100), u.x);
	float b10 = mix(float(v010), float(v110), u.x);
	float b01 = mix(float(v001), float(v101), u.x);
	float b11 = mix(float(v011), float(v111), u.x);

	float b0 = mix(b00, b10, u.y);
	float b1 = mix(b01, b11, u.y);

	return mix(b0, b1, uz) / 255.0;
}

float chaff_fast_noise2d(vec2 p) {
	vec2 ip = floor(p);
	vec2 fp = fract(p);
	vec2 u = fp * fp * (3.0 - 2.0 * fp);
	ivec2 c = ivec2(ip);
	float n00 = float(chaff_hash3(ivec3(c.x, c.y, 0), 0xA341316Cu) >> 24u);
	float n10 = float(chaff_hash3(ivec3(c.x + 1, c.y, 0), 0xA341316Cu) >> 24u);
	float n01 = float(chaff_hash3(ivec3(c.x, c.y + 1, 0), 0xA341316Cu) >> 24u);
	float n11 = float(chaff_hash3(ivec3(c.x + 1, c.y + 1, 0), 0xA341316Cu) >> 24u);
	return mix(mix(n00, n10, u.x), mix(n01, n11, u.x), u.y) * (1.0 / 255.0);
}

float eval_micro_detail(vec2 pos) {
	float meso  = chaff_fast_noise2d(pos * 0.35);
	float micro = chaff_fast_noise2d(pos * 1.60);
	float sub   = chaff_fast_noise2d(pos * 5.00);
	return (meso - 0.5) * 0.45 + (micro - 0.5) * 0.35 + (sub - 0.5) * 0.20;
}

void main() {
	vec2 sampleUV = vUV;
	if (vWaterDepth > 0.0) {
		// Multi-harmonic wave refraction displacement for submerged seabed
		float depthFactor = clamp(vWaterDepth / 16.0, 0.0, 1.0);
		vec2 pWave1 = vWorldPos * 0.18 + vec2(uAnimTime * 0.8, -uAnimTime * 0.6);
		vec2 pWave2 = vWorldPos * 0.38 + vec2(-uAnimTime * 1.1, uAnimTime * 0.9);
		vec2 refractOffset = vec2(
			sin(pWave1.x + pWave1.y) * 0.6 + cos(pWave2.x) * 0.4,
			cos(pWave1.x - pWave1.y) * 0.6 + sin(pWave2.y) * 0.4
		) * (0.012 * depthFactor);
		sampleUV = clamp(vUV + refractOffset, 0.0, 1.0);
	}

	vec4 baseCol = texture(uSubstrateTex, sampleUV);
	float effLighting = vLighting;

	if (vWaterDepth <= 0.0) {
		// Calculate terrain slope relative to torus zenith normal
		float slope = 1.0 - max(0.0, dot(normalize(vNormal), normalize(vTorusNormal)));
		float elevAboveWater = (uSeaLevel > -900.0) ? max(0.0, vZ - uSeaLevel) : max(0.0, vZ);

		// 1. Bedrock & Scree (Angle-of-repose transition: steep slopes > 20 deg shed loose soil and expose rock)
		float rockFactor = smoothstep(0.18, 0.40, slope);
		if (rockFactor > 0.0) {
			float strata = sin(vZ * 1.8) * 0.14 + cos(vZ * 0.7 + vWorldPos.x * 0.03) * 0.08;
			vec3 rockCol = vec3(0.38, 0.36, 0.34) * (0.85 + strata);
			baseCol.rgb = mix(baseCol.rgb, rockCol, rockFactor);
		}

		// Procedural micro-texture on dry terrain (faded out smoothly with distance to prevent distant aliasing)
		float distFade = clamp(1.0 - (vDistance - 600.0) / 1800.0, 0.0, 1.0);
		if (distFade > 0.0) {
			float h0 = eval_micro_detail(vWorldPos);
			const float delta = 0.25;
			float hX = eval_micro_detail(vWorldPos + vec2(delta, 0.0));
			float hY = eval_micro_detail(vWorldPos + vec2(0.0, delta));
			vec2 microGrad = vec2(hX - h0, hY - h0) * (distFade / delta);

			// Analytical fragment normal perturbation
			vec3 N_frag = normalize(vNormal - microGrad.x * 0.40 * vTangentU - microGrad.y * 0.40 * vTangentV);
			vec3 L_frag = normalize(vec3(-0.577, -0.577, 0.816));
			vec3 viewDir = normalize(vViewDir);
			vec3 H_half = normalize(L_frag + viewDir);
			float perFragDiff = max(0.0, dot(N_frag, L_frag));

			// Ambient occlusion in micro-valleys and crevice hollows
			float ao = clamp(1.0 - slope * 0.30 + h0 * 0.25, 0.60, 1.0);
			effLighting = (vLighting > 0.32) ? (0.40 * ao + 0.60 * perFragDiff * ao) : (vLighting * ao);

			// 2. Soil Micro-Material (clods, earthy loam, and moisture darkening near water tables)
			float soilFactor = smoothstep(0.08, 0.24, slope) * (1.0 - rockFactor);
			if (soilFactor > 0.0) {
				float clod = chaff_fast_noise2d(vWorldPos * 2.2);
				float soilMoisture = clamp(1.0 - elevAboveWater / 12.0, 0.0, 1.0);
				vec3 soilCol = vec3(0.40, 0.30, 0.20) * (0.80 + 0.40 * clod) * (1.0 - soilMoisture * 0.35);
				baseCol.rgb = mix(baseCol.rgb, soilCol, soilFactor * distFade);
			}

			// 3. Sand Micro-Material (beach ripple contours, waterline moisture darkening, and crystalline specular glints)
			bool isSandBiome = (baseCol.r > 0.55 && baseCol.g > 0.45 && baseCol.b < 0.45);
			float shoreFactor = smoothstep(6.0, 0.5, elevAboveWater) * (1.0 - rockFactor);
			if (isSandBiome || shoreFactor > 0.0) {
				float sandRipple = sin(vWorldPos.x * 2.2 + sin(vWorldPos.y * 1.1) * 1.5) * 0.5 + 0.5;
				float sandGrain = chaff_fast_noise2d(vWorldPos * 5.0);
				float wetSand = smoothstep(2.5, 0.0, elevAboveWater);
				vec3 drySandCol = vec3(0.74, 0.66, 0.48) * (0.88 + 0.24 * sandRipple);
				vec3 wetSandCol = vec3(0.52, 0.44, 0.32) * (0.90 + 0.20 * sandGrain);
				vec3 sandCol = mix(drySandCol, wetSandCol, wetSand);
				float sandBlend = isSandBiome ? 1.0 : shoreFactor;
				baseCol.rgb = mix(baseCol.rgb, sandCol, sandBlend * distFade);

				// Crystalline specular highlights on wet/damp sand
				float sandSpecAngle = max(0.0, dot(N_frag, H_half));
				float sandSpec = pow(sandSpecAngle, 32.0) * (0.35 * wetSand + 0.08 * (1.0 - wetSand)) * sandBlend * distFade;
				baseCol.rgb += vec3(sandSpec * 0.9, sandSpec * 0.95, sandSpec * 1.0);
			}

			// 4. Grass / Foliage Micro-Material (multi-frequency emerald vs golden dry thatch variation)
			if (rockFactor < 0.65 && !isSandBiome && shoreFactor < 0.7 && baseCol.g > baseCol.r * 1.02) {
				float thatch = chaff_fast_noise2d(vWorldPos * 0.18);
				vec3 lush = baseCol.rgb * vec3(0.92, 1.08, 0.90);
				vec3 dryT = baseCol.rgb * vec3(1.10, 1.02, 0.75);
				baseCol.rgb = mix(lush, dryT, smoothstep(0.38, 0.72, thatch) * distFade);
			}

			// Micro-shading on diffuse color
			float microShade = clamp(1.0 + h0 * 0.35 * distFade, 0.72, 1.28);
			baseCol.rgb *= microShade;

			// Specular sun glint on polished rock surfaces
			if (rockFactor > 0.3) {
				float specAngle = max(0.0, dot(N_frag, H_half));
				float spec = pow(specAngle, 20.0) * (rockFactor * 0.22 * distFade);
				baseCol.rgb += vec3(spec);
			}
		}
	}

	vec3 lit = baseCol.rgb * effLighting;

	if (vWaterDepth > 0.0) {
		float d = clamp(vWaterDepth, 0.0, 32.0);
		float distFade = clamp(1.0 - (vDistance - 600.0) / 1800.0, 0.0, 1.0);

		// 1. Realistic Caustics Cast on the Sea Floor According to Sea Surface Distance (faded with distance)
		float caustic = 0.0;
		if (distFade > 0.01) {
			float c1 = chaff_sample_3d(vec3(vWorldPos * 2.0, uAnimTime * 3.5), 3u, 0x1337u);
			float c2 = chaff_sample_3d(vec3((vWorldPos + vec2(37.0, -23.0)) * 2.0, uAnimTime * 2.5), 3u, 0xBEEFu);
			float diff = abs(c1 - c2);
			caustic = pow(clamp(1.0 - diff * 6.0, 0.0, 1.0), 3.0) * distFade;
		}

		// Beam diffusion: direct collimated sunlight diffuses into ambient glow as depth increases,
		// softly permeating the continental shelf and extinguishing before the abyssal basin (d >= 18)
		float causticCoherence = exp(-0.16 * d);
		vec3 seabedCaustic = vec3(0.40, 0.75, 0.95) * (caustic * 0.55 * causticCoherence);

		// 2. Volumetric Visual Extinction Across the Water Column
		vec3 viewDir = normalize(vViewDir);
		float cosView = max(0.25, dot(vec3(0.0, 0.0, 1.0), viewDir));
		float opticalPath = d / cosView;

		// Spectral Beer-Lambert absorption: calibrated across full bathymetry (d = 1 .. 28)
		vec3 seabedAbsorption = exp(-vec3(0.065, 0.020, 0.005) * opticalPath);
		vec3 seabedColor = (lit + seabedCaustic) * seabedAbsorption;

		// Volumetric deep ocean in-scattering accumulated from the water volume above
		vec3 deepOceanScattered = vec3(0.012, 0.08, 0.30) * (1.0 - exp(-vec3(0.018, 0.030, 0.055) * opticalPath));

		lit = seabedColor + deepOceanScattered;
	}

	float fogFactor = clamp((vDistance - uFogStart) / (uFogEnd - uFogStart), 0.0, 1.0);
	vec3 sky = (length(uSkyColor) > 0.001) ? uSkyColor : vec3(0.55, 0.70, 0.85);

	// Atmospheric Rayleigh limb glow on the planet horizon
	vec3 viewDirNorm = normalize(vViewDir);
	float limb = pow(clamp(1.0 - max(0.0, dot(vec3(0.0, 0.0, 1.0), viewDirNorm)), 0.0, 1.0), 3.0);
	vec3 surfaceLit = mix(lit, vec3(0.35, 0.65, 0.95), limb * 0.45 * uAtmoIntensity);

	FragColor = vec4(mix(surfaceLit, sky, fogFactor * uAtmoIntensity), 1.0);
}`;

const VS_BILLBOARD_3D = `#version 300 es
precision highp float;
layout (location = 0) in vec3 a_pos;
layout (location = 1) in vec2 a_uv;
layout (location = 2) in vec4 a_tint;
uniform mat4 uMVP;
uniform vec3 uPlanetCenter;
uniform vec2 uTorusRadii;
uniform vec2 uWorldDim;
out vec2 vUV;
out vec4 vTint;
out vec3 vWorldPos;
out vec3 vLocalPos;
void main() {
	vUV = a_uv;
	vTint = a_tint;
	vLocalPos = a_pos;
	const float TWO_PI = 6.28318530718;
	float R = (uTorusRadii.x > 0.0) ? uTorusRadii.x : 2000000.0;
	float r0 = (uTorusRadii.y > 0.0) ? uTorusRadii.y : 1335000.0;
	vec2 wdim = (uWorldDim.x > 0.0 && uWorldDim.y > 0.0) ? uWorldDim : vec2(10485760.0, 8388608.0);

	float u = TWO_PI * fract(a_pos.x / wdim.x);
	float v = TWO_PI * fract(a_pos.y / wdim.y);
	float cu = cos(u), su = sin(u);
	float cv = cos(v), sv = sin(v);
	float r = r0 + a_pos.z;

	vec3 worldPos = uPlanetCenter + vec3(
		(R + r * cv) * cu,
		(R + r * cv) * su,
		r * sv
	);
	vWorldPos = worldPos;
	gl_Position = uMVP * vec4(worldPos, 1.0);
}`;

const FS_BILLBOARD_3D = `#version 300 es
precision highp float;
precision highp sampler2D;
in vec2 vUV;
in vec4 vTint;
in vec3 vWorldPos;
in vec3 vLocalPos;
uniform sampler2D uAtlasTex;
uniform float uSeaLevel;
uniform float uAnimTime;
uniform vec3 uCameraPos;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec3 uSkyColor;
uniform float uAtmoIntensity;
out vec4 FragColor;

uint chaff_rotl32(uint x, int r) {
	return (x << uint(r)) | (x >> uint(32 - r));
}

uint chaff_hash3(ivec3 p, uint seed) {
	uint v1 = (uint(p.y) & 0xFFFFu) | ((uint(p.x) & 0xFFFFu) << 16u);
	uint v2 = (uint(p.z) & 0xFFFFu);
	uint h = seed + 0x165667B1u + 6u;
	h += v1 * 0xC2B2AE3Du;
	h = chaff_rotl32(h, 17) * 0x27D4EB2Fu;
	h += v2 * 0x85EBCA77u;
	h = chaff_rotl32(h, 13) * 0xC2B2AE3Du;
	h ^= h >> 15u;
	h *= 0x85EBCA77u;
	h ^= h >> 16u;
	return h;
}

float chaff_sample_3d(vec3 pos, uint shift, uint seed) {
	float cell_size = float(1u << shift);
	vec3 p = pos / cell_size;
	ivec3 i0 = ivec3(floor(p));
	vec3 f = fract(p);
	vec2 u = f.xy * f.xy * (3.0 - 2.0 * f.xy);
	float uz = f.z * f.z * (3.0 - 2.0 * f.z);
	int step_px = int(1u << shift);
	ivec3 cell = i0 * step_px;

	uint v000 = chaff_hash3(cell + ivec3(0, 0, 0), seed) >> 24u;
	uint v100 = chaff_hash3(cell + ivec3(step_px, 0, 0), seed) >> 24u;
	uint v010 = chaff_hash3(cell + ivec3(0, step_px, 0), seed) >> 24u;
	uint v110 = chaff_hash3(cell + ivec3(step_px, step_px, 0), seed) >> 24u;
	uint v001 = chaff_hash3(cell + ivec3(0, 0, step_px), seed) >> 24u;
	uint v101 = chaff_hash3(cell + ivec3(step_px, 0, step_px), seed) >> 24u;
	uint v011 = chaff_hash3(cell + ivec3(0, step_px, step_px), seed) >> 24u;
	uint v111 = chaff_hash3(cell + ivec3(step_px, step_px, step_px), seed) >> 24u;

	float b00 = mix(float(v000), float(v100), u.x);
	float b10 = mix(float(v010), float(v110), u.x);
	float b01 = mix(float(v001), float(v101), u.x);
	float b11 = mix(float(v011), float(v111), u.x);

	float b0 = mix(b00, b10, u.y);
	float b1 = mix(b01, b11, u.y);

	return mix(b0, b1, uz) / 255.0;
}

void main() {
	// 1. Waterline & Submersion Depth (incorporates surface wave chop)
	float waveElevation = 0.0;
	float d = 0.0;
	bool isSubmerged = false;

	if (uSeaLevel > -900.0) {
		waveElevation = sin(vLocalPos.x * 0.4 + uAnimTime * 3.0) * 0.4 +
		                cos(vLocalPos.y * 0.4 + uAnimTime * 2.2) * 0.3;
		float effSeaLevel = uSeaLevel + waveElevation;
		if (vLocalPos.z < effSeaLevel) {
			d = effSeaLevel - vLocalPos.z;
			isSubmerged = true;
		}
	}

	// 2. Subsurface Wave Refraction for Submerged Billboard
	vec2 sampleUV = vUV;
	if (isSubmerged) {
		float distortStrength = clamp(d * 0.25, 0.0, 1.0);
		vec2 waveOffset = vec2(
			sin(vLocalPos.y * 0.35 + uAnimTime * 3.5),
			cos(vLocalPos.x * 0.35 + uAnimTime * 2.8)
		) * (vec2(1.2 / 512.0, 1.2 / 328.0) * distortStrength);
		sampleUV = vUV + waveOffset;
	}

	vec4 baseCol = texture(uAtlasTex, vUV);
	if (baseCol.a < 0.1) discard;

	vec4 col = isSubmerged ? texture(uAtlasTex, sampleUV) * vTint : baseCol * vTint;
	if (col.a < 0.1) col = baseCol * vTint;

	vec3 lit = col.rgb;

	if (isSubmerged) {
		// Realistic Caustics on Submerged Billboard Surfaces (peaks at shallow depths)
		float c1 = chaff_sample_3d(vec3(vLocalPos.xy * 2.0, uAnimTime * 3.5), 3u, 0x1337u);
		float c2 = chaff_sample_3d(vec3((vLocalPos.xy + vec2(37.0, -23.0)) * 2.0, uAnimTime * 2.5), 3u, 0xBEEFu);
		float diff = abs(c1 - c2);
		float caustic = pow(clamp(1.0 - diff * 6.0, 0.0, 1.0), 3.0);
		float causticCoherence = exp(-0.12 * d);
		vec3 submergedCaustic = vec3(0.55, 0.90, 1.0) * (caustic * 0.60 * causticCoherence);

		// Dual-Regime Non-Linear Shallow Optical Path
		vec3 viewDir = normalize(uCameraPos - vWorldPos);
		float cosView = max(0.25, dot(vec3(0.0, 0.0, 1.0), viewDir));
		float shallowFactor = smoothstep(0.0, 16.0, d);
		float effectiveOpticalPath = (d * (0.25 + 0.75 * shallowFactor)) / cosView;

		// Immediate Caribbean Turquoise Baseline Wash (matches C_SHALLOW RGB(64, 218, 235))
		float submergedBlend = clamp(0.22 + 0.14 * smoothstep(0.0, 8.0, d), 0.0, 0.50);
		vec3 shallowWash = vec3(0.25, 0.85, 0.92);
		vec3 tintedSurface = mix(lit + submergedCaustic, shallowWash * (0.75 + 0.25 * lit), submergedBlend);

		// Spectral absorption: gentle in shallows, deep extinction at depth
		vec3 waterAbsorption = exp(-vec3(0.020, 0.008, 0.002) * effectiveOpticalPath);
		vec3 waterColor = tintedSurface * waterAbsorption;

		// Volumetric deep ocean in-scattering (suppressed in top 6 units)
		vec3 deepOceanScattered = vec3(0.005, 0.035, 0.12) * shallowFactor *
		                          (1.0 - exp(-vec3(0.010, 0.018, 0.030) * effectiveOpticalPath));
		lit = waterColor + deepOceanScattered;

		// Crisp Undulating Waterline Meniscus Highlight (1.6 units width)
		float meniscus = smoothstep(1.6, 0.0, d);
		vec3 meniscusColor = vec3(0.95, 0.98, 1.0);
		lit = mix(lit, meniscusColor, meniscus * 0.70);
	}

	float dist = length(vWorldPos - uCameraPos);
	float fogFactor = clamp((dist - uFogStart) / (uFogEnd - uFogStart), 0.0, 1.0);
	vec3 sky = (length(uSkyColor) > 0.001) ? uSkyColor : vec3(0.55, 0.70, 0.85);
	FragColor = vec4(mix(lit, sky, fogFactor * uAtmoIntensity), col.a);
}`;

const VS_WATER_3D = `#version 300 es
precision highp float;
precision highp int;
precision highp isampler2D;
precision highp sampler2D;
layout (location = 0) in vec2 a_grid_uv;
uniform mat4 uMVP;
uniform isampler2D uHeightTex;
uniform vec2 uChunkOrigin;
uniform vec2 uChunkSpan;
uniform float uHeightScale;
uniform float uSeaLevel;
uniform vec3 uCameraPos;
uniform vec3 uPlanetCenter;
uniform vec2 uTorusRadii;
uniform vec2 uWorldDim;
out vec2 vUV;
out float vWaterDepth;
out float vDistance;
out vec2 vWorldPos;
out vec3 vViewDir;
out vec3 vTorusNormal;
void main() {
	vUV = a_grid_uv;
	int raw_h = texture(uHeightTex, a_grid_uv).r;
	float scale = (uHeightScale != 0.0) ? uHeightScale : 1.0;
	float h = float(raw_h) * scale * 0.0625;
	vWaterDepth = uSeaLevel - h;

	vec2 worldXY = vec2(
		uChunkOrigin.x + a_grid_uv.x * uChunkSpan.x,
		uChunkOrigin.y + a_grid_uv.y * uChunkSpan.y
	);

	const float TWO_PI = 6.28318530718;
	float R = (uTorusRadii.x > 0.0) ? uTorusRadii.x : 2000000.0;
	float r0 = (uTorusRadii.y > 0.0) ? uTorusRadii.y : 1335000.0;
	vec2 wdim = (uWorldDim.x > 0.0 && uWorldDim.y > 0.0) ? uWorldDim : vec2(10485760.0, 8388608.0);

	float u = TWO_PI * fract(worldXY.x / wdim.x);
	float v = TWO_PI * fract(worldXY.y / wdim.y);

	float cu = cos(u), su = sin(u);
	float cv = cos(v), sv = sin(v);
	float r = r0 + uSeaLevel;

	vec3 worldPos = uPlanetCenter + vec3(
		(R + r * cv) * cu,
		(R + r * cv) * su,
		r * sv
	);

	vTorusNormal = vec3(cv * cu, cv * su, sv);
	vWorldPos = worldXY;
	vDistance = length(worldPos - uCameraPos);
	vViewDir = uCameraPos - worldPos;
	gl_Position = uMVP * vec4(worldPos, 1.0);
}`;

const FS_WATER_3D = `#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;
in vec2 vUV;
in float vWaterDepth;
in float vDistance;
in vec2 vWorldPos;
in vec3 vViewDir;
in vec3 vTorusNormal;
uniform sampler2D uWaterTex;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uAnimTime;
uniform vec3 uSkyColor;
uniform float uAtmoIntensity;
out vec4 FragColor;

uint chaff_rotl32(uint x, int r) {
	return (x << uint(r)) | (x >> uint(32 - r));
}

uint chaff_hash2(ivec2 p, uint seed) {
	uint val = (uint(p.y) & 0xFFFFu) | ((uint(p.x) & 0xFFFFu) << 16u);
	uint h = seed + 0x165667B1u + 4u;
	h += val * 0xC2B2AE3Du;
	h = chaff_rotl32(h, 17) * 0x27D4EB2Fu;
	h ^= h >> 15u;
	h *= 0x85EBCA77u;
	h ^= h >> 13u;
	h *= 0xC2B2AE3Du;
	h ^= h >> 16u;
	return h;
}

uint chaff_hash3(ivec3 p, uint seed) {
	uint v1 = (uint(p.y) & 0xFFFFu) | ((uint(p.x) & 0xFFFFu) << 16u);
	uint v2 = (uint(p.z) & 0xFFFFu);
	uint h = seed + 0x165667B1u + 6u;
	h += v1 * 0xC2B2AE3Du;
	h = chaff_rotl32(h, 17) * 0x27D4EB2Fu;
	h += v2 * 0x85EBCA77u;
	h = chaff_rotl32(h, 13) * 0xC2B2AE3Du;
	h ^= h >> 15u;
	h *= 0x85EBCA77u;
	h ^= h >> 16u;
	return h;
}

float chaff_sample_octave(vec2 world_pos, uint shift, uint vshift, uint seed) {
	float cell_size = float(1u << shift);
	vec2 p = world_pos / cell_size;
	ivec2 i0 = ivec2(floor(p));
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	int step_px = int(1u << shift);
	ivec2 cell = i0 * step_px;
	uint v00 = chaff_hash2(cell, seed) >> vshift;
	uint v10 = chaff_hash2(cell + ivec2(step_px, 0), seed) >> vshift;
	uint v01 = chaff_hash2(cell + ivec2(0, step_px), seed) >> vshift;
	uint v11 = chaff_hash2(cell + ivec2(step_px, step_px), seed) >> vshift;
	float b0 = mix(float(v00), float(v10), u.x);
	float b1 = mix(float(v01), float(v11), u.x);
	return mix(b0, b1, u.y);
}

vec3 chaff_sample_grad_2d(vec2 world_pos, uint shift, uint vshift, uint seed) {
	float cell_size = float(1u << shift);
	vec2 p = world_pos / cell_size;
	ivec2 i0 = ivec2(floor(p));
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	vec2 du = 6.0 * f * (1.0 - f);
	int step_px = int(1u << shift);
	ivec2 cell = i0 * step_px;
	float v00 = float(chaff_hash2(cell, seed) >> vshift);
	float v10 = float(chaff_hash2(cell + ivec2(step_px, 0), seed) >> vshift);
	float v01 = float(chaff_hash2(cell + ivec2(0, step_px), seed) >> vshift);
	float v11 = float(chaff_hash2(cell + ivec2(step_px, step_px), seed) >> vshift);
	float b0 = mix(v00, v10, u.x);
	float b1 = mix(v01, v11, u.x);
	float val = mix(b0, b1, u.y) / 255.0;
	float dH_dx = mix(v10 - v00, v11 - v01, u.y);
	float dH_dy = b1 - b0;
	vec2 grad = (vec2(dH_dx, dH_dy) * du) / (255.0 * cell_size);
	return vec3(val, grad.x, grad.y);
}

float chaff_sample_3d(vec3 pos, uint shift, uint seed) {
	float cell_size = float(1u << shift);
	vec3 p = pos / cell_size;
	ivec3 i0 = ivec3(floor(p));
	vec3 f = fract(p);
	vec3 u = f * f * (3.0 - 2.0 * f);
	int step_px = int(1u << shift);
	ivec3 cell = i0 * step_px;

	uint v000 = chaff_hash3(cell + ivec3(0, 0, 0), seed) >> 24u;
	uint v100 = chaff_hash3(cell + ivec3(step_px, 0, 0), seed) >> 24u;
	uint v010 = chaff_hash3(cell + ivec3(0, step_px, 0), seed) >> 24u;
	uint v110 = chaff_hash3(cell + ivec3(step_px, step_px, 0), seed) >> 24u;
	uint v001 = chaff_hash3(cell + ivec3(0, 0, step_px), seed) >> 24u;
	uint v101 = chaff_hash3(cell + ivec3(step_px, 0, step_px), seed) >> 24u;
	uint v011 = chaff_hash3(cell + ivec3(0, step_px, step_px), seed) >> 24u;
	uint v111 = chaff_hash3(cell + ivec3(step_px, step_px, step_px), seed) >> 24u;

	float b00 = mix(float(v000), float(v100), u.x);
	float b10 = mix(float(v010), float(v110), u.x);
	float b01 = mix(float(v001), float(v101), u.x);
	float b11 = mix(float(v011), float(v111), u.x);

	float b0 = mix(b00, b10, u.y);
	float b1 = mix(b01, b11, u.y);

	return mix(b0, b1, u.z) / 255.0;
}

vec4 chaff_sample_grad_3d(vec3 pos, uint shift, uint seed) {
	float cell_size = float(1u << shift);
	vec3 p = pos / cell_size;
	ivec3 i0 = ivec3(floor(p));
	vec3 f = fract(p);
	vec3 u = f * f * (3.0 - 2.0 * f);
	vec3 du = 6.0 * f * (1.0 - f);
	int step_px = int(1u << shift);
	ivec3 cell = i0 * step_px;

	float v000 = float(chaff_hash3(cell + ivec3(0, 0, 0), seed) >> 24u);
	float v100 = float(chaff_hash3(cell + ivec3(step_px, 0, 0), seed) >> 24u);
	float v010 = float(chaff_hash3(cell + ivec3(0, step_px, 0), seed) >> 24u);
	float v110 = float(chaff_hash3(cell + ivec3(step_px, step_px, 0), seed) >> 24u);
	float v001 = float(chaff_hash3(cell + ivec3(0, 0, step_px), seed) >> 24u);
	float v101 = float(chaff_hash3(cell + ivec3(step_px, 0, step_px), seed) >> 24u);
	float v011 = float(chaff_hash3(cell + ivec3(0, step_px, step_px), seed) >> 24u);
	float v111 = float(chaff_hash3(cell + ivec3(step_px, step_px, step_px), seed) >> 24u);

	float b00 = mix(v000, v100, u.x);
	float b10 = mix(v010, v110, u.x);
	float b01 = mix(v001, v101, u.x);
	float b11 = mix(v011, v111, u.x);

	float b0 = mix(b00, b10, u.y);
	float b1 = mix(b01, b11, u.y);

	float val = mix(b0, b1, u.z) / 255.0;

	float dx00 = v100 - v000;
	float dx10 = v110 - v010;
	float dx01 = v101 - v001;
	float dx11 = v111 - v011;
	float dx0 = mix(dx00, dx10, u.y);
	float dx1 = mix(dx01, dx11, u.y);
	float dN_dx = mix(dx0, dx1, u.z);

	float dy0 = b10 - b00;
	float dy1 = b11 - b01;
	float dN_dy = mix(dy0, dy1, u.z);

	float dN_dz = b1 - b0;

	vec3 grad = (vec3(dN_dx, dN_dy, dN_dz) * du) / (255.0 * cell_size);
	return vec4(val, grad.x, grad.y, grad.z);
}

vec2 chaff_rotate_golden(vec2 p) {
	return vec2(0.8506508 * p.x - 0.5257311 * p.y, 0.5257311 * p.x + 0.8506508 * p.y);
}

void main() {
	if (vWaterDepth <= 0.0) {
		discard;
	}

	vec3 viewDir = normalize(vViewDir);
	vec3 sunDir = normalize(vec3(-0.577, -0.577, 0.816));

	// 1. Multi-Scale Wave Hierarchy: Macro Swells, Mid Wind Chop, Capillary Ripples & Micro-Facets
	vec2 wind = vec2(0.7071, -0.7071);
	vec2 pMacro = vWorldPos * 0.035 - wind * (uAnimTime * 0.8);
	vec2 pMid   = chaff_rotate_golden(vWorldPos * 0.12) + vec2(0.5, 0.866) * (uAnimTime * 1.6);
	vec2 pCap   = vWorldPos * 0.45 - wind * (uAnimTime * 3.5);

	// Lower-frequency broad oceanic swell (shift 6 = 64 units) with analytical gradient
	vec4 sMacro = chaff_sample_grad_3d(vec3(pMacro * 8.0, uAnimTime * 1.2), 6u, 0x5357u);
	float wMacro = sMacro.x;

	// Mid-frequency travelling wind chop (shift 4 = 16 units) with Golden-Ratio rotational decorrelation
	vec4 sMid = chaff_sample_grad_3d(vec3(pMid * 8.0, uAnimTime * 2.2), 4u, 0xCAFEu);
	float wMid = sMid.x;

	// High-frequency capillary ripples for fine surface liquid tension (shift 2 = 4 units) with analytical gradient
	vec3 sCap = chaff_sample_grad_2d(pCap, 2u, 24u, 0xC491u);
	float wCap = sCap.x;

	// Ultra-high frequency micro-facet ripples for dancing specular glints (pixel scale) with analytical gradient
	vec2 pRipple = vWorldPos * 1.10 - wind * (uAnimTime * 4.8);
	vec3 sRip = chaff_sample_grad_2d(pRipple, 1u, 24u, 0x9E37u);
	float wRip = sRip.x;

	// Distance Nyquist band-limiting weights across wave octaves:
	// - Micro-facets (1u) fade out over 250..700 world units
	// - Capillary ripples (4u) fade out over 400..1200 world units
	// - Mid-frequency wind chop (16u) fades out over 1200..4500 world units
	// - Macro oceanic swells (64u) remain active across regional and orbital distances
	float nyqRip   = clamp(1.0 - (vDistance - 250.0) / 450.0, 0.0, 1.0);
	float nyqCap   = clamp(1.0 - (vDistance - 400.0) / 800.0, 0.0, 1.0);
	float nyqMid   = clamp(1.0 - (vDistance - 1200.0) / 3300.0, 0.0, 1.0);
	float nyqMacro = clamp(1.0 - (vDistance - 8000.0) / 32000.0, 0.25, 1.0);

	// High-fidelity wave surface normal combining Nyquist-filtered macro, mid, capillary, and micro-facet slopes
	vec2 gradMacro = sMacro.yz * (4.32 * nyqMacro);
	vec2 gradMid   = sMid.yz * (4.48 * nyqMid);
	vec2 gradCap   = sCap.yz * (0.42 * nyqCap);
	vec2 gradRip   = sRip.yz * (0.36 * nyqRip);
	vec2 waveGrad  = gradMacro + gradMid + gradCap + gradRip;
	vec3 localWaveNormal = vec3(-waveGrad.x, -waveGrad.y, 1.0);

	// Torus surface normal blending
	vec3 waveNormal = normalize(localWaveNormal + vTorusNormal * 0.7);

	// 2. Directional Sun Lighting on Wave Facets & Distance-Adaptive Specular Sun Glints
	float diffuseSun = clamp(dot(waveNormal, sunDir), 0.0, 1.0);
	vec3 halfVec = normalize(sunDir + viewDir);
	float specExp = mix(24.0, 192.0, nyqCap);
	float specStrength = mix(0.45, 1.0, nyqMid);
	float specSun = pow(clamp(dot(waveNormal, halfVec), 0.0, 1.0), specExp) * specStrength;

	// 3. Volumetric Optical Depth ("Ocular Esgotation"): true slant path length through fluid
	vec3 nUp = normalize(mix(vec3(0.0, 0.0, 1.0), vTorusNormal, clamp(vDistance / 8000.0, 0.0, 1.0)));
	float slantPath = vWaterDepth / max(0.25, dot(viewDir, nUp));

	// Procedural Animated Caustics on Illuminated Shallow Seabed (d < 4.0m)
	float c1 = chaff_sample_octave(vWorldPos * 0.5 + vec2(uAnimTime * 0.2), 3u, 24u, 0xCA11u);
	float c2 = chaff_sample_octave(vWorldPos * 0.5 - vec2(uAnimTime * 0.15), 3u, 24u, 0xCA22u);
	float caustic = pow(min(c1, c2) / 255.0, 2.0) * 2.5 * smoothstep(4.0, 0.5, vWaterDepth);

	// Seabed substrate base color with animated caustic network
	vec3 cSeabed = vec3(0.22, 0.30, 0.26) * (1.0 + caustic);

	// Beer-Lambert Spectral Absorption: C_transmitted = C_seabed * exp(-mu * L)
	vec3 mu = vec3(0.22, 0.07, 0.015);
	vec3 cTransmitted = cSeabed * exp(-mu * slantPath);

	// Volumetric In-Scattering (Deep Ocean Sapphire): C_inscatter = C_ocean * (1.0 - exp(-sigma * L))
	vec3 cOcean = vec3(0.012, 0.08, 0.30);
	float sigma = 0.08;
	vec3 cInscatter = cOcean * (1.0 - exp(-sigma * slantPath));

	vec3 fluidBody = cTransmitted + cInscatter;

	// 4. Schlick-Fresnel Sky Reflection & Specular Sun Glints: F(theta) = 0.04 + 0.96 * (1.0 - v . n)^5
	float fresnel = 0.04 + 0.96 * pow(clamp(1.0 - dot(waveNormal, viewDir), 0.0, 1.0), 5.0);
	vec3 sky = (length(uSkyColor) > 0.001) ? uSkyColor : vec3(0.55, 0.72, 0.88);
	vec3 sunColor = vec3(1.0, 0.98, 0.92);

	vec3 surfaceRgb = mix(fluidBody, sky, fresnel) + sunColor * specSun;
	surfaceRgb += vec3(0.04, 0.12, 0.18) * (diffuseSun * 0.20 * fresnel);

	// 5. Shoreline Wave Foam (surges and recedes at shore vWaterDepth <= 2.5)
	float shoreNoise = chaff_sample_octave(vWorldPos + vec2(0.7071, -0.7071) * (uAnimTime * 3.5), 2u, 24u, 0xF0A1u) / 255.0;
	float foam = smoothstep(2.5, 0.3, vWaterDepth) * step(0.52, shoreNoise + (wMid - 0.5) * 0.3);
	vec3 foamColor = vec3(0.94, 0.97, 1.0);
	vec3 fluidRgb = mix(surfaceRgb, foamColor, foam * 0.85);

	// Atmospheric Rayleigh limb scattering on the ocean horizon
	float limb = pow(clamp(1.0 - max(0.0, dot(waveNormal, viewDir)), 0.0, 1.0), 3.0);
	fluidRgb = mix(fluidRgb, vec3(0.35, 0.70, 1.0), limb * 0.55 * uAtmoIntensity);

	// Surface alpha is driven by fluid body optical extinction + Fresnel + specular glints + shore foam
	float fluidAlpha = 1.0 - exp(-0.10 * slantPath);
	float alpha = clamp(fresnel + fluidAlpha * 0.82 + specSun * 0.92 + foam * 0.85 + limb * 0.4, fresnel, 0.98);

	// 6. Atmospheric horizon distance fog
	float fogFactor = clamp((vDistance - uFogStart) / (uFogEnd - uFogStart), 0.0, 1.0);

	FragColor = vec4(mix(fluidRgb, sky, fogFactor * uAtmoIntensity), alpha);
}`;

/* --- Fast Matrix Mathematics for 3D Projection --- */

const LAM_DIORAMA_SCALE = 2.0;
const LAM_DIORAMA_CAM_DIST = 280.0;
const LAM_DIORAMA_PITCH_DEG = 55.0;
const LAM_CAM_ORTHO_NEAR = -30000000.0;
const LAM_CAM_ORTHO_FAR = 30000000.0;

/* Planetary curvature constants: 2^16 room planet (65,536 rooms * 160 units/room) */
const LAM_PLANET_ROOM_SPAN = 65536.0;
const LAM_ROOM_WIDTH_UNITS = 160.0;
const LAM_PLANET_CIRCUMFERENCE = LAM_PLANET_ROOM_SPAN * LAM_ROOM_WIDTH_UNITS;
const LAM_PLANET_RADIUS = LAM_PLANET_CIRCUMFERENCE / (2.0 * Math.PI); /* ~1,668,858 world units */

function mat4Create() {
	const out = new Float32Array(16);
	out[0] = 1; out[5] = 1; out[10] = 1; out[15] = 1;
	return out;
}

function mat4Ortho(out, left, right, bottom, top, near, far) {
	const lr = 1.0 / (right - left);
	const bt = 1.0 / (top - bottom);
	const nf = 1.0 / (near - far);
	out.fill(0);
	out[0] = 2.0 * lr;
	out[5] = 2.0 * bt;
	out[10] = 2.0 * nf;
	out[12] = -(right + left) * lr;
	out[13] = -(top + bottom) * bt;
	out[14] = (far + near) * nf;
	out[15] = 1.0;
	return out;
}

function mat4Perspective(out, fovRad, aspect, near, far) {
	const f = 1.0 / Math.tan(fovRad / 2.0);
	out.fill(0);
	out[0] = f / aspect;
	out[5] = f;
	out[10] = (far + near) / (near - far);
	out[11] = -1.0;
	out[14] = (2.0 * far * near) / (near - far);
	return out;
}

function mat4LookAt(out, eye, target, up) {
	const eyex = eye[0], eyey = eye[1], eyez = eye[2];
	const upx = up[0], upy = up[1], upz = up[2];
	const targetx = target[0], targety = target[1], targetz = target[2];

	let z0 = eyex - targetx, z1 = eyey - targety, z2 = eyez - targetz;
	let len = Math.hypot(z0, z1, z2);
	if (len === 0) { z0 = 0; z1 = 0; z2 = 1; } else { z0 /= len; z1 /= len; z2 /= len; }

	// In NeverDark (+X=East, +Y=South, +Z=Zenith), camera right points East (+X)
	let x0 = -(upy * z2 - upz * z1), x1 = -(upz * z0 - upx * z2), x2 = -(upx * z1 - upy * z0);
	len = Math.hypot(x0, x1, x2);
	if (len === 0) { x0 = 1; x1 = 0; x2 = 0; } else { x0 /= len; x1 /= len; x2 /= len; }

	let y0 = x1 * z2 - x2 * z1, y1 = x2 * z0 - x0 * z2, y2 = x0 * z1 - x1 * z0;

	out[0] = x0; out[1] = y0; out[2] = z0; out[3] = 0;
	out[4] = x1; out[5] = y1; out[6] = z1; out[7] = 0;
	out[8] = x2; out[9] = y2; out[10] = z2; out[11] = 0;
	out[12] = -(x0 * eyex + x1 * eyey + x2 * eyez);
	out[13] = -(y0 * eyex + y1 * eyey + y2 * eyez);
	out[14] = -(z0 * eyex + z1 * eyey + z2 * eyez);
	out[15] = 1;
	return out;
}

function mat4Multiply(out, a, b) {
	const a00 = a[0], a01 = a[1], a02 = a[2], a03 = a[3];
	const a10 = a[4], a11 = a[5], a12 = a[6], a13 = a[7];
	const a20 = a[8], a21 = a[9], a22 = a[10], a23 = a[11];
	const a30 = a[12], a31 = a[13], a32 = a[14], a33 = a[15];

	let b0 = b[0], b1 = b[1], b2 = b[2], b3 = b[3];
	out[0] = b0 * a00 + b1 * a10 + b2 * a20 + b3 * a30;
	out[1] = b0 * a01 + b1 * a11 + b2 * a21 + b3 * a31;
	out[2] = b0 * a02 + b1 * a12 + b2 * a22 + b3 * a32;
	out[3] = b0 * a03 + b1 * a13 + b2 * a23 + b3 * a33;

	b0 = b[4]; b1 = b[5]; b2 = b[6]; b3 = b[7];
	out[4] = b0 * a00 + b1 * a10 + b2 * a20 + b3 * a30;
	out[5] = b0 * a01 + b1 * a11 + b2 * a21 + b3 * a31;
	out[6] = b0 * a02 + b1 * a12 + b2 * a22 + b3 * a32;
	out[7] = b0 * a03 + b1 * a13 + b2 * a23 + b3 * a33;

	b0 = b[8]; b1 = b[9]; b2 = b[10]; b3 = b[11];
	out[8] = b0 * a00 + b1 * a10 + b2 * a20 + b3 * a30;
	out[9] = b0 * a01 + b1 * a11 + b2 * a21 + b3 * a31;
	out[10] = b0 * a02 + b1 * a12 + b2 * a22 + b3 * a32;
	out[11] = b0 * a03 + b1 * a13 + b2 * a23 + b3 * a33;

	b0 = b[12]; b1 = b[13]; b2 = b[14]; b3 = b[15];
	out[12] = b0 * a00 + b1 * a10 + b2 * a20 + b3 * a30;
	out[13] = b0 * a01 + b1 * a11 + b2 * a21 + b3 * a31;
	out[14] = b0 * a02 + b1 * a12 + b2 * a22 + b3 * a32;
	out[15] = b0 * a03 + b1 * a13 + b2 * a23 + b3 * a33;
	return out;
}

const cam3D = {
	active: false,
	tx: 0, ty: 0, tz: 0,
	dist: 435.0,
	pitch: 90.0,
	yaw: 0.0,
	fov: 45.0,
	needsClear: true
};

function computeMVP(width, height, cp) {
	const tx = cp ? cp[0] : cam3D.tx;
	const ty = cp ? cp[1] : cam3D.ty;
	const tz = cp ? cp[2] : cam3D.tz;
	const dist = (cp && cp[3] > 0) ? cp[3] : cam3D.dist;
	const pitchDeg = (cp && cp[4] > 0) ? cp[4] : (cam3D.pitch > 0 ? cam3D.pitch : LAM_DIORAMA_PITCH_DEG);

	const pitchRad = pitchDeg * Math.PI / 180.0;
	const cosP = Math.cos(pitchRad);
	const sinP = Math.sin(pitchRad);

	const majorR = (cp && cp.length >= 16 && cp[14] > 0) ? cp[14] : 2000000.0;
	const minorR = (cp && cp.length >= 16 && cp[15] > 0) ? cp[15] : 1335000.0;
	const cx = (cp && cp.length >= 19) ? cp[16] : 0.0;
	const cy = (cp && cp.length >= 19) ? cp[17] : 0.0;
	const cz = (cp && cp.length >= 19) ? cp[18] : 0.0;
	const worldW = (cp && cp.length >= 21 && cp[19] > 0) ? cp[19] : 10485760.0;
	const worldH = (cp && cp.length >= 21 && cp[20] > 0) ? cp[20] : 8388608.0;

	// Angular coordinates of player on the torus
	const TWO_PI = 6.28318530718;
	const u = TWO_PI * ((tx / worldW) - Math.floor(tx / worldW));
	const v = TWO_PI * ((ty / worldH) - Math.floor(ty / worldH));

	const cu = Math.cos(u), su = Math.sin(u);
	const cv = Math.cos(v), sv = Math.sin(v);

	const r = minorR + tz;

	// Player's 3D target position on the torus surface
	const target = [
		cx + (majorR + r * cv) * cu,
		cy + (majorR + r * cv) * su,
		cz + r * sv
	];

	// Local orthonormal basis at player
	const N = [cv * cu, cv * su, sv];            // Zenith / Up
	const S = [-sv * cu, -sv * su, cv];          // South tangent (along +wy)

	// Eye: Elevated along normal N, pulled back South along S by diorama pitch
	const eye = [
		target[0] + dist * (sinP * N[0] + cosP * S[0]),
		target[1] + dist * (sinP * N[1] + cosP * S[1]),
		target[2] + dist * (sinP * N[2] + cosP * S[2])
	];

	// Up vector: perpendicular to line of sight
	const up = [
		cosP * N[0] - sinP * S[0],
		cosP * N[1] - sinP * S[1],
		cosP * N[2] - sinP * S[2]
	];

	const view = mat4Create();
	mat4LookAt(view, eye, target, up);

	const fovDeg = (cp && cp.length >= 7 && cp[6] > 0) ? cp[6] : (cam3D.fov > 0 ? cam3D.fov : 45.0);
	const fovRad = fovDeg * Math.PI / 180.0;
	const aspect = (width || 640) / (height || 480);
	const nearZ = Math.max(2.0, dist * 0.005);
	const farZ = Math.max(200000.0, dist * 20.0);

	const proj = mat4Create();
	mat4Perspective(proj, fovRad, aspect, nearZ, farZ);

	const mvp = mat4Create();
	mat4Multiply(mvp, proj, view);
	mvp.eye = eye;
	mvp.target = target;
	mvp.dist = dist;
	return mvp;
}

const shaderDefs = new Map();
let nextShaderId = 1;
let activeShaderId = 0;

function compileProgram(gl, vsSrc, fsSrc) {
	try {
		const vs = gl.createShader(gl.VERTEX_SHADER);
		gl.shaderSource(vs, vsSrc);
		gl.compileShader(vs);
		if (!gl.getShaderParameter(vs, gl.COMPILE_STATUS)) {
			console.error('Vertex shader error:', gl.getShaderInfoLog(vs));
			return null;
		}

		const fs = gl.createShader(gl.FRAGMENT_SHADER);
		gl.shaderSource(fs, fsSrc);
		gl.compileShader(fs);
		if (!gl.getShaderParameter(fs, gl.COMPILE_STATUS)) {
			console.error('Fragment shader error:', gl.getShaderInfoLog(fs));
			return null;
		}

		const prog = gl.createProgram();
		gl.attachShader(prog, vs);
		gl.attachShader(prog, fs);
		gl.linkProgram(prog);
		if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
			console.error('Program link error:', gl.getProgramInfoLog(prog));
			return null;
		}

		const aPos = gl.getAttribLocation(prog, 'a_pos');
		const aUv = gl.getAttribLocation(prog, 'a_uv');
		const uTex = gl.getUniformLocation(prog, 'u_tex');
		const uTime = gl.getUniformLocation(prog, 'u_time');
		const uRes = gl.getUniformLocation(prog, 'u_res');
		const uParams = gl.getUniformLocation(prog, 'u_params');
		return { prog, aPos, aUv, uTex, uTime, uRes, uParams };
	} catch (e) {
		console.error('Shader pipeline exception:', e);
		return null;
	}
}

function initGlPipeline(gl) {
	try {
		const basePipe = compileProgram(gl, VS_QUAD, FS_QUAD);
		if (!basePipe) return null;

		gl.useProgram(basePipe.prog);

		// Quad vertices: 2 triangles (x, y, u, v)
		const verts = new Float32Array([
			-1.0, -1.0,  0.0,  1.0,
			 1.0, -1.0,  1.0,  1.0,
			-1.0,  1.0,  0.0,  0.0,
			-1.0,  1.0,  0.0,  0.0,
			 1.0, -1.0,  1.0,  1.0,
			 1.0,  1.0,  1.0,  0.0
		]);

		const vbo = gl.createBuffer();
		gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
		gl.bufferData(gl.ARRAY_BUFFER, verts, gl.STATIC_DRAW);

		if (basePipe.aPos >= 0) {
			gl.enableVertexAttribArray(basePipe.aPos);
			gl.vertexAttribPointer(basePipe.aPos, 2, gl.FLOAT, false, 16, 0);
		}
		if (basePipe.aUv >= 0) {
			gl.enableVertexAttribArray(basePipe.aUv);
			gl.vertexAttribPointer(basePipe.aUv, 2, gl.FLOAT, false, 16, 8);
		}

		const tex = gl.createTexture();
		gl.bindTexture(gl.TEXTURE_2D, tex);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

		return { prog: basePipe.prog, aPos: basePipe.aPos, aUv: basePipe.aUv, tex, vbo, uTex: basePipe.uTex, uTime: basePipe.uTime, uRes: basePipe.uRes };
	} catch (e) {
		return null;
	}
}

function createTerrainGrid(gl, gw, gh, skirtDepth) {
	gw = gw || 160;
	gh = gh || 128;
	skirtDepth = (skirtDepth !== undefined) ? skirtDepth : 4.0;

	// Surface grid vertices: (gw + 1) * (gh + 1) vertices with 3 components (u, v, skirtDrop)
	const surfVertsCount = (gw + 1) * (gh + 1);
	// 4 border skirts: top (gw+1), bottom (gw+1), left (gh+1), right (gh+1)
	const skirtVertsCount = (gw + 1) * 2 + (gh + 1) * 2;
	const totalVerts = surfVertsCount + skirtVertsCount;
	const verts = new Float32Array(totalVerts * 3);

	let vi = 0;
	for (let y = 0; y <= gh; y++) {
		const v = y / gh;
		for (let x = 0; x <= gw; x++) {
			verts[vi++] = x / gw;
			verts[vi++] = v;
			verts[vi++] = 0.0;
		}
	}

	const topSkirtBase = surfVertsCount;
	for (let x = 0; x <= gw; x++) {
		verts[vi++] = x / gw;
		verts[vi++] = 0.0;
		verts[vi++] = skirtDepth;
	}
	const botSkirtBase = topSkirtBase + (gw + 1);
	for (let x = 0; x <= gw; x++) {
		verts[vi++] = x / gw;
		verts[vi++] = 1.0;
		verts[vi++] = skirtDepth;
	}
	const leftSkirtBase = botSkirtBase + (gw + 1);
	for (let y = 0; y <= gh; y++) {
		verts[vi++] = 0.0;
		verts[vi++] = y / gh;
		verts[vi++] = skirtDepth;
	}
	const rightSkirtBase = leftSkirtBase + (gh + 1);
	for (let y = 0; y <= gh; y++) {
		verts[vi++] = 1.0;
		verts[vi++] = y / gh;
		verts[vi++] = skirtDepth;
	}

	const surfIndexCount = gw * gh * 6;
	const skirtIndexCount = (gw * 2 + gh * 2) * 6;
	const indices = new Uint16Array(surfIndexCount + skirtIndexCount);

	let ii = 0;
	const stride = gw + 1;
	for (let y = 0; y < gh; y++) {
		for (let x = 0; x < gw; x++) {
			const tl = y * stride + x;
			const tr = tl + 1;
			const bl = (y + 1) * stride + x;
			const br = bl + 1;
			indices[ii++] = tl;
			indices[ii++] = tr;
			indices[ii++] = bl;
			indices[ii++] = tr;
			indices[ii++] = br;
			indices[ii++] = bl;
		}
	}

	// Skirt indices
	for (let x = 0; x < gw; x++) {
		const s0 = x, s1 = x + 1;
		const k0 = topSkirtBase + x, k1 = topSkirtBase + x + 1;
		indices[ii++] = s0; indices[ii++] = s1; indices[ii++] = k0;
		indices[ii++] = s1; indices[ii++] = k1; indices[ii++] = k0;
	}
	for (let x = 0; x < gw; x++) {
		const s0 = gh * stride + x, s1 = gh * stride + x + 1;
		const k0 = botSkirtBase + x, k1 = botSkirtBase + x + 1;
		indices[ii++] = s0; indices[ii++] = k0; indices[ii++] = s1;
		indices[ii++] = s1; indices[ii++] = k0; indices[ii++] = k1;
	}
	for (let y = 0; y < gh; y++) {
		const s0 = y * stride, s1 = (y + 1) * stride;
		const k0 = leftSkirtBase + y, k1 = leftSkirtBase + y + 1;
		indices[ii++] = s0; indices[ii++] = k0; indices[ii++] = s1;
		indices[ii++] = s1; indices[ii++] = k0; indices[ii++] = k1;
	}
	for (let y = 0; y < gh; y++) {
		const s0 = y * stride + gw, s1 = (y + 1) * stride + gw;
		const k0 = rightSkirtBase + y, k1 = rightSkirtBase + y + 1;
		indices[ii++] = s0; indices[ii++] = s1; indices[ii++] = k0;
		indices[ii++] = s1; indices[ii++] = k1; indices[ii++] = k0;
	}

	const vbo = gl.createBuffer();
	gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
	gl.bufferData(gl.ARRAY_BUFFER, verts, gl.STATIC_DRAW);

	const ibo = gl.createBuffer();
	gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ibo);
	gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, indices, gl.STATIC_DRAW);

	return { vbo, ibo, indexCount: indices.length };
}

function createWaterGrid(gl, gw, gh) {
	gw = gw || 160;
	gh = gh || 128;
	const totalVerts = (gw + 1) * (gh + 1);
	const verts = new Float32Array(totalVerts * 2);
	let vi = 0;
	for (let y = 0; y <= gh; y++) {
		const v = y / gh;
		for (let x = 0; x <= gw; x++) {
			verts[vi++] = x / gw;
			verts[vi++] = v;
		}
	}
	const indices = new Uint16Array(gw * gh * 6);
	let ii = 0;
	const stride = gw + 1;
	for (let y = 0; y < gh; y++) {
		for (let x = 0; x < gw; x++) {
			const tl = y * stride + x;
			const tr = tl + 1;
			const bl = (y + 1) * stride + x;
			const br = bl + 1;
			indices[ii++] = tl;
			indices[ii++] = bl;
			indices[ii++] = tr;
			indices[ii++] = tr;
			indices[ii++] = bl;
			indices[ii++] = br;
		}
	}
	const vbo = gl.createBuffer();
	gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
	gl.bufferData(gl.ARRAY_BUFFER, verts, gl.STATIC_DRAW);

	const ibo = gl.createBuffer();
	gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ibo);
	gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, indices, gl.STATIC_DRAW);

	return { vbo, ibo, indexCount: indices.length };
}

function initTerrainPipeline(gl, isWebGL2) {
	if (!isWebGL2) return null;
	try {
		const prog = compileProgram(gl, VS_TERRAIN_3D, FS_TERRAIN_3D);
		if (!prog) return null;

		const uMVP = gl.getUniformLocation(prog.prog, 'uMVP');
		const uHeightTex = gl.getUniformLocation(prog.prog, 'uHeightTex');
		const uSubstrateTex = gl.getUniformLocation(prog.prog, 'uSubstrateTex');
		const uChunkOrigin = gl.getUniformLocation(prog.prog, 'uChunkOrigin');
		const uChunkSpan = gl.getUniformLocation(prog.prog, 'uChunkSpan');
		const uHeightScale = gl.getUniformLocation(prog.prog, 'uHeightScale');
		const uSeaLevel = gl.getUniformLocation(prog.prog, 'uSeaLevel');
		const uCameraPos = gl.getUniformLocation(prog.prog, 'uCameraPos');
		const uFogStart = gl.getUniformLocation(prog.prog, 'uFogStart');
		const uFogEnd = gl.getUniformLocation(prog.prog, 'uFogEnd');
		const uAnimTime = gl.getUniformLocation(prog.prog, 'uAnimTime');
		const uPlanetCenter = gl.getUniformLocation(prog.prog, 'uPlanetCenter');
		const uTorusRadii = gl.getUniformLocation(prog.prog, 'uTorusRadii');
		const uWorldDim = gl.getUniformLocation(prog.prog, 'uWorldDim');
		const uSkyColor = gl.getUniformLocation(prog.prog, 'uSkyColor');
		const uAtmoIntensity = gl.getUniformLocation(prog.prog, 'uAtmoIntensity');
		const aPos = gl.getAttribLocation(prog.prog, 'a_grid_uv');

		const lodGrids = {
			lod3: createTerrainGrid(gl, 160, 128, 4.0),
			lod2: createTerrainGrid(gl, 40, 32, 6.0),
			lod1: createTerrainGrid(gl, 10, 8, 12.0),
			macroChunk: createTerrainGrid(gl, 64, 64, 16.0)
		};
		const grid = lodGrids.lod3;

		const heightTex = gl.createTexture();
		gl.bindTexture(gl.TEXTURE_2D, heightTex);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

		const substrateTex = gl.createTexture();
		gl.bindTexture(gl.TEXTURE_2D, substrateTex);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

		return {
			prog: prog.prog,
			uMVP, uHeightTex, uSubstrateTex, uChunkOrigin, uChunkSpan, uHeightScale, uSeaLevel,
			uCameraPos, uFogStart, uFogEnd, uAnimTime,
			uPlanetCenter, uTorusRadii, uWorldDim, uSkyColor, uAtmoIntensity,
			aPos,
			gridVbo: grid.vbo,
			gridIbo: grid.ibo,
			indexCount: grid.indexCount,
			lodGrids,
			heightTex,
			substrateTex
		};
	} catch (e) {
		return null;
	}
}

function initWaterPipeline(gl, isWebGL2) {
	if (!isWebGL2) return null;
	try {
		const prog = compileProgram(gl, VS_WATER_3D, FS_WATER_3D);
		if (!prog) return null;
		const uMVP = gl.getUniformLocation(prog.prog, 'uMVP');
		const uHeightTex = gl.getUniformLocation(prog.prog, 'uHeightTex');
		const uWaterTex = gl.getUniformLocation(prog.prog, 'uWaterTex');
		const uChunkOrigin = gl.getUniformLocation(prog.prog, 'uChunkOrigin');
		const uChunkSpan = gl.getUniformLocation(prog.prog, 'uChunkSpan');
		const uHeightScale = gl.getUniformLocation(prog.prog, 'uHeightScale');
		const uSeaLevel = gl.getUniformLocation(prog.prog, 'uSeaLevel');
		const uCameraPos = gl.getUniformLocation(prog.prog, 'uCameraPos');
		const uFogStart = gl.getUniformLocation(prog.prog, 'uFogStart');
		const uFogEnd = gl.getUniformLocation(prog.prog, 'uFogEnd');
		const uAnimTime = gl.getUniformLocation(prog.prog, 'uAnimTime');
		const uPlanetCenter = gl.getUniformLocation(prog.prog, 'uPlanetCenter');
		const uTorusRadii = gl.getUniformLocation(prog.prog, 'uTorusRadii');
		const uWorldDim = gl.getUniformLocation(prog.prog, 'uWorldDim');
		const uSkyColor = gl.getUniformLocation(prog.prog, 'uSkyColor');
		const uAtmoIntensity = gl.getUniformLocation(prog.prog, 'uAtmoIntensity');
		const aPos = gl.getAttribLocation(prog.prog, 'a_grid_uv');

		const heightTex = gl.createTexture();
		gl.bindTexture(gl.TEXTURE_2D, heightTex);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

		const waterTex = gl.createTexture();
		gl.bindTexture(gl.TEXTURE_2D, waterTex);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

		const lodGrids = {
			lod3: createWaterGrid(gl, 160, 128),
			lod2: createWaterGrid(gl, 40, 32),
			lod1: createWaterGrid(gl, 10, 8),
			macroChunk: createWaterGrid(gl, 64, 64)
		};
		const grid = lodGrids.lod3;

		return {
			prog: prog.prog,
			uMVP, uHeightTex, uWaterTex, uChunkOrigin, uChunkSpan, uHeightScale, uSeaLevel,
			uCameraPos, uFogStart, uFogEnd, uAnimTime,
			uPlanetCenter, uTorusRadii, uWorldDim, uSkyColor, uAtmoIntensity,
			aPos,
			gridVbo: grid.vbo,
			gridIbo: grid.ibo,
			indexCount: grid.indexCount,
			lodGrids,
			heightTex,
			waterTex
		};
	} catch (e) {
		return null;
	}
}

function initBillboardPipeline(gl, isWebGL2) {
	if (!isWebGL2) return null;
	try {
		const prog = compileProgram(gl, VS_BILLBOARD_3D, FS_BILLBOARD_3D);
		if (!prog) return null;

		const uMVP = gl.getUniformLocation(prog.prog, 'uMVP');
		const uAtlasTex = gl.getUniformLocation(prog.prog, 'uAtlasTex');
		const uSeaLevel = gl.getUniformLocation(prog.prog, 'uSeaLevel');
		const uAnimTime = gl.getUniformLocation(prog.prog, 'uAnimTime');
		const uCameraPos = gl.getUniformLocation(prog.prog, 'uCameraPos');
		const uFogStart = gl.getUniformLocation(prog.prog, 'uFogStart');
		const uFogEnd = gl.getUniformLocation(prog.prog, 'uFogEnd');
		const uPlanetCenter = gl.getUniformLocation(prog.prog, 'uPlanetCenter');
		const uTorusRadii = gl.getUniformLocation(prog.prog, 'uTorusRadii');
		const uWorldDim = gl.getUniformLocation(prog.prog, 'uWorldDim');
		const uSkyColor = gl.getUniformLocation(prog.prog, 'uSkyColor');
		const uAtmoIntensity = gl.getUniformLocation(prog.prog, 'uAtmoIntensity');
		const aPos = gl.getAttribLocation(prog.prog, 'a_pos');
		const aUv = gl.getAttribLocation(prog.prog, 'a_uv');
		const aTint = gl.getAttribLocation(prog.prog, 'a_tint');

		const vbo = gl.createBuffer();
		const atlasTex = gl.createTexture();
		gl.bindTexture(gl.TEXTURE_2D, atlasTex);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

		return { prog: prog.prog, uMVP, uAtlasTex, uSeaLevel, uAnimTime, uCameraPos, uFogStart, uFogEnd, uPlanetCenter, uTorusRadii, uWorldDim, uSkyColor, uAtmoIntensity, aPos, aUv, aTint, vbo, atlasTex };
	} catch (e) {
		return null;
	}
}

export function createLaminaBridge(root, getWasm) {
	// One rendering target per <canvas data-bud-canvas>. A page may carry
	// several (e.g. a game stage and a mini-map); each lam_canvas_flush(buf, w,
	// h) is routed to the canvas whose intrinsic size is exactly w x h, falling
	// back to the first one, so independent frame producers never overwrite
	// each other's canvas.
	const targets = new WeakMap();
	const kicked = new WeakSet();

	function allCanvases() {
		const scope = root && root.querySelectorAll ? root : document;
		let list = scope.querySelectorAll('[data-bud-canvas]');
		if (!list.length && scope !== document) list = document.querySelectorAll('[data-bud-canvas]');
		return list;
	}

	function pickCanvas(width, height) {
		// 1. a canvas whose intrinsic size is exactly w x h (fixed-size canvases
		//    such as a mini-map keep their own frames);
		// 2. a canvas marked data-bud-canvas-fit: it follows the frame size, so
		//    it is resized to w x h and painted 1:1;
		// 3. the first canvas.
		const list = allCanvases();
		let first = null, fit = null;
		for (const c of list) {
			if (!first) first = c;
			if (c.width === width && c.height === height) return c;
			if (!fit && c.hasAttribute && c.hasAttribute('data-bud-canvas-fit')) fit = c;
		}
		if (fit && width > 0 && height > 0) {
			fit.width = width;
			fit.height = height;
			return fit;
		}
		return fit || first;
	}

	function findStageCanvas() {
		const list = allCanvases();
		for (const c of list) {
			if (c.hasAttribute && c.hasAttribute('data-bud-canvas-fit')) return c;
		}
		return list[0] || null;
	}

	function targetFor(el) {
		let t = targets.get(el);
		if (t) return t;
		t = { gl: null, glPipe: null, terrainPipe: null, waterPipe: null, billboardPipe: null, isWebGL2: false, canvas2d: null, shaderPrograms: new Map(), pendingWater: [], chunkCache: new Map(), frameCount: 0 };
		// Try WebGL2 first with preserveDrawingBuffer & depth enabled, falling back to WebGL
		try {
			const glOpts = { preserveDrawingBuffer: true, alpha: true, antialias: true, depth: true };
			t.gl = el.getContext('webgl2', glOpts);
			if (t.gl) {
				t.isWebGL2 = true;
			} else {
				t.gl = el.getContext('webgl', glOpts);
			}
			if (t.gl) {
				t.glPipe = initGlPipeline(t.gl);
				t.terrainPipe = initTerrainPipeline(t.gl, t.isWebGL2);
				t.waterPipe = initWaterPipeline(t.gl, t.isWebGL2);
				t.billboardPipe = initBillboardPipeline(t.gl, t.isWebGL2);
			}
		} catch (e) {
			t.gl = null;
			t.glPipe = null;
		}
		if (!t.gl || !t.glPipe) {
			t.gl = null;
			t.glPipe = null;
			try {
				t.canvas2d = el.getContext('2d');
			} catch (e) {
				t.canvas2d = null;
			}
		}
		targets.set(el, t);
		return t;
	}

	function flushPendingWater(t, canvasEl) {
		if (!t || !t.pendingWater || t.pendingWater.length === 0) return;
		const gl = t.gl;
		const wpipe = t.waterPipe;
		const tpipe = t.terrainPipe;
		if (!gl || !wpipe || !tpipe) {
			t.pendingWater = [];
			return;
		}

		const exports = getWasm();
		if (!exports || !exports.memory) {
			t.pendingWater = [];
			return;
		}

		gl.useProgram(wpipe.prog);
		gl.enable(gl.DEPTH_TEST);
		gl.depthFunc(gl.LEQUAL);
		gl.depthMask(false);
		gl.enable(gl.BLEND);
		gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
		// Protect canvas alpha channel so HTML background does not shine through
		gl.colorMask(true, true, true, false);

		for (const chunk of t.pendingWater) {
			gl.uniformMatrix4fv(wpipe.uMVP, false, chunk.mvp);
			gl.uniform2f(wpipe.uChunkOrigin, chunk.ox, chunk.oy);
			gl.uniform2f(wpipe.uChunkSpan, chunk.spanX, chunk.spanY);
			if (wpipe.uHeightScale !== null) gl.uniform1f(wpipe.uHeightScale, 1.0);
			if (wpipe.uSeaLevel !== null) gl.uniform1f(wpipe.uSeaLevel, chunk.sea_level);
			if (wpipe.uCameraPos && chunk.eye) gl.uniform3fv(wpipe.uCameraPos, chunk.eye);
			if (wpipe.uFogStart !== null) gl.uniform1f(wpipe.uFogStart, (chunk.dist || 280.0) * 1.5);
			if (wpipe.uFogEnd !== null) gl.uniform1f(wpipe.uFogEnd, (chunk.dist || 280.0) * 4.0);
			if (wpipe.uAnimTime !== null) gl.uniform1f(wpipe.uAnimTime, chunk.anim_time || 0.0);

			if (chunk.chunkEntry && chunk.chunkEntry.heightTex) {
				gl.activeTexture(gl.TEXTURE0);
				gl.bindTexture(gl.TEXTURE_2D, chunk.chunkEntry.heightTex);
				gl.uniform1i(wpipe.uHeightTex, 0);
			} else {
				const hPtr = chunk.height_ptr >>> 0;
				const hByteLen = chunk.chunk_w * chunk.chunk_h * 2;
				if (hPtr > 0 && hPtr + hByteLen <= exports.memory.buffer.byteLength) {
					const hArr = new Int16Array(exports.memory.buffer, hPtr, chunk.chunk_w * chunk.chunk_h);
					gl.activeTexture(gl.TEXTURE0);
					gl.bindTexture(gl.TEXTURE_2D, wpipe.heightTex);
					gl.texImage2D(gl.TEXTURE_2D, 0, gl.R16I, chunk.chunk_w, chunk.chunk_h, 0, gl.RED_INTEGER, gl.SHORT, hArr);
					gl.uniform1i(wpipe.uHeightTex, 0);
				}
			}

			/* 0 dynamic texture streaming: water rendering is completely procedural on GPU */
			let activeGrid = (wpipe && wpipe.lodGrids) ? wpipe.lodGrids.lod3 : wpipe;
			if (wpipe && wpipe.lodGrids) {
				const chunkLod = (chunk.chunkEntry && chunk.chunkEntry.lod !== undefined) ? chunk.chunkEntry.lod : 3;
				if (chunk.chunk_w === 33 && chunk.chunk_h === 33) activeGrid = wpipe.lodGrids.macroChunk || wpipe.lodGrids.lod2;
				else if (chunkLod <= 1 || (chunk.chunk_w <= 10 && chunk.chunk_h <= 8)) activeGrid = wpipe.lodGrids.lod1;
				else if (chunkLod === 2 || (chunk.chunk_w <= 40 && chunk.chunk_h <= 32)) activeGrid = wpipe.lodGrids.lod2;
			}

			if (wpipe.uPlanetCenter && t.lastPlanetCenter) gl.uniform3fv(wpipe.uPlanetCenter, t.lastPlanetCenter);
			if (wpipe.uTorusRadii && t.lastTorusRadii) gl.uniform2fv(wpipe.uTorusRadii, t.lastTorusRadii);
			if (wpipe.uWorldDim && t.lastWorldDim) gl.uniform2fv(wpipe.uWorldDim, t.lastWorldDim);
			if (wpipe.uSkyColor && t.lastSky) gl.uniform3f(wpipe.uSkyColor, t.lastSky[0], t.lastSky[1], t.lastSky[2]);
			if (wpipe.uAtmoIntensity) gl.uniform1f(wpipe.uAtmoIntensity, (t.lastAtmoIntensity !== undefined) ? t.lastAtmoIntensity : 0.0);

			gl.bindBuffer(gl.ARRAY_BUFFER, activeGrid.gridVbo || activeGrid.vbo);
			gl.enableVertexAttribArray(wpipe.aPos);
			gl.vertexAttribPointer(wpipe.aPos, 2, gl.FLOAT, false, 8, 0);

			gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, activeGrid.gridIbo || activeGrid.ibo);
			gl.drawElements(gl.TRIANGLES, activeGrid.indexCount, gl.UNSIGNED_SHORT, 0);
		}

		t.pendingWater = [];
		gl.depthMask(true);
		gl.colorMask(true, true, true, true);
	}

	function kickRender() {
		const fn = getExport(getWasm(), ['wasm_canvas_render']);
		if (fn) requestAnimationFrame(() => fn());
	}

	function bindAll() {
		for (const c of allCanvases()) {
			targetFor(c);
			if (!kicked.has(c)) {
				kicked.add(c);
				kickRender();
			}
		}
	}

	return {
		mount() {
			bindAll();
		},
		makeLaminaImports() {
			return {
				lam_canvas_flush(buf_ptr, width, height) {
					const canvasEl = pickCanvas(width, height);
					if (!canvasEl) return;
					const t = targetFor(canvasEl);
					if (!kicked.has(canvasEl)) {
						kicked.add(canvasEl);
						kickRender();
					}
					const exports = getWasm();
					if (!exports || !exports.memory) return;
					const uptr = buf_ptr >>> 0;
					const byteLen = width * height * 4;
					if (uptr + byteLen > exports.memory.buffer.byteLength) return;

					const bytes = new Uint8Array(exports.memory.buffer, uptr, byteLen);

					if (t.gl && t.glPipe) {
						const gl = t.gl;
						gl.viewport(0, 0, canvasEl.width || width, canvasEl.height || height);

						gl.disable(gl.DEPTH_TEST);
						gl.disable(gl.BLEND);
						gl.depthMask(true);
						gl.colorMask(true, true, true, true);
						gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, null);
						gl.disableVertexAttribArray(2);

						let pipe = t.glPipe;
						let def = null;
						if (activeShaderId > 0 && shaderDefs.has(activeShaderId)) {
							def = shaderDefs.get(activeShaderId);
							if (!t.shaderPrograms.has(activeShaderId)) {
								const cp = compileProgram(gl, VS_QUAD, def.fragSrc);
								t.shaderPrograms.set(activeShaderId, cp);
							}
							const cp = t.shaderPrograms.get(activeShaderId);
							if (cp) pipe = cp;
						}

						gl.useProgram(pipe.prog);
						if (pipe.uTime) gl.uniform1f(pipe.uTime, performance.now() * 0.001);
						if (pipe.uRes) gl.uniform2f(pipe.uRes, width, height);
						if (pipe.uParams && def && def.uniforms) gl.uniform4fv(pipe.uParams, def.uniforms);

						gl.bindBuffer(gl.ARRAY_BUFFER, t.glPipe.vbo);
						if (pipe.aPos >= 0) {
							gl.enableVertexAttribArray(pipe.aPos);
							gl.vertexAttribPointer(pipe.aPos, 2, gl.FLOAT, false, 16, 0);
						}
						if (pipe.aUv >= 0) {
							gl.enableVertexAttribArray(pipe.aUv);
							gl.vertexAttribPointer(pipe.aUv, 2, gl.FLOAT, false, 16, 8);
						}

						gl.bindTexture(gl.TEXTURE_2D, t.glPipe.tex);
						gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, width, height, 0, gl.RGBA, gl.UNSIGNED_BYTE, bytes);
						gl.drawArrays(gl.TRIANGLES, 0, 6);
						cam3D.needsClear = true;
						return;
					}

					if (t.canvas2d) {
						const clamped = new Uint8ClampedArray(exports.memory.buffer, uptr, byteLen);
						const imgData = new ImageData(clamped, width, height);
						t.canvas2d.putImageData(imgData, 0, 0);
					}
				},
				lam_shader_register(name_ptr, frag_ptr) {
					const exports = getWasm();
					if (!exports || !exports.memory) return 0;
					const mem = new Uint8Array(exports.memory.buffer);
					let p = name_ptr >>> 0, name = "";
					while (p < mem.length && mem[p] !== 0) name += String.fromCharCode(mem[p++]);
					p = frag_ptr >>> 0;
					let fragSrc = "";
					while (p < mem.length && mem[p] !== 0) fragSrc += String.fromCharCode(mem[p++]);

					const id = nextShaderId++;
					shaderDefs.set(id, { name, fragSrc, uniforms: new Float32Array(16) });
					return id;
				},
				lam_shader_bind(shader_id) {
					activeShaderId = shader_id >>> 0;
				},
				lam_shader_uniforms(shader_id, vals_ptr, count) {
					const exports = getWasm();
					if (!exports || !exports.memory) return;
					const id = shader_id >>> 0;
					const def = shaderDefs.get(id);
					if (!def) return;
					const n = Math.min(count >>> 0, 16);
					const f32 = new Float32Array(exports.memory.buffer, vals_ptr >>> 0, n);
					def.uniforms.set(f32);
				},
				lam_cam_set_3d(tx, ty, tz, dist, pitch, yaw, fov) {
					cam3D.active = true;
					cam3D.tx = tx;
					cam3D.ty = ty;
					cam3D.tz = tz;
					cam3D.dist = (dist > 0) ? dist : 435.0;
					cam3D.pitch = (pitch > 0) ? pitch : 90.0;
					cam3D.yaw = yaw;
					cam3D.fov = (fov > 0) ? fov : 45.0;
					cam3D.needsClear = true;
				},
				lam_cam_set_2d() {
					if (cam3D.active) {
						const canvasEl = findStageCanvas();
						if (canvasEl) {
							const t = targets.get(canvasEl);
							if (t) flushPendingWater(t, canvasEl);
						}
					}
					cam3D.active = false;
					cam3D.needsClear = true;
				},
				lam_terrain_mesh_flush(color_ptr, height_ptr, chunk_w, chunk_h, stage_w, stage_h, sea_level, cam_params_ptr, water_ptr) {
					const canvasEl = pickCanvas(stage_w, stage_h);
					if (!canvasEl) return;
					const t = targetFor(canvasEl);
					if (!kicked.has(canvasEl)) {
						kicked.add(canvasEl);
						kickRender();
					}
					const exports = getWasm();
					if (!exports || !exports.memory) return;
					const gl = t.gl;
					if (!gl || !t.terrainPipe) return;

					let cp = null;
					if (cam_params_ptr) {
						const ucp = cam_params_ptr >>> 0;
						const floatCount = Math.min(32, Math.floor((exports.memory.buffer.byteLength - ucp) / 4));
						if (floatCount >= 8) {
							cp = new Float32Array(exports.memory.buffer, ucp, floatCount);
						}
					}

					const mvp = computeMVP(canvasEl.width || stage_w, canvasEl.height || stage_h, cp);
					const eye = mvp.eye;
					const dist = mvp.dist;

					gl.viewport(0, 0, canvasEl.width || stage_w, canvasEl.height || stage_h);
					gl.enable(gl.DEPTH_TEST);
					gl.depthFunc(gl.LEQUAL);
					gl.disable(gl.CULL_FACE);
					gl.enable(gl.BLEND);
					gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

					const tx = cp ? cp[0] : cam3D.tx;
					const ty = cp ? cp[1] : cam3D.ty;
					const tz = cp ? cp[2] : cam3D.tz;
					const pitchDeg = (cp && cp[4] > 0) ? cp[4] : (cam3D.pitch > 0 ? cam3D.pitch : LAM_DIORAMA_PITCH_DEG);
					const pitchRad = pitchDeg * Math.PI / 180.0;

					const R_PLANET = Math.max(16000.0, Math.min(80000.0, dist * 1.25));
					const spaceT = Math.min(1.0, Math.max(0.0, (dist - 12000.0) / 40000.0));
					const skyR = 0.55 * (1.0 - spaceT) + 0.012 * spaceT;
					const skyG = 0.70 * (1.0 - spaceT) + 0.016 * spaceT;
					const skyB = 0.85 * (1.0 - spaceT) + 0.035 * spaceT;

					if (cam3D.needsClear) {
						gl.clearColor(skyR, skyG, skyB, 1.0);
						gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
						cam3D.needsClear = false;
					}

					const pipe = t.terrainPipe;
					gl.useProgram(pipe.prog);
					gl.uniformMatrix4fv(pipe.uMVP, false, mvp);

					const majorR = (cp && cp.length >= 16 && cp[14] > 0) ? cp[14] : 2000000.0;
					const minorR = (cp && cp.length >= 16 && cp[15] > 0) ? cp[15] : 1335000.0;
					const cx = (cp && cp.length >= 19) ? cp[16] : 0.0;
					const cy = (cp && cp.length >= 19) ? cp[17] : 0.0;
					const cz = (cp && cp.length >= 19) ? cp[18] : 0.0;
					const worldW = (cp && cp.length >= 21 && cp[19] > 0) ? cp[19] : 10485760.0;
					const worldH = (cp && cp.length >= 21 && cp[20] > 0) ? cp[20] : 8388608.0;

					const atmoIntensity = Math.min(1.0, Math.max(0.0, (dist - 1200.0) / 8000.0));
					t.lastAtmoIntensity = atmoIntensity;
					if (pipe.uAtmoIntensity) gl.uniform1f(pipe.uAtmoIntensity, atmoIntensity);

					if (pipe.uPlanetCenter) gl.uniform3f(pipe.uPlanetCenter, cx, cy, cz);
					if (pipe.uTorusRadii) gl.uniform2f(pipe.uTorusRadii, majorR, minorR);
					if (pipe.uWorldDim) gl.uniform2f(pipe.uWorldDim, worldW, worldH);
					if (pipe.uSkyColor) gl.uniform3f(pipe.uSkyColor, skyR, skyG, skyB);

					t.lastPlanetCenter = [cx, cy, cz];
					t.lastTorusRadii = [majorR, minorR];
					t.lastWorldDim = [worldW, worldH];
					t.lastSky = [skyR, skyG, skyB];

					let ox = 0, oy = 0;
					if (cp && cp.length >= 10) {
						ox = cp[8];
						oy = cp[9];
					} else if (cp) {
						ox = Math.floor(cp[0] / 160.0) * 160.0;
						oy = Math.floor(cp[1] / 128.0) * 128.0;
					}
					const spanX = (cp && cp.length >= 12 && cp[10] > 0) ? cp[10] : 160.0;
					const spanY = (cp && cp.length >= 12 && cp[11] > 0) ? cp[11] : 128.0;

					gl.uniform2f(pipe.uChunkOrigin, ox, oy);
					gl.uniform2f(pipe.uChunkSpan, spanX, spanY);
					if (pipe.uHeightScale !== null) gl.uniform1f(pipe.uHeightScale, 1.0);
					if (pipe.uSeaLevel !== null) gl.uniform1f(pipe.uSeaLevel, sea_level);

					if (pipe.uCameraPos) gl.uniform3fv(pipe.uCameraPos, eye);
					if (pipe.uFogStart !== null) gl.uniform1f(pipe.uFogStart, dist * 1.5);
					if (pipe.uFogEnd !== null) gl.uniform1f(pipe.uFogEnd, dist * 4.0);
					if (pipe.uAnimTime !== null) gl.uniform1f(pipe.uAnimTime, (cp && cp.length >= 8) ? cp[7] : 0.0);

					if (sea_level > -900.0) t.lastSeaLevel = sea_level;
					t.lastAnimTime = (cp && cp.length >= 8) ? cp[7] : 0.0;
					t.lastEye = eye;
					t.lastDist = dist;

					if (!t.chunkCache) t.chunkCache = new Map();
					t.frameCount = (t.frameCount || 0) + 1;

					const lod = (cp && cp.length >= 13) ? Math.round(cp[12]) : 3;
					const rev = (cp && cp.length >= 14) ? Math.round(cp[13]) : 0;
					const chunkKey = Math.round(ox) + ":" + Math.round(oy) + ":" + Math.round(spanX) + ":" + Math.round(spanY) + ":" + chunk_w + ":" + chunk_h;
					let chunkEntry = t.chunkCache.get(chunkKey);
					const hPtr = height_ptr >>> 0;
					const hByteLen = chunk_w * chunk_h * 2;
					const cPtr = color_ptr >>> 0;
					const cByteLen = chunk_w * chunk_h * 4;

					const isMacro = (chunk_w === 33 && chunk_h === 33);
					const needsUpload = !chunkEntry || chunkEntry.hPtr !== hPtr || chunkEntry.cPtr !== cPtr || chunkEntry.lod !== lod || chunkEntry.rev !== rev;

					if (!chunkEntry) {
						chunkEntry = {
							heightTex: gl.createTexture(),
							substrateTex: gl.createTexture(),
							waterTex: gl.createTexture(),
							hPtr: 0,
							cPtr: 0,
							wPtr: 0,
							lastFrame: t.frameCount
						};
						gl.bindTexture(gl.TEXTURE_2D, chunkEntry.heightTex);
						gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
						gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
						gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
						gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

						gl.bindTexture(gl.TEXTURE_2D, chunkEntry.substrateTex);
						gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
						gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
						gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
						gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

						gl.bindTexture(gl.TEXTURE_2D, chunkEntry.waterTex);
						gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
						gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
						gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
						gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

						t.chunkCache.set(chunkKey, chunkEntry);
					}
					chunkEntry.lastFrame = t.frameCount;

					if (needsUpload) {
						chunkEntry.hPtr = hPtr;
						chunkEntry.cPtr = cPtr;
						chunkEntry.lod = lod;
						chunkEntry.rev = rev;
						if (hPtr > 0 && hPtr + hByteLen <= exports.memory.buffer.byteLength) {
							const hArr = new Int16Array(exports.memory.buffer, hPtr, chunk_w * chunk_h);
							gl.bindTexture(gl.TEXTURE_2D, chunkEntry.heightTex);
							gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
							if (t.isWebGL2) {
								gl.texImage2D(gl.TEXTURE_2D, 0, gl.R16I, chunk_w, chunk_h, 0, gl.RED_INTEGER, gl.SHORT, hArr);
							}
						}
						if (cPtr > 0 && cPtr + cByteLen <= exports.memory.buffer.byteLength) {
							const cArr = new Uint8Array(exports.memory.buffer, cPtr, cByteLen);
							gl.bindTexture(gl.TEXTURE_2D, chunkEntry.substrateTex);
							gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
							gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, chunk_w, chunk_h, 0, gl.RGBA, gl.UNSIGNED_BYTE, cArr);
						}
					}

					gl.activeTexture(gl.TEXTURE0);
					gl.bindTexture(gl.TEXTURE_2D, chunkEntry.heightTex);
					gl.uniform1i(pipe.uHeightTex, 0);

					gl.activeTexture(gl.TEXTURE1);
					gl.bindTexture(gl.TEXTURE_2D, chunkEntry.substrateTex);
					gl.uniform1i(pipe.uSubstrateTex, 1);

					if (t.chunkCache.size > 96) {
						for (const [key, entry] of t.chunkCache.entries()) {
							if (t.frameCount - entry.lastFrame > 180) {
								if (entry.heightTex) gl.deleteTexture(entry.heightTex);
								if (entry.substrateTex) gl.deleteTexture(entry.substrateTex);
								if (entry.waterTex) gl.deleteTexture(entry.waterTex);
								t.chunkCache.delete(key);
							}
						}
					}

					let activeGrid = (pipe.lodGrids) ? pipe.lodGrids.lod3 : pipe;
					if (pipe.lodGrids) {
						if (chunk_w === 33 && chunk_h === 33) {
							if (lod === 0 && dist < 1200.0) {
								activeGrid = pipe.lodGrids.lod3 || pipe.lodGrids.lod2;
							} else if (lod <= 1) {
								activeGrid = pipe.lodGrids.lod2;
							} else {
								activeGrid = pipe.lodGrids.macroChunk || pipe.lodGrids.lod2;
							}
						} else if (lod <= 1 || (chunk_w <= 10 && chunk_h <= 8)) {
							activeGrid = pipe.lodGrids.lod1;
						} else if (lod === 2 || (chunk_w <= 40 && chunk_h <= 32)) {
							activeGrid = pipe.lodGrids.lod2;
						}
					}

					gl.bindBuffer(gl.ARRAY_BUFFER, activeGrid.gridVbo || activeGrid.vbo);
					gl.enableVertexAttribArray(pipe.aPos);
					gl.vertexAttribPointer(pipe.aPos, 3, gl.FLOAT, false, 12, 0);

					gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, activeGrid.gridIbo || activeGrid.ibo);
					gl.drawElements(gl.TRIANGLES, activeGrid.indexCount, gl.UNSIGNED_SHORT, 0);

					const actualWaterPtr = (water_ptr !== undefined && water_ptr !== null) ? (water_ptr >>> 0) : ((sea_level > -900.0) ? (color_ptr >>> 0) : 0);
					if (actualWaterPtr > 0 && sea_level > -900.0) {
						if (!t.pendingWater) t.pendingWater = [];
						t.pendingWater.push({
							chunkEntry,
							anim_time: (cp && cp.length >= 8) ? cp[7] : 0.0,
							water_ptr: actualWaterPtr,
							height_ptr,
							chunk_w,
							chunk_h,
							ox,
							oy,
							spanX,
							spanY,
							sea_level,
							mvp,
							eye,
							dist
						});
					}
				},
				lam_billboard_batch_flush(billboard_ptr, count, atlas_ptr) {
					if (!billboard_ptr || count <= 0) return;
					const canvasEl = findStageCanvas();
					if (!canvasEl) return;
					const t = targetFor(canvasEl);
					const gl = t.gl;
					if (!gl || !t.billboardPipe) return;

					const exports = getWasm();
					if (!exports || !exports.memory) return;

					const uptr = billboard_ptr >>> 0;
					const stride = 40;
					const byteLen = count * stride;
					if (uptr + byteLen > exports.memory.buffer.byteLength) return;

					const pipe = t.billboardPipe;
					if (!pipe.atlasLoaded && atlas_ptr) {
						const aPtr = atlas_ptr >>> 0;
						if (aPtr && aPtr + 512 * 328 * 4 <= exports.memory.buffer.byteLength) {
							const rgba = new Uint8Array(exports.memory.buffer, aPtr, 512 * 328 * 4);
							gl.activeTexture(gl.TEXTURE0);
							gl.bindTexture(gl.TEXTURE_2D, pipe.atlasTex);
							gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 512, 328, 0, gl.RGBA, gl.UNSIGNED_BYTE, rgba);
							pipe.atlasLoaded = true;
						}
					}

					const mvp = computeMVP(canvasEl.width, canvasEl.height);
					const eye = mvp.eye;

					const f32 = new Float32Array(exports.memory.buffer, uptr, count * 10);
					const u32 = new Uint32Array(exports.memory.buffer, uptr, count * 10);

					const vertData = new Float32Array(count * 54);
					let vi = 0;

					for (let i = 0; i < count; i++) {
						const base = i * 10;
						const x = f32[base + 0];
						const y = f32[base + 1];
						const z = f32[base + 2] + 0.15;
						const w = f32[base + 3];
						const h = f32[base + 4];
						const u0 = f32[base + 5];
						const v0 = f32[base + 6];
						const u1 = f32[base + 7];
						const v1 = f32[base + 8];
						const tint = u32[base + 9];

						const tr = (tint & 0xFF) / 255.0;
						const tg = ((tint >> 8) & 0xFF) / 255.0;
						const tb = ((tint >> 16) & 0xFF) / 255.0;
						const ta = ((tint >> 24) & 0xFF) / 255.0;

						const hw = w * 0.5;
						const h3D = h;
						const baseZ = z;

						vertData[vi++] = x - hw; vertData[vi++] = y; vertData[vi++] = baseZ;
						vertData[vi++] = u0; vertData[vi++] = v1;
						vertData[vi++] = tr; vertData[vi++] = tg; vertData[vi++] = tb; vertData[vi++] = ta;

						vertData[vi++] = x + hw; vertData[vi++] = y; vertData[vi++] = baseZ;
						vertData[vi++] = u1; vertData[vi++] = v1;
						vertData[vi++] = tr; vertData[vi++] = tg; vertData[vi++] = tb; vertData[vi++] = ta;

						vertData[vi++] = x + hw; vertData[vi++] = y; vertData[vi++] = baseZ + h3D;
						vertData[vi++] = u1; vertData[vi++] = v0;
						vertData[vi++] = tr; vertData[vi++] = tg; vertData[vi++] = tb; vertData[vi++] = ta;

						vertData[vi++] = x - hw; vertData[vi++] = y; vertData[vi++] = baseZ;
						vertData[vi++] = u0; vertData[vi++] = v1;
						vertData[vi++] = tr; vertData[vi++] = tg; vertData[vi++] = tb; vertData[vi++] = ta;

						vertData[vi++] = x + hw; vertData[vi++] = y; vertData[vi++] = baseZ + h3D;
						vertData[vi++] = u1; vertData[vi++] = v0;
						vertData[vi++] = tr; vertData[vi++] = tg; vertData[vi++] = tb; vertData[vi++] = ta;

						vertData[vi++] = x - hw; vertData[vi++] = y; vertData[vi++] = baseZ + h3D;
						vertData[vi++] = u0; vertData[vi++] = v0;
						vertData[vi++] = tr; vertData[vi++] = tg; vertData[vi++] = tb; vertData[vi++] = ta;
					}

					gl.useProgram(pipe.prog);
					gl.uniformMatrix4fv(pipe.uMVP, false, mvp);
					if (pipe.uPlanetCenter && t.lastPlanetCenter) gl.uniform3fv(pipe.uPlanetCenter, t.lastPlanetCenter);
					if (pipe.uTorusRadii && t.lastTorusRadii) gl.uniform2fv(pipe.uTorusRadii, t.lastTorusRadii);
					if (pipe.uWorldDim && t.lastWorldDim) gl.uniform2fv(pipe.uWorldDim, t.lastWorldDim);
					if (pipe.uSkyColor && t.lastSky) gl.uniform3f(pipe.uSkyColor, t.lastSky[0], t.lastSky[1], t.lastSky[2]);
					if (pipe.uAtmoIntensity) gl.uniform1f(pipe.uAtmoIntensity, (t.lastAtmoIntensity !== undefined) ? t.lastAtmoIntensity : 0.0);
					if (pipe.uSeaLevel !== null) gl.uniform1f(pipe.uSeaLevel, (t.lastSeaLevel !== undefined) ? t.lastSeaLevel : -999.0);
					if (pipe.uAnimTime !== null) gl.uniform1f(pipe.uAnimTime, t.lastAnimTime || 0.0);
					if (pipe.uCameraPos && (eye || t.lastEye)) gl.uniform3fv(pipe.uCameraPos, eye || t.lastEye);
					if (pipe.uFogStart !== null) gl.uniform1f(pipe.uFogStart, (t.lastDist || 280.0) * 1.5);
					if (pipe.uFogEnd !== null) gl.uniform1f(pipe.uFogEnd, (t.lastDist || 280.0) * 4.0);

					gl.activeTexture(gl.TEXTURE0);
					gl.bindTexture(gl.TEXTURE_2D, pipe.atlasTex);
					gl.uniform1i(pipe.uAtlasTex, 0);

					gl.enable(gl.DEPTH_TEST);
					gl.depthFunc(gl.LEQUAL);
					gl.enable(gl.POLYGON_OFFSET_FILL);
					gl.polygonOffset(-2.0, -4.0);
					gl.enable(gl.BLEND);
					gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

					gl.bindBuffer(gl.ARRAY_BUFFER, pipe.vbo);
					gl.bufferData(gl.ARRAY_BUFFER, vertData, gl.DYNAMIC_DRAW);

					const strideBytes = 36;
					gl.enableVertexAttribArray(pipe.aPos);
					gl.vertexAttribPointer(pipe.aPos, 3, gl.FLOAT, false, strideBytes, 0);

					gl.enableVertexAttribArray(pipe.aUv);
					gl.vertexAttribPointer(pipe.aUv, 2, gl.FLOAT, false, strideBytes, 12);

					gl.enableVertexAttribArray(pipe.aTint);
					gl.vertexAttribPointer(pipe.aTint, 4, gl.FLOAT, false, strideBytes, 20);

					gl.drawArrays(gl.TRIANGLES, 0, count * 6);
					gl.disable(gl.POLYGON_OFFSET_FILL);
					gl.disableVertexAttribArray(pipe.aTint);
				},
				lam_fill() {},
				lam_tile_draw() {},
				lam_flush() {}
			};
		},
		destroy() {
			// Targets are keyed weakly by element; nothing to release.
		}
	};
}

export const SHADERS = {
	VS_WATER_3D,
	FS_WATER_3D,
	VS_TERRAIN_3D,
	FS_TERRAIN_3D,
	VS_BILLBOARD_3D,
	FS_BILLBOARD_3D
};

if (typeof window !== 'undefined') {
	window.__wasm_capability_registry = window.__wasm_capability_registry || {};
	window.__wasm_capability_registry.lamina = (root, getWasm) => {
		const b = createLaminaBridge(root, getWasm);
		return {
			mount() { b.mount(); },
			destroy() { b.destroy(); },
			makeImports() { return b.makeLaminaImports(); }
		};
	};
}
