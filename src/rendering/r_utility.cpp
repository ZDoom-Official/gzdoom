//-----------------------------------------------------------------------------
//
// Copyright 1993-1996 id Software
// Copyright 1994-1996 Raven Software
// Copyright 1999-2016 Randy Heit
// Copyright 2002-2016 Christoph Oelckers
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//		Rendering main loop and setup functions,
//		 utility functions (BSP, geometry, trigonometry).
//		See tables.c, too.
//
//-----------------------------------------------------------------------------

// HEADER FILES ------------------------------------------------------------

#include <stdlib.h>
#include <math.h>


#include "doomdef.h"
#include "d_net.h"
#include "doomstat.h"
#include "m_random.h"
#include "m_bbox.h"
#include "r_sky.h"
#include "st_stuff.h"
#include "c_dispatch.h"
#include "v_video.h"
#include "stats.h"
#include "i_video.h"
#include "a_sharedglobal.h"
#include "p_3dmidtex.h"
#include "r_data/r_interpolate.h"
#include "po_man.h"
#include "p_effect.h"
#include "st_start.h"
#include "v_font.h"
#include "swrenderer/r_renderer.h"
#include "serializer.h"
#include "r_utility.h"
#include "d_player.h"
#include "p_local.h"
#include "g_levellocals.h"
#include "p_maputl.h"
#include "sbar.h"
#include "vm.h"
#include "i_time.h"
#include "actorinlines.h"
#include "g_game.h"
#include "i_system.h"
#include "v_draw.h"
#include "i_interface.h"
#include "d_main.h"

const float MY_SQRT2    = 1.41421356237309504880; // sqrt(2)
// EXTERNAL DATA DECLARATIONS ----------------------------------------------

extern bool DrawFSHUD;		// [RH] Defined in d_main.cpp
EXTERN_CVAR (Bool, cl_capfps)

// TYPES -------------------------------------------------------------------

struct InterpolationViewer
{
	struct instance
	{
		DVector3 Pos, ViewPos;
		DRotator Angles;
		DRotator ViewAngles;
	};

	AActor* ViewActor;
	DVector3 ViewOffset, RelativeViewOffset; // This has to be a separate field since it needs the real-time mouse angles.
	DRotator AngleOffsets;
	int prevTic;
	instance Old, New;
};

// PRIVATE DATA DECLARATIONS -----------------------------------------------
static TArray<InterpolationViewer> PastViewers;
static FCRandom pr_torchflicker ("TorchFlicker");
static FCRandom pr_hom;
bool NoInterpolateView;	// GL needs access to this.
static TArray<DVector3a> InterpolationPath;

// PUBLIC DATA DEFINITIONS -------------------------------------------------

CVAR (Bool, r_deathcamera, false, CVAR_ARCHIVE)
CVAR (Int, r_clearbuffer, 0, 0)
CVAR (Bool, r_drawvoxels, true, 0)
CVAR (Bool, r_drawplayersprites, true, 0)	// [RH] Draw player sprites?
CVARD (Bool, r_radarclipper, false, CVAR_ARCHIVE | CVAR_SERVERINFO | CVAR_CHEAT, "Use the horizontal clipper from camera->tracer's perspective")
CVARD (Bool, r_dithertransparency, false, CVAR_ARCHIVE | CVAR_SERVERINFO | CVAR_CHEAT, "Use dithered-transparency shading for actor-occluding level geometry")
CUSTOM_CVAR(Float, r_quakeintensity, 1.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (self < 0.f) self = 0.f;
	else if (self > 1.f) self = 1.f;
}

CUSTOM_CVARD(Int, r_actorspriteshadow, 1, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "render actor sprite shadows. 0 = off, 1 = default, 2 = always on")
{
	if (self < 0)
		self = 0;
	else if (self > 2)
		self = 2;
}
CUSTOM_CVARD(Float, r_actorspriteshadowdist, 1500.0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "how far sprite shadows should be rendered")
{
	if (self < 0.f)
		self = 0.f;
	else if (self > 8192.f)
		self = 8192.f;
}
CUSTOM_CVARD(Float, r_actorspriteshadowalpha, 0.5, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "maximum sprite shadow opacity, only effective with hardware renderers (0.0 = fully transparent, 1.0 = opaque)")
{
	if (self < 0.f)
		self = 0.f;
	else if (self > 1.f)
		self = 1.f;
}
CUSTOM_CVARD(Float, r_actorspriteshadowfadeheight, 0.0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "distance over which sprite shadows should fade, only effective with hardware renderers (0 = infinite)")
{
	if (self < 0.f)
		self = 0.f;
	else if (self > 8192.f)
		self = 8192.f;
}

int 			viewwindowx;
int 			viewwindowy;
int				viewwidth;
int 			viewheight;

FRenderViewpoint::FRenderViewpoint()
{
	player = nullptr;
	Pos = { 0.0, 0.0, 0.0 };
	ActorPos = { 0.0, 0.0, 0.0 };
	Angles = { nullAngle, nullAngle, nullAngle };
	Path[0] = { 0.0, 0.0, 0.0 };
	Path[1] = { 0.0, 0.0, 0.0 };
	Cos = 0.0;
	Sin = 0.0;
	TanCos = 0.0;
	TanSin = 0.0;
	PitchCos = 0.0;
	PitchSin = 0.0;
	floordistfact = 0.0;
	cotfloor = 0.0;
	camera = nullptr;
	sector = nullptr;
	FieldOfView =  DAngle::fromDeg(90.); // Angles in the SCREENWIDTH wide window
	ScreenProj = 0.0;
	ScreenProjX = 0.0;
	TicFrac = 0.0;
	FrameTime = 0;
	extralight = 0;
	showviewer = false;
}

