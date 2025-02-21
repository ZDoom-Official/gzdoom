// 
//---------------------------------------------------------------------------
//
// Copyright(C) 2004-2016 Christoph Oelckers
// All rights reserved.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//--------------------------------------------------------------------------
//
/*
** gl_scene.cpp
** manages the rendering of the player's view
**
*/

#include "gi.h"
#include "a_dynlight.h"
#include "m_png.h"
#include "doomstat.h"
#include "r_data/r_interpolate.h"
#include "r_utility.h"
#include "d_player.h"
#include "i_time.h"
#include "swrenderer/r_swscene.h"
#include "swrenderer/r_renderer.h"
#include "hw_dynlightdata.h"
#include "hw_clock.h"
#include "flatvertices.h"
#include "v_palette.h"
#include "d_main.h"
#include "g_cvars.h"
#include "v_draw.h"

#include "hw_lightbuffer.h"
#include "hw_bonebuffer.h"
#include "hw_cvars.h"
#include "hwrenderer/data/hw_viewpointbuffer.h"
#include "hwrenderer/scene/hw_fakeflat.h"
#include "hwrenderer/scene/hw_clipper.h"
#include "hwrenderer/scene/hw_portal.h"
#include "hw_vrmodes.h"

EXTERN_CVAR(Bool, cl_capfps)
extern bool NoInterpolateView;

static SWSceneDrawer *swdrawer;

void CleanSWDrawer()
{
	if (swdrawer) delete swdrawer;
	swdrawer = nullptr;
}

#include "g_levellocals.h"
#include "a_dynlight.h"


void CollectLights(FLevelLocals* Level)
{
	IShadowMap* sm = &screen->mShadowMap;
	int lightindex = 0;

	// Todo: this should go through the blockmap in a spiral pattern around the player so that closer lights are preferred.
	for (auto light = Level->lights; light; light = light->next)
	{
		IShadowMap::LightsProcessed++;
		if (light->shadowmapped && light->IsActive() && lightindex < 1024)
		{
			IShadowMap::LightsShadowmapped++;

			light->mShadowmapIndex = lightindex;
			sm->SetLight(lightindex, (float)light->X(), (float)light->Y(), (float)light->Z(), light->GetRadius());
			lightindex++;
		}
		else
		{
			light->mShadowmapIndex = 1024;
		}

	}

	for (; lightindex < 1024; lightindex++)
	{
		sm->SetLight(lightindex, 0, 0, 0, 0);
	}
}


//-----------------------------------------------------------------------------
//
// Renders one viewpoint in a scene
//
//-----------------------------------------------------------------------------

sector_t* RenderViewpoint(FRenderViewpoint& mainvp, AActor* camera, IntRect* bounds, float fov, float ratio, float fovratio, bool mainview, bool toscreen)
{
	auto& RenderState = *screen->RenderState();

	R_SetupFrame(mainvp, r_viewwindow, camera);

	if (mainview && toscreen && !(camera->Level->flags3 & LEVEL3_NOSHADOWMAP) && camera->Level->HasDynamicLights && gl_light_shadowmap && screen->allowSSBO() && (screen->hwcaps & RFL_SHADER_STORAGE_BUFFER))
	{
		screen->SetAABBTree(camera->Level->aabbTree);
		screen->mShadowMap.SetCollectLights([=] {
			CollectLights(camera->Level);
		});
		screen->UpdateShadowMap();
	}
	else
	{
		// null all references to the level if we do not need a shadowmap. This will shortcut all internal calculations without further checks.
		screen->SetAABBTree(nullptr);
		screen->mShadowMap.SetCollectLights(nullptr);
	}

	screen->SetLevelMesh(camera->Level->levelMesh);

	// Update the attenuation flag of all light defaults for each viewpoint.
	// This function will only do something if the setting differs.
	FLightDefaults::SetAttenuationForLevel(!!(camera->Level->flags3 & LEVEL3_ATTENUATE));

	// Render (potentially) multiple views for stereo 3d
	// Fixme. The view offsetting should be done with a static table and not require setup of the entire render state for the mode.
	auto vrmode = VRMode::GetVRMode(mainview && toscreen);
	const int eyeCount = vrmode->mEyeCount;
	screen->FirstEye();
	for (int eye_ix = 0; eye_ix < eyeCount; ++eye_ix)
	{
		const auto& eye = vrmode->mEyes[eye_ix];
		screen->SetViewportRects(bounds);

		if (mainview) // Bind the scene frame buffer and turn on draw buffers used by ssao
		{
			bool useSSAO = (gl_ssao != 0);
			screen->SetSceneRenderTarget(useSSAO);
			RenderState.SetPassType(useSSAO ? GBUFFER_PASS : NORMAL_PASS);
			RenderState.EnableDrawBuffers(RenderState.GetPassDrawBufferCount(), true);
		}

		auto di = HWDrawInfo::StartDrawInfo(mainvp.ViewLevel, nullptr, mainvp, nullptr);
		auto& vp = di->Viewpoint;

		di->Set3DViewport(RenderState);
		di->SetViewArea();
		auto cm = di->SetFullbrightFlags(mainview ? vp.camera->player : nullptr);
		float flash = 1.f;

		// Only used by the GLES2 renderer
		RenderState.SetSpecialColormap(cm, flash);

		di->Viewpoint.FieldOfView = DAngle::fromDeg(fov);	// Set the real FOV for the current scene (it's not necessarily the same as the global setting in r_viewpoint)

		// Stereo mode specific perspective projection
		float inv_iso_dist = 1.0f;
		bool iso_ortho = (camera->ViewPos != NULL) && (camera->ViewPos->Flags & VPSF_ORTHOGRAPHIC);
		if (iso_ortho && (camera->ViewPos->Offset.Length() > 0)) inv_iso_dist = 1.0/camera->ViewPos->Offset.Length();
		di->VPUniforms.mProjectionMatrix = eye.GetProjection(fov, ratio, fovratio * inv_iso_dist, iso_ortho);
		di->ProjectionMatrix2 = eye.GetProjection(fov, ratio, fovratio, false); // Regular ol' perspective projection matrix

		// Stereo mode specific viewpoint adjustment
		vp.Pos += eye.GetViewShift(vp.HWAngles.Yaw.Degrees());
		di->SetupView(RenderState, vp.Pos.X, vp.Pos.Y, vp.Pos.Z, false, false);

		di->ProcessScene(toscreen);

		if (mainview)
		{
			PostProcess.Clock();
			if (toscreen) di->EndDrawScene(mainvp.sector, RenderState); // do not call this for camera textures.

			if (RenderState.GetPassType() == GBUFFER_PASS) // Turn off ssao draw buffers
			{
				RenderState.SetPassType(NORMAL_PASS);
				RenderState.EnableDrawBuffers(1);
			}

			screen->PostProcessScene(false, cm, flash, [&]() { di->DrawEndScene2D(mainvp.sector, RenderState); });
			PostProcess.Unclock();
		}
		// Reset colormap so 2D drawing isn't affected
		RenderState.SetSpecialColormap(CM_DEFAULT, 1);

		di->EndDrawInfo();
		if (eyeCount - eye_ix > 1)
			screen->NextEye(eyeCount);
	}

	return mainvp.sector;
}

