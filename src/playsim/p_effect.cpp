/*
** p_effect.cpp
** Particle effects
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
** If particles used real sprites instead of blocks, they could be much
** more useful.
*/

#if __has_include(<execution>) && !( __linux__ || __APPLE__  )

	#include <execution>

#endif

#include "doomtype.h"
#include "doomstat.h"

#include "actor.h"
#include "m_argv.h"
#include "p_effect.h"
#include "p_local.h"
#include "r_defs.h"
#include "gi.h"
#include "d_player.h"
#include "r_utility.h"
#include "g_levellocals.h"
#include "vm.h"
#include "actorinlines.h"
#include "g_game.h"
#include "serializer_doom.h"

#include "hwrenderer/scene/hw_drawstructs.h"

#include "i_time.h"

#ifdef _MSC_VER
#pragma warning(disable: 6011) // dereference null pointer in thinker iterator
#endif

CVAR (Int, cl_rockettrails, 1, CVAR_ARCHIVE);
CVAR (Bool, r_rail_smartspiral, false, CVAR_ARCHIVE);
CVAR (Int, r_rail_spiralsparsity, 1, CVAR_ARCHIVE);
CVAR (Int, r_rail_trailsparsity, 1, CVAR_ARCHIVE);
CVAR (Bool, r_particles, true, 0);
CVAR (Bool, r_particles_multithreaded, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG);

EXTERN_CVAR(Int, r_maxparticles);

FRandom pr_railtrail("RailTrail");

#define FADEFROMTTL(a)	(1.f/(a))

static int grey1, grey2, grey3, grey4, red, green, blue, yellow, black,
		   red1, green1, blue1, yellow1, purple, purple1, white,
		   rblue1, rblue2, rblue3, rblue4, orange, yorange, dred, grey5,
		   maroon1, maroon2, blood1, blood2;

static const struct ColorList {
	int *color;
	uint8_t r, g, b;
} Colors[] = {
	{&grey1,	85,  85,  85 },
	{&grey2,	171, 171, 171},
	{&grey3,	50,  50,  50 },
	{&grey4,	210, 210, 210},
	{&grey5,	128, 128, 128},
	{&red,		255, 0,   0  },  
	{&green,	0,   200, 0  },  
	{&blue,		0,   0,   255},
	{&yellow,	255, 255, 0  },  
	{&black,	0,   0,   0  },  
	{&red1,		255, 127, 127},
	{&green1,	127, 255, 127},
	{&blue1,	127, 127, 255},
	{&yellow1,	255, 255, 180},
	{&purple,	120, 0,   160},
	{&purple1,	200, 30,  255},
	{&white, 	255, 255, 255},
	{&rblue1,	81,  81,  255},
	{&rblue2,	0,   0,   227},
	{&rblue3,	0,   0,   130},
	{&rblue4,	0,   0,   80 },
	{&orange,	255, 120, 0  },
	{&yorange,	255, 170, 0  },
	{&dred,		80,  0,   0  },
	{&maroon1,	154, 49,  49 },
	{&maroon2,	125, 24,  24 },
	{NULL, 0, 0, 0 }
};

static_assert(std::is_trivially_copyable_v<particle_t>);

constexpr int PARTICLE_TIC_AVG_COUNT = 35;
constexpr int  PARTICLE_FRAME_AVG_COUNT = 60;

static int ParticleCount;
static float ParticleThinkMs;
static float ParticleThinkMsAvg[PARTICLE_TIC_AVG_COUNT];
static uint32_t ParticleReplaceCount;
static uint32_t ParticleReplaceCountAvg[PARTICLE_TIC_AVG_COUNT];
static uint64_t ParticleCreateNs;
//static uint64_t ParticleSubsectorNs;
//static float ParticleSubsectorMsAvg[PARTICLE_FRAME_AVG_COUNT];
static int ticAvgPos = 0;
//static int frameAvgPos = 0;

static bool checkCreateNs = false;

ADD_STAT(particles)
{
	FString str;

	double ParticleThinkMsAvgTotal = ParticleThinkMs;
	double ParticleReplaceCountAvgTotal = ParticleReplaceCount;

	for(int i = 0; i < PARTICLE_TIC_AVG_COUNT; i++)
	{
		ParticleThinkMsAvgTotal += ParticleThinkMsAvg[i];
		ParticleReplaceCountAvgTotal += ParticleReplaceCountAvg[i];
	}

	ParticleThinkMsAvgTotal /= PARTICLE_TIC_AVG_COUNT + 1.0;
	ParticleReplaceCountAvgTotal /= PARTICLE_TIC_AVG_COUNT + 1.0;

	str.Format(
		"Particles: %d, Particles Replaced Last Tic: %d, Particle Think Time: %.2f ms\n"
		"Average Particles Replaced (%dtic): %.2f, Average Particle Think Time (%dtic): %.2f ms\n"
		, ParticleCount, ParticleReplaceCount, ParticleThinkMs, PARTICLE_TIC_AVG_COUNT + 1, ParticleReplaceCountAvgTotal, PARTICLE_TIC_AVG_COUNT + 1, ParticleThinkMsAvgTotal
		);
	return str;
}

ADD_STAT2(particleCreation)
{
	FString str;
	double ParticleCreateMs = ParticleCreateNs / 1'000'000.0;

	double ParticleCreateMsAvgTotal = ParticleCreateMs;

	str.Format(
		"Particle Create Time: %.2f ms\n"
		, ParticleCreateMs
		);
	return str;
}


void Stat_particleCreation::ToggleStat()
{
	FStat::ToggleStat();
	checkCreateNs = isActive();
}