FRenderViewpoint r_viewpoint;
FViewWindow		r_viewwindow;

bool			r_NoInterpolate;

angle_t			LocalViewAngle;
int				LocalViewPitch;
bool			LocalKeyboardTurner;

int				setblocks;

unsigned int	R_OldBlend = ~0;
int 			validcount = 1; 	// increment every time a check is made
int 			dl_validcount = 1; 	// increment every time a check is made
int			freelookviewheight;

DVector3a view;
DAngle viewpitch;

DEFINE_GLOBAL(LocalViewPitch);

// CODE --------------------------------------------------------------------

//==========================================================================
//
// R_SetFOV
//
// Changes the field of view in degrees
//
//==========================================================================

void R_SetFOV (FRenderViewpoint &viewpoint, DAngle fov)
{

	if (fov < DAngle::fromDeg(5.)) fov =  DAngle::fromDeg(5.);
	else if (fov > DAngle::fromDeg(170.)) fov = DAngle::fromDeg(170.);
	if (fov != viewpoint.FieldOfView)
	{
		viewpoint.FieldOfView = fov;
		setsizeneeded = true;
	}
}

//==========================================================================
//
// R_SetViewSize
//
// Do not really change anything here, because it might be in the middle
// of a refresh. The change will take effect next refresh.
//
//==========================================================================

void R_SetViewSize (int blocks)
{
	setsizeneeded = true;
	setblocks = blocks;
}

//==========================================================================
//
// R_SetWindow
//
//==========================================================================

void R_SetWindow (FRenderViewpoint &viewpoint, FViewWindow &viewwindow, int windowSize, int fullWidth, int fullHeight, int stHeight, bool renderingToCanvas)
{
	if (windowSize >= 11)
	{
		viewwidth = fullWidth;
		freelookviewheight = viewheight = fullHeight;
	}
	else if (windowSize == 10)
	{
		viewwidth = fullWidth;
		viewheight = stHeight;
		freelookviewheight = fullHeight;
	}
	else
	{
		viewwidth = ((setblocks*fullWidth)/10) & (~15);
		viewheight = ((setblocks*stHeight)/10)&~7;
		freelookviewheight = ((setblocks*fullHeight)/10)&~7;
	}

	if (renderingToCanvas)
	{
		viewwindow.WidescreenRatio = fullWidth / (float)fullHeight;
	}
	else
	{
		viewwindow.WidescreenRatio = ActiveRatio(fullWidth, fullHeight);
		DrawFSHUD = (windowSize == 11);
	}

	
	// [RH] Sky height fix for screens not 200 (or 240) pixels tall
	R_InitSkyMap ();

	viewwindow.centery = viewheight/2;
	viewwindow.centerx = viewwidth/2;
	if (AspectTallerThanWide(viewwindow.WidescreenRatio))
	{
		viewwindow.centerxwide = viewwindow.centerx;
	}
	else
	{
		viewwindow.centerxwide = viewwindow.centerx * AspectMultiplier(viewwindow.WidescreenRatio) / 48;
	}


	DAngle fov = viewpoint.FieldOfView;

	// For widescreen displays, increase the FOV so that the middle part of the
	// screen that would be visible on a 4:3 display has the requested FOV.
	if (viewwindow.centerxwide != viewwindow.centerx)
	{ // centerxwide is what centerx would be if the display was not widescreen
		fov = DAngle::fromRad(2 * atan(viewwindow.centerx * tan(fov.Radians()/2) / double(viewwindow.centerxwide)));
		if (fov > DAngle::fromDeg(170.)) fov =  DAngle::fromDeg(170.);
	}
	viewwindow.FocalTangent = tan(fov.Radians() / 2);
}

//==========================================================================
//
// R_ExecuteSetViewSize
//
//==========================================================================

void R_ExecuteSetViewSize (FRenderViewpoint &viewpoint, FViewWindow &viewwindow)
{
	setsizeneeded = false;

	R_SetWindow (viewpoint, viewwindow, setblocks, SCREENWIDTH, SCREENHEIGHT, StatusBar->GetTopOfStatusbar());

	// Handle resize, e.g. smaller view windows with border and/or status bar.
	viewwindowx = (screen->GetWidth() - viewwidth) >> 1;

	// Same with base row offset.
	viewwindowy = (viewwidth == screen->GetWidth()) ? 0 : (StatusBar->GetTopOfStatusbar() - viewheight) >> 1;
}

//==========================================================================
//
// r_visibility
//
// Controls how quickly light ramps across a 1/z range.
//
//==========================================================================

double R_ClampVisibility(double vis)
{
	// Allow negative visibilities, just for novelty's sake
	return clamp(vis, -204.7, 204.7);	// (205 and larger do not work in 5:4 aspect ratio)
}

CUSTOM_CVAR(Float, r_visibility, 8.0f, CVAR_NOINITCALL)
{
	if (netgame && self != 8.0f)
	{
		Printf("Visibility cannot be changed in net games.\n");
		self = 8.0f;
	}
	else
	{
		float clampValue = (float)R_ClampVisibility(self);
		if (self != clampValue)
			self = clampValue;
	}
}

//==========================================================================
//
// R_GetGlobVis
//
// Calculates the global visibility constant used by the software renderer
//
//==========================================================================

