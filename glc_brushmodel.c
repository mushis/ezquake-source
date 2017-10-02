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

$Id: gl_model.c,v 1.41 2007-10-07 08:06:33 tonik Exp $
*/
// gl_model.c  -- model loading and caching

// models are the only shared resource between a client and server running on the same machine.

#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "rulesets.h"

extern glpoly_t *lightmap_polys[MAX_LIGHTMAPS];
extern glpoly_t *fullbright_polys[MAX_GLTEXTURES];
extern glpoly_t *luma_polys[MAX_GLTEXTURES];

extern glpoly_t *caustics_polys;
extern glpoly_t *detail_polys;

static void GLC_DrawTextureChains(model_t *model, int contents);
void R_RenderFullbrights(void);
void R_RenderLumas(void);

static void GLC_DrawFlat(model_t *model)
{
	extern glpoly_t* caustics_polys;

	msurface_t *s;
	int waterline, k;
	float *v;
	byte w[3], f[3];
	int lastType = -1;
	qbool draw_caustics = underwatertexture && gl_caustics.value;

	memcpy(w, r_wallcolor.color, 3);
	memcpy(f, r_floorcolor.color, 3);

	GL_DisableMultitexture();
	GL_TextureEnvMode(GL_BLEND);
	GL_EnableTMU(GL_TEXTURE0);

	// START shaman BUG /fog not working with /r_drawflat {
	if (gl_fogenable.value) {
		glEnable(GL_FOG);
	}
	// } END shaman BUG /fog not working with /r_drawflat

	for (waterline = 0; waterline < 2; waterline++) {
		if (!(s = model->drawflat_chain[waterline])) {
			continue;
		}

		for (; s; s = s->texturechain) {
			GLC_SetTextureLightmap(s->lightmaptexturenum);

			v = s->polys->verts[0];

			if (s->flags & SURF_DRAWFLAT_FLOOR) {
				if (lastType != 0) {
					glColor3ubv(f);
					lastType = 0;
				}
			}
			else if (lastType != 1) {
				glColor3ubv(w);
				lastType = 1;
			}

			glBegin(GL_POLYGON);
			for (k = 0; k < s->polys->numverts; k++, v += VERTEXSIZE) {
				glTexCoord2f(v[5], v[6]);
				glVertex3fv(v);
			}
			glEnd();

			// START shaman FIX /r_drawflat + /gl_caustics {
			if (waterline && draw_caustics) {
				s->polys->caustics_chain = caustics_polys;
				caustics_polys = s->polys;
			}
			// } END shaman FIX /r_drawflat + /gl_caustics
		}
	}

	if (gl_fogenable.value) {
		glDisable(GL_FOG);
	}

	glColor3f(1.0f, 1.0f, 1.0f);
	// START shaman FIX /r_drawflat + /gl_caustics {
	EmitCausticsPolys();
	// } END shaman FIX /r_drawflat + /gl_caustics
}

