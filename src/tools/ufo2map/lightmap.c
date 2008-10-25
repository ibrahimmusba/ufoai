/**
 * @file lightmap.c
 */

/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "lighting.h"
#include "../../common/tracing.h"

#define	sun_pitch			config.sun_pitch[config.compile_for_day]
#define	sun_yaw				config.sun_yaw[config.compile_for_day]
#define	sun_dir				config.sun_dir[config.compile_for_day]
#define	sun_color			config.sun_color[config.compile_for_day]
#define	sun_intensity		config.sun_intensity[config.compile_for_day]
#define	sun_ambient_color	config.sun_ambient_color[config.compile_for_day]

vec3_t face_offset[MAX_MAP_FACES];		/**< for rotating bmodels */

/** @brief lightinfo is a temporary bucket for lighting calculations */
typedef struct {
	vec_t	facedist;
	vec3_t	facenormal;

	int		numsurfpt;
	vec3_t	*surfpt;

	vec3_t	modelorg;		/**< for origined bmodels */

	vec3_t	texorg;
	vec3_t	worldtotex[2];	/**< s = (world - texorg) . worldtotex[0] */
	vec3_t	textoworld[2];	/**< world = texorg + s * textoworld[0] */

	int		texmins[2], texsize[2];
	dBspFace_t	*face;
} lightinfo_t;

/** @brief face extents */
typedef struct extents_s {
	vec3_t mins, maxs;
	vec3_t center;
	vec2_t stmins, stmaxs;
} extents_t;

static extents_t face_extents[MAX_MAP_FACES];

/**
 * @brief Populates face_extents for all dBspSurface_t, prior to light creation.
 * This is done so that sample positions may be nudged outward along
 * the face normal and towards the face center to help with traces.
 */
static void BuildFaceExtents (void)
{
	int k;

	for (k = 0; k < curTile->numfaces; k++) {
		const dBspFace_t *s = &curTile->faces[k];
		const dBspTexinfo_t *tex = &curTile->texinfo[s->texinfo];

		float *mins = face_extents[s - curTile->faces].mins;
		float *maxs = face_extents[s - curTile->faces].maxs;

		float *center = face_extents[s - curTile->faces].center;

		float *stmins = face_extents[s - curTile->faces].stmins;
		float *stmaxs = face_extents[s - curTile->faces].stmaxs;
		int i;

		VectorSet(mins, 999999, 999999, 999999);
		VectorSet(maxs, -999999, -999999, -999999);

		stmins[0] = stmins[1] = 999999;
		stmaxs[0] = stmaxs[1] = -999999;

		for (i = 0; i < s->numedges; i++) {
			const int e = curTile->surfedges[s->firstedge + i];
			const dBspVertex_t *v;
			int j;

			if (e >= 0)
				v = curTile->vertexes + curTile->edges[e].v[0];
			else
				v = curTile->vertexes + curTile->edges[-e].v[1];

			for (j = 0; j < 3; j++) {  /* calculate mins, maxs */
				if (v->point[j] > maxs[j])
					maxs[j] = v->point[j];
				if (v->point[j] < mins[j])
					mins[j] = v->point[j];
			}

			for (j = 0; j < 2; j++) {  /* calculate stmins, stmaxs */
				const float val = DotProduct(v->point, tex->vecs[j]) + tex->vecs[j][3];
				if (val < stmins[j])
					stmins[j] = val;
				if (val > stmaxs[j])
					stmaxs[j] = val;
			}
		}

		for (i = 0; i < 3; i++)
			center[i] = (mins[i] + maxs[i]) / 2.0f;
	}
}

/**
 * @sa BuildFaceExtents
 */
static void CalcFaceExtents (lightinfo_t *l)
{
	const dBspFace_t *s;
	float *mins, *maxs;
	float *stmins, *stmaxs;
	vec2_t lm_mins, lm_maxs;
	int i;

	s = l->face;

	mins = face_extents[s - curTile->faces].mins;
	maxs = face_extents[s - curTile->faces].maxs;
	stmins = face_extents[s - curTile->faces].stmins;
	stmaxs = face_extents[s - curTile->faces].stmaxs;

	for (i = 0; i < 2; i++) {
		lm_mins[i] = floor(stmins[i] / (1 << config.lightquant));
		lm_maxs[i] = ceil(stmaxs[i] / (1 << config.lightquant));

		l->texmins[i] = lm_mins[i];
		l->texsize[i] = lm_maxs[i] - lm_mins[i];
	}

	if (l->texsize[0] * l->texsize[1] > MAX_MAP_LIGHTMAP)
		Sys_Error("Surface too large to light (%dx%d)", l->texsize[0], l->texsize[1]);
}