double R_GetGlobVis(const FViewWindow &viewwindow, double vis)
{
	vis = R_ClampVisibility(vis);

	double virtwidth = screen->GetWidth();
	double virtheight = screen->GetHeight();

	if (AspectTallerThanWide(viewwindow.WidescreenRatio))
	{
		virtheight = (virtheight * AspectMultiplier(viewwindow.WidescreenRatio)) / 48;
	}
	else
	{
		virtwidth = (virtwidth * AspectMultiplier(viewwindow.WidescreenRatio)) / 48;
	}

	double YaspectMul = 320.0 * virtheight / (200.0 * virtwidth);
	double InvZtoScale = YaspectMul * viewwindow.centerx;

	double wallVisibility = vis;

	// Prevent overflow on walls
	double maxVisForWall = (InvZtoScale * (screen->GetWidth() * r_Yaspect) / (viewwidth * screen->GetHeight() * viewwindow.FocalTangent));
	maxVisForWall = 32767.0 / maxVisForWall;
	if (vis < 0 && vis < -maxVisForWall)
		wallVisibility = -maxVisForWall;
	else if (vis > 0 && vis > maxVisForWall)
		wallVisibility = maxVisForWall;

	wallVisibility = InvZtoScale * screen->GetWidth() * AspectBaseHeight(viewwindow.WidescreenRatio) / (viewwidth * screen->GetHeight() * 3) * (wallVisibility * viewwindow.FocalTangent);

	return wallVisibility / viewwindow.FocalTangent;
}

//==========================================================================
//
// CVAR screenblocks
//
// Selects the size of the visible window
//
//==========================================================================

CUSTOM_CVAR (Int, screenblocks, 10, CVAR_ARCHIVE)
{
	if (self > 12)
		self = 12;
	else if (self < 3)
		self = 3;
	else
		R_SetViewSize (self);
}

//==========================================================================
//
//
//
//==========================================================================

FRenderer *CreateSWRenderer();
FRenderer* SWRenderer;

//==========================================================================
//
// R_Init
//
//==========================================================================

void R_Init ()
{
	R_InitTranslationTables ();
	R_SetViewSize (screenblocks);

	if (SWRenderer == NULL)
	{
		SWRenderer = CreateSWRenderer();
	}

	SWRenderer->Init();
}

//==========================================================================
//
// R_Shutdown
//
//==========================================================================

void R_Shutdown ()
{
	if (SWRenderer != nullptr) delete SWRenderer;
	SWRenderer = nullptr;
}

//==========================================================================
//
// P_NoInterpolation
//
//==========================================================================

//CVAR (Int, tf, 0, 0)

bool P_NoInterpolation(player_t const *player, AActor const *actor)
{
	return player != nullptr
		&& !(player->cheats & CF_INTERPVIEW)
		&& player - players == consoleplayer
		&& actor == player->mo
		&& !demoplayback
		&& !(player->cheats & (CF_TOTALLYFROZEN | CF_FROZEN))
		&& player->playerstate == PST_LIVE
		&& player->mo->reactiontime == 0
		&& !NoInterpolateView
		&& !paused
		&& !LocalKeyboardTurner
		&& !player->mo->isFrozen();
}

//==========================================================================
//
// R_InterpolateView
//
//==========================================================================