static void GLC_DrawTextureChains(model_t *model, int contents)
{
	extern cvar_t gl_lumaTextures;
	extern cvar_t gl_textureless;

	int waterline, i, k, GL_LIGHTMAP_TEXTURE = 0, GL_FB_TEXTURE = 0, fb_texturenum;
	msurface_t *s;
	texture_t *t;
	float *v;

	qbool render_lightmaps = false;
	qbool doMtex1, doMtex2;
	qbool isLumaTexture;
	qbool draw_fbs, draw_caustics, draw_details;
	qbool can_mtex_lightmaps, can_mtex_fbs;
	qbool draw_mtex_fbs;
	qbool mtex_lightmaps, mtex_fbs;

	qbool drawfullbrights = false;
	qbool drawlumas = false;

	draw_caustics = underwatertexture && gl_caustics.value;
	draw_details = detailtexture && gl_detail.value;

	if (gl_fb_bmodels.value) {
		can_mtex_lightmaps = gl_mtexable;
		can_mtex_fbs = gl_textureunits >= 3;
	}
	else {
		can_mtex_lightmaps = gl_textureunits >= 3;
		can_mtex_fbs = gl_textureunits >= 3 && gl_add_ext;
	}

	GL_DisableMultitexture();
	if (gl_fogenable.value) {
		glEnable(GL_FOG);
	}

	GL_EnableTMU(GL_TEXTURE0);
	GL_TextureEnvMode(GL_REPLACE);

	for (i = 0; i < model->numtextures; i++) {
		if (!model->textures[i] || (!model->textures[i]->texturechain[0] && !model->textures[i]->texturechain[1]))
			continue;

		t = R_TextureAnimation(model->textures[i]);

		if (t->isLumaTexture) {
			isLumaTexture = gl_lumaTextures.value && r_refdef2.allow_lumas;
			fb_texturenum = isLumaTexture ? t->fb_texturenum : 0;
		}
		else {
			isLumaTexture = false;
			fb_texturenum = t->fb_texturenum;
		}

		//bind the world texture
		GL_SelectTexture(GL_TEXTURE0);
		GL_Bind(t->gl_texturenum);

		draw_fbs = gl_fb_bmodels.value /* || isLumaTexture */;
		draw_mtex_fbs = draw_fbs && can_mtex_fbs;

		if (gl_mtexable) {
			if (isLumaTexture && !gl_fb_bmodels.value) {
				if (gl_add_ext) {
					doMtex1 = true;
					GL_EnableTMU(GL_TEXTURE1);
					GL_FB_TEXTURE = GL_TEXTURE1;
					GL_TextureEnvMode(GL_ADD);
					GL_Bind(fb_texturenum);

					mtex_lightmaps = can_mtex_lightmaps;
					mtex_fbs = true;

					if (mtex_lightmaps) {
						doMtex2 = true;
						GL_LIGHTMAP_TEXTURE = GL_TEXTURE2;
						GL_EnableTMU(GL_LIGHTMAP_TEXTURE);
						GLC_SetLightmapTextureEnvironment();
					}
					else {
						doMtex2 = false;
						render_lightmaps = true;
					}
				}
				else {
					GL_DisableTMU(GL_TEXTURE1);
					render_lightmaps = true;
					doMtex1 = doMtex2 = mtex_lightmaps = mtex_fbs = false;
				}
			}
			else {
				doMtex1 = true;
				GL_EnableTMU(GL_TEXTURE1);
				GL_LIGHTMAP_TEXTURE = GL_TEXTURE1;
				GLC_SetLightmapTextureEnvironment();

				mtex_lightmaps = true;
				mtex_fbs = fb_texturenum && draw_mtex_fbs;

				if (mtex_fbs) {
					doMtex2 = true;
					GL_FB_TEXTURE = GL_TEXTURE2;
					GL_EnableTMU(GL_FB_TEXTURE);
					GL_Bind(fb_texturenum);
					GL_TextureEnvMode(isLumaTexture ? GL_ADD : GL_DECAL);
				}
				else {
					doMtex2 = false;
				}
			}
		}
		else {
			render_lightmaps = true;
			doMtex1 = doMtex2 = mtex_lightmaps = mtex_fbs = false;
		}

		for (waterline = 0; waterline < 2; waterline++) {
			if (!(s = model->textures[i]->texturechain[waterline])) {
				continue;
			}

			for (; s; s = s->texturechain) {
				if (mtex_lightmaps) {
					GLC_MultitextureLightmap(s->lightmaptexturenum);
				}
				else {
					s->polys->chain = lightmap_polys[s->lightmaptexturenum];
					lightmap_polys[s->lightmaptexturenum] = s->polys;
				}

				glBegin(GL_POLYGON);
				v = s->polys->verts[0];

				if (!s->texinfo->flags & TEX_SPECIAL) {
					for (k = 0; k < s->polys->numverts; k++, v += VERTEXSIZE) {
						float tex_s = gl_textureless.value && model->isworldmodel ? 0 : v[3];
						float tex_t = gl_textureless.value && model->isworldmodel ? 0 : v[4];

						if (doMtex1) {
							//Tei: textureless for the world brush models (Qrack)
							qglMultiTexCoord2f(GL_TEXTURE0, tex_s, tex_t);

							if (mtex_lightmaps) {
								qglMultiTexCoord2f(GL_LIGHTMAP_TEXTURE, v[5], v[6]);
							}

							if (mtex_fbs) {
								qglMultiTexCoord2f(GL_FB_TEXTURE, tex_s, tex_t);
							}
						}
						else {
							glTexCoord2f(tex_s, tex_t);
						}
						glVertex3fv(v);
					}
				}
				glEnd();

				if (draw_caustics && (waterline || ISUNDERWATER(contents))) {
					s->polys->caustics_chain = caustics_polys;
					caustics_polys = s->polys;
				}

				if (!waterline && draw_details) {
					s->polys->detail_chain = detail_polys;
					detail_polys = s->polys;
				}

				if (fb_texturenum && draw_fbs && !mtex_fbs) {
					if (isLumaTexture) {
						s->polys->luma_chain = luma_polys[fb_texturenum];
						luma_polys[fb_texturenum] = s->polys;
						drawlumas = true;
					}
					else {
						s->polys->fb_chain = fullbright_polys[fb_texturenum];
						fullbright_polys[fb_texturenum] = s->polys;
						drawfullbrights = true;
					}
				}
			}
		}

		if (doMtex1) {
			GL_DisableTMU(GL_TEXTURE1);
		}
		if (doMtex2) {
			GL_DisableTMU(GL_TEXTURE2);
		}
	}

	if (gl_mtexable) {
		GL_SelectTexture(GL_TEXTURE0);
	}

	if (gl_fb_bmodels.value) {
		if (render_lightmaps) {
			R_BlendLightmaps();
			render_lightmaps = false;
		}
		if (drawfullbrights) {
			R_RenderFullbrights();
			drawfullbrights = false;
		}
		if (drawlumas) {
			R_RenderLumas();
			drawlumas = false;
		}
	}
	else {
		if (drawlumas) {
			R_RenderLumas();
			drawlumas = false;
		}
		if (render_lightmaps) {
			R_BlendLightmaps();
			render_lightmaps = false;
		}
		if (drawfullbrights) {
			R_RenderFullbrights();
			drawfullbrights = false;
		}
	}

	if (gl_fogenable.value) {
		glDisable(GL_FOG);
	}

	EmitCausticsPolys();
	EmitDetailPolys();
}

