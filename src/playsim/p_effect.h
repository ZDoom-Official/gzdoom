/*
** p_effect.h
**
**---------------------------------------------------------------------------
** Copyright 1998-2006 Randy Heit
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
*/

#pragma once

#include "vectors.h"
#include "doomdef.h"
#include "renderstyle.h"
#include "dthinker.h"

enum
{
	FX_ROCKET			= 0x00000001,
	FX_GRENADE			= 0x00000002,
	FX_RESPAWNINVUL		= 0x00000020,
	FX_VISIBILITYPULSE	= 0x00000040
};

struct subsector_t;
struct FLevelLocals;

// [RH] Particle details

enum EParticleFlags
{
	SPF_FULLBRIGHT =		1,
	SPF_RELPOS =			1 << 1,
	SPF_RELVEL =			1 << 2,
	SPF_RELACCEL =			1 << 3,
	SPF_RELANG =			1 << 4,
	SPF_NOTIMEFREEZE =		1 << 5,
	SPF_ROLL =				1 << 6,
	SPF_REPLACE =           1 << 7,
	SPF_NO_XY_BILLBOARD =	1 << 8,
};

struct particle_t
{
    DVector3 Pos;
    DVector3 Vel;
    DVector3 Acc;
    double    size, sizestep;
    float    fadestep, alpha;
    subsector_t* subsector;
    int32_t    ttl;
    int        color;
    FTextureID texture;
    ERenderStyle style;
    double Roll, RollVel, RollAcc;
    uint16_t    tnext, snext, tprev;
    bool    bright;
	uint32_t flags;
};

const uint16_t NO_PARTICLE = 0xffff;

void P_InitParticles(FLevelLocals *);
void P_ClearParticles (FLevelLocals *Level);
void P_FindParticleSubsectors (FLevelLocals *Level);


class AActor;

particle_t *JitterParticle (FLevelLocals *Level, int ttl);
particle_t *JitterParticle (FLevelLocals *Level, int ttl, double drift);

void P_ThinkParticles (FLevelLocals *Level);

struct FSpawnParticleParams
{
	int color;
	FTextureID texture;
	int style;
	int flags;
	int lifetime;

	double size;
	double sizestep;

	DVector3 pos;
	DVector3 vel;
	DVector3 accel;

	double startalpha;
	double fadestep;

	double startroll;
	double rollvel;
	double rollacc;
};

void P_SpawnParticle(FLevelLocals *Level, const DVector3 &pos, const DVector3 &vel, const DVector3 &accel, PalEntry color, double startalpha, int lifetime, double size, double fadestep, double sizestep, int flags = 0, FTextureID texture = FNullTextureID(), ERenderStyle style = STYLE_None, double startroll = 0, double rollvel = 0, double rollacc = 0);

void P_InitEffects (void);

void P_RunEffect (AActor *actor, int effects);

struct SPortalHit
{
	DVector3 HitPos;
	DVector3 ContPos;
	DVector3 OutDir;
};

void P_DrawRailTrail(AActor *source, TArray<SPortalHit> &portalhits, int color1, int color2, double maxdiff = 0, int flags = 0, PClassActor *spawnclass = NULL, DAngle angle = nullAngle, int duration = TICRATE, double sparsity = 1.0, double drift = 1.0, int SpiralOffset = 270, DAngle pitch = nullAngle);
void P_DrawSplash (FLevelLocals *Level, int count, const DVector3 &pos, DAngle angle, int kind);
void P_DrawSplash2 (FLevelLocals *Level, int count, const DVector3 &pos, DAngle angle, int updown, int kind);
void P_DisconnectEffect (AActor *actor);

//===========================================================================
// 
// Particles Expanded
// by Major Cooke
// 
//===========================================================================

struct FSpawnZSpriteParams
{
	DVector3		Pos, Vel;
	DVector2		Scale, Offset;
	double			Roll;
	double			Alpha;

	FTextureID		Texture; // For whenever particles don't have states.
	int				Style;
	uint32_t		Translation;

	uint32_t		Flags;

};

class DZSprite : public DThinker
{
	DECLARE_CLASS(DZSprite, DThinker);

public:
	DZSprite();
	void OnDestroy() override;
	/*
	~DZSprite() override;
	*/
	// TO DO: Enable states for this class
/*
	int32_t			tics;				// state tic counter
	int				sprite;				// used to find patch_t and flip value
	uint8_t			frame;				// sprite frame to draw

	FState			*State;
	FState			*SpawnState;
*/

	DVector3		Pos, Vel, Prev;
	DVector2		Scale, Offset;
	double			Roll;
	double			Alpha;

	// Style will be changed to FRenderStyle when taken off of particle_t.
	// ERenderStyle is an enum and isn't handled by the serializer, but the F variant is.
	int				Style;
	FTextureID		Texture;
	uint32_t		Translation;

	uint32_t		Flags;

	
	particle_t		PT;	// ONLY used for injecting into HWDrawInfo::RenderParticles.
	subsector_t		*sub;


	static DZSprite* NewZSprite(FLevelLocals* Level, PClass* type, FSpawnZSpriteParams *params);
	void SetTranslation(FName trname);
	bool IsFrozen();

public:
	void Tick() override;
	void Serialize(FSerializer& arc) override;

};