/**
 * @brief Fills in texorg, worldtotex. and textoworld
 */
static void CalcFaceVectors (lightinfo_t *l)
{
	const dBspTexinfo_t *tex;
	int i;
	vec3_t texnormal;
	vec_t distscale, dist;

	tex = &curTile->texinfo[l->face->texinfo];

	/* convert from float to double */
	for (i = 0; i < 2; i++)
		VectorCopy(tex->vecs[i], l->worldtotex[i]);

	/* calculate a normal to the texture axis.  points can be moved along this
	 * without changing their S/T */
	texnormal[0] = tex->vecs[1][1] * tex->vecs[0][2]
					- tex->vecs[1][2] * tex->vecs[0][1];
	texnormal[1] = tex->vecs[1][2] * tex->vecs[0][0]
					- tex->vecs[1][0] * tex->vecs[0][2];
	texnormal[2] = tex->vecs[1][0] * tex->vecs[0][1]
					- tex->vecs[1][1] * tex->vecs[0][0];
	VectorNormalize(texnormal);

	/* flip it towards plane normal */
	distscale = DotProduct(texnormal, l->facenormal);
	if (!distscale) {
		Verb_Printf(VERB_EXTRA, "WARNING: Texture axis perpendicular to face\n");
		distscale = 1.0;
	}
	if (distscale < 0.0) {
		distscale = -distscale;
		VectorSubtract(vec3_origin, texnormal, texnormal);
	}

	/* distscale is the ratio of the distance along the texture normal to
	 * the distance along the plane normal */
	distscale = 1.0 / distscale;

	for (i = 0; i < 2; i++) {
		const vec_t len = VectorLength(l->worldtotex[i]);
		const vec_t distance = DotProduct(l->worldtotex[i], l->facenormal) * distscale;
		VectorMA(l->worldtotex[i], -distance, texnormal, l->textoworld[i]);
		VectorScale(l->textoworld[i], (1.0f / len) * (1.0f / len), l->textoworld[i]);
	}

	/* calculate texorg on the texture plane */
	for (i = 0; i < 3; i++)
		l->texorg[i] =
			-tex->vecs[0][3] * l->textoworld[0][i] -
			tex->vecs[1][3] * l->textoworld[1][i];

	/* project back to the face plane */
	dist = DotProduct(l->texorg, l->facenormal) - l->facedist - 1;
	dist *= distscale;
	VectorMA(l->texorg, -dist, texnormal, l->texorg);

	/* compensate for org'd bmodels */
	VectorAdd(l->texorg, l->modelorg, l->texorg);

	/* total sample count */
	l->numsurfpt = (l->texsize[0] + 1) * (l->texsize[1] + 1);
	l->surfpt = malloc(l->numsurfpt * sizeof(vec3_t));
	if (!l->surfpt)
		Sys_Error("Surface too large to light ("UFO_SIZE_T")", l->numsurfpt * sizeof(*l->surfpt));
}

/**
 * @brief For each texture aligned grid point, back project onto the plane
 * to get the world xyz value of the sample point
 */
static void CalcPoints (lightinfo_t *l, float sofs, float tofs)
{
	int s, t, j;
	int w, h, step;
	vec_t starts, startt;
	vec_t *surf;
	vec3_t pos;
	const dBspLeaf_t *leaf;

	/* fill in surforg
	 * the points are biased towards the center of the surfaces
	 * to help avoid edge cases just inside walls */
	surf = l->surfpt[0];

	h = l->texsize[1] + 1;
	w = l->texsize[0] + 1;

	step = 1 << config.lightquant;
	starts = l->texmins[0] * step;
	startt = l->texmins[1] * step;

	for (t = 0; t < h; t++) {
		for (s = 0; s < w; s++, surf += 3) {
			const vec_t us = starts + (s + sofs) * step;
			const vec_t ut = startt + (t + tofs) * step;

			/* calculate texture point */
			for (j = 0; j < 3; j++)
				surf[j] = l->texorg[j] + l->textoworld[0][j] * us +
						l->textoworld[1][j] * ut;

			VectorMA(surf, 2, l->facenormal, pos);
			leaf = Light_PointInLeaf(pos);
			if (leaf->contentFlags == CONTENTS_SOLID)
				continue;
		}
	}
}

