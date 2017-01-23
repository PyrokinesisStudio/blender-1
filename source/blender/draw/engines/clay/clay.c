/*
 * Copyright 2016, Blender Foundation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Contributor(s): Blender Institute
 *
 */

#include "DRW_render.h"

#include "BKE_icons.h"
#include "BKE_main.h"

#include "BLI_dynstr.h"
#include "BLI_rand.h"

#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"

#include "UI_resources.h"
#include "UI_interface_icons.h"

#include "clay.h"

/* Shaders */

extern char datatoc_clay_frag_glsl[];
extern char datatoc_clay_vert_glsl[];
extern char datatoc_ssao_alchemy_glsl[];
extern char datatoc_ssao_groundtruth_glsl[];

/* Storage */

/* UBOs data needs to be 16 byte aligned (size of vec4) */
/* Reminder : float, int, bool are 4 bytes */
typedef struct CLAY_UBO_Material {
	float ssao_params_var[4];
	/* - 16 -*/
	float matcap_hsv[3];
	float matcap_id; /* even float encoding have enough precision */
	/* - 16 -*/
	float matcap_rot[2];
	float pad[2]; /* ensure 16 bytes alignement */
} CLAY_UBO_Material; /* 48 bytes */

typedef struct CLAY_UBO_Storage {
	CLAY_UBO_Material materials[512]; /* 512 = 9 bit material id */
} CLAY_UBO_Storage;

static struct CLAY_data {
	/* Depth Pre Pass */
	struct GPUShader *depth_sh;
	/* Shading Pass */
	struct GPUShader *clay_sh;

	/* Materials Parameter UBO */
	struct GPUUniformBuffer *mat_ubo;
	CLAY_UBO_Storage mat_storage;
	short ubo_flag;

	/* Matcap textures */
	struct GPUTexture *matcap_array;
	float matcap_colors[24][3];

	/* Ssao */
	float winmat[4][4];
	float viewvecs[3][4];
	float ssao_params[4];
	struct GPUTexture *jitter_tx;
	struct GPUTexture *sampling_tx;
} data = {NULL};

/* CLAY_data.ubo_flag */
#define CLAY_UBO_CLEAR     (1 << 0)
#define CLAY_UBO_REFRESH   (1 << 1)

/* keep it under MAX_BUFFERS */
typedef struct CLAY_FramebufferList{
	/* default */
	struct GPUFrameBuffer *default_fb;
	/* engine specific */
	struct GPUFrameBuffer *downsample_depth;
} CLAY_FramebufferList;

/* keep it under MAX_TEXTURES */
typedef struct CLAY_TextureList{
	/* default */
	struct GPUTexture *color;
	struct GPUTexture *depth;
	/* engine specific */
	struct GPUTexture *depth_low;
} CLAY_TextureList;

/* for clarity follow the same layout as CLAY_TextureList */
#define SCENE_COLOR 0
#define SCENE_DEPTH 1
#define SCENE_DEPTH_LOW 2

/* keep it under MAX_PASSES */
typedef struct CLAY_PassList{
	struct DRWPass *depth_pass;
	struct DRWPass *clay_pass;
	struct DRWPass *mode_ob_wire_pass;
	struct DRWPass *mode_ob_center_pass;
} CLAY_PassList;

//#define GTAO

/* Functions */

static void add_icon_to_rect(PreviewImage *prv, float *final_rect, int layer)
{
	int image_size = prv->w[0] * prv->h[0];
	float *new_rect = &final_rect[image_size * 4 * layer];

	IMB_buffer_float_from_byte(new_rect, (unsigned char *)prv->rect[0], IB_PROFILE_SRGB, IB_PROFILE_SRGB,
	                           false, prv->w[0], prv->h[0], prv->w[0], prv->w[0]);

	/* Find overall color */
	for (int y = 0; y < 4; ++y)	{
		for (int x = 0; x < 4; ++x) {
			data.matcap_colors[layer][0] += new_rect[y * 512 * 128 * 4 + x * 128 * 4 + 0];
			data.matcap_colors[layer][1] += new_rect[y * 512 * 128 * 4 + x * 128 * 4 + 1];
			data.matcap_colors[layer][2] += new_rect[y * 512 * 128 * 4 + x * 128 * 4 + 2];
		}
	}

	data.matcap_colors[layer][0] /= 16.0f * 2.0f; /* the * 2 is to darken for shadows */
	data.matcap_colors[layer][1] /= 16.0f * 2.0f;
	data.matcap_colors[layer][2] /= 16.0f * 2.0f;
}