void R_InterpolateView(FRenderViewpoint& viewPoint, const player_t* const player, const double ticFrac, InterpolationViewer* const iView)
{
	if (NoInterpolateView)
	{
		InterpolationPath.Clear();
		NoInterpolateView = false;
		iView->Old = iView->New;
	}

	const double inverseTicFrac = 1.0 - ticFrac;
	const auto viewLvl = viewPoint.ViewLevel;

	DAngle prevYaw = iView->Old.Angles.Yaw;
	DAngle curYaw = iView->New.Angles.Yaw;
	if (!cl_capfps)
	{
		if ((iView->Old.Pos.X != iView->New.Pos.X || iView->Old.Pos.Y != iView->New.Pos.Y) && InterpolationPath.Size() > 0)
		{
			// Interpolating through line portals is a messy affair.
			// What needs be done is to store the portal transitions of the camera actor as waypoints
			// and then find out on which part of the path the current view lies.
			double totalPathLength = 0.0;
			double totalZDiff = 0.0;
			DAngle totalYawDiff = nullAngle;
			DVector3a oldPos = { { iView->Old.Pos.X, iView->Old.Pos.Y, 0.0 }, nullAngle };
			DVector3a newPos = { { iView->New.Pos.X, iView->New.Pos.Y, 0.0 }, nullAngle };
			InterpolationPath.Push(newPos);	// Add this to  the array to simplify the loops below.

			for (size_t i = 0u; i < InterpolationPath.Size(); i += 2u)
			{
				const DVector3a& start = !i ? oldPos : InterpolationPath[i - 1u];
				const DVector3a& end = InterpolationPath[i];
				totalPathLength += (end.pos - start.pos).Length();
				totalZDiff += start.pos.Z;
				totalYawDiff += start.angle;
			}

			double interpolatedLength = totalPathLength * ticFrac;
			double zDiff = 0.0;
			DAngle yawDiff = nullAngle;
			double prevViewZ = iView->Old.Pos.Z;
			double curViewZ = iView->New.Pos.Z;
			for (size_t i = 0u; i < InterpolationPath.Size(); i += 2u)
			{
				const DVector3a& start = !i ? oldPos : InterpolationPath[i - 1u];
				const DVector3a& end = InterpolationPath[i];
				const double fragmentLength = (end.pos - start.pos).Length();
				zDiff += start.pos.Z;
				yawDiff += start.angle;

				if (fragmentLength <= interpolatedLength)
				{
					interpolatedLength -= fragmentLength;
				}
				else
				{
					prevViewZ += zDiff;
					curViewZ -= totalZDiff - zDiff;
					prevYaw += yawDiff;
					curYaw -= totalYawDiff - yawDiff;

					const DVector2 viewPos = start.pos.XY() + ((interpolatedLength / fragmentLength) * (end.pos - start.pos).XY());
					viewPoint.Pos = { viewPos, prevViewZ * inverseTicFrac + curViewZ * ticFrac };
					break;
				}
			}

			InterpolationPath.Pop();
			viewPoint.Path[0] = iView->Old.Pos;
			viewPoint.Path[1] = viewPoint.Path[0] + (InterpolationPath[0].pos - viewPoint.Path[0]).XY().MakeResize(totalPathLength);
		}
		else
		{
			const int prevPortalGroup = viewLvl->PointInRenderSubsector(iView->Old.Pos)->sector->PortalGroup;
			const int curPortalGroup = viewLvl->PointInRenderSubsector(iView->New.Pos)->sector->PortalGroup;

			if (viewPoint.IsAllowedOoB() && prevPortalGroup != curPortalGroup) viewPoint.Pos = iView->New.Pos;
			else
			{
				const DVector2 portalOffset = viewLvl->Displacements.getOffset(prevPortalGroup, curPortalGroup);
				viewPoint.Pos = iView->Old.Pos * inverseTicFrac + (iView->New.Pos - portalOffset) * ticFrac;
			}
			viewPoint.Path[0] = viewPoint.Path[1] = iView->New.Pos;
		}
	}
	else
	{
		viewPoint.Pos = iView->New.Pos;
		viewPoint.Path[0] = viewPoint.Path[1] = iView->New.Pos;
	}

	// Due to interpolation this is not necessarily the same as the sector the camera is in.
	viewPoint.sector = viewLvl->PointInRenderSubsector(viewPoint.Pos)->sector;
	if (!viewPoint.IsAllowedOoB() || !V_IsHardwareRenderer())
	{
		bool moved = false;
		while (!viewPoint.sector->PortalBlocksMovement(sector_t::ceiling))
		{
			if (viewPoint.Pos.Z > viewPoint.sector->GetPortalPlaneZ(sector_t::ceiling))
			{
				const DVector2 offset = viewPoint.sector->GetPortalDisplacement(sector_t::ceiling);
				viewPoint.Pos += offset;
				viewPoint.ActorPos += offset;
				viewPoint.sector = viewPoint.sector->GetPortal(sector_t::ceiling)->mDestination;
				moved = true;
			}
			else
			{
				break;
			}
		}

		if (!moved)
		{
			while (!viewPoint.sector->PortalBlocksMovement(sector_t::floor))
			{
				if (viewPoint.Pos.Z < viewPoint.sector->GetPortalPlaneZ(sector_t::floor))
				{
					const DVector2 offset = viewPoint.sector->GetPortalDisplacement(sector_t::floor);
					viewPoint.Pos += offset;
					viewPoint.ActorPos += offset;
					viewPoint.sector = viewPoint.sector->GetPortal(sector_t::floor)->mDestination;
				}
				else
				{
					break;
				}
			}
		}
	}

	if (P_NoInterpolation(player, viewPoint.camera))
	{
		viewPoint.Angles.Yaw = curYaw + DAngle::fromBam(LocalViewAngle);
		const DAngle delta = player->centering ? nullAngle : DAngle::fromBam(LocalViewPitch);
		viewPoint.Angles.Pitch = clamp<DAngle>((iView->New.Angles.Pitch - delta).Normalized180(), player->MinPitch, player->MaxPitch);
		viewPoint.Angles.Roll = iView->New.Angles.Roll;
	}
	else
	{
		viewPoint.Angles.Pitch = iView->Old.Angles.Pitch + deltaangle(iView->Old.Angles.Pitch, iView->New.Angles.Pitch) * ticFrac;
		viewPoint.Angles.Yaw = prevYaw + deltaangle(prevYaw, curYaw) * ticFrac;
		viewPoint.Angles.Roll = iView->Old.Angles.Roll + deltaangle(iView->Old.Angles.Roll, iView->New.Angles.Roll) * ticFrac;
	}

	// Now that the base position and angles are set, add offsets.

	const DViewPosition* const vPos = iView->ViewActor->ViewPos;
	if (vPos != nullptr && !(vPos->Flags & VPSF_ABSOLUTEPOS)
		&& (player == nullptr || gamestate == GS_TITLELEVEL || (!(player->cheats & CF_CHASECAM) && (!r_deathcamera || !(iView->ViewActor->flags6 & MF6_KILLED)))))
	{
		DVector3 vOfs = {};
		if (player == nullptr || !(player->cheats & CF_NOVIEWPOSINTERP))
			vOfs = iView->Old.ViewPos * inverseTicFrac + iView->New.ViewPos * ticFrac;
		else
			vOfs = iView->New.ViewPos;

		if (vPos->Flags & VPSF_ABSOLUTEOFFSET)
			iView->ViewOffset += vOfs;
		else
			iView->RelativeViewOffset += vOfs;
	}

	DVector3 posOfs = iView->ViewOffset;
	if (!iView->RelativeViewOffset.isZero())
		posOfs += DQuaternion::FromAngles(viewPoint.Angles.Yaw, viewPoint.Angles.Pitch, viewPoint.Angles.Roll) * iView->RelativeViewOffset;

	// Now that we have the current interpolated position, offset from that directly (for view offset + chase cam).
	if (!posOfs.isZero())
	{
		const double distance = posOfs.Length();
		posOfs /= distance;
		R_OffsetView(viewPoint, posOfs, distance);
	}

	viewPoint.Angles += iView->AngleOffsets;

	// [MR] Apply the view angles as an offset if ABSVIEWANGLES isn't specified.
	if (!(viewPoint.camera->flags8 & MF8_ABSVIEWANGLES))
	{
		if (player == nullptr || (player->cheats & CF_INTERPVIEWANGLES))
		{
			viewPoint.Angles.Yaw += iView->Old.ViewAngles.Yaw + deltaangle(iView->Old.ViewAngles.Yaw, iView->New.ViewAngles.Yaw) * ticFrac;
			viewPoint.Angles.Pitch += iView->Old.ViewAngles.Pitch + deltaangle(iView->Old.ViewAngles.Pitch, iView->New.ViewAngles.Pitch) * ticFrac;
			viewPoint.Angles.Roll += iView->Old.ViewAngles.Roll + deltaangle(iView->Old.ViewAngles.Roll, iView->New.ViewAngles.Roll) * ticFrac;
		}
		else
		{
			viewPoint.Angles += iView->New.ViewAngles;
		}
	}

	viewPoint.Angles.Yaw = viewPoint.Angles.Yaw.Normalized180();
	viewPoint.Angles.Pitch = viewPoint.Angles.Pitch.Normalized180();
	viewPoint.Angles.Roll = viewPoint.Angles.Roll.Normalized180();
}