typedef struct {
	int numsamples;
	float *origins;
	float *samples;
	float *directions;
} facelight_t;

static facelight_t facelight[LIGHTMAP_MAX][MAX_MAP_FACES];

typedef enum {
	emit_surface,
	emit_point,
	emit_spotlight
} emittype_t;

typedef struct directlight_s {
	struct directlight_s *next;
	emittype_t	type;

	float		intensity;
	vec3_t		origin;
	vec3_t		color;
	vec3_t		normal;		/**< for surfaces and spotlights */
	float		stopdot;	/**< for spotlights */
} directlight_t;

static directlight_t *directlights[2];
static int numdlights[2];

#define	DIRECT_LIGHT	3

/**
 * @brief Create directlights out of patches and lights
 * @sa LightWorld
 */
void CreateDirectLights (void)
{
	int i;
	directlight_t *dl;
	const dBspLeaf_t *leaf;

	/* surfaces */
	for (i = 0; i < num_patches; i++) {
		patch_t *p = &patches[i];

		if (p->totallight[0] < DIRECT_LIGHT &&
				p->totallight[1] < DIRECT_LIGHT &&
				p->totallight[2] < DIRECT_LIGHT)
			continue;

		numdlights[config.compile_for_day]++;
		dl = malloc(sizeof(*dl));
		memset(dl, 0, sizeof(*dl));

		VectorCopy(p->origin, dl->origin);

		leaf = Light_PointInLeaf(dl->origin);
		dl->next = directlights[config.compile_for_day];
		directlights[config.compile_for_day] = dl;

		dl->type = emit_surface;
		VectorCopy(p->plane->normal, dl->normal);

		dl->intensity = ColorNormalize(p->totallight, dl->color);
		dl->intensity *= p->area * config.direct_scale;
		VectorClear(p->totallight);	/* all sent now */
	}

	/* entities (skip the world) */
	for (i = 1; i < num_entities; i++) {
		float intensity;
		const char *color;
		const char *target;
		const entity_t *e = &entities[i];
		const char *name = ValueForKey(e, "classname");
		if (strncmp(name, "light", 5))
			continue;

		/* remove those lights that are only for the night version */
		if (config.compile_for_day) {
			const int spawnflags = atoi(ValueForKey(e, "spawnflags"));
			if (!(spawnflags & 1))	/* day */
				continue;
		}

		numdlights[config.compile_for_day]++;
		dl = malloc(sizeof(*dl));
		memset(dl, 0, sizeof(*dl));

		GetVectorForKey(e, "origin", dl->origin);

		/* link in */
		dl->next = directlights[config.compile_for_day];
		directlights[config.compile_for_day] = dl;

		intensity = FloatForKey(e, "light");
		if (!intensity)
			intensity = 300;
		color = ValueForKey(e, "_color");
		if (color && color[0]){
			sscanf(color, "%f %f %f", &dl->color[0], &dl->color[1], &dl->color[2]);
			ColorNormalize(dl->color, dl->color);
		} else
			dl->color[0] = dl->color[1] = dl->color[2] = 1.0;
		dl->intensity = intensity * config.entity_scale;
		dl->type = emit_point;

		target = ValueForKey(e, "target");
		if (!strcmp(name, "light_spot") || target[0]) {
			dl->type = emit_spotlight;
			dl->stopdot = FloatForKey(e, "_cone");
			if (!dl->stopdot)
				dl->stopdot = 10;
			dl->stopdot = cos(dl->stopdot / 180.0f * M_PI);
			if (target[0]) {	/* point towards target */
				entity_t *e2 = FindTargetEntity(target);
				if (!e2)
					Com_Printf("WARNING: light at (%i %i %i) has missing target '%s' - e.g. create an info_null that has a 'targetname' set to '%s'\n",
						(int)dl->origin[0], (int)dl->origin[1], (int)dl->origin[2], target, target);
				else {
					vec3_t dest;
					GetVectorForKey(e2, "origin", dest);
					VectorSubtract(dest, dl->origin, dl->normal);
					VectorNormalize(dl->normal);
				}
			} else {	/* point down angle */
				const float angle = FloatForKey(e, "angle");
				if (angle == ANGLE_UP) {
					dl->normal[0] = dl->normal[1] = 0;
					dl->normal[2] = 1;
				} else if (angle == ANGLE_DOWN) {
					dl->normal[0] = dl->normal[1] = 0;
					dl->normal[2] = -1;
				} else {
					dl->normal[2] = 0;
					dl->normal[0] = cos(angle / 180.0f * M_PI);
					dl->normal[1] = sin(angle / 180.0f * M_PI);
				}
			}
		}
	}

	/* handle worldspawn light settings */
	{
		const entity_t *e = &entities[0];
		const char *ambient, *light, *angles, *color;

		if (config.compile_for_day) {
			ambient = ValueForKey(e, "ambient_day");
			light = ValueForKey(e, "light_day");
			angles = ValueForKey(e, "angles_day");
			color = ValueForKey(e, "color_day");
		} else {
			ambient = ValueForKey(e, "ambient_night");
			light = ValueForKey(e, "light_night");
			angles = ValueForKey(e, "angles_night");
			color = ValueForKey(e, "color_night");
		}

		if (light[0] != '\0')
			sun_intensity = atoi(light);

		if (angles[0] != '\0') {
			vec3_t vector;
			GetVectorFromString(angles, vector);
			sun_pitch = vector[PITCH] * torad;
			sun_yaw = vector[YAW] * torad;
			VectorSet(sun_dir, cos(sun_yaw) * sin(sun_pitch),
				sin(sun_yaw) * sin(sun_pitch), cos(sun_pitch));
		}

		if (color[0] != '\0') {
			GetVectorFromString(color, sun_color);
			ColorNormalize(sun_color, sun_color);
		}

		if (ambient[0] != '\0') {
			GetVectorFromString(ambient, sun_ambient_color);
			VectorScale(sun_ambient_color, 128.0f, sun_ambient_color);
		}
	}

	Verb_Printf(VERB_EXTRA, "light settings:\n * intensity: %i\n * sun_angles: pitch %f yaw %f\n * sun_color: %f:%f:%f\n * sun_ambient_color: %f:%f:%f\n",
		sun_intensity, sun_pitch, sun_yaw, sun_color[0], sun_color[1], sun_color[2], sun_ambient_color[0], sun_ambient_color[1], sun_ambient_color[2]);
	Verb_Printf(VERB_NORMAL, "%i direct lights for %s lightmap\n", numdlights[config.compile_for_day], (config.compile_for_day ? "day" : "night"));
}