void DoWriteSavePic(FileWriter* file, ESSType ssformat, uint8_t* scr, int width, int height, sector_t* viewsector, bool upsidedown)
{
	PalEntry palette[256];
	PalEntry modulateColor;
	auto blend = V_CalcBlend(viewsector, &modulateColor);
	int pixelsize = 1;
	// Apply the screen blend, because the renderer does not provide this.
	if (ssformat == SS_RGB)
	{
		int numbytes = width * height * 3;
		pixelsize = 3;
		if (modulateColor != 0xffffffff)
		{
			float r = modulateColor.r / 255.f;
			float g = modulateColor.g / 255.f;
			float b = modulateColor.b / 255.f;
			for (int i = 0; i < numbytes; i += 3)
			{
				scr[i] = uint8_t(scr[i] * r);
				scr[i + 1] = uint8_t(scr[i + 1] * g);
				scr[i + 2] = uint8_t(scr[i + 2] * b);
			}
		}
		float iblendfac = 1.f - blend.W;
		blend.X *= blend.W;
		blend.Y *= blend.W;
		blend.Z *= blend.W;
		for (int i = 0; i < numbytes; i += 3)
		{
			scr[i] = uint8_t(scr[i] * iblendfac + blend.X);
			scr[i + 1] = uint8_t(scr[i + 1] * iblendfac + blend.Y);
			scr[i + 2] = uint8_t(scr[i + 2] * iblendfac + blend.Z);
		}
	}
	else
	{
		// Apply the screen blend to the palette. The colormap related parts get skipped here because these are already part of the image.
		DoBlending(GPalette.BaseColors, palette, 256, uint8_t(blend.X), uint8_t(blend.Y), uint8_t(blend.Z), uint8_t(blend.W * 255));
	}

	int pitch = width * pixelsize;
	if (upsidedown)
	{
		scr += ((height - 1) * width * pixelsize);
		pitch *= -1;
	}

	M_CreatePNG(file, scr, ssformat == SS_PAL ? palette : nullptr, ssformat, width, height, pitch, vid_gamma);
}

//===========================================================================
//
// Render the view to a savegame picture
//
//===========================================================================