//==========================================================================
//
// R_ResetViewInterpolation
//
//==========================================================================

void R_ResetViewInterpolation ()
{
	InterpolationPath.Clear();
	NoInterpolateView = true;
}

//==========================================================================
//
// R_SetViewAngle
// sets all values derived from the view yaw.
//
//==========================================================================

void FRenderViewpoint::SetViewAngle(const FViewWindow& viewWindow)
{
	Sin = Angles.Yaw.Sin();
	Cos = Angles.Yaw.Cos();

	TanSin = viewWindow.FocalTangent * Sin;
	TanCos = viewWindow.FocalTangent * Cos;

	PitchSin = Angles.Pitch.Sin();
	PitchCos = Angles.Pitch.Cos();

	floordistfact = MY_SQRT2 + ( fabs(Cos) > fabs(Sin) ? 1.0/fabs(Cos) : 1.0/fabs(Sin) );
	cotfloor = ( fabs(Cos) > fabs(Sin) ? fabs(Sin/Cos) : fabs(Cos/Sin) );

	const DVector2 v = Angles.Yaw.ToVector();
	ViewVector.X = v.X;
	ViewVector.Y = v.Y;
	HWAngles.Yaw = FAngle::fromDeg(270.0 - Angles.Yaw.Degrees());
	ViewVector3D.X = v.X * PitchCos;
	ViewVector3D.Y = v.Y * PitchCos;
	ViewVector3D.Z = -PitchSin;

	if (IsOrtho() || IsAllowedOoB()) // These auto-ensure that camera and camera->ViewPos exist
	{
		if (camera->tracer != NULL)
		{
			OffPos = camera->tracer->Pos();
		}
		else
		{
			OffPos = Pos + ViewVector3D * camera->ViewPos->Offset.Length();
		}
	}

	if (IsOrtho() && (camera->ViewPos->Offset.XY().Length() > 0.0))
	{
		ScreenProj = 1.34396 / camera->ViewPos->Offset.Length() / tan (FieldOfView.Radians()*0.5); // [DVR] Estimated. +/-1 should be top/bottom of screen.
		ScreenProjX = ScreenProj * 0.5 / viewWindow.WidescreenRatio;
	}

}

//==========================================================================
//
// R_IsAllowedOoB()
// Checks if camera actor exists, has viewpos,
// and viewpos has VPSF_ALLOWOUTOFBOUNDS flag set.
//
//==========================================================================

bool FRenderViewpoint::IsAllowedOoB()
{
	return (camera && camera->ViewPos && (camera->ViewPos->Flags & VPSF_ALLOWOUTOFBOUNDS));
}

//==========================================================================
//
// R_IsOrtho()
// Checks if camera actor exists, has viewpos,
// and viewpos has VPSF_ORTHOGRAPHIC flag set.
//
//==========================================================================

bool FRenderViewpoint::IsOrtho()
{
	return (camera && camera->ViewPos && (camera->ViewPos->Flags & VPSF_ORTHOGRAPHIC));
}

//==========================================================================
//
// FindPastViewer
//
//==========================================================================

static InterpolationViewer *FindPastViewer (AActor *actor)
{
	for (unsigned int i = 0; i < PastViewers.Size(); ++i)
	{
		if (PastViewers[i].ViewActor == actor)
		{
			return &PastViewers[i];
		}
	}

	// Not found, so make a new one
	InterpolationViewer iview;
	memset(&iview, 0, sizeof(iview));
	iview.ViewActor = actor;
	iview.prevTic = -1;
	InterpolationPath.Clear();
	return &PastViewers[PastViewers.Push (iview)];
}

//==========================================================================
//
// R_FreePastViewers
//
//==========================================================================