static struct GPUTexture *load_matcaps(PreviewImage *prv[24], int nbr)
{
	struct GPUTexture *tex;
	int w = prv[0]->w[0];
	int h = prv[0]->h[0];
	float *final_rect = MEM_callocN(sizeof(float) * 4 * w * h * nbr, "Clay Matcap array rect");

	for (int i = 0; i < nbr; ++i) {
		add_icon_to_rect(prv[i], final_rect, i);
		BKE_previewimg_free(&prv[i]);
	}

	tex = DRW_texture_create_2D_array(w, h, nbr, DRW_TEX_RGBA_8, DRW_TEX_FILTER, final_rect);
	MEM_freeN(final_rect);

	return tex;
}

static int matcap_to_index(int matcap)
{
	if (matcap == ICON_MATCAP_02) return 1;
	else if (matcap == ICON_MATCAP_03) return 2;
	else if (matcap == ICON_MATCAP_04) return 3;
	else if (matcap == ICON_MATCAP_05) return 4;
	else if (matcap == ICON_MATCAP_06) return 5;
	else if (matcap == ICON_MATCAP_07) return 6;
	else if (matcap == ICON_MATCAP_08) return 7;
	else if (matcap == ICON_MATCAP_09) return 8;
	else if (matcap == ICON_MATCAP_10) return 9;
	else if (matcap == ICON_MATCAP_11) return 10;
	else if (matcap == ICON_MATCAP_12) return 11;
	else if (matcap == ICON_MATCAP_13) return 12;
	else if (matcap == ICON_MATCAP_14) return 13;
	else if (matcap == ICON_MATCAP_15) return 14;
	else if (matcap == ICON_MATCAP_16) return 15;
	else if (matcap == ICON_MATCAP_17) return 16;
	else if (matcap == ICON_MATCAP_18) return 17;
	else if (matcap == ICON_MATCAP_19) return 18;
	else if (matcap == ICON_MATCAP_20) return 19;
	else if (matcap == ICON_MATCAP_21) return 20;
	else if (matcap == ICON_MATCAP_22) return 21;
	else if (matcap == ICON_MATCAP_23) return 22;
	else if (matcap == ICON_MATCAP_24) return 23;
	return 0;
}

static struct GPUTexture *create_spiral_sample_texture(int numsaples)
{
	struct GPUTexture *tex;
	float (*texels)[2] = MEM_mallocN(sizeof(float[2]) * numsaples, "concentric_tex");
	const float numsaples_inv = 1.0f / numsaples;
	int i;
	/* arbitrary number to ensure we don't get conciding samples every circle */
	const float spirals = 7.357;

	for (i = 0; i < numsaples; i++) {
		float r = (i + 0.5f) * numsaples_inv;
		float phi = r * spirals * (float)(2.0 * M_PI);
		texels[i][0] = r * cosf(phi);
		texels[i][1] = r * sinf(phi);
	}

	tex = DRW_texture_create_1D(numsaples, DRW_TEX_RG_16, 0, (float *)texels);

	MEM_freeN(texels);
	return tex;
}

static struct GPUTexture *create_jitter_texture(void)
{
	float jitter[64 * 64][2];
	int i;

	/* TODO replace by something more evenly distributed like blue noise */
	for (i = 0; i < 64 * 64; i++) {
#ifdef GTAO
		jitter[i][0] = BLI_frand();
		jitter[i][1] = BLI_frand();
#else
		jitter[i][0] = 2.0f * BLI_frand() - 1.0f;
		jitter[i][1] = 2.0f * BLI_frand() - 1.0f;
		normalize_v2(jitter[i]);
#endif
	}

