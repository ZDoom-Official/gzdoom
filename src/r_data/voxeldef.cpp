
/*
** voxels.cpp
**
**---------------------------------------------------------------------------
** Copyright 2010-2011 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
**
*/

#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <ctype.h>

#include "m_swap.h"
#include "m_argv.h"
#include "filesystem.h"
#include "v_video.h"
#include "sc_man.h"
#include "voxels.h"
#include "info.h"
#include "s_sound.h"
#include "sbar.h"
#include "g_level.h"
#include "r_data/sprites.h"

using namespace FileSys;

struct VoxelOptions
{
	int			DroppedSpin = 0;
	int			PlacedSpin = 0;
	double		Scale = 1;
	DAngle		AngleOffset = DAngle90;
	DAngle		PitchOffset = nullAngle;
	DAngle		RollOffset = nullAngle;
	double		xoffset = 0.0;
	double		yoffset = 0.0;
	double		zoffset = 0.0;
	bool		OverridePalette = false;
	bool		PitchFromMomentum = false;
	bool		UseActorPitch = false;
	bool		UseActorRoll = false;
};

void VOX_AddVoxel(int sprnum, int frame, FVoxelDef* def);

//==========================================================================
//
// VOX_ReadSpriteNames
//
// Reads a list of sprite names from a VOXELDEF lump.
//
//==========================================================================

static bool VOX_ReadSpriteNames(FScanner &sc, TArray<uint32_t> &vsprites)
{
	vsprites.Clear();
	while (sc.GetString())
	{
		// A sprite name list is terminated by an '=' character.
		if (sc.String[0] == '=')
		{
			if (vsprites.Size() == 0)
			{
				sc.ScriptMessage("No sprites specified for voxel.\n");
			}
			return true;
		}
		if (sc.StringLen != 4 && sc.StringLen != 5)
		{
			sc.ScriptMessage("Sprite name \"%s\" is wrong size.\n", sc.String);
		}
		else if (sc.StringLen == 5 && (sc.String[4] = toupper(sc.String[4]), sc.String[4] < 'A' || sc.String[4] >= 'A' + MAX_SPRITE_FRAMES))
		{
			sc.ScriptMessage("Sprite frame %c is invalid.\n", sc.String[4]);
		}
		else
		{
			int frame = (sc.StringLen == 4) ? 255 : sc.String[4] - 'A';
			int i = GetSpriteIndex(sc.String, false);
			if (i != -1)
			{
				vsprites.Push((frame << 24) | i);
			}
		}
	}
	if (vsprites.Size() != 0)
	{
		sc.ScriptMessage("Unexpected end of file\n");
	}
	return false;
}

//==========================================================================
//
// VOX_ReadOptions
//
// Reads a list of options from a VOXELDEF lump, terminated with a '}'
// character. The leading '{' must already be consumed
//
//==========================================================================