static int MaxParticles;

inline particle_t *NewParticle (FLevelLocals *Level, bool replace = false)
{
	if(MaxParticles > 0)
	{
		uint64_t start = checkCreateNs ? I_nsTime() : 0;
		if(Level->NumParticles == MaxParticles)
		{
			if(!replace) return nullptr;
			ParticleCreateNs += checkCreateNs ? (I_nsTime() - start) : 0;
			return Level->Particles.Data() + Level->ParticleIndices[(Level->ParticleReplaceEnd++) % MaxParticles];
		}
		else
		{
			ParticleCreateNs += checkCreateNs ? (I_nsTime() - start) : 0;
			return Level->Particles.Data() + Level->ParticleIndices[Level->NumParticles++];
		}
	}
	return nullptr;
}

//
// [RH] Particle functions
//

void P_ReInitParticles (FLevelLocals *Level, int num)
{
	MaxParticles = clamp<int>(num, ABSOLUTE_MIN_PARTICLES, ABSOLUTE_MAX_PARTICLES);
	Level->ParticleIndices.Resize(MaxParticles);
	Level->Particles.Resize(MaxParticles);
	for(int i = 0; i < MaxParticles; i++)
	{
		Level->ParticleIndices[i] = i;
	}
}

void P_InitParticles (FLevelLocals *Level)
{
	int num;

	if (const char *i; (i = Args->CheckValue ("-numparticles")))
	{
		num = atoi (i);
	}
	else
	{
		// [BC] Use r_maxparticles now.
		num = r_maxparticles;
	}
	P_ReInitParticles(Level, num);
}

void P_ClearParticles (FLevelLocals *Level)
{
	Level->NumParticles = 0;
}

// Group particles by subsectors. Because particles are always
// in motion, there is little benefit to caching this information
// from one frame to the next.
// [MC] VisualThinkers hitches a ride here

void P_FindParticleSubsectors (FLevelLocals *Level)
{
	// [MC] Hitch a ride on particle subsectors since VisualThinkers are effectively using the same kind of system.
	for(uint32_t i = 0; i < Level->subsectors.Size(); i++)
	{
		Level->subsectors[i].sprites.Clear();
		Level->subsectors[i].particles.Clear();
	}
	// [MC] Not too happy about using an iterator for this but I can't think of another way to handle it.
	// At least it's on its own statnum for maximum efficiency.
	auto it = Level->GetThinkerIterator<DVisualThinker>(NAME_None, STAT_VISUALTHINKER);
	DVisualThinker* sp;
	while (sp = it.Next())
	{
		if (!sp->PT.subsector) sp->PT.subsector = Level->PointInRenderSubsector(sp->PT.Pos);

		sp->PT.subsector->sprites.Push(sp);
	}
	// End VisualThinker hitching. Now onto the particles. 

	if (!r_particles)
	{
		return;
	}

	particle_t * pp = Level->Particles.Data();

	for(uint32_t i = 0; i < Level->NumParticles; i++)
	{
		particle_t * p = pp + Level->ParticleIndices[i];
		 // Try to reuse the subsector from the last portal check, if still valid.
		if (p->subsector == nullptr) p->subsector = Level->PointInRenderSubsector(p->Pos);
		p->subsector->particles.Push(p);
	}
}

static TMap<int, int> ColorSaver;

static uint32_t ParticleColor(int rgb)
{
	int *val;
	int stuff;

	val = ColorSaver.CheckKey(rgb);
	if (val != NULL)
	{
		return *val;
	}
	stuff = rgb | (ColorMatcher.Pick(RPART(rgb), GPART(rgb), BPART(rgb)) << 24);
	ColorSaver[rgb] = stuff;
	return stuff;
}

static uint32_t ParticleColor(int r, int g, int b)
{
	return ParticleColor(MAKERGB(r, g, b));
}

void P_InitEffects ()
{
	const struct ColorList *color = Colors;

	while (color->color)
	{
		*(color->color) = ParticleColor(color->r, color->g, color->b);
		color++;
	}

	int kind = gameinfo.defaultbloodparticlecolor;
	blood1 = ParticleColor(kind);
	blood2 = ParticleColor(RPART(kind)/3, GPART(kind)/3, BPART(kind)/3);
}

// [RL0] true = particle should be deleted
bool P_ThinkParticle(FLevelLocals * Level, particle_t &particle)
{
    if (Level->isFrozen() && !(particle.flags &SPF_NOTIMEFREEZE))
    {
        if(particle.flags & SPF_LOCAL_ANIM)
        {
            particle.animData.SwitchTic++;
        }
        return false;
    }
    
	auto oldtrans = particle.alpha;
	particle.alpha -= particle.fadestep;
	particle.size += particle.sizestep;
	if(particle.alpha <= 0 || oldtrans < particle.alpha || --particle.ttl <= 0 || (particle.size <= 0))
	{ // The particle has expired, so free it
		return true;
	}
    
    // Handle crossing a line portal
	DVector2 newxy = Level->GetPortalOffsetPosition(particle.Pos.X, particle.Pos.Y, particle.Vel.X, particle.Vel.Y);
	particle.Pos.X = newxy.X;
	particle.Pos.Y = newxy.Y;
	particle.Pos.Z += particle.Vel.Z;
	particle.Vel += particle.Acc;
    
	if(particle.flags & SPF_ROLL)
	{
		particle.Roll += particle.RollVel;
		particle.RollVel += particle.RollAcc;
	}

	particle.subsector = Level->PointInRenderSubsector(particle.Pos);
	sector_t *s = particle.subsector->sector;
	// Handle crossing a sector portal.
	if (!s->PortalBlocksMovement(sector_t::ceiling))
	{
		if (particle.Pos.Z > s->GetPortalPlaneZ(sector_t::ceiling))
		{
			particle.Pos += s->GetPortalDisplacement(sector_t::ceiling);
			particle.subsector = NULL;
		}
	}

	else if (!s->PortalBlocksMovement(sector_t::floor))
	{
		if (particle.Pos.Z < s->GetPortalPlaneZ(sector_t::floor))
		{
			particle.Pos += s->GetPortalDisplacement(sector_t::floor);
			particle.subsector = NULL;
		}
	}
	return false;
}

