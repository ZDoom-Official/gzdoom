// 
//---------------------------------------------------------------------------
//
// Copyright(C) 2002-2018 Christoph Oelckers
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
** gl_light.cpp
** Light level / fog management / dynamic lights
**
**/

#include "c_cvars.h"
#include "r_sky.h"
#include "g_levellocals.h"
#include "hw_lighting.h"
#include "hwrenderer/scene/hw_drawinfo.h"

// externally settable lighting properties
static float distfogtable[2][256];	// light to fog conversion table for black fog

CVAR(Int, gl_weaponlight, 8, CVAR_ARCHIVE);
CVAR(Bool, gl_enhanced_nightvision, true, CVAR_ARCHIVE|CVAR_NOINITCALL)

//==========================================================================
//
// Sets up the fog tables
//
//==========================================================================

CUSTOM_CVAR(Int, gl_distfog, 70, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	for (int i = 0; i < 256; i++)
	{

		if (i < 164)
		{
			distfogtable[0][i] = (float)((gl_distfog >> 1) + (gl_distfog)*(164 - i) / 164);
		}
		else if (i < 230)
		{
			distfogtable[0][i] = (float)((gl_distfog >> 1) - (gl_distfog >> 1)*(i - 164) / (230 - 164));
		}
		else distfogtable[0][i] = 0;

		if (i < 128)
		{
			distfogtable[1][i] = 6.f + (float)((gl_distfog >> 1) + (gl_distfog)*(128 - i) / 48);
		}
		else if (i < 216)
		{
			distfogtable[1][i] = (216.f - i) / ((216.f - 128.f)) * gl_distfog / 10;
		}
		else distfogtable[1][i] = 0;
	}
}

//==========================================================================
//
// Get current light level
//
//==========================================================================

int CalcLightLevel(ELightMode lightmode, int lightlevel, int rellight, bool weapon, int blendfactor, bool isModel)
{
	int light;

	if (lightlevel <= 0) return 0;

	bool darklightmode = (isDarkLightMode(lightmode)) || (isSoftwareLighting(lightmode) && blendfactor > 0);

	if (darklightmode && lightlevel < 192 && !weapon) 
	{
		if (lightlevel > 100)
		{
			light = xs_CRoundToInt(192.f - (192 - lightlevel)* 1.87f);
			if (light + rellight < 20)
			{
				light = 20 + (light + rellight - 20) / 5;
			}
			else
			{
				light += rellight;
			}
		}
		else
		{
			light = (lightlevel + rellight) / 5;
		}

	}
	else
	{
		// if (!isModel)
		light=lightlevel+rellight;
		// else
		// light=????????;
	}

	// Fake contrast should never turn a positive value into 0.
	return clamp(light, 1, 255);
}

//==========================================================================
//
// Get current light color
//
//==========================================================================

PalEntry CalcLightColor(ELightMode lightmode, int light, PalEntry pe, int blendfactor)
{
	int r,g,b;

	if (blendfactor == 0)
	{
		if (isSoftwareLighting(lightmode))
		{
			return pe;
		}

		r = pe.r * light / 255;
		g = pe.g * light / 255;
		b = pe.b * light / 255;
	}
	else
	{
		// This is what Legacy does with colored light in 3D volumes. No, it doesn't really make sense...
		// It also doesn't translate well to software style lighting.
		int mixlight = light * (255 - blendfactor);

		r = (mixlight + pe.r * blendfactor) / 255;
		g = (mixlight + pe.g * blendfactor) / 255;
		b = (mixlight + pe.b * blendfactor) / 255;
	}
	return PalEntry(255, uint8_t(r), uint8_t(g), uint8_t(b));
}

//==========================================================================
//
// calculates the current fog density
//
//	Rules for fog:
//
//  1. If bit 4 of gl_lightmode is set always use the level's fog density. 
//     This is what Legacy's GL render does.
//	2. black fog means no fog and always uses the distfogtable based on the level's fog density setting
//	3. If outside fog is defined and the current fog color is the same as the outside fog
//	   the engine always uses the outside fog density to make the fog uniform across the Level->
//	   If the outside fog's density is undefined it uses the level's fog density and if that is
//	   not defined it uses a default of 70.
//	4. If a global fog density is specified it is being used for all fog on the level
//	5. If none of the above apply fog density is based on the light level as for the software renderer.
//
//==========================================================================