/**
 * @brief A follow-up to GatherSampleLight, simply trace along the sun normal, adding sunlight
 */
static void GatherSampleSunlight (const vec3_t pos, const vec3_t normal, float *sample, float *direction, float scale)
{
	vec3_t delta, light;
	float dot;

	/* add sun light */
	if (!sun_intensity)
		return;

	dot = DotProduct(sun_dir, normal);
	if (dot <= 0.001)
		return; /* wrong direction */

	/* don't use only 512 (which would be the 8 level max unit) but a
	 * higher value - because the light angle is not fixed at 90 degree */
	VectorMA(pos, 8192, sun_dir, delta);

	if (TR_TestLineSingleTile(pos, delta))
		return; /* occluded */

	/* add some light to it */
	VectorScale(sun_color, sun_intensity * dot * scale, light);
	VectorAdd(sample, light, sample);

	/* and accumulate the direction */
	VectorMA(direction, VectorLength(light) * scale, sun_dir, direction);
}


/**
 * @param[in] scale is the normalizer for multisampling
 */
static void GatherSampleLight (vec3_t pos, const vec3_t normal, float *sample, float *direction, float scale)
{
	directlight_t *l;
	vec3_t delta;
	float dot, dot2;
	float dist, halfDist;
	float light, dir;

	for (l = directlights[config.compile_for_day]; l; l = l->next) {
		light = dir = 0.0;

		VectorSubtract(l->origin, pos, delta);
		dist = VectorNormalize(delta);
		halfDist = dist * 0.5;

		dot = DotProduct(delta, normal);
		if (dot <= 0.001)
			continue;	/* behind sample surface */

		switch (l->type) {
		case emit_point:
			/* linear falloff */
			light = (l->intensity - dist) * dot;
			dir = (l->intensity - halfDist) * dot;
			break;

		case emit_surface:
			/* exponential falloff */
			dot2 = -DotProduct(delta, l->normal);
			if (dot2 <= 0.001)
				continue;	/* behind light surface */
			light = (l->intensity / (dist * dist)) * dot * dot2;
			dir = (l->intensity / (halfDist * halfDist)) * dot * dot2;
			break;

		case emit_spotlight:
			/* linear falloff with cone */
			dot2 = -DotProduct(delta, l->normal);
			if (dot2 <= l->stopdot)
				continue;	/* outside light cone */
			light = (l->intensity - dist) * dot;
			dir = (l->intensity - halfDist) * dot;
			break;
		default:
			Sys_Error("Bad l->type");
		}

		if (light < 0.0)  /* clamp light */
			light = 0.0;

		if (dir < 0.0)  /* and direction */
			dir = 0.0;

		if (light == 0.0 && dir == 0.0)
			continue;

		if (TR_TestLineSingleTile(pos, l->origin))
			continue;	/* occluded */

		light *= scale;
		dir *= scale;

		/* add some light to it */
		VectorMA(sample, light, l->color, sample);

		VectorMA(direction, dir, delta, direction);
		VectorMA(direction, scale / dir, normal, direction);
	}

	GatherSampleSunlight(pos, normal, sample, direction, scale);
}

