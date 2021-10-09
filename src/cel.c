#include <global.h>

/* Segment pointing to the vertex array */
#define CEL_SEG_VTX 8

/* Vtx * pointer to the start of the vertex array segment */
#define CEL_VTX ((Vtx *) (CEL_SEG_VTX << 24))

/* This is the combine mode used for cel shading;
 * output primitive color, output shade alpha */
#define G_CC_PRIMITIVE_SHADEA 0, 0, 0, PRIMITIVE, 0, 0, 0, SHADE

/* Combine mode to be used for cel shading with textures;
 * multiply texture color with primitive color, output shade alpha */
#define G_CC_MODULATE_PRIM_SHADEA TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, SHADE

/* Base render mode */
#define G_RM_CEL_BASE1 \
	( \
		Z_CMP | Z_UPD | ZMODE_OPA | CVG_DST_CLAMP | FORCE_BL \
		| GBL_c1(G_BL_CLR_IN, G_BL_0, G_BL_CLR_IN, G_BL_1) \
	)
#define G_RM_CEL_BASE2 \
	( \
		Z_CMP | Z_UPD | ZMODE_OPA | CVG_DST_CLAMP | FORCE_BL \
		| GBL_c2(G_BL_CLR_IN, G_BL_0, G_BL_CLR_IN, G_BL_1) \
	)

/* Shading render mode */
#define G_RM_CEL_SHADE1 \
	( \
		Z_CMP | ZMODE_DEC | CVG_DST_CLAMP | FORCE_BL \
		| GBL_c1(G_BL_CLR_IN, G_BL_0, G_BL_CLR_IN, G_BL_1) \
	)
#define G_RM_CEL_SHADE2 \
	( \
		Z_CMP | ZMODE_DEC | CVG_DST_CLAMP | FORCE_BL \
		| GBL_c2(G_BL_CLR_IN, G_BL_0, G_BL_CLR_IN, G_BL_1) \
	)

/*
 * struct cel_layer
 *
 * Describes one shading layer of a cel-shaded object.
 */
struct cel_layer
{
	/* Light level threshold */
	unsigned char	thresh;

	/* Shading color */
	unsigned char	r;
	unsigned char	g;
	unsigned char	b;
};

/*
 * struct cel_object
 *
 * Describes a cel-shaded object.
 */
struct cel_object
{
	/* Material color, used for lighting calculations. This is the color of
	 * light that the object will respond to. */
	unsigned char		mat_r;
	unsigned char		mat_g;
	unsigned char		mat_b;

	/* Base color */
	unsigned char		r;
	unsigned char		g;
	unsigned char		b;

	/* Number of shading layers */
	int			n_layer;
	/* Pointer to array of shading layers */
	struct cel_layer *	layer;

	/* Number of vertices in this object */
	int			n_vtx;
	/* Pointer to vertices */
	Vtx *			vtx;

	/* Pointer to display list */
	Gfx *			gfx;
};