void GLC_DrawWorld(void)
{
	extern msurface_t* alphachain;
	extern cvar_t gl_outline;

	if (r_drawflat.integer) {
		if (r_drawflat.integer == 1) {
			GLC_DrawFlat(cl.worldmodel);
		}
		else {
			GLC_DrawTextureChains(cl.worldmodel, 0);
			GLC_DrawFlat(cl.worldmodel);
		}
	}
	else {
		GLC_DrawTextureChains(cl.worldmodel, 0);
	}

	if ((gl_outline.integer & 2) && !RuleSets_DisallowModelOutline(NULL)) {
		R_DrawMapOutline(cl.worldmodel);
	}

	//draw the world alpha textures
	R_DrawAlphaChain(alphachain);
}

void GLC_DrawBrushModel(entity_t* e, model_t* clmodel)
{
	extern cvar_t gl_outline;

	if (r_drawflat.value != 0 && clmodel->isworldmodel) {
		if (r_drawflat.integer == 1) {
			GLC_DrawFlat(clmodel);
		}
		else {
			GLC_DrawTextureChains(clmodel, (TruePointContents(e->origin)));//R00k added contents point for underwater bmodels
			GLC_DrawFlat(clmodel);
		}
	}
	else {
		GLC_DrawTextureChains(clmodel, (TruePointContents(e->origin)));//R00k added contents point for underwater bmodels
	}

	if ((gl_outline.integer & 2) && clmodel->isworldmodel && !RuleSets_DisallowModelOutline(NULL)) {
		R_DrawMapOutline(clmodel);
	}
}