void P_ThinkParticles (FLevelLocals *Level)
{
	uint64_t startNs = I_nsTime();
	uint32_t * newEnd;
	
	ParticleCount = Level->NumParticles;

	uint32_t replacedCount = Level->ParticleReplaceEnd;

	if(replacedCount)
	{
		uint32_t cnt = replacedCount % MaxParticles;
		if(cnt > 0)
		{
			uint32_t num = MaxParticles - cnt;
			uint32_t * buf = new uint32_t[cnt];
			uint32_t * arr = Level->ParticleIndices.Data();
			memcpy(buf, arr, cnt);
			memmove(arr, arr + cnt, num);
			memcpy(arr + num, buf, cnt);
			delete buf;
		}
		Level->ParticleReplaceEnd = 0;
	}

	particle_t * p = Level->Particles.Data();

	auto proc = [Level, p](uint32_t i)
	{
		return P_ThinkParticle(Level, *(p + i));
	};

#if __has_include(<execution>) && !( __linux__ || __APPLE__  )
	if(r_particles_multithreaded)
	{
		newEnd = std::remove_if(std::execution::par_unseq, Level->ParticleIndices.Data(), Level->ParticleIndices.Data(Level->NumParticles), proc);
	}
	else
#endif
	{
		newEnd = std::remove_if(Level->ParticleIndices.Data(), Level->ParticleIndices.Data(Level->NumParticles), proc);
	}

	Level->NumParticles = reinterpret_cast<uintptr_t>(newEnd) - reinterpret_cast<uintptr_t>(Level->ParticleIndices.Data());


	ParticleThinkMsAvg[ticAvgPos] = ParticleThinkMs;
	ParticleReplaceCountAvg[ticAvgPos] = ParticleReplaceCount;

	ticAvgPos = (ticAvgPos + 1) % PARTICLE_TIC_AVG_COUNT;

	ParticleThinkMs = ( I_nsTime() - startNs) / 1'000'000.0f;
	ParticleReplaceCount = replacedCount;
	ParticleCreateNs = 0;
}

void P_SpawnParticle(FLevelLocals *Level, const DVector3 &pos, const DVector3 &vel, const DVector3 &accel, PalEntry color, double startalpha, int lifetime, double size,
	double fadestep, double sizestep, int flags, FTextureID texture, ERenderStyle style, double startroll, double rollvel, double rollacc)
{
	particle_t *particle = NewParticle(Level, !!(flags & SPF_REPLACE));

	if (particle)
	{
		particle->Pos = pos;
		particle->Vel = FVector3(vel);
		particle->Acc = FVector3(accel);
		particle->color = ParticleColor(color);
		particle->alpha = float(startalpha);
		particle->fadestep = (fadestep < 0) ? FADEFROMTTL(lifetime) : float(fadestep);
		particle->ttl = lifetime;
		particle->size = size;
		particle->sizestep = sizestep;
		particle->subsector = nullptr;
		particle->texture = texture;
		particle->style = style;
		particle->Roll = (float)startroll;
		particle->RollVel = (float)rollvel;
		particle->RollAcc = (float)rollacc;
		particle->flags = flags;
		if(flags & SPF_LOCAL_ANIM)
		{
			TexAnim.InitStandaloneAnimation(particle->animData, texture, Level->maptime);
		}
	}
}

//
// JitterParticle
//
// Creates a particle with "jitter"
//
particle_t *JitterParticle (FLevelLocals *Level, int ttl)
{
	return JitterParticle (Level, ttl, 1.0);
}
// [XA] Added "drift speed" multiplier setting for enhanced railgun stuffs.
particle_t *JitterParticle (FLevelLocals *Level, int ttl, double drift)
{
	particle_t *particle = NewParticle (Level);

	if (particle) {
		int i;

		*particle = {};

		// Set initial velocities
		for (i = 3; i; i--)
			particle->Vel[i] = ((1./4096) * (M_Random () - 128) * drift);
		// Set initial accelerations
		for (i = 3; i; i--)
			particle->Acc[i] = ((1./16384) * (M_Random () - 128) * drift);

		particle->alpha = 1.f;	// fully opaque
		particle->ttl = ttl;
		particle->fadestep = FADEFROMTTL(ttl);
	}
	return particle;
}

static void MakeFountain (AActor *actor, int color1, int color2)
{
	particle_t *particle;

	if (!(actor->Level->time & 1))
		return;

	particle = JitterParticle (actor->Level, 51);

	if (particle)
	{
		DAngle an = DAngle::fromDeg(M_Random() * (360. / 256));
		double out = actor->radius * M_Random() / 256.;

		particle->Pos = actor->Vec3Angle(out, an, actor->Height + 1);
		if (out < actor->radius/8)
			particle->Vel.Z += 10.f/3;
		else
			particle->Vel.Z += 3;
		particle->Acc.Z -= 1.f/11;
		if (M_Random() < 30) {
			particle->size = 4;
			particle->color = color2;
		} else {
			particle->size = 6;
			particle->color = color1;
		}
	}
}