/* Vertex lighting function */
static void light_vertex(GlobalContext *globalCtx, struct cel_object *cel,
				Vtx *vtx, MtxF *mf_mdl)
{
	LightNode *light;
	/* Transformed vertex coordinates */
	float vx;
	float vy;
	float vz;
	/* Transformed vertex normal */
	float nx;
	float ny;
	float nz;
	float nm;
	/* Vertex lighting */
	float vr;
	float vg;
	float vb;
	/* Final vertex light intensity */
	float a;

	/* Compute transformed vertex coordinates */
	vx = vtx->n.ob[0] * mf_mdl->xx
		+ vtx->n.ob[1] * mf_mdl->xy
		+ vtx->n.ob[2] * mf_mdl->xz
		+ mf_mdl->xw;
	vy = vtx->n.ob[0] * mf_mdl->yx
		+ vtx->n.ob[1] * mf_mdl->yy
		+ vtx->n.ob[2] * mf_mdl->yz
		+ mf_mdl->yw;
	vz = vtx->n.ob[0] * mf_mdl->zx
		+ vtx->n.ob[1] * mf_mdl->zy
		+ vtx->n.ob[2] * mf_mdl->zz
		+ mf_mdl->zw;

	/* Compute transformed vertex normal */
	nx = vtx->n.n[0] * mf_mdl->xx
		+ vtx->n.n[1] * mf_mdl->xy
		+ vtx->n.n[2] * mf_mdl->xz;
	ny = vtx->n.n[0] * mf_mdl->yx
		+ vtx->n.n[1] * mf_mdl->yy
		+ vtx->n.n[2] * mf_mdl->yz;
	nz = vtx->n.n[0] * mf_mdl->zx
		+ vtx->n.n[1] * mf_mdl->zy
		+ vtx->n.n[2] * mf_mdl->zz;
	nm = sqrtf(nx * nx + ny * ny + nz * nz);
	nx = nx / nm;
	ny = ny / nm;
	nz = nz / nm;

	/* Start with the scene's ambient lighting */
	vr = globalCtx->lightCtx.ambientColor[0] / 255.f;
	vg = globalCtx->lightCtx.ambientColor[1] / 255.f;
	vb = globalCtx->lightCtx.ambientColor[2] / 255.f;

	/* Process each light */
	light = globalCtx->lightCtx.listHead;
	while (light != NULL)
	{
		LightInfo *info = light->info;

		/* Point lights */
		if (info->type == 0 || info->type == 2)
		{
			LightPoint *p = &info->params.point;
			float dx = p->x - vx;
			float dy = p->y - vy;
			float dz = p->z - vz;
			float dm = sqrtf(dx * dx + dy * dy + dz * dz);

			/* For point lights we consider the distance to the
			 * light in addition to the normal */
			if (dm < p->radius)
			{
				float m;

				dx = dx / dm;
				dy = dy / dm;
				dz = dz / dm;

				m = nx * dx + ny * dy + nz * dz;

				if (m > 0.f)
				{
					/* The intensity decreases with the
					 * distance squared */
					dm = dm / p->radius;
					dm = 1.f - dm * dm;

					m = m * dm;

					vr = vr + p->color[0] / 255.f * m;
					vg = vg + p->color[1] / 255.f * m;
					vb = vb + p->color[2] / 255.f * m;
				}
			}
		}
		/* Directional lights */
		else if (info->type == 1)
		{
			LightDirectional *p = &info->params.dir;
			float dx = p->x / 127.f;
			float dy = p->y / 127.f;
			float dz = p->z / 127.f;
			float m = nx * dx + ny * dy + nz * dz;

			if (m > 0.f)
			{
				vr = vr + p->color[0] / 255.f * m;
				vg = vg + p->color[1] / 255.f * m;
				vb = vb + p->color[2] / 255.f * m;
			}
		}

		light = light->next;
	}

	/* The result is the sum of all lights multiplied by the material color
	 */
	{
		float r = cel->mat_r / 255.f;
		float g = cel->mat_g / 255.f;
		float b = cel->mat_b / 255.f;

		a = (vr * r + vg * g + vb * b) / (r + g + b);
		if (a > 1.f)
		{
			a = 1.f;
		}
	}

	/* Store the light intensity */
	vtx->n.a = a * 255.f;
}

/*
 * cel_draw()
 *
 * Renders a cel-shaded model object.
 *
 * GlobalContext *globalCtx
 * 	Game context.
 * struct cel_object *cel
 * 	Structure describing the object.
 * Gfx *p_gfx_p
 * 	Pointer to the head of the display list to render to.
 * Gfx *p_gfx_p
 * 	Pointer to the tail of the display list to render to.
 * MtxF *mf_mdl
 * 	The modelview matrix describing the world transformation of the model.
 */