static void VOX_ReadOptions(FScanner &sc, VoxelOptions &opts)
{
	while (sc.GetToken())
	{
		if (sc.TokenType == '}')
		{
			return;
		}
		sc.TokenMustBe(TK_Identifier);
		if (sc.Compare("scale"))
		{
			sc.MustGetToken('=');
			sc.MustGetToken(TK_FloatConst);
			opts.Scale = sc.Float;
		}
		else if (sc.Compare("spin"))
		{
			int mul = 1;
			sc.MustGetToken('=');
			if (sc.CheckToken('-')) mul = -1;
			sc.MustGetToken(TK_IntConst);
			opts.DroppedSpin = opts.PlacedSpin = sc.Number*mul;
		}
		else if (sc.Compare("placedspin"))
		{
			int mul = 1;
			sc.MustGetToken('=');
			if (sc.CheckToken('-')) mul = -1;
			sc.MustGetToken(TK_IntConst);
			opts.PlacedSpin = sc.Number*mul;
		}
		else if (sc.Compare("droppedspin"))
		{
			int mul = 1;
			sc.MustGetToken('=');
			if (sc.CheckToken('-')) mul = -1;
			sc.MustGetToken(TK_IntConst);
			opts.DroppedSpin = sc.Number*mul;
		}
		else if (sc.Compare("angleoffset"))
		{
			int mul = 1;
			sc.MustGetToken('=');
			if (sc.CheckToken('-')) mul = -1;
			sc.MustGetAnyToken();
			if (sc.TokenType == TK_IntConst)
			{
				sc.Float = sc.Number;
			}
			else
			{
				sc.TokenMustBe(TK_FloatConst);
			}
			opts.AngleOffset = DAngle::fromDeg(mul * sc.Float + 90.);
		}
		else if (sc.Compare("pitchoffset"))
		{
			int mul = 1;
			sc.MustGetToken('=');
			if (sc.CheckToken('-')) mul = -1;
			sc.MustGetAnyToken();
			if (sc.TokenType == TK_IntConst)
			{
				sc.Float = sc.Number;
			}
			else
			{
				sc.TokenMustBe(TK_FloatConst);
			}
			opts.PitchOffset = DAngle::fromDeg(mul * sc.Float);
		}
		else if (sc.Compare("rolloffset"))
		{
			int mul = 1;
			sc.MustGetToken('=');
			if (sc.CheckToken('-')) mul = -1;
			sc.MustGetAnyToken();
			if (sc.TokenType == TK_IntConst)
			{
				sc.Float = sc.Number;
			}
			else
			{
				sc.TokenMustBe(TK_FloatConst);
			}
			opts.RollOffset = DAngle::fromDeg(mul * sc.Float);
		}
		else if (sc.Compare("xoffset"))
		{
			int mul = 1;
			sc.MustGetToken('=');
			if (sc.CheckToken('-')) mul = -1;
			sc.MustGetAnyToken();
			if (sc.TokenType == TK_IntConst)
			{
				sc.Float = sc.Number;
			}
			else
			{
				sc.TokenMustBe(TK_FloatConst);
			}
			opts.xoffset = sc.Float * mul;
		}
		else if (sc.Compare("yoffset"))
		{
			int mul = 1;
			sc.MustGetToken('=');
			if (sc.CheckToken('-')) mul = -1;
			sc.MustGetAnyToken();
			if (sc.TokenType == TK_IntConst)
			{
				sc.Float = sc.Number;
			}
			else
			{
				sc.TokenMustBe(TK_FloatConst);
			}
			opts.yoffset = sc.Float * mul;
		}
		else if (sc.Compare("zoffset"))
		{
			int mul = 1;
			sc.MustGetToken('=');
			if (sc.CheckToken('-')) mul = -1;
			sc.MustGetAnyToken();
			if (sc.TokenType == TK_IntConst)
			{
				sc.Float = sc.Number;
			}
			else
			{
				sc.TokenMustBe(TK_FloatConst);
			}
			opts.zoffset = sc.Float * mul;
		}
		else if (sc.Compare("overridepalette"))
		{
			opts.OverridePalette = true;
		}
		else if (sc.Compare("pitchfrommomentum"))
		{
			opts.PitchFromMomentum = true;
		}
		else if (sc.Compare("useactorpitch"))
		{
			opts.UseActorPitch = true;
		}
		else if (sc.Compare("useactorroll"))
		{
			opts.UseActorRoll = true;
		}
		else
		{
			sc.ScriptMessage("Unknown voxel option '%s'\n", sc.String);
			if (sc.CheckToken('='))
			{
				sc.MustGetAnyToken();
			}
		}
	}
	sc.ScriptMessage("Unterminated voxel option block\n");
}

//==========================================================================
//
// R_InitVoxels
//
// Process VOXELDEF lumps for defining voxel options that cannot be
// condensed neatly into a sprite name format.
//
//==========================================================================

void R_InitVoxels()
{
	int lump, lastlump = 0;
	
	while ((lump = fileSystem.FindLump("VOXELDEF", &lastlump)) != -1)
	{
		FScanner sc(lump);
		TArray<uint32_t> vsprites;

		while (VOX_ReadSpriteNames(sc, vsprites))
		{
			FVoxel *voxeldata = NULL;
			int voxelfile;
			VoxelOptions opts;

			sc.SetCMode(true);
			sc.MustGetToken(TK_StringConst);
			voxelfile = fileSystem.CheckNumForFullName(sc.String, true, ns_voxels);
			if (voxelfile < 0)
			{
				sc.ScriptMessage("Voxel \"%s\" not found.\n", sc.String);
			}
			else
			{
				voxeldata = VOX_GetVoxel(voxelfile);
				if (voxeldata == NULL)
				{
					sc.ScriptMessage("\"%s\" is not a valid voxel file.\n", sc.String);
				}
			}
			if (sc.CheckToken('{'))
			{
				VOX_ReadOptions(sc, opts);
			}
			sc.SetCMode(false);
			if (voxeldata != NULL && vsprites.Size() != 0)
			{
				if (opts.OverridePalette)
				{
					voxeldata->RemovePalette();
				}
				FVoxelDef *def = new FVoxelDef;

				def->Voxel = voxeldata;
				def->Scale = opts.Scale;
				def->DroppedSpin = opts.DroppedSpin;
				def->PlacedSpin = opts.PlacedSpin;
				def->AngleOffset = opts.AngleOffset;
				def->PitchOffset = opts.PitchOffset;
				def->RollOffset = opts.RollOffset;
				def->xoffset = opts.xoffset;
				def->yoffset = opts.yoffset;
				def->zoffset = opts.zoffset;
				def->PitchFromMomentum = opts.PitchFromMomentum;
				def->UseActorPitch = opts.UseActorPitch;
				def->UseActorRoll = opts.UseActorRoll;
				VoxelDefs.Push(def);

				for (unsigned i = 0; i < vsprites.Size(); ++i)
				{
					int sprnum = int(vsprites[i] & 0xFFFFFF);
					int frame = int(vsprites[i] >> 24);
					if (frame == 255)
					{ // Apply voxel to all frames.
						for (int j = MAX_SPRITE_FRAMES - 1; j >= 0; --j)
						{
							VOX_AddVoxel(sprnum, j, def);
						}
					}
					else
					{ // Apply voxel to only one frame.
						VOX_AddVoxel(sprnum, frame, def);
					}
				}
			}
		}
	}
}