void P_RunEffect (AActor *actor, int effects)
{
	DAngle moveangle = actor->Vel.Angle();

	particle_t *particle;
	int i;

	if ((effects & FX_ROCKET) && (cl_rockettrails & 1))
	{
		// Rocket trail
		double backx = -actor->radius * 2 * moveangle.Cos();
		double backy = -actor->radius * 2 * moveangle.Sin();
		double backz = actor->Height * ((2. / 3) - actor->Vel.Z / 8);

		DAngle an = moveangle + DAngle::fromDeg(90.);
		double speed;

		particle = JitterParticle (actor->Level, 3 + (M_Random() & 31));
		if (particle) {
			double pathdist = M_Random() / 256.;
			DVector3 pos = actor->Vec3Offset(
				backx - actor->Vel.X * pathdist,
				backy - actor->Vel.Y * pathdist,
				backz - actor->Vel.Z * pathdist);
			particle->Pos = pos;
			speed = (M_Random () - 128) * (1./200);
			particle->Vel.X += speed * an.Cos();
			particle->Vel.Y += speed * an.Sin();
			particle->Vel.Z -= 1.f/36;
			particle->Acc.Z -= 1.f/20;
			particle->color = yellow;
			particle->size = 2;
		}
		for (i = 6; i; i--) {
			particle_t *particle = JitterParticle (actor->Level, 3 + (M_Random() & 31));
			if (particle) {
				double pathdist = M_Random() / 256.;
				DVector3 pos = actor->Vec3Offset(
					backx - actor->Vel.X * pathdist,
					backy - actor->Vel.Y * pathdist,
					backz - actor->Vel.Z * pathdist + (M_Random() / 64.));
				particle->Pos = pos;

				speed = (M_Random () - 128) * (1./200);
				particle->Vel.X += speed * an.Cos();
				particle->Vel.Y += speed * an.Sin();
				particle->Vel.Z += 1.f / 80;
				particle->Acc.Z += 1.f / 40;
				if (M_Random () & 7)
					particle->color = grey2;
				else
					particle->color = grey1;
				particle->size = 3;
			} else
				break;
		}
	}
	if ((effects & FX_GRENADE) && (cl_rockettrails & 1))
	{
		// Grenade trail

		DVector3 pos = actor->Vec3Angle(-actor->radius * 2, moveangle, -actor->Height * actor->Vel.Z / 8 + actor->Height * (2. / 3));

		P_DrawSplash2 (actor->Level, 6, pos, moveangle + DAngle::fromDeg(180), 2, 2);
	}
	if (actor->fountaincolor)
	{
		// Particle fountain

		static const int *fountainColors[16] = 
			{ &black,	&black,
			  &red,		&red1,
			  &green,	&green1,
			  &blue,	&blue1,
			  &yellow,	&yellow1,
			  &purple,	&purple1,
			  &black,	&grey3,
			  &grey4,	&white
			};
		int color = actor->fountaincolor*2;
		if (color < 0 || color >= 16) color = 0;
		MakeFountain (actor, *fountainColors[color], *fountainColors[color+1]);
	}
	if (effects & FX_RESPAWNINVUL)
	{
		// Respawn protection

		static const int *protectColors[2] = { &yellow1, &white };

		for (i = 3; i > 0; i--)
		{
			particle = JitterParticle (actor->Level, 16);
			if (particle != NULL)
			{
				DAngle ang = DAngle::fromDeg(M_Random() * (360 / 256.));
				DVector3 pos = actor->Vec3Angle(actor->radius, ang, 0);
				particle->Pos = pos;
				particle->color = *protectColors[M_Random() & 1];
				particle->Vel.Z = 1;
				particle->Acc.Z = M_Random () / 512.;
				particle->size = 1;
				if (M_Random () < 128)
				{ // make particle fall from top of actor
					particle->Pos.Z += actor->Height;
					particle->Vel.Z = -particle->Vel.Z;
					particle->Acc.Z = -particle->Acc.Z;
				}
			}
		}
	}
}

void P_DrawSplash (FLevelLocals *Level, int count, const DVector3 &pos, DAngle angle, int kind)
{
	int color1, color2;

	switch (kind)
	{
	case 1:		// Spark
		color1 = orange;
		color2 = yorange;
		break;
	default:
		return;
	}

	for (; count; count--)
	{
		particle_t *p = JitterParticle (Level, 10);

		if (!p)
			break;

		p->size = 2;
		p->color = M_Random() & 0x80 ? color1 : color2;
		p->Vel.Z -= M_Random () / 128.;
		p->Acc.Z -= 1./8;
		p->Acc.X += (M_Random () - 128) / 8192.;
		p->Acc.Y += (M_Random () - 128) / 8192.;
		p->Pos.Z = pos.Z - M_Random () / 64.;
		angle += DAngle::fromDeg(M_Random() * (45./256));
		p->Pos.X = pos.X + (M_Random() & 15)*angle.Cos();
		p->Pos.Y = pos.Y + (M_Random() & 15)*angle.Sin();
	}
}