void R_FreePastViewers ()
{
	InterpolationPath.Clear();
	PastViewers.Clear ();
}

//==========================================================================
//
// R_ClearPastViewer
//
// If the actor changed in a non-interpolatable way, remove it.
//
//==========================================================================

void R_ClearPastViewer (AActor *actor)
{
	InterpolationPath.Clear();
	for (unsigned int i = 0; i < PastViewers.Size(); ++i)
	{
		if (PastViewers[i].ViewActor == actor)
		{
			// Found it, so remove it.
			if (i == PastViewers.Size())
			{
				PastViewers.Delete (i);
			}
			else
			{
				PastViewers.Pop (PastViewers[i]);
			}
		}
	}
}

//==========================================================================
//
// R_RebuildViewInterpolation
//
//==========================================================================

void R_RebuildViewInterpolation(player_t *player)
{
	if (player == NULL || player->camera == NULL)
		return;

	if (!NoInterpolateView)
		return;
	NoInterpolateView = false;

	InterpolationViewer *iview = FindPastViewer(player->camera);

	iview->Old = iview->New;
	InterpolationPath.Clear();
}

//==========================================================================
//
// R_GetViewInterpolationStatus
//
//==========================================================================

bool R_GetViewInterpolationStatus()
{
	return NoInterpolateView;
}


//==========================================================================
//
// R_ClearInterpolationPath
//
//==========================================================================

void R_ClearInterpolationPath()
{
	InterpolationPath.Clear();
}

//==========================================================================
//
// R_AddInterpolationPoint
//
//==========================================================================

void R_AddInterpolationPoint(const DVector3a &vec)
{
	InterpolationPath.Push(vec);
}

//==========================================================================
//
// QuakePower
//
//==========================================================================

static double QuakePower(double factor, double intensity, double offset)
{ 
	double randumb;
	if (intensity == 0)
	{
		randumb = 0;
	}
	else
	{
		randumb = pr_torchflicker.GenRand_Real2() * (intensity * 2) - intensity;
	}
	return factor * (offset + randumb);
}

//==========================================================================
//
// R_DoActorTickerAngleChanges
//
//==========================================================================

static void R_DoActorTickerAngleChanges(player_t* const player, DRotator& angles, const double scale)
{
	for (unsigned i = 0; i < 3; i++)
	{
		if (fabs(player->angleOffsetTargets[i].Degrees()) >= EQUAL_EPSILON)
			angles[i] += player->angleOffsetTargets[i] * scale;
	}
}

//==========================================================================
//
// R_SetupFrame
//
//==========================================================================

EXTERN_CVAR(Float, chase_dist)
EXTERN_CVAR(Float, chase_height)

