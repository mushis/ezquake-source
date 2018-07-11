/*
Copyright (C) 1996-1997 Id Software, Inc.

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

#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "gl_framebuffer.h"
#include "tr_types.h"
#include "glsl/constants.glsl"
#include "r_lighting.h"
#include "r_matrix.h"
#include "r_buffers.h"
#include "glm_local.h"

static buffer_ref ubo_frameConstants;
static uniform_block_frame_constants_t frameConstants;
static qbool frameConstantsUploaded = false;

void GLM_UploadFrameConstants(void);

#define PASS_COLOR_AS_4F(target, cvar) \
{ \
	target[0] = (cvar.color[0] * 1.0f / 255); \
	target[1] = (cvar.color[1] * 1.0f / 255); \
	target[2] = (cvar.color[2] * 1.0f / 255); \
	target[3] = 1.0f; \
}

// TODO: !?  Called as first step in 2D.  Include in frame-buffer at end of 3D scene rendering?
void GLM_PolyBlend(float v_blend[4])
{
	color_t v_blend_color = RGBA_TO_COLOR(
		bound(0, v_blend[0], 1) * 255,
		bound(0, v_blend[1], 1) * 255,
		bound(0, v_blend[2], 1) * 255,
		bound(0, v_blend[3], 1) * 255
	);

	Draw_AlphaRectangleRGB(r_refdef.vrect.x, r_refdef.vrect.y, r_refdef.vrect.width, r_refdef.vrect.height, 0.0f, true, v_blend_color);
}

void GLM_DrawVelocity3D(void)
{
	// MEAG: TODO
}

void GLM_RenderSceneBlurDo(float alpha)
{
	// MEAG: TODO
}

void GLM_PreRenderView(void)
{
	extern cvar_t gl_alphafont, gl_max_size;
	extern cvar_t r_telecolor, r_lavacolor, r_slimecolor, r_watercolor, r_fastturb, r_skycolor;
	extern cvar_t gl_textureless, gl_hwblend;
	int i, active_lights = 0;
	float blend_alpha;

	// General constants
	frameConstants.time = cl.time;
	frameConstants.gamma = bound(0.3, v_gamma.value, 3);
	frameConstants.contrast = bound(1, v_contrast.value, 3);
	frameConstants.r_alphafont = gl_alphafont.value;
	blend_alpha = (!vid_hwgamma_enabled || !gl_hwblend.value || cl.teamfortress) ? 0 : v_blend[3];
	frameConstants.v_blend[0] = v_blend[0] * blend_alpha;
	frameConstants.v_blend[1] = v_blend[1] * blend_alpha;
	frameConstants.v_blend[2] = v_blend[2] * blend_alpha;
	frameConstants.v_blend[3] = 1 - blend_alpha;

	// Lights
	for (i = 0; i < MAX_DLIGHTS; ++i) {
		if (cl_dlight_active[i / 32] & (1 << (i % 32))) {
			extern cvar_t gl_colorlights;

			dlight_t* light = &cl_dlights[i];
			float* lightColor = frameConstants.lightColor[active_lights];
			float* lightPosition = frameConstants.lightPosition[active_lights];
			
			VectorCopy(light->origin, lightPosition);
			lightPosition[3] = light->radius;
			if (gl_colorlights.integer) {
				if (light->type == lt_custom) {
					lightColor[0] = light->color[0] / 255.0;
					lightColor[1] = light->color[1] / 255.0;
					lightColor[2] = light->color[2] / 255.0;
				}
				else {
					lightColor[0] = dlightcolor[light->type][0] / 255.0;
					lightColor[1] = dlightcolor[light->type][1] / 255.0;
					lightColor[2] = dlightcolor[light->type][2] / 255.0;
				}
			}
			else {
				lightColor[0] = lightColor[1] = lightColor[2] = 0.5;
			}
			lightColor[3] = light->minlight;
			++active_lights;
		}
	}
	frameConstants.lightsActive = active_lights;
	frameConstants.lightScale = ((lightmode == 2 ? 1.5 : 2) * bound(0.5, gl_modulate.value, 3));

	// Draw-world constants
	frameConstants.r_textureless = gl_textureless.integer || gl_max_size.integer == 1;
	frameConstants.r_farclip = max(r_farclip.value, 4096) * 0.577;
	frameConstants.skySpeedscale = r_refdef2.time * 8;
	frameConstants.skySpeedscale -= (int)frameConstants.skySpeedscale & ~127;
	frameConstants.skySpeedscale2 = r_refdef2.time * 16;
	frameConstants.skySpeedscale2 -= (int)frameConstants.skySpeedscale2 & ~127;

	frameConstants.waterAlpha = GL_WaterAlpha();

	frameConstants.r_drawflat = r_drawflat.integer;
	PASS_COLOR_AS_4F(frameConstants.r_wallcolor, r_wallcolor);
	PASS_COLOR_AS_4F(frameConstants.r_floorcolor, r_floorcolor);

	frameConstants.r_fastturb = r_fastturb.integer;
	PASS_COLOR_AS_4F(frameConstants.r_telecolor, r_telecolor);
	PASS_COLOR_AS_4F(frameConstants.r_lavacolor, r_lavacolor);
	PASS_COLOR_AS_4F(frameConstants.r_slimecolor, r_slimecolor);
	PASS_COLOR_AS_4F(frameConstants.r_watercolor, r_watercolor);

	PASS_COLOR_AS_4F(frameConstants.r_skycolor, r_skycolor);

	frameConstants.r_texture_luma_fb = gl_fb_bmodels.integer ? 1 : 0;

	// Alias models
	{
		extern cvar_t gl_powerupshells_base1level, gl_powerupshells_base2level;
		extern cvar_t gl_powerupshells_effect1level, gl_powerupshells_effect2level;

		frameConstants.shellSize = bound(0, gl_powerupshells_size.value, 20);
		frameConstants.shell_base_level1 = gl_powerupshells_base1level.value;
		frameConstants.shell_base_level2 = gl_powerupshells_base2level.value;
		frameConstants.shell_effect_level1 = gl_powerupshells_effect1level.value;
		frameConstants.shell_effect_level2 = gl_powerupshells_effect2level.value;
		frameConstants.shell_alpha = bound(0, gl_powerupshells.value, 1);
	}

	frameConstantsUploaded = false;
}

void GLM_SetupGL(void)
{
	memcpy(frameConstants.modelViewMatrix, GLM_ModelviewMatrix(), sizeof(frameConstants.modelViewMatrix));
	memcpy(frameConstants.projectionMatrix, GLM_ProjectionMatrix(), sizeof(frameConstants.projectionMatrix));
	VectorCopy(r_refdef.vieworg, frameConstants.position);

	frameConstantsUploaded = false;
}

void GLM_UploadFrameConstants(void)
{
	if (!frameConstantsUploaded) {
		if (!GL_BufferReferenceIsValid(ubo_frameConstants)) {
			ubo_frameConstants = buffers.Create(buffertype_uniform, "frameConstants", sizeof(frameConstants), &ubo_frameConstants, bufferusage_once_per_frame);
		}

		buffers.BindRange(ubo_frameConstants, EZQ_GL_BINDINGPOINT_FRAMECONSTANTS, buffers.BufferOffset(ubo_frameConstants), sizeof(frameConstants));
		buffers.Update(ubo_frameConstants, sizeof(frameConstants), &frameConstants);
		frameConstantsUploaded = true;
	}
}

void GLM_ScreenDrawStart(void)
{
	GLM_FramebufferScreenDrawStart();
}

void GLM_PostProcessScreen(void)
{
	GLM_FramebufferPostProcessScreen();
}

void GLM_Shutdown(qbool restarting)
{
	GLM_DeletePrograms(restarting);
}