void P_DrawSplash2 (FLevelLocals *Level, int count, const DVector3 &pos, DAngle angle, int updown, int kind)
{
	int color1, color2, zadd;
	double zvel, zspread;

	switch (kind)
	{
	case 0:		// Blood
		color1 = blood1;
		color2 = blood2;
		break;
	case 1:		// Gunshot
		color1 = grey3;
		color2 = grey5;
		break;
	case 2:		// Smoke
		color1 = grey3;
		color2 = grey1;
		break;
	default:	// colorized blood
		color1 = ParticleColor(kind);
		color2 = ParticleColor(RPART(kind)/3, GPART(kind)/3, BPART(kind)/3);
		break;
	}

	zvel = -1./512.;
	zspread = updown ? -6000 / 65536. : 6000 / 65536.;
	zadd = (updown == 2) ? -128 : 0;

	for (; count; count--)
	{
		particle_t *p = NewParticle (Level);
		DAngle an;

		if (!p)
			break;
		*p = {};

		p->ttl = 12;
		p->fadestep = FADEFROMTTL(12);
		p->alpha = 1.f;
		p->size = 4;
		p->color = M_Random() & 0x80 ? color1 : color2;
		p->Vel.Z = M_Random() * zvel;
		p->Acc.Z = -1 / 22.f;
		if (kind) 
		{
			an = angle + DAngle::fromDeg((M_Random() - 128) * (180 / 256.));
			p->Vel.X = M_Random() * an.Cos() / 2048.;
			p->Vel.Y = M_Random() * an.Sin() / 2048.;
			p->Acc.X = p->Vel.X / 16.;
			p->Acc.Y = p->Vel.Y / 16.;
		}
		an = angle + DAngle::fromDeg((M_Random() - 128) * (90 / 256.));
		p->Pos.X = pos.X + ((M_Random() & 31) - 15) * an.Cos();
		p->Pos.Y = pos.Y + ((M_Random() & 31) - 15) * an.Sin();
		p->Pos.Z = pos.Z + (M_Random() + zadd - 128) * zspread;
	}
}

struct TrailSegment
{
	DVector3 start;
	DVector3 dir;
	DVector3 extend;
	DVector2 soundpos;
	double length;
	double sounddist;
};