#define SAMPLE_NUDGE 0.25

/**
 * @brief Move the incoming sample position towards the surface center and along the
 * surface normal to reduce false-positive traces.
 */
static inline void NudgeSamplePosition (const vec3_t in, const vec3_t normal, const vec3_t center, vec3_t out)
{
	vec3_t dir;

	VectorCopy(in, out);

	/* move into the level using the normal and surface center */
	VectorSubtract(out, center, dir);
	VectorNormalize(dir);

	VectorMA(out, SAMPLE_NUDGE, dir, out);
	VectorMA(out, SAMPLE_NUDGE, normal, out);
}

#define MAX_VERT_FACES 256

/**
 * @brief Populate faces with indexes of all dBspFace_t's referencing the specified edge.
 * @param[out] nfaces The number of dBspFace_t's referencing edge
 * @param[out] faces
 * @param[in] vert
 */
static void FacesWithVert (int vert, int *faces, int *nfaces)
{
	int i, j, k;

	k = 0;
	for (i = 0; i < curTile->numfaces; i++) {
		const dBspFace_t *face = &curTile->faces[i];

		if (!(curTile->texinfo[face->texinfo].surfaceFlags & SURF_PHONG))
			continue;

		for (j = 0; j < face->numedges; j++) {
			const int e = curTile->surfedges[face->firstedge + j];
			const int v = e >= 0 ? curTile->edges[e].v[0] : curTile->edges[-e].v[1];

			/* face references vert */
			if (v == vert) {
				faces[k++] = i;
				if (k == MAX_VERT_FACES)
					Sys_Error("MAX_VERT_FACES");
				break;
			}
		}
	}
	*nfaces = k;
}

/**
 * @brief Calculate per-vertex (instead of per-plane) normal vectors. This is done by
 * finding all of the faces which use a given vertex, and calculating an average
 * of their normal vectors.
 */
void BuildVertexNormals (void)
{
	int vert_faces[MAX_VERT_FACES];
	int num_vert_faces;
	vec3_t norm, delta;
	float scale;
	int i, j;

	BuildFaceExtents();

	for (i = 0; i < curTile->numvertexes; i++) {
		VectorClear(curTile->normals[i].normal);

		FacesWithVert(i, vert_faces, &num_vert_faces);
		if (!num_vert_faces)  /* rely on plane normal only */
			continue;

		for (j = 0; j < num_vert_faces; j++) {
			const dBspFace_t *face = &curTile->faces[vert_faces[j]];
			const dBspPlane_t *plane = &curTile->planes[face->planenum];

			/* scale the contribution of each face based on size */
			VectorSubtract(face_extents[vert_faces[j]].maxs, face_extents[vert_faces[j]].mins, delta);
			scale = VectorLength(delta);

 			if (face->side)
				VectorScale(plane->normal, -scale, norm);
			else
				VectorScale(plane->normal, scale, norm);

			VectorAdd(curTile->normals[i].normal, norm, curTile->normals[i].normal);
		}
		VectorNormalize(curTile->normals[i].normal);
	}
}


/**
 * @brief For Phong-shaded samples, interpolate the vertex normals for the surface in
 * question, weighting them according to their proximity to the sample position.
 */