void cel_draw(GlobalContext *globalCtx, struct cel_object *cel,
		Gfx **p_gfx_p, Gfx **p_gfx_d, MtxF *mf_mdl)
{
	Gfx *gfx_p;
	Gfx *gfx_d;
	Vtx *vtx;
	int i;

	/* Load gfx pointers */
	gfx_p = *p_gfx_p;
	gfx_d = *p_gfx_d;

	/*
	 * Modifying vertices in place will cause conflicts between the CPU and
	 * the RSP while rendering, resulting in flickering. We have to make a
	 * copy of the vertices to circumvent this issue. This in turn means
	 * that vertices can't be directly addressed by the display list, they
	 * have to be relative to CEL_SEG_VTX which points to the start of the
	 * vertex array.
	 */
	{
		int vtx_size = sizeof(Vtx) * cel->n_vtx;

		/* Allocate and copy vertices */
		gfx_d = (void *) ((char *) gfx_d - vtx_size);
		vtx = (void *) gfx_d;
		memcpy(vtx, cel->vtx, vtx_size);

		/* Set up the vertex segment */
		gSPSegment(gfx_p++, CEL_SEG_VTX, vtx);
	}

	/* Compute the lighting for each vertex and place it in vertex alpha */
	for (i = 0; i < cel->n_vtx; i++)
	{
		light_vertex(globalCtx, cel, &vtx[i], mf_mdl);
	}

	/* Set up modelview matrix */
	gfx_d = (void *)((char *) gfx_d - sizeof(Mtx));
	{
		Mtx *m_mdl = (void *) gfx_d;
		guMtxF2L(mf_mdl, m_mdl);
		gSPMatrix(gfx_p++, m_mdl,
				G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_PUSH);
	}

	/* Shading should always be enabled. Lighting is turned off since we're
	 * doing it manually. */
	gSPLoadGeometryMode(gfx_p++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);

	/* Configure the combiner for cel shading */
	gDPPipeSync(gfx_p++);
	gDPSetCombineMode(gfx_p++, G_CC_PRIMITIVE_SHADEA,
				G_CC_PRIMITIVE_SHADEA);

	/* Set up render mode for base layer */
	gDPSetOtherMode(gfx_p++,
			G_AD_DISABLE | G_CD_DISABLE | G_CK_NONE | G_TC_FILT
			| G_TF_BILERP | G_TT_NONE | G_TL_TILE | G_TD_CLAMP
			| G_TP_PERSP | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
			G_AC_NONE | G_ZS_PIXEL | G_RM_CEL_BASE1
			| G_RM_CEL_BASE2);

	/* Set base color */
	gDPSetPrimColor(gfx_p++, 0, 0, cel->r, cel->g, cel->b, 0xFF);

	/* Draw base layer */
	gSPDisplayList(gfx_p++, cel->gfx);

	/* Do shading layers */
	for (i = 0; i < cel->n_layer; i++)
	{
		struct cel_layer *lyr = &cel->layer[i];

		/* Reload the default geometry and combine mode */
		gSPLoadGeometryMode(gfx_p++, G_ZBUFFER | G_SHADE |
					G_SHADING_SMOOTH);
		gDPPipeSync(gfx_p++);
		gDPSetCombineMode(gfx_p++, G_CC_PRIMITIVE_SHADEA,
					G_CC_PRIMITIVE_SHADEA);

		/* Set up render mode for shading layer */
		gDPSetOtherMode(gfx_p++, G_AD_DISABLE | G_CD_DISABLE
				| G_CK_NONE | G_TC_FILT | G_TF_BILERP
				| G_TT_NONE | G_TL_TILE | G_TD_CLAMP
				| G_TP_PERSP | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
				G_AC_THRESHOLD | G_ZS_PIXEL | G_RM_CEL_SHADE1
				| G_RM_CEL_SHADE2);

		/* Set blend alpha for alpha compare */
		gDPSetBlendColor(gfx_p++, 0xFF, 0xFF, 0xFF, lyr->thresh);

		/* Set shading color */
		gDPSetPrimColor(gfx_p++, 0, 0, lyr->r, lyr->g, lyr->b, 0xFF);

		/* Draw shading layer */
		gSPDisplayList(gfx_p++, cel->gfx);
	}

	/* Restore modelview matrix */
	gSPPopMatrix(gfx_p++, G_MTX_MODELVIEW);

	/* Save gfx pointers */
	*p_gfx_p = gfx_p;
	*p_gfx_d = gfx_d;
}