void P_DrawRailTrail(AActor *source, TArray<SPortalHit> &portalhits, int color1, int color2, double maxdiff, int flags, PClassActor *spawnclass, DAngle angle, int duration, double sparsity, double drift, int SpiralOffset, DAngle pitch)
{
	double length = 0;
	int steps, i;
	TArray<TrailSegment> trail;
	TAngle<double> deg;
	DVector3 pos;
	bool fullbright;
	unsigned segment;
	double lencount;

	for (unsigned i = 0; i < portalhits.Size() - 1; i++)
	{
		TrailSegment seg;

		seg.start = portalhits[i].ContPos;
		seg.dir = portalhits[i].OutDir;
		seg.length = (portalhits[i + 1].HitPos - seg.start).Length();

		//Calculate PerpendicularVector (extend, dir):
		double minelem = 1;
		int epos;
		int ii;
		for (epos = 0, ii = 0; ii < 3; ++ii)
		{
			if (fabs(seg.dir[ii]) < minelem)
			{
				epos = ii;
				minelem = fabs(seg.dir[ii]);
			}
		}
		DVector3 tempvec(0, 0, 0);
		tempvec[epos] = 1;
		seg.extend = (tempvec - (seg.dir | tempvec) * seg.dir) * 3;
		length += seg.length;

		auto player = source->Level->GetConsolePlayer();
		if (player)
		{
			// Only consider sound in 2D (for now, anyway)
			// [BB] You have to divide by lengthsquared here, not multiply with it.
			AActor *mo = player->camera;
			double r = ((seg.start.Y - mo->Y()) * (-seg.dir.Y) - (seg.start.X - mo->X()) * (seg.dir.X)) / (seg.length * seg.length);
			r = clamp<double>(r, 0., 1.);
			seg.soundpos = seg.start.XY() + r * seg.dir.XY();
			seg.sounddist = (seg.soundpos - mo->Pos()).LengthSquared();
		}
		else
		{
			// Set to invalid for secondary levels.
			seg.soundpos = {0,0};
			seg.sounddist = -1;
		}
		trail.Push(seg);
	}

	steps = xs_FloorToInt(length / 3);
	fullbright = !!(flags & RAF_FULLBRIGHT);

	if (steps)
	{
		if (!(flags & RAF_SILENT))
		{
			auto player = source->Level->GetConsolePlayer();
			if (player)
			{
				FSoundID sound;
				
				// Allow other sounds than 'weapons/railgf'!
				if (!source->player) sound = source->AttackSound;
				else if (source->player->ReadyWeapon) sound = source->player->ReadyWeapon->AttackSound;
				else sound = NO_SOUND;
				if (!sound.isvalid()) sound = S_FindSound("weapons/railgf");
				
				// The railgun's sound is special. It gets played from the
				// point on the slug's trail that is closest to the hearing player.
				AActor *mo = player->camera;
				
				if (fabs(mo->X() - trail[0].start.X) < 20 && fabs(mo->Y() - trail[0].start.Y) < 20)
				{ // This player (probably) fired the railgun
					S_Sound (mo, CHAN_WEAPON, 0, sound, 1, ATTN_NORM);
				}
				else
				{
					TrailSegment *shortest = NULL;
					for (auto &seg : trail)
					{
						if (shortest == NULL || shortest->sounddist > seg.sounddist) shortest = &seg;
					}
					S_Sound (source->Level, DVector3(shortest->soundpos, r_viewpoint.Pos.Z), CHAN_WEAPON, 0, sound, 1, ATTN_NORM);
				}
			}
		}
	}
	else
	{
		// line is 0 length, so nothing to do
		return;
	}

	// Create the outer spiral.
	if (color1 != -1 && (!r_rail_smartspiral || color2 == -1) && r_rail_spiralsparsity > 0 && (spawnclass == NULL))
	{
		double stepsize = 3 * r_rail_spiralsparsity * sparsity;
		int spiral_steps = (int)(steps * r_rail_spiralsparsity / sparsity);
		segment = 0;
		lencount = trail[0].length;
		
		color1 = color1 == 0 ? -1 : ParticleColor(color1);
		pos = trail[0].start;
		deg = DAngle::fromDeg(SpiralOffset);
		for (i = spiral_steps; i; i--)
		{
			particle_t *p = NewParticle (source->Level);
			DVector3 tempvec;

			if (!p)
				return;
			*p = {};

			int spiralduration = (duration == 0) ? TICRATE : duration;

			p->alpha = 1.f;
			p->ttl = spiralduration;
			p->fadestep = FADEFROMTTL(spiralduration);
			p->size = 3;
			if(fullbright)
			{
				p->flags |= SPF_FULLBRIGHT;
			}

			tempvec = DMatrix3x3(trail[segment].dir, deg) * trail[segment].extend;
			p->Vel = FVector3(tempvec * drift / 16.);
			p->Pos = tempvec + pos;
			pos += trail[segment].dir * stepsize;
			deg += DAngle::fromDeg(r_rail_spiralsparsity * 14);
			lencount -= stepsize;
			if (color1 == -1)
			{
				int rand = M_Random();

				if (rand < 155)
					p->color = rblue2;
				else if (rand < 188)
					p->color = rblue1;
				else if (rand < 222)
					p->color = rblue3;
				else
					p->color = rblue4;
			}
			else 
			{
				p->color = color1;
			}

			if (lencount <= 0)
			{
				segment++;
				if (segment < trail.Size())
				{
					pos = trail[segment].start - trail[segment].dir * lencount;
					lencount += trail[segment].length;
				}
				else
				{
					// should never happen but if something goes wrong, just terminate the loop.
					break;
				}
			}
		}
	}

	// Create the inner trail.
	if (color2 != -1 && r_rail_trailsparsity > 0 && spawnclass == NULL)
	{
		double stepsize = 3 * r_rail_trailsparsity * sparsity;
		int trail_steps = xs_FloorToInt(steps * r_rail_trailsparsity / sparsity);

		color2 = color2 == 0 ? -1 : ParticleColor(color2);
		DVector3 diff(0, 0, 0);

		pos = trail[0].start;
		lencount = trail[0].length;
		segment = 0;
		for (i = trail_steps; i; i--)
		{
			// [XA] inner trail uses a different default duration (33).
			int innerduration = (duration == 0) ? 33 : duration;
			particle_t *p = JitterParticle (source->Level, innerduration, (float)drift);

			if (!p)
				return;

			if (maxdiff > 0)
			{
				int rnd = M_Random ();
				if (rnd & 1)
					diff.X = clamp<double>(diff.X + ((rnd & 8) ? 1 : -1), -maxdiff, maxdiff);
				if (rnd & 2)
					diff.Y = clamp<double>(diff.Y + ((rnd & 16) ? 1 : -1), -maxdiff, maxdiff);
				if (rnd & 4)
					diff.Z = clamp<double>(diff.Z + ((rnd & 32) ? 1 : -1), -maxdiff, maxdiff);
			}

			DVector3 postmp = pos + diff;

			p->size = 2;
			p->Pos = postmp;
			if (color1 != -1)
				p->Acc.Z -= 1./4096;
			pos += trail[segment].dir * stepsize;
			lencount -= stepsize;
			if(fullbright)
			{
				p->flags |= SPF_FULLBRIGHT;
			}

			if (color2 == -1)
			{
				int rand = M_Random();

				if (rand < 85)
					p->color = grey4;
				else if (rand < 170)
					p->color = grey2;
				else
					p->color = grey1;
			}
			else 
			{
				p->color = color2;
			}
			if (lencount <= 0)
			{
				segment++;
				if (segment < trail.Size())
				{
					pos = trail[segment].start - trail[segment].dir * lencount;
					lencount += trail[segment].length;
				}
				else
				{
					// should never happen but if something goes wrong, just terminate the loop.
					break;
				}
			}

		}
	}
	// create actors
	if (spawnclass != NULL)
	{
		if (sparsity < 1)
			sparsity = 32;

		double stepsize = sparsity;
		int trail_steps = (int)((steps * 3) / sparsity);
		DVector3 diff(0, 0, 0);

		pos = trail[0].start;
		lencount = trail[0].length;
		segment = 0;

		for (i = trail_steps; i; i--)
		{
			if (maxdiff > 0)
			{
				int rnd = pr_railtrail();
				if (rnd & 1)
					diff.X = clamp<double>(diff.X + ((rnd & 8) ? 1 : -1), -maxdiff, maxdiff);
				if (rnd & 2)
					diff.Y = clamp<double>(diff.Y + ((rnd & 16) ? 1 : -1), -maxdiff, maxdiff);
				if (rnd & 4)
					diff.Z = clamp<double>(diff.Z + ((rnd & 32) ? 1 : -1), -maxdiff, maxdiff);
			}			
			AActor *thing = Spawn (source->Level, spawnclass, pos + diff, ALLOW_REPLACE);
			if (thing)
			{
				if (source)	thing->target = source;
				thing->Angles.Pitch = pitch;
				thing->Angles.Yaw = angle;
			}
			pos += trail[segment].dir * stepsize;
			lencount -= stepsize;
			if (lencount <= 0)
			{
				segment++;
				if (segment < trail.Size())
				{
					pos = trail[segment].start - trail[segment].dir * lencount;
					lencount += trail[segment].length;
				}
				else
				{
					// should never happen but if something goes wrong, just terminate the loop.
					break;
				}
			}
		}
	}
}