	return DRW_texture_create_2D(64, 64, DRW_TEX_RG_16, DRW_TEX_FILTER | DRW_TEX_WRAP, &jitter[0][0]);
}

static void CLAY_engine_init(void)
{
	/* Create Texture Array */
	if (!data.matcap_array) {
		PreviewImage *prv[24]; /* For now use all of the 24 internal matcaps */

		/* TODO only load used matcaps */
		prv[0]  = UI_icon_to_preview(ICON_MATCAP_01);
		prv[1]  = UI_icon_to_preview(ICON_MATCAP_02);
		prv[2]  = UI_icon_to_preview(ICON_MATCAP_03);
		prv[3]  = UI_icon_to_preview(ICON_MATCAP_04);
		prv[4]  = UI_icon_to_preview(ICON_MATCAP_05);
		prv[5]  = UI_icon_to_preview(ICON_MATCAP_06);
		prv[6]  = UI_icon_to_preview(ICON_MATCAP_07);
		prv[7]  = UI_icon_to_preview(ICON_MATCAP_08);
		prv[8]  = UI_icon_to_preview(ICON_MATCAP_09);
		prv[9]  = UI_icon_to_preview(ICON_MATCAP_10);
		prv[10] = UI_icon_to_preview(ICON_MATCAP_11);
		prv[11] = UI_icon_to_preview(ICON_MATCAP_12);
		prv[12] = UI_icon_to_preview(ICON_MATCAP_13);
		prv[13] = UI_icon_to_preview(ICON_MATCAP_14);
		prv[14] = UI_icon_to_preview(ICON_MATCAP_15);
		prv[15] = UI_icon_to_preview(ICON_MATCAP_16);
		prv[16] = UI_icon_to_preview(ICON_MATCAP_17);
		prv[17] = UI_icon_to_preview(ICON_MATCAP_18);
		prv[18] = UI_icon_to_preview(ICON_MATCAP_19);
		prv[19] = UI_icon_to_preview(ICON_MATCAP_20);
		prv[20] = UI_icon_to_preview(ICON_MATCAP_21);
		prv[21] = UI_icon_to_preview(ICON_MATCAP_22);
		prv[22] = UI_icon_to_preview(ICON_MATCAP_23);
		prv[23] = UI_icon_to_preview(ICON_MATCAP_24);

		data.matcap_array = load_matcaps(prv, 24);
	}

	/* AO Jitter */
	if (!data.jitter_tx) {
		data.jitter_tx = create_jitter_texture();
	}

	/* AO Samples */
	/* TODO use hammersley sequence */
	if (!data.sampling_tx) {
		data.sampling_tx = create_spiral_sample_texture(500);
	}

	/* Depth prepass */
	if (!data.depth_sh) {
		data.depth_sh = DRW_shader_create_3D_depth_only();
	}

	if (!data.mat_ubo) {
		data.mat_ubo = DRW_uniformbuffer_create(sizeof(CLAY_UBO_Storage), NULL);
	}

	/* Shading pass */
	if (!data.clay_sh) {
		DynStr *ds = BLI_dynstr_new();
		const char *max_mat =
			"#define MAX_MATERIAL 512\n"
			"#define USE_ROTATION\n"
			"#define USE_AO\n"
			"#define USE_HSV\n";
		char *matcap_with_ao;

		BLI_dynstr_append(ds, datatoc_clay_frag_glsl);
#ifdef GTAO
		BLI_dynstr_append(ds, datatoc_ssao_groundtruth_glsl);
#else
		BLI_dynstr_append(ds, datatoc_ssao_alchemy_glsl);
#endif

		matcap_with_ao = BLI_dynstr_get_cstring(ds);

		data.clay_sh = DRW_shader_create(datatoc_clay_vert_glsl, NULL, matcap_with_ao, max_mat);

		BLI_dynstr_free(ds);
		MEM_freeN(matcap_with_ao);
	}
}