static void SampleNormal (const lightinfo_t *l, const vec3_t pos, vec3_t normal)
{
	vec3_t temp;
	float dist[MAX_VERT_FACES];
	float nearest;
	int i, v, nearv;

	nearest = 9999.0;
	nearv = 0;

	/* calculate the distance to each vertex */
	for (i = 0; i < l->face->numedges; i++) {  /* find nearest and farthest verts */
		const int e = curTile->surfedges[l->face->firstedge + i];
		if (e >= 0)
			v = curTile->edges[e].v[0];
		else
			v = curTile->edges[-e].v[1];

		VectorSubtract(pos, curTile->vertexes[v].point, temp);
		dist[i] = VectorLength(temp);
		if (dist[i] < nearest) {
			nearest = dist[i];
			nearv = v;
		}
	}
	VectorCopy(curTile->normals[nearv].normal, normal);
}


#define MAX_SAMPLES 5
static const float sampleofs[MAX_SAMPLES][2] = {
	{0.0, 0.0}, {-0.25, -0.25}, {0.25, -0.25}, {0.25, 0.25}, {-0.25, 0.25}
};

#define CLAMP_DELUXEMAP_Z 0.5

/**
 * @brief
 * @sa FinalLightFace
 */
void BuildFacelights (unsigned int facenum)
{
	dBspFace_t *face;
	dBspPlane_t *plane;
	dBspTexinfo_t *tex;
	float *center;
	float *sdir, *tdir;
	vec3_t normal, binormal;
	vec4_t tangent;
	lightinfo_t l[MAX_SAMPLES];
	float scale;
	int i, j, numsamples;
	facelight_t *fl;

	if (facenum >= MAX_MAP_FACES) {
		Com_Printf("MAX_MAP_FACES hit\n");
		return;
	}

	face = &curTile->faces[facenum];
	plane = &curTile->planes[face->planenum];
	tex = &curTile->texinfo[face->texinfo];

	if (tex->surfaceFlags & SURF_WARP)
		return;		/* non-lit texture */

	sdir = tex->vecs[0];
	tdir = tex->vecs[1];

	/* lighting -extra antialiasing */
	if (config.extrasamples)
		numsamples = MAX_SAMPLES;
	else
		numsamples = 1;

	memset(l, 0, sizeof(l));

	scale = 1.0 / numsamples; /* each sample contributes this much */

	for (i = 0; i < numsamples; i++) {
		l[i].face = face;
		l[i].facedist = plane->dist;
		VectorCopy(plane->normal, l[i].facenormal);
		/* negate the normal and dist */
		if (face->side) {
			VectorNegate(l[i].facenormal, l[i].facenormal);
			l[i].facedist = -l[i].facedist;
		}

		/* get the origin offset for rotating bmodels */
		VectorCopy(face_offset[facenum], l[i].modelorg);

		/* calculate lightmap texture mins and maxs */
		CalcFaceExtents(&l[i]);

		/* and the lightmap texture vectors */
		CalcFaceVectors(&l[i]);

		/* now generate all of the sample points */
		CalcPoints(&l[i], sampleofs[i][0], sampleofs[i][1]);
	}

	fl = &facelight[config.compile_for_day][facenum];
	fl->numsamples = l[0].numsurfpt;

	fl->origins = malloc(fl->numsamples * sizeof(vec3_t));
	memcpy(fl->origins, l[0].surfpt, fl->numsamples * sizeof(vec3_t));
	fl->samples = malloc(fl->numsamples * sizeof(vec3_t));
	memset(fl->samples, 0, fl->numsamples * sizeof(vec3_t));
	fl->directions = malloc(fl->numsamples * sizeof(vec3_t));
	memset(fl->directions, 0, fl->numsamples * sizeof(vec3_t));

	center = face_extents[facenum].center;  /* center of the face */

	/* get the light samples */
	for (i = 0; i < l[0].numsurfpt; i++) {
		float *sample = fl->samples + i * 3;			/* accumulate lighting here */
		float *direction = fl->directions + i * 3;		/* accumulate direction here */

		for (j = 0; j < numsamples; j++) {  /* with antialiasing */
			vec3_t pos;

			if (tex->surfaceFlags & SURF_PHONG)
				/* interpolated normal */
				SampleNormal(&l[0], l[j].surfpt[i], normal);
			else
				/* or just plane normal */
				VectorCopy(l[0].facenormal, normal);

			NudgeSamplePosition(l[j].surfpt[i], normal, center, pos);

			GatherSampleLight(pos, normal, sample, direction, scale);
		}
		if (!VectorCompare(direction, vec3_origin)) {
			vec3_t dir;

			/* normalize it */
			VectorNormalize(direction);

			/* finalize the lighting direction for the sample */
			TangentVectors(normal, sdir, tdir, tangent, binormal);

			dir[0] = DotProduct(direction, tangent);
			dir[1] = DotProduct(direction, binormal);
			dir[2] = DotProduct(direction, normal);

			VectorCopy(dir, direction);

			if (direction[2] < CLAMP_DELUXEMAP_Z) {  /* clamp it */
				direction[2] = CLAMP_DELUXEMAP_Z;
				VectorNormalize(direction);
			}
		}
	}

	for (i = 0; i < l[0].numsurfpt; i++) {  /* pad them */
		float *direction = fl->directions + i * 3;
		if (VectorCompare(direction, vec3_origin))
			VectorSet(direction, 0.0, 0.0, 1.0);
	}

	/* free the sample positions for the face */
	for (i = 0; i < numsamples; i++)
		free(l[i].surfpt);

	assert(face_patches[facenum]);
	/* the light from DIRECT_LIGHTS is sent out, but the
	 * texture itself should still be full bright */
	if (face_patches[facenum]->baselight[0] >= DIRECT_LIGHT ||
		face_patches[facenum]->baselight[1] >= DIRECT_LIGHT ||
		face_patches[facenum]->baselight[2] >= DIRECT_LIGHT) {
		float *sample = fl->samples;
		for (i = 0; i < l[0].numsurfpt; i++, sample += 3)
			VectorAdd(sample, face_patches[facenum]->baselight, sample);
	}
}