/* Model vertices */
static Vtx_tn sph_body_vtx[] =
{
	{ {      0,    128,      0 },     0, {      0,      0 }, {    0,  127,    0 }, 255 },
	{ {     67,    109,      0 },     0, {      0,      0 }, {   66,  108,    0 }, 255 },
	{ {     21,    109,    -64 },     0, {      0,      0 }, {   20,  108,  -63 }, 255 },
	{ {    -54,    109,    -40 },     0, {      0,      0 }, {  -54,  108,  -39 }, 255 },
	{ {    -54,    109,     40 },     0, {      0,      0 }, {  -54,  108,   39 }, 255 },
	{ {     21,    109,     64 },     0, {      0,      0 }, {   20,  108,   63 }, 255 },
	{ {    114,     57,      0 },     0, {      0,      0 }, {  113,   56,    0 }, 255 },
	{ {     88,     67,    -64 },     0, {      0,      0 }, {   87,   66,  -63 }, 255 },
	{ {     35,     57,   -109 },     0, {      0,      0 }, {   35,   56, -108 }, 255 },
	{ {    -34,     67,   -104 },     0, {      0,      0 }, {  -33,   66, -102 }, 255 },
	{ {    -93,     57,    -67 },     0, {      0,      0 }, {  -91,   56,  -66 }, 255 },
	{ {   -109,     67,      0 },     0, {      0,      0 }, { -108,   66,    0 }, 255 },
	{ {    -93,     57,     67 },     0, {      0,      0 }, {  -91,   56,   66 }, 255 },
	{ {    -34,     67,    104 },     0, {      0,      0 }, {  -33,   66,  102 }, 255 },
	{ {     35,     57,    109 },     0, {      0,      0 }, {   35,   56,  108 }, 255 },
	{ {     88,     67,     64 },     0, {      0,      0 }, {   87,   66,   63 }, 255 },
	{ {    122,      0,    -40 },     0, {      0,      0 }, {  120,    0,  -39 }, 255 },
	{ {     75,      0,   -104 },     0, {      0,      0 }, {   74,    0, -102 }, 255 },
	{ {      0,      0,   -128 },     0, {      0,      0 }, {    0,    0, -126 }, 255 },
	{ {    -75,      0,   -104 },     0, {      0,      0 }, {  -74,    0, -102 }, 255 },
	{ {   -122,      0,    -40 },     0, {      0,      0 }, { -120,    0,  -39 }, 255 },
	{ {   -122,      0,     40 },     0, {      0,      0 }, { -120,    0,   39 }, 255 },
	{ {    -75,      0,    104 },     0, {      0,      0 }, {  -74,    0,  102 }, 255 },
	{ {      0,      0,    128 },     0, {      0,      0 }, {    0,    0,  126 }, 255 },
	{ {     75,      0,    104 },     0, {      0,      0 }, {   74,    0,  102 }, 255 },
	{ {    122,      0,     40 },     0, {      0,      0 }, {  120,    0,   39 }, 255 },
	{ {     93,    -57,    -67 },     0, {      0,      0 }, {   91,  -56,  -66 }, 255 },
	{ {     34,    -67,   -104 },     0, {      0,      0 }, {   33,  -66, -102 }, 255 },
	{ {    -35,    -57,   -109 },     0, {      0,      0 }, {  -35,  -56, -108 }, 255 },
	{ {    -88,    -67,    -64 },     0, {      0,      0 }, {  -87,  -66,  -63 }, 255 },
	{ {   -114,    -57,      0 },     0, {      0,      0 }, { -113,  -56,    0 }, 255 },
	{ {    -88,    -67,     64 },     0, {      0,      0 }, {  -87,  -66,   63 }, 255 },
	{ {    -35,    -57,    109 },     0, {      0,      0 }, {  -35,  -56,  108 }, 255 },
	{ {     34,    -67,    104 },     0, {      0,      0 }, {   33,  -66,  102 }, 255 },
	{ {     93,    -57,     67 },     0, {      0,      0 }, {   91,  -56,   66 }, 255 },
	{ {    109,    -67,      0 },     0, {      0,      0 }, {  108,  -66,    0 }, 255 },
	{ {     54,   -109,    -40 },     0, {      0,      0 }, {   54, -108,  -39 }, 255 },
	{ {    -21,   -109,    -64 },     0, {      0,      0 }, {  -20, -108,  -63 }, 255 },
	{ {    -67,   -109,      0 },     0, {      0,      0 }, {  -66, -108,    0 }, 255 },
	{ {    -21,   -109,     64 },     0, {      0,      0 }, {  -20, -108,   63 }, 255 },
	{ {     54,   -109,     40 },     0, {      0,      0 }, {   54, -108,   39 }, 255 },
	{ {      0,   -128,      0 },     0, {      0,      0 }, {    0, -127,    0 }, 255 },
};