void WriteSavePic(player_t* player, FileWriter* file, int width, int height)
{
	if (!V_IsHardwareRenderer())
	{
		SWRenderer->WriteSavePic(player, file, width, height);
	}
	else
	{
		IntRect bounds;
		bounds.left = 0;
		bounds.top = 0;
		bounds.width = width;
		bounds.height = height;
		auto& RenderState = *screen->RenderState();

		// we must be sure the GPU finished reading from the buffer before we fill it with new data.
		screen->WaitForCommands(false);

		// Switch to render buffers dimensioned for the savepic
		screen->SetSaveBuffers(true);
		screen->ImageTransitionScene(true);

		hw_postprocess.SetTonemapMode(level.info ? level.info->tonemap : ETonemapMode::None);
		hw_ClearFakeFlat();
		screen->mVertexData->Reset();
		RenderState.SetVertexBuffer(screen->mVertexData);
		screen->mLights->Clear();
		screen->mBones->Clear();
		screen->mViewpoints->Clear();

		// This shouldn't overwrite the global viewpoint even for a short time.
		FRenderViewpoint savevp;
		sector_t* viewsector = RenderViewpoint(savevp, players[consoleplayer].camera, &bounds, r_viewpoint.FieldOfView.Degrees(), 1.6f, 1.6f, true, false);
		RenderState.EnableStencil(false);
		RenderState.SetNoSoftLightLevel();

		TArray<uint8_t> scr(width * height * 3, true);
		screen->CopyScreenToBuffer(width, height, scr.Data());

		DoWriteSavePic(file, SS_RGB, scr.Data(), width, height, viewsector, screen->FlipSavePic());

		// Switch back the screen render buffers
		screen->SetViewportRects(nullptr);
		screen->SetSaveBuffers(false);
	}
}

//===========================================================================
//
// Renders the main view
//
//===========================================================================

static void CheckTimer(FRenderState &state, uint64_t ShaderStartTime)
{
	// if firstFrame is not yet initialized, initialize it to current time
	// if we're going to overflow a float (after ~4.6 hours, or 24 bits), re-init to regain precision
	if ((state.firstFrame == 0) || (screen->FrameTime - state.firstFrame >= 1 << 24) || ShaderStartTime >= state.firstFrame)
		state.firstFrame = screen->FrameTime - 1;
}


sector_t* RenderView(player_t* player)
{
	auto RenderState = screen->RenderState();
	RenderState->SetVertexBuffer(screen->mVertexData);
	screen->mVertexData->Reset();
	hw_postprocess.SetTonemapMode(level.info ? level.info->tonemap : ETonemapMode::None);

	sector_t* retsec;
	if (!V_IsHardwareRenderer())
	{
		screen->SetActiveRenderTarget();	// only relevant for Vulkan

		if (!swdrawer) swdrawer = new SWSceneDrawer;
		retsec = swdrawer->RenderView(player);
	}
	else
	{
		hw_ClearFakeFlat();

		iter_dlightf = iter_dlight = draw_dlight = draw_dlightf = 0;

		checkBenchActive();

		// reset statistics counters
		ResetProfilingData();

		// Get this before everything else
		if (cl_capfps || r_NoInterpolate) r_viewpoint.TicFrac = 1.;
		else r_viewpoint.TicFrac = I_GetTimeFrac();

		screen->mLights->Clear();
		screen->mBones->Clear();
		screen->mViewpoints->Clear();

		// NoInterpolateView should have no bearing on camera textures, but needs to be preserved for the main view below.
		bool saved_niv = NoInterpolateView;
		NoInterpolateView = false;

		// Shader start time does not need to be handled per level. Just use the one from the camera to render from.
		if (player->camera)
			CheckTimer(*RenderState, player->camera->Level->ShaderStartTime);

		// Draw all canvases that changed
		for (FCanvas* canvas : AllCanvases)
		{
			if (canvas->Tex && canvas->Tex->CheckNeedsUpdate())
			{
				screen->RenderTextureView(canvas->Tex, [=](IntRect& bounds)
					{
						screen->SetViewportRects(&bounds);
						Draw2D(&canvas->Drawer, *screen->RenderState(), 0, 0, canvas->Tex->GetWidth(), canvas->Tex->GetHeight());
						canvas->Drawer.Clear();
					});
				canvas->Tex->SetUpdated(true);
			}
		}

		// prepare all camera textures that have been used in the last frame.
		// This must be done for all levels, not just the primary one!
		for (auto Level : AllLevels())
		{
			Level->canvasTextureInfo.UpdateAll([&](AActor* camera, FCanvasTexture* camtex, double fov)
				{
					screen->RenderTextureView(camtex, [=](IntRect& bounds)
						{
							FRenderViewpoint texvp;
							float ratio = camtex->aspectRatio / Level->info->pixelstretch;
							RenderViewpoint(texvp, camera, &bounds, fov, ratio, ratio, false, false);
						});
				});
		}
		NoInterpolateView = saved_niv;

		// now render the main view
		float fovratio;
		float ratio = r_viewwindow.WidescreenRatio;
		if (r_viewwindow.WidescreenRatio >= 1.3f)
		{
			fovratio = 1.333333f;
		}
		else
		{
			fovratio = ratio;
		}

		screen->ImageTransitionScene(true); // Only relevant for Vulkan.

		retsec = RenderViewpoint(r_viewpoint, player->camera, NULL, r_viewpoint.FieldOfView.Degrees(), ratio, fovratio, true, true);
	}
	All.Unclock();
	return retsec;
}