/**
 * @brief Add the indirect lighting on top of the direct
 * lighting and save into final map format
 * @sa BuildFacelights
 */
void FinalLightFace (unsigned int facenum)
{
	dBspFace_t *f;
	int j, k;
	vec3_t dir;
	facelight_t	*fl;
	float max, newmax;
	byte *dest;

	f = &curTile->faces[facenum];
	fl = &facelight[config.compile_for_day][facenum];

	/* none-lit texture */
	if (curTile->texinfo[f->texinfo].surfaceFlags & SURF_WARP)
		return;

	ThreadLock();

	f->lightofs[config.compile_for_day] = curTile->lightdatasize[config.compile_for_day];
	curTile->lightdatasize[config.compile_for_day] += fl->numsamples * 3;
	/* account for light direction data as well */
	curTile->lightdatasize[config.compile_for_day] += fl->numsamples * 3;

	if (curTile->lightdatasize[config.compile_for_day] > MAX_MAP_LIGHTING)
		Sys_Error("MAX_MAP_LIGHTING (%i exceeded %i) - try to reduce the brush size (%s)",
			curTile->lightdatasize[config.compile_for_day], MAX_MAP_LIGHTING,
			curTile->texinfo[f->texinfo].texture);

	ThreadUnlock();

	/* write it out */
	dest = &curTile->lightdata[config.compile_for_day][f->lightofs[config.compile_for_day]];

	for (j = 0; j < fl->numsamples; j++) {
		vec3_t lb;
		float clamp;

		/* start with raw sample data */
		VectorCopy((fl->samples + j * 3), lb);

		/* add an ambient term if desired */
		VectorAdd(lb, sun_ambient_color, lb);

		/* apply global scale factor */
		VectorScale(lb, config.lightscale, lb);

		max = -999;

		/* find the brightest component */
		for (k = 0; k < 3; k++) {
			/* enforcing some minumum */
			if (lb[k] < 1.0)
				lb[k] = 1.0;

			if (lb[k] > max)
				max = lb[k];
		}

		newmax = max;

		if (newmax > config.maxlight)
			newmax = config.maxlight;

		clamp = newmax / max;

		/* write the lightmap sample data */
		for (k = 0; k < 3; k++) {
			*dest++ = (byte)(lb[k] * clamp);
		}

		/* also write the directional data */
		VectorCopy((fl->directions + j * 3), dir);
		for (k = 0; k < 3; k++)
			*dest++ = (byte)((dir[k] + 1.0f) * 127.0f);
	}
}