void R_SetupFrame(FRenderViewpoint& viewPoint, const FViewWindow& viewWindow, AActor* const actor)
{
	viewPoint.TicFrac = I_GetTimeFrac();
	if (cl_capfps || r_NoInterpolate)
		viewPoint.TicFrac = 1.0;

	const int curTic = I_GetTime();

	if (actor == nullptr)
		I_Error("Tried to render from a null actor.");

	viewPoint.ViewLevel = actor->Level;

	player_t* player = actor->player;
	if (player != nullptr && player->mo == actor)
	{
		if (player->camera == nullptr)
			player->camera = player->mo;

		viewPoint.camera = player->camera; // [RH] Use camera instead of view player.
	}
	else
	{
		viewPoint.camera = actor;
	}

	if (viewPoint.camera == nullptr)
		I_Error("You lost your body. Bad dehacked work is likely to blame.");

	InterpolationViewer* const iView = FindPastViewer(viewPoint.camera);
	// Always reset these back to zero.
	iView->ViewOffset.Zero();
	iView->RelativeViewOffset.Zero();
	iView->AngleOffsets.Zero();
	if (iView->prevTic != -1 && curTic > iView->prevTic)
	{
		iView->prevTic = curTic;
		iView->Old = iView->New;
	}

	const auto& mainView = r_viewpoint;
	AActor* const client = players[consoleplayer].mo;
	const bool matchPlayer = gamestate != GS_TITLELEVEL && viewPoint.camera->player == nullptr && (viewPoint.camera->renderflags2 & RF2_CAMFOLLOWSPLAYER);
	const bool usePawn = matchPlayer ? mainView.camera != client : false;
	//==============================================================================================
	// Sets up the view position offset.
	{
		AActor* const mo = viewPoint.camera;
		const DViewPosition* const viewOffset = mo->ViewPos;
		DVector3 camPos;
		if (matchPlayer)
		{
			if (usePawn)
			{
				camPos = DVector3(client->Pos().XY(), client->player->viewz);
				const DViewPosition* const pawnVP = client->ViewPos;
				if (pawnVP != nullptr)
				{
					// Add these directly to the view position offset (not 100% accurate but close enough).
					if (pawnVP->Flags & VPSF_ABSOLUTEPOS)
						camPos = pawnVP->Offset;
					else if (pawnVP->Flags & VPSF_ABSOLUTEOFFSET)
						iView->ViewOffset = pawnVP->Offset;
					else
						iView->ViewOffset = DQuaternion::FromAngles(client->Angles.Yaw, client->Angles.Pitch, client->Angles.Roll) * pawnVP->Offset;
				}
			}
			else
			{
				camPos = mainView.Pos;
			}
		}
		else
		{
			camPos = { mo->Pos().XY(), mo->player != nullptr ? mo->player->viewz : mo->Z() + mo->GetCameraHeight() };
		}

		viewPoint.showviewer = false;
		viewPoint.bForceNoViewer = matchPlayer;

		if (player != nullptr && gamestate != GS_TITLELEVEL
			&& ((player->cheats & CF_CHASECAM) || (r_deathcamera && (viewPoint.camera->flags6 & MF6_KILLED))))
		{
			// The cam Actor should probably be visible in third person.
			viewPoint.showviewer = true;
			camPos.Z = mo->Top() - mo->Floorclip;
			iView->ViewOffset.Z = clamp<double>(chase_height, -1000.0, 1000.0);
			iView->RelativeViewOffset.X = -clamp<double>(chase_dist, 0.0, 30000.0);
		}

		if (viewOffset != nullptr)
		{
			// No chase/death cam, so use the view offset.
			if (!viewPoint.bForceNoViewer)
				viewPoint.bForceNoViewer = (viewOffset->Flags & VPSF_ABSOLUTEPOS) || !viewOffset->Offset.isZero();
			
			if (viewOffset->Flags & VPSF_ABSOLUTEPOS)
			{
				iView->New.ViewPos.Zero();
				if (!matchPlayer)
					camPos = viewOffset->Offset;
			}
			else
			{
				iView->New.ViewPos = viewOffset->Offset;
			}
		}

		viewPoint.ActorPos = iView->New.Pos = camPos;
		viewPoint.sector = viewPoint.ViewLevel->PointInRenderSubsector(camPos)->sector;
	}

	if (!paused)
	{
		FQuakeJiggers jiggers;
		if (DEarthquake::StaticGetQuakeIntensities(viewPoint.TicFrac, viewPoint.camera, jiggers) > 0)
		{
			const double quakeFactor = r_quakeintensity;
			if (jiggers.RollIntensity || jiggers.RollWave)
				iView->AngleOffsets.Roll = DAngle::fromDeg(QuakePower(quakeFactor, jiggers.RollIntensity, jiggers.RollWave));

			if (jiggers.RelIntensity.X || jiggers.RelOffset.X)
				iView->RelativeViewOffset.X += QuakePower(quakeFactor, jiggers.RelIntensity.X, jiggers.RelOffset.X);
			if (jiggers.RelIntensity.Y || jiggers.RelOffset.Y)
				iView->RelativeViewOffset.Y += QuakePower(quakeFactor, jiggers.RelIntensity.Y, jiggers.RelOffset.Y);
			if (jiggers.RelIntensity.Z || jiggers.RelOffset.Z)
				iView->RelativeViewOffset.Z += QuakePower(quakeFactor, jiggers.RelIntensity.Z, jiggers.RelOffset.Z);

			if (jiggers.Intensity.X || jiggers.Offset.X)
				iView->ViewOffset.X += QuakePower(quakeFactor, jiggers.Intensity.X, jiggers.Offset.X);
			if (jiggers.Intensity.Y || jiggers.Offset.Y)
				iView->ViewOffset.Y += QuakePower(quakeFactor, jiggers.Intensity.Y, jiggers.Offset.Y);
			if (jiggers.Intensity.Z || jiggers.Offset.Z)
				iView->ViewOffset.Z += QuakePower(quakeFactor, jiggers.Intensity.Z, jiggers.Offset.Z);
		}
	}

	// [MR] Apply view angles as the viewpoint angles if asked to do so.
	if (matchPlayer)
		iView->New.Angles = usePawn ? (client->flags8 & MF8_ABSVIEWANGLES ? client->ViewAngles : client->Angles + client->ViewAngles) : mainView.Angles;
	else
		iView->New.Angles = !(viewPoint.camera->flags8 & MF8_ABSVIEWANGLES) ? viewPoint.camera->Angles : viewPoint.camera->ViewAngles;

	iView->New.ViewAngles = viewPoint.camera->ViewAngles;
	// [MR] Process player angle changes if permitted to do so.
	if (player != nullptr && (player->cheats & CF_SCALEDNOLERP) && P_NoInterpolation(player, viewPoint.camera))
		R_DoActorTickerAngleChanges(player, iView->New.Angles, viewPoint.TicFrac);

	// If currently tracking the player's real view, don't do any sort of interpolating.
	if (matchPlayer && !usePawn)
		viewPoint.camera->renderflags |= RF_NOINTERPOLATEVIEW;

	if (viewPoint.camera->player != nullptr)
		player = viewPoint.camera->player;

	if (iView->prevTic == -1 || r_NoInterpolate || (viewPoint.camera->renderflags & RF_NOINTERPOLATEVIEW))
	{
		viewPoint.camera->renderflags &= ~RF_NOINTERPOLATEVIEW;
		R_ResetViewInterpolation();
		iView->prevTic = curTic;
	}

	R_InterpolateView(viewPoint, player, viewPoint.TicFrac, iView);

	viewPoint.SetViewAngle(viewWindow);

	// Keep the view within the sector's floor and ceiling
	// But allow VPSF_ALLOWOUTOFBOUNDS camera viewpoints to go out of bounds when using hardware renderer
	if (viewPoint.sector->PortalBlocksMovement(sector_t::ceiling) && (!viewPoint.IsAllowedOoB() || !V_IsHardwareRenderer()))
	{
		const double z = viewPoint.sector->ceilingplane.ZatPoint(viewPoint.Pos) - 4.0;
		if (viewPoint.Pos.Z > z)
			viewPoint.Pos.Z = z;
	}

	if (viewPoint.sector->PortalBlocksMovement(sector_t::floor) && (!viewPoint.IsAllowedOoB() || !V_IsHardwareRenderer()))
	{
		const double z = viewPoint.sector->floorplane.ZatPoint(viewPoint.Pos) + 4.0;
		if (viewPoint.Pos.Z < z)
			viewPoint.Pos.Z = z;
	}

	viewPoint.extralight = viewPoint.camera->player != nullptr ? viewPoint.camera->player->extralight : 0;

	// killough 3/20/98, 4/4/98: select colormap based on player status
	// [RH] Can also select a blend
	unsigned int newBlend = 0u;
	const TArray<lightlist_t>& lightlist = viewPoint.sector->e->XFloor.lightlist;
	if (lightlist.Size() > 0)
	{
		for (size_t i = 0u; i < lightlist.Size(); ++i)
		{
			const secplane_t& plane = (i < lightlist.Size() - 1u) ? lightlist[i + 1u].plane : viewPoint.sector->floorplane;
			int viewSide = plane.PointOnSide(viewPoint.Pos);
			
			// Reverse the direction of the test if the plane was downward facing.
			// We want to know if the view is above it, whatever its orientation may be.
			if (plane.fC() < 0.0)
				viewSide = -viewSide;

			if (viewSide > 0)
			{
				// 3d floor 'fog' is rendered as a blending value.
				PalEntry blend = lightlist[i].blend;

				// If no alpha is set, use 50%.
				if (!blend.a && blend != 0)
					blend.a = 128u;

				newBlend = blend.d;
				break;
			}
		}
	}
	else
	{
		const sector_t* const sec = viewPoint.sector->GetHeightSec();
		if (sec != nullptr)
		{
			newBlend = sec->floorplane.PointOnSide(viewPoint.Pos) < 0
				? sec->bottommap
				: sec->ceilingplane.PointOnSide(viewPoint.Pos) < 0
				? sec->topmap
				: sec->midmap;

			if (APART(newBlend) == 0u && newBlend >= fakecmaps.Size())
				newBlend = 0u;
		}
	}

	// [RH] Don't override testblend unless entering a sector with a
	//		blend different from the previous sector's. Same goes with
	//		NormalLight's maps pointer.
	if (R_OldBlend != newBlend)
		R_OldBlend = newBlend;

	++validcount;

	if (r_clearbuffer != 0)
	{
		int color = 0;
		int hom = r_clearbuffer;
		if (hom == 3)
			hom = ((screen->FrameTime / 128) & 1) + 1;
		if (hom == 1)
			color = GPalette.BlackIndex;
		else if (hom == 2)
			color = GPalette.WhiteIndex;
		else if (hom == 4)
			color = (screen->FrameTime / 32) & 255;
		else
			color = pr_hom();

		screen->SetClearColor(color);
		SWRenderer->SetClearColor(color);
	}
    else
	{
		screen->SetClearColor(GPalette.BlackIndex);
    }
	
	
	// And finally some info that is needed for the hardware renderer
	
	// Scale the pitch to account for the pixel stretching, because the playsim doesn't know about this and treats it as 1:1.
	// However, to set up a projection matrix this needs to be adjusted.
	const double radPitch = viewPoint.Angles.Pitch.Normalized180().Radians();
	const double angx = cos(radPitch);
	const double angy = sin(radPitch) * actor->Level->info->pixelstretch;
	const double alen = sqrt(angx*angx + angy*angy);

	viewPoint.HWAngles.Pitch = FAngle::fromRad((float)asin(angy / alen));
	viewPoint.HWAngles.Roll = FAngle::fromDeg(viewPoint.Angles.Roll.Degrees());
	
	// ViewActor only gets set if the camera actor shouldn't be rendered.
	viewPoint.ViewActor = viewPoint.showviewer ? nullptr : actor;
}