void P_DisconnectEffect (AActor *actor)
{
	int i;

	if (actor == NULL)
		return;

	for (i = 64; i; i--)
	{
		particle_t *p = JitterParticle (actor->Level, TICRATE*2);

		if (!p)
			break;

		double xo = (M_Random() - 128)*actor->radius / 128;
		double yo = (M_Random() - 128)*actor->radius / 128;
		double zo = M_Random()*actor->Height / 256;

		DVector3 pos = actor->Vec3Offset(xo, yo, zo);
		p->Pos = pos;
		p->Acc.Z -= 1./4096;
		p->color = M_Random() < 128 ? maroon1 : maroon2;
		p->size = 4;
	}
}

//===========================================================================
// 
// ZScript Sprite (DVisualThinker)
// Concept by Major Cooke
// Most code borrowed by Actor and particles above
// 
//===========================================================================

void DVisualThinker::Construct()
{
	PT = {};
	PT.Pos = { 0,0,0 };
	PT.Vel = { 0,0,0 };
	Offset = { 0,0 };
	Scale = { 1,1 };
	PT.Roll = 0.0;
	PT.alpha = 1.0;
	LightLevel = -1;
	PT.texture = FTextureID();
	PT.style = STYLE_Normal;
	PT.flags = 0;
	Translation = NO_TRANSLATION;
	PT.subsector = nullptr;
	cursector = nullptr;
	PT.color = 0xffffff;
	spr = new HWSprite();
	AnimatedTexture.SetNull();
}

DVisualThinker::DVisualThinker()
{
	Construct();
}

void DVisualThinker::OnDestroy()
{
	PT.alpha = 0.0; // stops all rendering.
	if(spr)
	{
		delete spr;
		spr = nullptr;
	}
	Super::OnDestroy();
}

DVisualThinker* DVisualThinker::NewVisualThinker(FLevelLocals* Level, PClass* type)
{
	if (type == nullptr)
		return nullptr;
	else if (type->bAbstract)
	{
		Printf("Attempt to spawn an instance of abstract VisualThinker class %s\n", type->TypeName.GetChars());
		return nullptr;
	}
	else if (!type->IsDescendantOf(RUNTIME_CLASS(DVisualThinker)))
	{
		Printf("Attempt to spawn class not inherent to VisualThinker: %s\n", type->TypeName.GetChars());
		return nullptr;
	}

	DVisualThinker *zs = static_cast<DVisualThinker*>(Level->CreateThinker(type, STAT_VISUALTHINKER));
	zs->Construct();
	return zs;
}

static DVisualThinker* SpawnVisualThinker(FLevelLocals* Level, PClass* type)
{
	return DVisualThinker::NewVisualThinker(Level, type);
}

DEFINE_ACTION_FUNCTION_NATIVE(FLevelLocals, SpawnVisualThinker, SpawnVisualThinker)
{
	PARAM_SELF_STRUCT_PROLOGUE(FLevelLocals);
	PARAM_CLASS_NOT_NULL(type, DVisualThinker);
	DVisualThinker* zs = SpawnVisualThinker(self, type);
	ACTION_RETURN_OBJECT(zs);
}

void DVisualThinker::UpdateSpriteInfo()
{
	PT.style = ERenderStyle(GetRenderStyle());
	if((PT.flags & SPF_LOCAL_ANIM) && PT.texture != AnimatedTexture)
	{
		AnimatedTexture = PT.texture;
		TexAnim.InitStandaloneAnimation(PT.animData, PT.texture, Level->maptime);
	}
}

// This runs just like Actor's, make sure to call Super.Tick() in ZScript.
void DVisualThinker::Tick()
{
	if (ObjectFlags & OF_EuthanizeMe)
		return;

	// There won't be a standard particle for this, it's only for graphics.
	if (!PT.texture.isValid())
	{
		Printf("No valid texture, destroyed");
		Destroy();
		return;
	}

	if (isFrozen())
	{	// needed here because it won't retroactively update like actors do.
		PT.subsector = Level->PointInRenderSubsector(PT.Pos);
		cursector = PT.subsector->sector;
		UpdateSpriteInfo(); 
		return;
	}
	Prev = PT.Pos;
	PrevRoll = PT.Roll;
	// Handle crossing a line portal
	DVector2 newxy = Level->GetPortalOffsetPosition(PT.Pos.X, PT.Pos.Y, PT.Vel.X, PT.Vel.Y);
	PT.Pos.X = newxy.X;
	PT.Pos.Y = newxy.Y;
	PT.Pos.Z += PT.Vel.Z;

	PT.subsector = Level->PointInRenderSubsector(PT.Pos);
	cursector = PT.subsector->sector;
	// Handle crossing a sector portal.
	if (!cursector->PortalBlocksMovement(sector_t::ceiling))
	{
		if (PT.Pos.Z > cursector->GetPortalPlaneZ(sector_t::ceiling))
		{
			PT.Pos += cursector->GetPortalDisplacement(sector_t::ceiling);
			PT.subsector = Level->PointInRenderSubsector(PT.Pos);
			cursector = PT.subsector->sector;
		}
	}
	else if (!cursector->PortalBlocksMovement(sector_t::floor))
	{
		if (PT.Pos.Z < cursector->GetPortalPlaneZ(sector_t::floor))
		{
			PT.Pos += cursector->GetPortalDisplacement(sector_t::floor);
			PT.subsector = Level->PointInRenderSubsector(PT.Pos);
			cursector = PT.subsector->sector;
		}
	}
	UpdateSpriteInfo();
}