float GetFogDensity(FLevelLocals* Level, ELightMode lightmode, int lightlevel, PalEntry fogcolor, int sectorfogdensity, int blendfactor)
{
	float density;

	auto oldlightmode = lightmode;
	if (isSoftwareLighting(lightmode) && blendfactor > 0) lightmode = ELightMode::Doom;	// The blendfactor feature does not work with software-style lighting.

	if (lightmode == ELightMode::DoomLegacy)
	{
		// uses approximations of Legacy's default settings.
		density = Level->fogdensity ? (float)Level->fogdensity : 18;
	}
	else if (sectorfogdensity != 0)
	{
		// case 1: Sector has an explicit fog density set.
		density = (float)sectorfogdensity;
	}
	else if ((fogcolor.d & 0xffffff) == 0)
	{
		// case 2: black fog
		if ((!isDoomSoftwareLighting(lightmode) || blendfactor > 0) && !(Level->flags3 & LEVEL3_NOLIGHTFADE))
		{
			density = distfogtable[lightmode != ELightMode::LinearStandard][hw_ClampLight(lightlevel)];
		}
		else
		{
			density = 0;
		}
	}
	else if (Level->outsidefogdensity != 0 && APART(Level->info->outsidefog) != 0xff && (fogcolor.d & 0xffffff) == (Level->info->outsidefog & 0xffffff))
	{
		// case 3. outsidefogdensity has already been set as needed
		density = (float)Level->outsidefogdensity;
	}
	else  if (Level->fogdensity != 0)
	{
		// case 4: level has fog density set
		density = (float)Level->fogdensity;
	}
	else if (lightlevel < 248)
	{
		// case 5: use light level
		density = (float)clamp<int>(255 - lightlevel, 30, 255);
	}
	else
	{
		density = 0.f;
	}
	return density;
}

//==========================================================================
//
// Check if the current linedef is a candidate for a fog boundary
//
// Requirements for a fog boundary:
// - front sector has no fog
// - back sector has fog
// - at least one of both does not have a sky ceiling.
//
//==========================================================================

bool CheckFog(FLevelLocals* Level, sector_t *frontsector, sector_t *backsector, ELightMode lightmode)
{
	if (frontsector == backsector) return false;	// there can't be a boundary if both sides are in the same sector.

	// Check for fog boundaries. This needs a few more checks for the sectors

	PalEntry fogcolor = frontsector->Colormap.FadeColor;

	if ((fogcolor.d & 0xffffff) == 0)
	{
		return false;
	}
	else if (fogcolor.a != 0)
	{
	}
	else if (Level->outsidefogdensity != 0 && APART(Level->info->outsidefog) != 0xff && (fogcolor.d & 0xffffff) == (Level->info->outsidefog & 0xffffff))
	{
	}
	else  if (Level->fogdensity!=0 || lightmode == ELightMode::DoomLegacy)
	{
		// case 3: level has fog density set
	}
	else 
	{
		// case 4: use light level
		if (frontsector->lightlevel >= 248) return false;
	}

	fogcolor = backsector->Colormap.FadeColor;

	if ((fogcolor.d & 0xffffff) == 0)
	{
	}
	else if (Level->outsidefogdensity != 0 && APART(Level->info->outsidefog) != 0xff && (fogcolor.d & 0xffffff) == (Level->info->outsidefog & 0xffffff))
	{
		return false;
	}
	else  if (Level->fogdensity!=0 || lightmode == ELightMode::DoomLegacy)
	{
		// case 3: level has fog density set
		return false;
	}
	else 
	{
		// case 4: use light level
		if (backsector->lightlevel < 248) return false;
	}

	// in all other cases this might create more problems than it solves.
	return ((frontsector->GetTexture(sector_t::ceiling)!=skyflatnum || 
			 backsector->GetTexture(sector_t::ceiling)!=skyflatnum));
}