CUSTOM_CVAR(Float, maxviewpitch, 90.f, CVAR_ARCHIVE | CVAR_SERVERINFO)
{
	if (self>90.f) self = 90.f;
	else if (self<-90.f) self = -90.f;
	if (usergame)
	{
		// [SP] Update pitch limits to the netgame/gamesim.
		players[consoleplayer].SendPitchLimits();
	}
}

//==========================================================================
//
// R_ShouldDrawSpriteShadow
//
//==========================================================================

bool R_ShouldDrawSpriteShadow(AActor *thing)
{
	int rf = thing->renderflags;
	// for wall and flat sprites the shadow math does not work so these must be unconditionally skipped.
	if (rf & (RF_FLATSPRITE | RF_WALLSPRITE)) return false;	

	bool doit = false;
	switch (r_actorspriteshadow)
	{
	case 1:
		doit = (rf & RF_CASTSPRITESHADOW);
		break;

	case 2:
		doit = (rf & RF_CASTSPRITESHADOW) || (!(rf & RF_NOSPRITESHADOW) && ((thing->flags3 & MF3_ISMONSTER) || thing->player != nullptr));
		break;

	default:
		break;
	}

	if (doit)
	{
		auto rs = thing->RenderStyle;
		rs.CheckFuzz();
		// For non-standard render styles, draw no shadows. This will always look weird. However, if the sprite forces shadows, render them anyway.
		if (!(rf & RF_CASTSPRITESHADOW))
		{
			if (rs.BlendOp != STYLEOP_Add && rs.BlendOp != STYLEOP_Shadow) return false;
			if (rs.DestAlpha != STYLEALPHA_Zero && rs.DestAlpha != STYLEALPHA_InvSrc) return false;
		}
	}
	return doit;


}