static DRWShadingGroup *CLAY_shgroup_create(DRWPass *pass, int *material_id)
{
	const int depthloc = 0, matcaploc = 1, jitterloc = 2, sampleloc = 3;

	CLAY_UBO_Material *mat = &data.mat_storage.materials[0];
	DRWShadingGroup *grp = DRW_shgroup_create(data.clay_sh, pass);

	DRW_shgroup_uniform_ivec2(grp, "screenres", DRW_viewport_size_get(), 1);
	DRW_shgroup_uniform_buffer(grp, "depthtex", SCENE_DEPTH, depthloc);
	DRW_shgroup_uniform_texture(grp, "matcaps", data.matcap_array, matcaploc);
	DRW_shgroup_uniform_mat4(grp, "WinMatrix", (float *)data.winmat);
	DRW_shgroup_uniform_vec4(grp, "viewvecs", (float *)data.viewvecs, 3);
	DRW_shgroup_uniform_vec4(grp, "ssao_params", data.ssao_params, 1);
	DRW_shgroup_uniform_vec3(grp, "matcaps_color", (float *)data.matcap_colors, 24);

	//DRW_shgroup_uniform_int(grp, "material_id", material_id, 1);

#ifndef GTAO
	DRW_shgroup_uniform_texture(grp, "ssao_jitter", data.jitter_tx, jitterloc);
	DRW_shgroup_uniform_texture(grp, "ssao_samples", data.sampling_tx, sampleloc);
#endif

	return grp;
}

static void CLAY_update_material_runtime(MaterialSettingsClay *settings)
{
	MaterialDataClayRuntime *runtime_data;

	if (!settings->runtime) {
		settings->runtime = MEM_mallocN(sizeof(MaterialDataClayRuntime), "MaterialDataClayRuntime");
		settings->runtime->flag = CLAY_OUTDATED;
		data.ubo_flag |= CLAY_UBO_CLEAR;
	}

	runtime_data = settings->runtime;

	if (runtime_data->flag & CLAY_OUTDATED) {

		/* Update default material */
		runtime_data->matcap_rot[0] = cosf(settings->matcap_rot * 3.14159f * 2.0f);
		runtime_data->matcap_rot[1] = sinf(settings->matcap_rot * 3.14159f * 2.0f);

		runtime_data->matcap_hsv[0] = settings->matcap_hue + 0.5f;
		runtime_data->matcap_hsv[1] = settings->matcap_sat * 2.0f;
		runtime_data->matcap_hsv[2] = settings->matcap_val * 2.0f;

		runtime_data->ssao_params_var[0] = settings->ssao_distance;
		runtime_data->ssao_params_var[1] = settings->ssao_factor_cavity;
		runtime_data->ssao_params_var[2] = settings->ssao_factor_edge;
		runtime_data->ssao_params_var[3] = settings->ssao_attenuation;

		if (settings->matcap_icon < ICON_MATCAP_01 ||
		    settings->matcap_icon > ICON_MATCAP_24)
		{
			settings->matcap_icon = ICON_MATCAP_01;
		}

		runtime_data->matcap_id = matcap_to_index(settings->matcap_icon);

		if ((runtime_data->type != settings->type)) {
			data.ubo_flag |= CLAY_UBO_CLEAR;
		}

		runtime_data->type = settings->type;

		data.ubo_flag |= CLAY_UBO_REFRESH;
		runtime_data->flag &= ~CLAY_OUTDATED;
	}
}

static void update_ubo_storage(MaterialDataClayRuntime *runtime_data, unsigned int current_id)
{
	CLAY_UBO_Material *ubo = &data.mat_storage.materials[current_id];

	ubo->matcap_id = runtime_data->matcap_id;
	copy_v3_v3(ubo->matcap_hsv, runtime_data->matcap_hsv);
	copy_v2_v2(ubo->matcap_rot, runtime_data->matcap_rot);
	copy_v4_v4(ubo->ssao_params_var, runtime_data->ssao_params_var);

	runtime_data->material_id = current_id;
}