/* Model display list */
static Gfx sph_body_gfx[] =
{
	gsSPTexture(32768, 32768, 0, 0, G_OFF),
	gsSPSetGeometryMode(G_CULL_BACK | G_SHADING_SMOOTH),
	gsSPVertex(&CEL_VTX[0], 26, 0),
	gsSP2Triangles( 0,  1,  2, 0,  1,  6,  7, 0),
	gsSP2Triangles( 1,  7,  2, 0,  2,  7,  8, 0),
	gsSP2Triangles( 6, 16,  7, 0,  7, 16, 17, 0),
	gsSP2Triangles( 7, 17,  8, 0,  8, 17, 18, 0),
	gsSP2Triangles( 0,  2,  3, 0,  2,  8,  9, 0),
	gsSP2Triangles( 2,  9,  3, 0,  3,  9, 10, 0),
	gsSP2Triangles( 8, 18,  9, 0,  9, 18, 19, 0),
	gsSP2Triangles( 9, 19, 10, 0, 10, 19, 20, 0),
	gsSP2Triangles( 0,  3,  4, 0,  3, 10, 11, 0),
	gsSP2Triangles( 3, 11,  4, 0,  4, 11, 12, 0),
	gsSP2Triangles(10, 20, 11, 0, 11, 20, 21, 0),
	gsSP2Triangles(11, 21, 12, 0, 12, 21, 22, 0),
	gsSP2Triangles( 0,  4,  5, 0,  4, 12, 13, 0),
	gsSP2Triangles( 4, 13,  5, 0,  5, 13, 14, 0),
	gsSP2Triangles(12, 22, 13, 0, 13, 22, 23, 0),
	gsSP2Triangles(13, 23, 14, 0, 14, 23, 24, 0),
	gsSP2Triangles( 0,  5,  1, 0,  5, 14, 15, 0),
	gsSP2Triangles( 5, 15,  1, 0,  1, 15,  6, 0),
	gsSP2Triangles(14, 24, 15, 0, 15, 24, 25, 0),
	gsSP2Triangles(15, 25,  6, 0,  6, 25, 16, 0),
	gsSPVertex(&CEL_VTX[26], 16, 0),
	gsSP2Triangles(17,  0,  1, 0, 17,  1, 18, 0),
	gsSP2Triangles(18,  1,  2, 0, 18,  2, 19, 0),
	gsSP2Triangles( 0, 10,  1, 0,  1, 10, 11, 0),
	gsSP2Triangles( 1, 11,  2, 0, 10, 15, 11, 0),
	gsSP2Triangles(19,  2,  3, 0, 19,  3, 20, 0),
	gsSP2Triangles(20,  3,  4, 0, 20,  4, 21, 0),
	gsSP2Triangles( 2, 11,  3, 0,  3, 11, 12, 0),
	gsSP2Triangles( 3, 12,  4, 0, 11, 15, 12, 0),
	gsSP2Triangles(21,  4,  5, 0, 21,  5, 22, 0),
	gsSP2Triangles(22,  5,  6, 0, 22,  6, 23, 0),
	gsSP2Triangles( 4, 12,  5, 0,  5, 12, 13, 0),
	gsSP2Triangles( 5, 13,  6, 0, 12, 15, 13, 0),
	gsSP2Triangles(23,  6,  7, 0, 23,  7, 24, 0),
	gsSP2Triangles(24,  7,  8, 0, 24,  8, 25, 0),
	gsSP2Triangles( 6, 13,  7, 0,  7, 13, 14, 0),
	gsSP2Triangles( 7, 14,  8, 0, 13, 15, 14, 0),
	gsSP2Triangles(25,  8,  9, 0, 25,  9, 16, 0),
	gsSP2Triangles(16,  9,  0, 0, 16,  0, 17, 0),
	gsSP2Triangles( 8, 14,  9, 0,  9, 14, 10, 0),
	gsSP2Triangles( 9, 10,  0, 0, 14, 15, 10, 0),
	gsSPClearGeometryMode(G_CULL_BACK | G_SHADING_SMOOTH),
	gsSPEndDisplayList(),
};

/* Cel-shaded object layers */
static struct cel_layer sph_body_layers[] =
{
	/* Threshold, R, G, B */
	{ 0xA0, 0xC0, 0x00, 0x00 },
	{ 0xE0, 0xFF, 0x00, 0x00 },
};

/* Cel-shaded object description */
static struct cel_object sph_body =
{
	/* Material color */
	0xFF, 0x00, 0x00,

	/* Base color */
	0xA0, 0x00, 0x00,

	/* Shading layers */
	ARRAY_COUNT(sph_body_layers),
	sph_body_layers,

	/* Vertices */
	ARRAY_COUNT(sph_body_vtx),
	(void *) sph_body_vtx,

	/* Display list */
	sph_body_gfx,
};

void cel_test(GlobalContext *globalCtx)
{
	Player* player = GET_PLAYER(globalCtx);
	MtxF mtx;

	Matrix_Push();

	/* Place above link */
	Matrix_Translate(	player->actor.world.pos.x,
				player->actor.world.pos.y + 100.f,
				player->actor.world.pos.z,
				MTXMODE_NEW);

	/* Scale to a radius of 40 units */
	{
		float r = 40.f / 128.f;
		Matrix_Scale(r, r, r, MTXMODE_APPLY);
	};

	/* Rotate with link's y rotation */
	Matrix_RotateY(	player->actor.world.rot.y * M_PI / 32768,
			MTXMODE_APPLY);

	Matrix_Get(&mtx);
	Matrix_Pop();

	/* Draw cel-shaded body */
	cel_draw(globalCtx,
			&sph_body,
			&globalCtx->state.gfxCtx->polyOpa.p,
			&globalCtx->state.gfxCtx->polyOpa.d,
			&mtx);
}