int DVisualThinker::GetLightLevel(sector_t* rendersector) const
{
	int lightlevel = rendersector->GetSpriteLight();

	if (bAddLightLevel)
	{
		lightlevel += LightLevel;
	}
	else if (LightLevel > -1)
	{
		lightlevel = LightLevel;
	}
	return lightlevel;
}

FVector3 DVisualThinker::InterpolatedPosition(double ticFrac) const
{
	if (bDontInterpolate) return FVector3(PT.Pos);

	DVector3 proc = Prev + (ticFrac * (PT.Pos - Prev));
	return FVector3(proc);

}

float DVisualThinker::InterpolatedRoll(double ticFrac) const
{
	if (bDontInterpolate) return PT.Roll;

	return float(PrevRoll + (PT.Roll - PrevRoll) * ticFrac);
}



void DVisualThinker::SetTranslation(FName trname)
{
	// There is no constant for the empty name...
	if (trname.GetChars()[0] == 0)
	{
		// '' removes it
		Translation = NO_TRANSLATION;
		return;
	}

	auto tnum = R_FindCustomTranslation(trname);
	if (tnum != INVALID_TRANSLATION)
	{
		Translation = tnum;
	}
	// silently ignore if the name does not exist, this would create some insane message spam otherwise.
}

void SetTranslation(DVisualThinker * self, int i_trans)
{
	FName trans {ENamedName(i_trans)};
	self->SetTranslation(trans);
}

DEFINE_ACTION_FUNCTION_NATIVE(DVisualThinker, SetTranslation, SetTranslation)
{
	PARAM_SELF_PROLOGUE(DVisualThinker);
	PARAM_NAME(trans);
	self->SetTranslation(trans);
	return 0;
}

static int IsFrozen(DVisualThinker * self)
{
	return !!(self->Level->isFrozen() && !(self->PT.flags & SPF_NOTIMEFREEZE));
}

bool DVisualThinker::isFrozen()
{
	return IsFrozen(this);
}

DEFINE_ACTION_FUNCTION_NATIVE(DVisualThinker, IsFrozen, IsFrozen)
{
	PARAM_SELF_PROLOGUE(DVisualThinker);
	ACTION_RETURN_BOOL(self->isFrozen());
}

static void SetRenderStyle(DVisualThinker *self, int mode)
{
	if(mode >= 0 && mode < STYLE_Count)
	{
		self->PT.style = ERenderStyle(mode);
	}
}

DEFINE_ACTION_FUNCTION_NATIVE(DVisualThinker, SetRenderStyle, SetRenderStyle)
{
	PARAM_SELF_PROLOGUE(DVisualThinker);
	PARAM_INT(mode);

	self->PT.style = ERenderStyle(mode);
	return 0;
}

int DVisualThinker::GetRenderStyle()
{
	return PT.style;
}

float DVisualThinker::GetOffset(bool y) const // Needed for the renderer.
{
	if (y)
		return (float)(bFlipOffsetY ? Offset.Y : -Offset.Y);
	else
		return (float)(bFlipOffsetX ? Offset.X : -Offset.X);
}

void DVisualThinker::Serialize(FSerializer& arc)
{
	Super::Serialize(arc);

	arc
		("pos", PT.Pos)
		("vel", PT.Vel)
		("prev", Prev)
		("scale", Scale)
		("roll", PT.Roll)
		("prevroll", PrevRoll)
		("offset", Offset)
		("alpha", PT.alpha)
		("texture", PT.texture)
		("style", *reinterpret_cast<int*>(&PT.style))
		("translation", Translation)
		("cursector", cursector)
		("scolor", PT.color)
		("flipx", bXFlip)
		("flipy", bYFlip)
		("dontinterpolate", bDontInterpolate)
		("addlightlevel", bAddLightLevel)
		("flipoffsetx", bFlipOffsetX)
		("flipoffsetY", bFlipOffsetY)
		("lightlevel", LightLevel)
		("flags", PT.flags);
		
}

IMPLEMENT_CLASS(DVisualThinker, false, false);
DEFINE_FIELD_NAMED(DVisualThinker, PT.color, SColor);
DEFINE_FIELD_NAMED(DVisualThinker, PT.Pos, Pos);
DEFINE_FIELD_NAMED(DVisualThinker, PT.Vel, Vel);
DEFINE_FIELD_NAMED(DVisualThinker, PT.Roll, Roll);
DEFINE_FIELD_NAMED(DVisualThinker, PT.alpha, Alpha);
DEFINE_FIELD_NAMED(DVisualThinker, PT.texture, Texture);
DEFINE_FIELD_NAMED(DVisualThinker, PT.flags, Flags);

DEFINE_FIELD(DVisualThinker, Prev);
DEFINE_FIELD(DVisualThinker, Scale);
DEFINE_FIELD(DVisualThinker, Offset);
DEFINE_FIELD(DVisualThinker, PrevRoll);
DEFINE_FIELD(DVisualThinker, Translation);
DEFINE_FIELD(DVisualThinker, LightLevel);
DEFINE_FIELD(DVisualThinker, cursector);
DEFINE_FIELD(DVisualThinker, bXFlip);
DEFINE_FIELD(DVisualThinker, bYFlip);
DEFINE_FIELD(DVisualThinker, bDontInterpolate);
DEFINE_FIELD(DVisualThinker, bAddLightLevel);
DEFINE_FIELD(DVisualThinker, bFlipOffsetX);
DEFINE_FIELD(DVisualThinker, bFlipOffsetY);