static void CLAY_update_material_ubo(const struct bContext *C)
{
	Main *bmain = CTX_data_main(C);

	/* Update Default materials */
	for (Scene *sce = bmain->scene.first; sce; sce = sce->id.next) {
		EngineDataClay *ed = &sce->claydata;
		CLAY_update_material_runtime((MaterialSettingsClay *)&ed->defsettings);
	}

	/* Update Scene Materials */
	for (Material *mat = bmain->mat.first; mat; mat = mat->id.next) {
		CLAY_update_material_runtime(&mat->clay);
	}

	if (data.ubo_flag & CLAY_UBO_REFRESH) {
		MaterialDataClayRuntime *runtime_data;
		int current_id = 0;

		/* Default materials */
		for (Scene *sce = bmain->scene.first; sce; sce = sce->id.next) {
			EngineDataClay *ed = &sce->claydata;
			runtime_data = ed->defsettings.runtime;
			update_ubo_storage(runtime_data, current_id);
			current_id++;
		}

		/* TODO only add materials linked to geometry */
		for (Material *mat = bmain->mat.first; mat; mat = mat->id.next) {
			runtime_data = mat->clay.runtime;
			update_ubo_storage(runtime_data, current_id);
			current_id++;
		}

		DRW_uniformbuffer_update(data.mat_ubo, &data.mat_storage);
	}

	data.ubo_flag = 0;
}

static void CLAY_create_cache(CLAY_PassList *passes, const struct bContext *C)
{
	SceneLayer *sl = CTX_data_scene_layer(C);
	DRWShadingGroup *default_shgrp, *depthbatch;
	Object *ob;

	/* Depth Pass */
	{
		passes->depth_pass = DRW_pass_create("Depth Pass", DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS);

		depthbatch = DRW_shgroup_create(data.depth_sh, passes->depth_pass);
	}

	/* Clay Pass */
	{
		MaterialDataClayRuntime *runtime;
		EngineDataClay *settings = DRW_render_settings();

		passes->clay_pass = DRW_pass_create("Clay Pass", DRW_STATE_WRITE_COLOR);

		runtime = settings->defsettings.runtime;

		default_shgrp = CLAY_shgroup_create(passes->clay_pass, &runtime->material_id);
		DRW_shgroup_uniform_block(default_shgrp, "material_block", data.mat_ubo, 0);
	}

	/* Object Mode */
	{
		DRW_mode_object_setup(&passes->mode_ob_wire_pass, &passes->mode_ob_center_pass);
	}

	/* TODO Create hash table of batch based on material id*/
	FOREACH_OBJECT(sl, ob)
	{
		if (ob->type == OB_MESH) {
			struct Batch *geom = DRW_cache_surface_get(ob);

			/* Add everything for now */
			DRW_shgroup_call_add(default_shgrp, geom, &ob->obmat);

			/* When encountering a new material :
			 * - Create new Batch
			 * - Initialize Batch
			 * - Push it to the hash table
			 * - The pass takes care of inserting it
			 * next to the same shader calls */

			DRW_shgroup_call_add(depthbatch, geom, &ob->obmat);

			/* Free hash table */

			DRW_mode_object_add(passes->mode_ob_wire_pass, passes->mode_ob_center_pass, ob);
		}
	}
	FOREACH_OBJECT_END
}

static void CLAY_ssao_setup(void)
{
	float invproj[4][4];
	float dfdyfacs[2];
	bool is_persp = DRW_viewport_is_persp_get();
	/* view vectors for the corners of the view frustum. Can be used to recreate the world space position easily */
	float viewvecs[3][4] = {
	    {-1.0f, -1.0f, -1.0f, 1.0f},
	    {1.0f, -1.0f, -1.0f, 1.0f},
	    {-1.0f, 1.0f, -1.0f, 1.0f}
	};
	int i;
	int *size = DRW_viewport_size_get();
	EngineDataClay *settings = DRW_render_settings();

	DRW_get_dfdy_factors(dfdyfacs);

	data.ssao_params[0] = settings->ssao_samples;
	data.ssao_params[1] = size[0] / 64.0;
	data.ssao_params[2] = size[1] / 64.0;
	data.ssao_params[3] = dfdyfacs[1]; /* dfdy sign for offscreen */

	/* invert the view matrix */
	DRW_viewport_matrix_get(data.winmat, DRW_MAT_WIN);
	invert_m4_m4(invproj, data.winmat);

	/* convert the view vectors to view space */
	for (i = 0; i < 3; i++) {
		mul_m4_v4(invproj, viewvecs[i]);
		/* normalized trick see http://www.derschmale.com/2014/01/26/reconstructing-positions-from-the-depth-buffer */
		mul_v3_fl(viewvecs[i], 1.0f / viewvecs[i][3]);
		if (is_persp)
			mul_v3_fl(viewvecs[i], 1.0f / viewvecs[i][2]);
		viewvecs[i][3] = 1.0;

		copy_v4_v4(data.viewvecs[i], viewvecs[i]);
	}

	/* we need to store the differences */
	data.viewvecs[1][0] -= data.viewvecs[0][0];
	data.viewvecs[1][1] = data.viewvecs[2][1] - data.viewvecs[0][1];

	/* calculate a depth offset as well */
	if (!is_persp) {
		float vec_far[] = {-1.0f, -1.0f, 1.0f, 1.0f};
		mul_m4_v4(invproj, vec_far);
		mul_v3_fl(vec_far, 1.0f / vec_far[3]);
		data.viewvecs[1][2] = vec_far[2] - data.viewvecs[0][2];
	}
}

static void CLAY_view_draw(RenderEngine *UNUSED(engine), const struct bContext *context)
{
	/* This function may run for multiple viewports
	 * so get the current viewport buffers */
	CLAY_FramebufferList *buffers = NULL;
	CLAY_TextureList *textures = NULL;
	CLAY_PassList *passes = NULL;

	DRW_viewport_init(context, (void **)&buffers, (void **)&textures, (void **)&passes);

	CLAY_engine_init();

	CLAY_update_material_ubo(context);

	/* TODO : tag to refresh by the deps graph */
	/* ideally only refresh when objects are added/removed */
	/* or render properties / materials change */
	if (DRW_viewport_cache_is_dirty()) {
		CLAY_create_cache(passes, context);
	}

	/* Start Drawing */
	DRW_draw_background();

	/* Pass 1 : Depth pre-pass */
	DRW_draw_pass(passes->depth_pass);

	/* Pass 2 (Optionnal) : Separated Downsampled AO */
	DRW_framebuffer_texture_detach(textures->depth);
	/* TODO */

	/* Pass 3 : Shading */
	CLAY_ssao_setup();
	DRW_draw_pass(passes->clay_pass);

	/* Pass 4 : Overlays */
	DRW_framebuffer_texture_attach(buffers->default_fb, textures->depth, 0);
	DRW_draw_pass(passes->mode_ob_wire_pass);

	/* Always finish by this */
	DRW_state_reset();
}

void clay_engine_free(void)
{
	/* data.depth_sh Is builtin so it's automaticaly freed */
	if (data.clay_sh) {
		DRW_shader_free(data.clay_sh);
	}

	if (data.matcap_array) {
		DRW_texture_free(data.matcap_array);
	}

	if (data.jitter_tx) {
		DRW_texture_free(data.jitter_tx);
	}

	if (data.sampling_tx) {
		DRW_texture_free(data.sampling_tx);
	}

	if (data.mat_ubo) {
		DRW_uniformbuffer_free(data.mat_ubo);
	}
}

RenderEngineType viewport_clay_type = {
	NULL, NULL,
	"BLENDER_CLAY", N_("Clay"), RE_INTERNAL | RE_USE_OGL_PIPELINE,
	NULL, NULL, NULL, NULL, &CLAY_view_draw, NULL,
	{NULL, NULL, NULL}
};