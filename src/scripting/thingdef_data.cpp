/*
** thingdef_data.cpp
**
** DECORATE data tables
**
**---------------------------------------------------------------------------
** Copyright 2002-2008 Christoph Oelckers
** Copyright 2004-2008 Randy Heit
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
** 4. When not used as part of ZDoom or a ZDoom derivative, this code will be
**    covered by the terms of the GNU General Public License as published by
**    the Free Software Foundation; either version 2 of the License, or (at
**    your option) any later version.
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

#include "thingdef.h"
#include "actor.h"
#include "d_player.h"
#include "p_effect.h"
#include "gi.h"
#include "p_terrain.h"
#include "gstrings.h"
#include "g_levellocals.h"
#include "p_checkposition.h"
#include "p_linetracedata.h"
#include "v_font.h"
#include "menu.h"
#include "teaminfo.h"
#include "r_data/sprites.h"
#include "serializer_doom.h"
#include "wi_stuff.h"
#include "a_dynlight.h"
#include "types.h"
#include "dictionary.h"
#include "events.h"

static TArray<FPropertyInfo*> properties;
static TArray<AFuncDesc> AFTable;
static TArray<FieldDesc> FieldTable;
extern int				BackbuttonTime;
extern float			BackbuttonAlpha;

//==========================================================================
//
// List of all flags
//
//==========================================================================

// [RH] Keep GCC quiet by not using offsetof on Actor types.
#define DEFINE_FLAG(prefix, name, type, variable) { (unsigned int)prefix##_##name, #name, (int)(size_t)&((type*)1)->variable - 1, sizeof(((type *)0)->variable), VARF_Native }
#define DEFINE_PROTECTED_FLAG(prefix, name, type, variable) { (unsigned int)prefix##_##name, #name, (int)(size_t)&((type*)1)->variable - 1, sizeof(((type *)0)->variable), VARF_Native|VARF_ReadOnly|VARF_InternalAccess }
#define DEFINE_FLAG2(symbol, name, type, variable) { (unsigned int)symbol, #name, (int)(size_t)&((type*)1)->variable - 1, sizeof(((type *)0)->variable), VARF_Native }
#define DEFINE_FLAG2_DEPRECATED(symbol, name, type, variable) { (unsigned int)symbol, #name, (int)(size_t)&((type*)1)->variable - 1, sizeof(((type *)0)->variable), VARF_Native|VARF_Deprecated }
#define DEFINE_DEPRECATED_FLAG(name) { DEPF_##name, #name, -1, 0, true }
#define DEFINE_DUMMY_FLAG(name, deprec) { DEPF_UNUSED, #name, -1, 0, deprec? VARF_Deprecated:0 }

// internal flags. These do not get exposed to actor definitions but scripts need to be able to access them as variables.
static FFlagDef InternalActorFlagDefs[]=
{
	DEFINE_FLAG(MF7, INCHASE, AActor, flags7),
	DEFINE_FLAG(MF, UNMORPHED, AActor, flags),
	DEFINE_FLAG(MF2, FLY, AActor, flags2),
	DEFINE_FLAG(MF2, ONMOBJ, AActor, flags2),
	DEFINE_FLAG(MF2, ARGSDEFINED, AActor, flags2),
	DEFINE_FLAG(MF3, NOSIGHTCHECK, AActor, flags3),
	DEFINE_FLAG(MF3, CRASHED, AActor, flags3),
	DEFINE_FLAG(MF3, WARNBOT, AActor, flags3),
	DEFINE_FLAG(MF3, HUNTPLAYERS, AActor, flags3),
	DEFINE_FLAG(MF4, NOHATEPLAYERS, AActor, flags4),
	DEFINE_FLAG(MF4, SCROLLMOVE, AActor, flags4),
	DEFINE_FLAG(MF4, VFRICTION, AActor, flags4),
	DEFINE_FLAG(MF4, BOSSSPAWNED, AActor, flags4),
	DEFINE_FLAG(MF5, AVOIDINGDROPOFF, AActor, flags5),
	DEFINE_FLAG(MF5, CHASEGOAL, AActor, flags5),
	DEFINE_FLAG(MF5, INCONVERSATION, AActor, flags5),
	DEFINE_FLAG(MF6, ARMED, AActor, flags6),
	DEFINE_FLAG(MF6, FALLING, AActor, flags6),
	DEFINE_FLAG(MF6, LINEDONE, AActor, flags6),
	DEFINE_FLAG(MF6, SHATTERING, AActor, flags6),
	DEFINE_FLAG(MF6, KILLED, AActor, flags6),
	DEFINE_FLAG(MF6, BOSSCUBE, AActor, flags6),
	DEFINE_FLAG(MF6, INTRYMOVE, AActor, flags6),
	DEFINE_FLAG(MF7, HANDLENODELAY, AActor, flags7),
	DEFINE_FLAG(MF7, FLYCHEAT, AActor, flags7),
	DEFINE_FLAG(FX, RESPAWNINVUL, AActor, effects),
};


static FFlagDef ActorFlagDefs[]=
{
	DEFINE_FLAG(MF, PICKUP, AActor, flags),
	DEFINE_FLAG(MF, SPECIAL, AActor, flags),
	DEFINE_FLAG(MF, SOLID, AActor, flags),
	DEFINE_FLAG(MF, SHOOTABLE, AActor, flags),
	DEFINE_PROTECTED_FLAG(MF, NOSECTOR, AActor, flags),
	DEFINE_PROTECTED_FLAG(MF, NOBLOCKMAP, AActor, flags),
	DEFINE_FLAG(MF, AMBUSH, AActor, flags),
	DEFINE_FLAG(MF, JUSTHIT, AActor, flags),
	DEFINE_FLAG(MF, JUSTATTACKED, AActor, flags),
	DEFINE_FLAG(MF, SPAWNCEILING, AActor, flags),
	DEFINE_FLAG(MF, NOGRAVITY, AActor, flags),
	DEFINE_FLAG(MF, DROPOFF, AActor, flags),
	DEFINE_FLAG(MF, NOCLIP, AActor, flags),
	DEFINE_FLAG(MF, FLOAT, AActor, flags),
	DEFINE_FLAG(MF, TELEPORT, AActor, flags),
	DEFINE_FLAG(MF, MISSILE, AActor, flags),
	DEFINE_FLAG(MF, DROPPED, AActor, flags),
	DEFINE_FLAG(MF, SHADOW, AActor, flags),
	DEFINE_FLAG(MF, NOBLOOD, AActor, flags),
	DEFINE_FLAG(MF, CORPSE, AActor, flags),
	DEFINE_FLAG(MF, INFLOAT, AActor, flags),
	DEFINE_FLAG(MF, COUNTKILL, AActor, flags),
	DEFINE_FLAG(MF, COUNTITEM, AActor, flags),
	DEFINE_FLAG(MF, SKULLFLY, AActor, flags),
	DEFINE_FLAG(MF, NOTDMATCH, AActor, flags),
	DEFINE_FLAG(MF, SPAWNSOUNDSOURCE, AActor, flags),
	DEFINE_FLAG(MF, FRIENDLY, AActor, flags),
	DEFINE_FLAG(MF, NOLIFTDROP, AActor, flags),
	DEFINE_FLAG(MF, STEALTH, AActor, flags),
	DEFINE_FLAG(MF, ICECORPSE, AActor, flags),

	DEFINE_FLAG(MF2, DONTREFLECT, AActor, flags2),
	DEFINE_FLAG(MF2, WINDTHRUST, AActor, flags2),
	DEFINE_FLAG(MF2, DONTSEEKINVISIBLE, AActor, flags2),
	DEFINE_FLAG(MF2, BLASTED, AActor, flags2),
	DEFINE_FLAG(MF2, FLOORCLIP, AActor, flags2),
	DEFINE_FLAG(MF2, SPAWNFLOAT, AActor, flags2),
	DEFINE_FLAG(MF2, NOTELEPORT, AActor, flags2),
	DEFINE_FLAG2(MF2_RIP, RIPPER, AActor, flags2),
	DEFINE_FLAG(MF2, PUSHABLE, AActor, flags2),
	DEFINE_FLAG2(MF2_SLIDE, SLIDESONWALLS, AActor, flags2),
	DEFINE_FLAG2(MF2_PASSMOBJ, CANPASS, AActor, flags2),
	DEFINE_FLAG(MF2, CANNOTPUSH, AActor, flags2),
	DEFINE_FLAG(MF2, THRUGHOST, AActor, flags2),
	DEFINE_FLAG(MF2, BOSS, AActor, flags2),
	DEFINE_FLAG2(MF2_NODMGTHRUST, NODAMAGETHRUST, AActor, flags2),
	DEFINE_FLAG(MF2, DONTTRANSLATE, AActor, flags2),
	DEFINE_FLAG(MF2, TELESTOMP, AActor, flags2),
	DEFINE_FLAG(MF2, FLOATBOB, AActor, flags2),
	DEFINE_FLAG(MF2, THRUACTORS, AActor, flags2),
	DEFINE_FLAG2(MF2_IMPACT, ACTIVATEIMPACT, AActor, flags2),
	DEFINE_FLAG2(MF2_PUSHWALL, CANPUSHWALLS, AActor, flags2),
	DEFINE_FLAG2(MF2_MCROSS, ACTIVATEMCROSS, AActor, flags2),
	DEFINE_FLAG2(MF2_PCROSS, ACTIVATEPCROSS, AActor, flags2),
	DEFINE_FLAG(MF2, CANTLEAVEFLOORPIC, AActor, flags2),
	DEFINE_FLAG(MF2, NONSHOOTABLE, AActor, flags2),
	DEFINE_FLAG(MF2, INVULNERABLE, AActor, flags2),
	DEFINE_FLAG(MF2, DORMANT, AActor, flags2),
	DEFINE_FLAG(MF2, SEEKERMISSILE, AActor, flags2),
	DEFINE_FLAG(MF2, REFLECTIVE, AActor, flags2),

	DEFINE_FLAG(MF3, FLOORHUGGER, AActor, flags3),
	DEFINE_FLAG(MF3, CEILINGHUGGER, AActor, flags3),
	DEFINE_FLAG(MF3, NORADIUSDMG, AActor, flags3),
	DEFINE_FLAG(MF3, GHOST, AActor, flags3),
	DEFINE_FLAG(MF3, SPECIALFLOORCLIP, AActor, flags3),
	DEFINE_FLAG(MF3, ALWAYSPUFF, AActor, flags3),
	DEFINE_FLAG(MF3, DONTSPLASH, AActor, flags3),
	DEFINE_FLAG(MF3, DONTOVERLAP, AActor, flags3),
	DEFINE_FLAG(MF3, DONTMORPH, AActor, flags3),
	DEFINE_FLAG(MF3, DONTSQUASH, AActor, flags3),
	DEFINE_FLAG(MF3, EXPLOCOUNT, AActor, flags3),
	DEFINE_FLAG(MF3, FULLVOLACTIVE, AActor, flags3),
	DEFINE_FLAG(MF3, ISMONSTER, AActor, flags3),
	DEFINE_FLAG(MF3, SKYEXPLODE, AActor, flags3),
	DEFINE_FLAG(MF3, STAYMORPHED, AActor, flags3),
	DEFINE_FLAG(MF3, DONTBLAST, AActor, flags3),
	DEFINE_FLAG(MF3, CANBLAST, AActor, flags3),
	DEFINE_FLAG(MF3, NOTARGET, AActor, flags3),
	DEFINE_FLAG(MF3, DONTGIB, AActor, flags3),
	DEFINE_FLAG(MF3, NOBLOCKMONST, AActor, flags3),
	DEFINE_FLAG(MF3, FULLVOLDEATH, AActor, flags3),
	DEFINE_FLAG(MF3, AVOIDMELEE, AActor, flags3),
	DEFINE_FLAG(MF3, SCREENSEEKER, AActor, flags3),
	DEFINE_FLAG(MF3, FOILINVUL, AActor, flags3),
	DEFINE_FLAG(MF3, NOTELEOTHER, AActor, flags3),
	DEFINE_FLAG(MF3, BLOODLESSIMPACT, AActor, flags3),
	DEFINE_FLAG(MF3, NOEXPLODEFLOOR, AActor, flags3),
	DEFINE_FLAG(MF3, PUFFONACTORS, AActor, flags3),

	DEFINE_FLAG(MF4, QUICKTORETALIATE, AActor, flags4),
	DEFINE_FLAG(MF4, NOICEDEATH, AActor, flags4),
	DEFINE_FLAG(MF4, RANDOMIZE, AActor, flags4),
	DEFINE_FLAG(MF4, FIXMAPTHINGPOS , AActor, flags4),
	DEFINE_FLAG(MF4, ACTLIKEBRIDGE, AActor, flags4),
	DEFINE_FLAG(MF4, STRIFEDAMAGE, AActor, flags4),
	DEFINE_FLAG(MF4, CANUSEWALLS, AActor, flags4),
	DEFINE_FLAG(MF4, MISSILEMORE, AActor, flags4),
	DEFINE_FLAG(MF4, MISSILEEVENMORE, AActor, flags4),
	DEFINE_FLAG(MF4, FORCERADIUSDMG, AActor, flags4),
	DEFINE_FLAG(MF4, DONTFALL, AActor, flags4),
	DEFINE_FLAG(MF4, SEESDAGGERS, AActor, flags4),
	DEFINE_FLAG(MF4, INCOMBAT, AActor, flags4),
	DEFINE_FLAG(MF4, LOOKALLAROUND, AActor, flags4),
	DEFINE_FLAG(MF4, STANDSTILL, AActor, flags4),
	DEFINE_FLAG(MF4, SPECTRAL, AActor, flags4),
	DEFINE_FLAG(MF4, NOSPLASHALERT, AActor, flags4),
	DEFINE_FLAG(MF4, SYNCHRONIZED, AActor, flags4),
	DEFINE_FLAG(MF4, NOTARGETSWITCH, AActor, flags4),
	DEFINE_FLAG(MF4, DONTHARMCLASS, AActor, flags4),
	DEFINE_FLAG(MF4, SHIELDREFLECT, AActor, flags4),
	DEFINE_FLAG(MF4, DEFLECT, AActor, flags4),
	DEFINE_FLAG(MF4, ALLOWPARTICLES, AActor, flags4),
	DEFINE_FLAG(MF4, EXTREMEDEATH, AActor, flags4),
	DEFINE_FLAG(MF4, NOEXTREMEDEATH, AActor, flags4),
	DEFINE_FLAG(MF4, FRIGHTENED, AActor, flags4),
	DEFINE_FLAG(MF4, NOSKIN, AActor, flags4),
	DEFINE_FLAG(MF4, BOSSDEATH, AActor, flags4),

	DEFINE_FLAG(MF5, DONTDRAIN, AActor, flags5),
	DEFINE_FLAG(MF5, GETOWNER, AActor, flags5),
	DEFINE_FLAG(MF5, NODROPOFF, AActor, flags5),
	DEFINE_FLAG(MF5, NOFORWARDFALL, AActor, flags5),
	DEFINE_FLAG(MF5, COUNTSECRET, AActor, flags5),
	DEFINE_FLAG(MF5, NODAMAGE, AActor, flags5),
	DEFINE_FLAG(MF5, BLOODSPLATTER, AActor, flags5),
	DEFINE_FLAG(MF5, OLDRADIUSDMG, AActor, flags5),
	DEFINE_FLAG(MF5, DEHEXPLOSION, AActor, flags5),
	DEFINE_FLAG(MF5, PIERCEARMOR, AActor, flags5),
	DEFINE_FLAG(MF5, NOBLOODDECALS, AActor, flags5),
	DEFINE_FLAG(MF5, USESPECIAL, AActor, flags5),
	DEFINE_FLAG(MF5, NOPAIN, AActor, flags5),
	DEFINE_FLAG(MF5, ALWAYSFAST, AActor, flags5),
	DEFINE_FLAG(MF5, NEVERFAST, AActor, flags5),
	DEFINE_FLAG(MF5, ALWAYSRESPAWN, AActor, flags5),
	DEFINE_FLAG(MF5, NEVERRESPAWN, AActor, flags5),
	DEFINE_FLAG(MF5, DONTRIP, AActor, flags5),
	DEFINE_FLAG(MF5, NOINFIGHTING, AActor, flags5),
	DEFINE_FLAG(MF5, NOINTERACTION, AActor, flags5),
	DEFINE_FLAG(MF5, NOTIMEFREEZE, AActor, flags5),
	DEFINE_FLAG(MF5, PUFFGETSOWNER, AActor, flags5), // [BB] added PUFFGETSOWNER
	DEFINE_FLAG(MF5, SPECIALFIREDAMAGE, AActor, flags5),
	DEFINE_FLAG(MF5, SUMMONEDMONSTER, AActor, flags5),
	DEFINE_FLAG(MF5, NOVERTICALMELEERANGE, AActor, flags5),
	DEFINE_FLAG(MF5, BRIGHT, AActor, flags5),
	DEFINE_FLAG(MF5, CANTSEEK, AActor, flags5),
	DEFINE_FLAG(MF5, PAINLESS, AActor, flags5),
	DEFINE_FLAG(MF5, MOVEWITHSECTOR, AActor, flags5),

	DEFINE_FLAG(MF6, NOBOSSRIP, AActor, flags6),
	DEFINE_FLAG(MF6, THRUSPECIES, AActor, flags6),
	DEFINE_FLAG(MF6, MTHRUSPECIES, AActor, flags6),
	DEFINE_FLAG(MF6, FORCEPAIN, AActor, flags6),
	DEFINE_FLAG(MF6, NOFEAR, AActor, flags6),
	DEFINE_FLAG(MF6, BUMPSPECIAL, AActor, flags6),
	DEFINE_FLAG(MF6, DONTHARMSPECIES, AActor, flags6),
	DEFINE_FLAG(MF6, STEPMISSILE, AActor, flags6),
	DEFINE_FLAG(MF6, NOTELEFRAG, AActor, flags6),
	DEFINE_FLAG(MF6, TOUCHY, AActor, flags6),
	DEFINE_FLAG(MF6, CANJUMP, AActor, flags6),
	DEFINE_FLAG(MF6, JUMPDOWN, AActor, flags6),
	DEFINE_FLAG(MF6, VULNERABLE, AActor, flags6),
	DEFINE_FLAG(MF6, NOTRIGGER, AActor, flags6),
	DEFINE_FLAG(MF6, ADDITIVEPOISONDAMAGE, AActor, flags6),
	DEFINE_FLAG(MF6, ADDITIVEPOISONDURATION, AActor, flags6),
	DEFINE_FLAG(MF6, BLOCKEDBYSOLIDACTORS, AActor, flags6),
	DEFINE_FLAG(MF6, NOMENU, AActor, flags6),
	DEFINE_FLAG(MF6, SEEINVISIBLE, AActor, flags6),
	DEFINE_FLAG(MF6, DONTCORPSE, AActor, flags6),
	DEFINE_FLAG(MF6, DOHARMSPECIES, AActor, flags6),
	DEFINE_FLAG(MF6, POISONALWAYS, AActor, flags6),
	DEFINE_FLAG(MF6, NOTAUTOAIMED, AActor, flags6),
	DEFINE_FLAG(MF6, NOTONAUTOMAP, AActor, flags6),
	DEFINE_FLAG(MF6, RELATIVETOFLOOR, AActor, flags6),

	DEFINE_FLAG(MF7, NEVERTARGET, AActor, flags7),
	DEFINE_FLAG(MF7, NOTELESTOMP, AActor, flags7),
	DEFINE_FLAG(MF7, ALWAYSTELEFRAG, AActor, flags7),
	DEFINE_FLAG(MF7, WEAPONSPAWN, AActor, flags7),
	DEFINE_FLAG(MF7, HARMFRIENDS, AActor, flags7),
	DEFINE_FLAG(MF7, BUDDHA, AActor, flags7),
	DEFINE_FLAG(MF7, FOILBUDDHA, AActor, flags7),
	DEFINE_FLAG(MF7, DONTTHRUST, AActor, flags7),
	DEFINE_FLAG(MF7, ALLOWPAIN, AActor, flags7),
	DEFINE_FLAG(MF7, CAUSEPAIN, AActor, flags7),
	DEFINE_FLAG(MF7, THRUREFLECT, AActor, flags7),
	DEFINE_FLAG(MF7, MIRRORREFLECT, AActor, flags7),
	DEFINE_FLAG(MF7, AIMREFLECT, AActor, flags7),
	DEFINE_FLAG(MF7, HITTARGET, AActor, flags7),
	DEFINE_FLAG(MF7, HITMASTER, AActor, flags7),
	DEFINE_FLAG(MF7, HITTRACER, AActor, flags7),
	DEFINE_FLAG(MF7, NODECAL, AActor, flags7),		// [ZK] Decal flags
	DEFINE_FLAG(MF7, FORCEDECAL, AActor, flags7),
	DEFINE_FLAG(MF7, LAXTELEFRAGDMG, AActor, flags7),
	DEFINE_FLAG(MF7, ICESHATTER, AActor, flags7),
	DEFINE_FLAG(MF7, ALLOWTHRUFLAGS, AActor, flags7),
	DEFINE_FLAG(MF7, USEKILLSCRIPTS, AActor, flags7),
	DEFINE_FLAG(MF7, NOKILLSCRIPTS, AActor, flags7),
	DEFINE_FLAG(MF7, SPRITEANGLE, AActor, flags7),
	DEFINE_FLAG(MF7, SMASHABLE, AActor, flags7),
	DEFINE_FLAG(MF7, NOSHIELDREFLECT, AActor, flags7),
	DEFINE_FLAG(MF7, FORCEZERORADIUSDMG, AActor, flags7),
	DEFINE_FLAG(MF7, NOINFIGHTSPECIES, AActor, flags7),
	DEFINE_FLAG(MF7, FORCEINFIGHTING, AActor, flags7),

	DEFINE_FLAG(MF8, FRIGHTENING, AActor, flags8),
	DEFINE_FLAG(MF8, BLOCKASPLAYER, AActor, flags8),
	DEFINE_FLAG(MF8, DONTFACETALKER, AActor, flags8),
	DEFINE_FLAG(MF8, HITOWNER, AActor, flags8),
	DEFINE_FLAG(MF8, NOFRICTION, AActor, flags8),
	DEFINE_FLAG(MF8, NOFRICTIONBOUNCE, AActor, flags8),
	DEFINE_FLAG(MF8, RETARGETAFTERSLAM, AActor, flags8),
	DEFINE_FLAG(MF8, STOPRAILS, AActor, flags8),
	DEFINE_FLAG(MF8, FALLDAMAGE, AActor, flags8),
	DEFINE_FLAG(MF8, MINVISIBLE, AActor, flags8),
	DEFINE_FLAG(MF8, MVISBLOCKED, AActor, flags8),
	DEFINE_FLAG(MF8, ABSVIEWANGLES, AActor, flags8),
	DEFINE_FLAG(MF8, ALLOWTHRUBITS, AActor, flags8),
	DEFINE_FLAG(MF8, FULLVOLSEE, AActor, flags8),
	DEFINE_FLAG(MF8, E1M8BOSS, AActor, flags8),
	DEFINE_FLAG(MF8, E2M8BOSS, AActor, flags8),
	DEFINE_FLAG(MF8, E3M8BOSS, AActor, flags8),
	DEFINE_FLAG(MF8, E4M8BOSS, AActor, flags8),
	DEFINE_FLAG(MF8, E4M6BOSS, AActor, flags8),
	DEFINE_FLAG(MF8, MAP07BOSS1, AActor, flags8),
	DEFINE_FLAG(MF8, MAP07BOSS2, AActor, flags8),
	DEFINE_FLAG(MF8, AVOIDHAZARDS, AActor, flags8),
	DEFINE_FLAG(MF8, STAYONLIFT, AActor, flags8),
	DEFINE_FLAG(MF8, DONTFOLLOWPLAYERS, AActor, flags8),
	DEFINE_FLAG(MF8, SEEFRIENDLYMONSTERS, AActor, flags8),
	DEFINE_FLAG(MF8, CROSSLINECHECK, AActor, flags8),
	DEFINE_FLAG(MF8, MASTERNOSEE, AActor, flags8),
	DEFINE_FLAG(MF8, ADDLIGHTLEVEL, AActor, flags8),
	DEFINE_FLAG(MF8, ONLYSLAMSOLID, AActor, flags8),

	DEFINE_FLAG(MF9, SHADOWAIM, AActor, flags9),
	DEFINE_FLAG(MF9, DOSHADOWBLOCK, AActor, flags9),
	DEFINE_FLAG(MF9, SHADOWBLOCK, AActor, flags9),
	DEFINE_FLAG(MF9, SHADOWAIMVERT, AActor, flags9),
	DEFINE_FLAG(MF9, DECOUPLEDANIMATIONS, AActor, flags9),
	DEFINE_FLAG(MF9, BLOCKSIGHT, AActor, flags9),
	DEFINE_FLAG(MF9, DOBLOCKSIGHT, AActor, flags9),

	// Effect flags
	DEFINE_FLAG(FX, VISIBILITYPULSE, AActor, effects),
	DEFINE_FLAG2(FX_ROCKET, ROCKETTRAIL, AActor, effects),
	DEFINE_FLAG2(FX_GRENADE, GRENADETRAIL, AActor, effects),
	DEFINE_FLAG(RF, INVISIBLE, AActor, renderflags),
	DEFINE_FLAG(RF, FORCEYBILLBOARD, AActor, renderflags),
	DEFINE_FLAG(RF, FORCEXYBILLBOARD, AActor, renderflags),
	DEFINE_FLAG(RF, ROLLSPRITE, AActor, renderflags), // [marrub] roll the sprite billboard
	DEFINE_FLAG(RF, FLATSPRITE, AActor, renderflags), // [fgsfds] Flat sprites
	DEFINE_FLAG(RF, WALLSPRITE, AActor, renderflags),
	DEFINE_FLAG(RF, DONTFLIP, AActor, renderflags),
	DEFINE_FLAG(RF, ROLLCENTER, AActor, renderflags),
	DEFINE_FLAG(RF, MASKROTATION, AActor, renderflags),
	DEFINE_FLAG(RF, ABSMASKANGLE, AActor, renderflags),
	DEFINE_FLAG(RF, ABSMASKPITCH, AActor, renderflags),
	DEFINE_FLAG(RF, XFLIP, AActor, renderflags),
	DEFINE_FLAG(RF, YFLIP, AActor, renderflags),
	DEFINE_FLAG(RF, INTERPOLATEANGLES, AActor, renderflags),
	DEFINE_FLAG(RF, DONTINTERPOLATE, AActor, renderflags),
	DEFINE_FLAG(RF, SPRITEFLIP, AActor, renderflags),
	DEFINE_FLAG(RF, ZDOOMTRANS, AActor, renderflags),
	DEFINE_FLAG(RF, CASTSPRITESHADOW, AActor, renderflags),
	DEFINE_FLAG(RF, NOSPRITESHADOW, AActor, renderflags),
	DEFINE_FLAG(RF2, INVISIBLEINMIRRORS, AActor, renderflags2),
	DEFINE_FLAG(RF2, ONLYVISIBLEINMIRRORS, AActor, renderflags2),
	DEFINE_FLAG(RF2, BILLBOARDFACECAMERA, AActor, renderflags2),
	DEFINE_FLAG(RF2, BILLBOARDNOFACECAMERA, AActor, renderflags2),
	DEFINE_FLAG(RF2, FLIPSPRITEOFFSETX, AActor, renderflags2),
	DEFINE_FLAG(RF2, FLIPSPRITEOFFSETY, AActor, renderflags2),
	DEFINE_FLAG(RF2, CAMFOLLOWSPLAYER, AActor, renderflags2),
	DEFINE_FLAG(RF2, NOMIPMAP, AActor, renderflags2),
	DEFINE_FLAG(RF2, ISOMETRICSPRITES, AActor, renderflags2),
	DEFINE_FLAG(RF2, SQUAREPIXELS, AActor, renderflags2),

	// Bounce flags
	DEFINE_FLAG2(BOUNCE_Walls, BOUNCEONWALLS, AActor, BounceFlags),
	DEFINE_FLAG2(BOUNCE_Floors, BOUNCEONFLOORS, AActor, BounceFlags),
	DEFINE_FLAG2(BOUNCE_Ceilings, BOUNCEONCEILINGS, AActor, BounceFlags),
	DEFINE_FLAG2(BOUNCE_Actors, ALLOWBOUNCEONACTORS, AActor, BounceFlags),
	DEFINE_FLAG2(BOUNCE_AutoOff, BOUNCEAUTOOFF, AActor, BounceFlags),
	DEFINE_FLAG2(BOUNCE_HereticType, BOUNCELIKEHERETIC, AActor, BounceFlags),
	DEFINE_FLAG2(BOUNCE_CanBounceWater, CANBOUNCEWATER, AActor, BounceFlags),
	DEFINE_FLAG2(BOUNCE_NoWallSound, NOWALLBOUNCESND, AActor, BounceFlags),
	DEFINE_FLAG2(BOUNCE_Quiet, NOBOUNCESOUND, AActor, BounceFlags),
	DEFINE_FLAG2(BOUNCE_AllActors, BOUNCEONACTORS, AActor, BounceFlags),
	DEFINE_FLAG2(BOUNCE_ExplodeOnWater, EXPLODEONWATER, AActor, BounceFlags),
	DEFINE_FLAG2(BOUNCE_MBF, MBFBOUNCER, AActor, BounceFlags),
	DEFINE_FLAG2(BOUNCE_AutoOffFloorOnly, BOUNCEAUTOOFFFLOORONLY, AActor, BounceFlags),
	DEFINE_FLAG2(BOUNCE_UseBounceState, USEBOUNCESTATE, AActor, BounceFlags),
	DEFINE_FLAG2(BOUNCE_NotOnShootables, DONTBOUNCEONSHOOTABLES, AActor, BounceFlags),
	DEFINE_FLAG2(BOUNCE_BounceOnUnrips, BOUNCEONUNRIPPABLES, AActor, BounceFlags),
	DEFINE_FLAG2(BOUNCE_NotOnSky, DONTBOUNCEONSKY, AActor, BounceFlags),
	
	DEFINE_FLAG2(OF_Transient, NOSAVEGAME, AActor, ObjectFlags),
};

// These won't be accessible through bitfield variables
static FFlagDef MoreFlagDefs[] =
{

	// Deprecated flags. Handling must be performed in HandleDeprecatedFlags
	DEFINE_DEPRECATED_FLAG(FIREDAMAGE),
	DEFINE_DEPRECATED_FLAG(ICEDAMAGE),
	DEFINE_DEPRECATED_FLAG(LOWGRAVITY),
	DEFINE_DEPRECATED_FLAG(SHORTMISSILERANGE),
	DEFINE_DEPRECATED_FLAG(LONGMELEERANGE),
	DEFINE_DEPRECATED_FLAG(QUARTERGRAVITY),
	DEFINE_DEPRECATED_FLAG(FIRERESIST),
	DEFINE_DEPRECATED_FLAG(HERETICBOUNCE),
	DEFINE_DEPRECATED_FLAG(HEXENBOUNCE),
	DEFINE_DEPRECATED_FLAG(DOOMBOUNCE),
	DEFINE_DEPRECATED_FLAG(HIGHERMPROB),

	// Deprecated flags with no more existing functionality.
	DEFINE_DUMMY_FLAG(FASTER, true),				// obsolete, replaced by 'Fast' state flag
	DEFINE_DUMMY_FLAG(FASTMELEE, true),			// obsolete, replaced by 'Fast' state flag

	// Deprecated name as an alias
	DEFINE_FLAG2_DEPRECATED(MF4_DONTHARMCLASS, DONTHURTSPECIES, AActor, flags4),

	// Various Skulltag flags that are quite irrelevant to ZDoom
	// [BC] New DECORATE flag defines here.
	DEFINE_DUMMY_FLAG(BLUETEAM, false),
	DEFINE_DUMMY_FLAG(REDTEAM, false),
	DEFINE_DUMMY_FLAG(USESPECIAL, false),
	DEFINE_DUMMY_FLAG(BASEHEALTH, false),
	DEFINE_DUMMY_FLAG(SUPERHEALTH, false),
	DEFINE_DUMMY_FLAG(BASEARMOR, false),
	DEFINE_DUMMY_FLAG(SUPERARMOR, false),
	DEFINE_DUMMY_FLAG(SCOREPILLAR, false),
	DEFINE_DUMMY_FLAG(NODE, false),
	DEFINE_DUMMY_FLAG(USESTBOUNCESOUND, false),
	DEFINE_DUMMY_FLAG(EXPLODEONDEATH, true),
	DEFINE_DUMMY_FLAG(DONTIDENTIFYTARGET, false), // [CK]

	// Skulltag netcode-based flags.
	// [BB] New DECORATE network related flag defines here.
	DEFINE_DUMMY_FLAG(NONETID, false),
	DEFINE_DUMMY_FLAG(ALLOWCLIENTSPAWN, false),
	DEFINE_DUMMY_FLAG(CLIENTSIDEONLY, false),
	DEFINE_DUMMY_FLAG(SERVERSIDEONLY, false),
};

static const struct FFlagList { const PClass * const *Type; FFlagDef *Defs; int NumDefs; int Use; } FlagLists[] =
{
	{ &RUNTIME_CLASS_CASTLESS(AActor), 		ActorFlagDefs,		countof(ActorFlagDefs), 3 },	// -1 to account for the terminator
	{ &RUNTIME_CLASS_CASTLESS(AActor), 		MoreFlagDefs,		countof(MoreFlagDefs), 1 },
	{ &RUNTIME_CLASS_CASTLESS(AActor), 	InternalActorFlagDefs,	countof(InternalActorFlagDefs), 2 },
};
#define NUM_FLAG_LISTS (countof(FlagLists))

static FFlagDef forInternalFlags;

//==========================================================================
//
// Find a flag by name using a binary search
//
//==========================================================================
static FFlagDef *FindFlag (FFlagDef *flags, int numflags, const char *flag)
{
	int min = 0, max = numflags - 1;

	while (min <= max)
	{
		int mid = (min + max) / 2;
		int lexval = stricmp (flag, flags[mid].name);
		if (lexval == 0)
		{
			return &flags[mid];
		}
		else if (lexval > 0)
		{
			min = mid + 1;
		}
		else
		{
			max = mid - 1;
		}
	}
	return NULL;
}

//==========================================================================
//
// Finds a flag that may have a qualified name
//
//==========================================================================

FFlagDef *FindFlag (const PClass *type, const char *part1, const char *part2, bool strict)
{

	if (part2 == nullptr)
	{
		FStringf internalname("@flagdef@%s", part1);
		FName name(internalname, true);
		if (name != NAME_None)
		{
			auto field = dyn_cast<PPropFlag>(type->FindSymbol(name, true));
			if (field != nullptr && (!strict || !field->decorateOnly))
			{
				forInternalFlags.fieldsize = 4;
				forInternalFlags.name = "";
				forInternalFlags.flagbit = field->Offset? 1 << field->bitval : field->bitval;
				forInternalFlags.structoffset = field->Offset? (int)field->Offset->Offset : -1;
				forInternalFlags.varflags = field->Offset == nullptr && field->bitval > 0? VARF_Deprecated : 0;
				return &forInternalFlags;
			}
		}
	}
	else
	{
		FStringf internalname("@flagdef@%s.%s", part1, part2);
		FName name(internalname, true);
		if (name != NAME_None)
		{
			auto field = dyn_cast<PPropFlag>(type->FindSymbol(name, true));
			if (field != nullptr)
			{
				forInternalFlags.fieldsize = 4;
				forInternalFlags.name = "";
				forInternalFlags.flagbit = field->Offset ? 1 << field->bitval : field->bitval;
				forInternalFlags.structoffset = field->Offset ? (int)field->Offset->Offset : -1;
				forInternalFlags.varflags = field->Offset == nullptr && field->bitval > 0? VARF_Deprecated : 0;
				return &forInternalFlags;
			}
		}
	}

	// Not found. Try the internal flag definitions.


	FFlagDef *def;

	if (part2 == NULL)
	{ // Search all lists
		int max = strict ? 2 : NUM_FLAG_LISTS;
		for (int i = 0; i < max; ++i)
		{
			if ((FlagLists[i].Use & 1) && type->IsDescendantOf (*FlagLists[i].Type))
			{
				def = FindFlag (FlagLists[i].Defs, FlagLists[i].NumDefs, part1);
				if (def != NULL)
				{
					return def;
				}
			}
		}
	}
	else
	{ // Search just the named list
		for (size_t i = 0; i < NUM_FLAG_LISTS; ++i)
		{
			if (stricmp ((*FlagLists[i].Type)->TypeName.GetChars(), part1) == 0)
			{
				if (type->IsDescendantOf (*FlagLists[i].Type))
				{
					return FindFlag (FlagLists[i].Defs, FlagLists[i].NumDefs, part2);
				}
				else
				{
					return NULL;
				}
			}
		}
	}

	return NULL;
}


//==========================================================================
//
// Gets the name of an actor flag
//
//==========================================================================

const char *GetFlagName(unsigned int flagnum, int flagoffset)
{
	for(size_t i = 0; i < countof(ActorFlagDefs); i++)
	{
		if (ActorFlagDefs[i].flagbit == flagnum && ActorFlagDefs[i].structoffset == flagoffset)
		{
			return ActorFlagDefs[i].name;
		}
	}
	return "(unknown)";	// return something printable
}

//==========================================================================
//
// Find a property by name using a binary search
//
//==========================================================================

FPropertyInfo *FindProperty(const char * string)
{
	int min = 0, max = properties.Size()-1;

	while (min <= max)
	{
		int mid = (min + max) / 2;
		int lexval = stricmp (string, properties[mid]->name);
		if (lexval == 0)
		{
			return properties[mid];
		}
		else if (lexval > 0)
		{
			min = mid + 1;
		}
		else
		{
			max = mid - 1;
		}
	}
	return NULL;
}

//==========================================================================
//
// Sorting helpers
//
//==========================================================================

static int flagcmp (const void * a, const void * b)
{
	return stricmp( ((FFlagDef*)a)->name, ((FFlagDef*)b)->name);
}

static int propcmp(const void * a, const void * b)
{
	return stricmp( (*(FPropertyInfo**)a)->name, (*(FPropertyInfo**)b)->name);
}

//==========================================================================
//
// Initialization
//
//==========================================================================
void InitImports();

struct UserInfoCVarNamePlayer
{
	FBaseCVar** addr;
	FString name;
	int pnum;
};

TArray<UserInfoCVarNamePlayer> LoadGameUserInfoCVars;

void InitThingdef()
{
	// Some native types need size and serialization information added before the scripts get compiled.
	auto secplanestruct = NewStruct("Secplane", nullptr, true);
	secplanestruct->Size = sizeof(secplane_t);
	secplanestruct->Align = alignof(secplane_t);

	auto sectorstruct = NewStruct("Sector", nullptr, true);
	sectorstruct->Size = sizeof(sector_t);
	sectorstruct->Align = alignof(sector_t);
	NewPointer(sectorstruct, false)->InstallHandlers(
		[](FSerializer &ar, const char *key, const void *addr)
		{
			ar(key, *(sector_t **)addr);
		},
		[](FSerializer &ar, const char *key, void *addr)
		{
			Serialize<sector_t>(ar, key, *(sector_t **)addr, nullptr);
			return true;
		}
	);

	auto linestruct = NewStruct("Line", nullptr, true);
	linestruct->Size = sizeof(line_t);
	linestruct->Align = alignof(line_t);
	NewPointer(linestruct, false)->InstallHandlers(
		[](FSerializer &ar, const char *key, const void *addr)
		{
			ar(key, *(line_t **)addr);
		},
		[](FSerializer &ar, const char *key, void *addr)
		{
			Serialize<line_t>(ar, key, *(line_t **)addr, nullptr);
			return true;
		}
	);

	auto sidestruct = NewStruct("Side", nullptr, true);
	sidestruct->Size = sizeof(side_t);
	sidestruct->Align = alignof(side_t);
	NewPointer(sidestruct, false)->InstallHandlers(
		[](FSerializer &ar, const char *key, const void *addr)
		{
			ar(key, *(side_t **)addr);
		},
			[](FSerializer &ar, const char *key, void *addr)
		{
			Serialize<side_t>(ar, key, *(side_t **)addr, nullptr);
			return true;
		}
	);

	auto vertstruct = NewStruct("Vertex", nullptr, true);
	vertstruct->Size = sizeof(vertex_t);
	vertstruct->Align = alignof(vertex_t);
	NewPointer(vertstruct, false)->InstallHandlers(
		[](FSerializer &ar, const char *key, const void *addr)
		{
			ar(key, *(vertex_t **)addr);
		},
		[](FSerializer &ar, const char *key, void *addr)
		{
			Serialize<vertex_t>(ar, key, *(vertex_t **)addr, nullptr);
			return true;
		}
	);

	auto sectorportalstruct = NewStruct("SectorPortal", nullptr, true);
	sectorportalstruct->Size = sizeof(FSectorPortal);
	sectorportalstruct->Align = alignof(FSectorPortal);

	auto lineportalstruct = NewStruct("LinePortal", nullptr, true);
	lineportalstruct->Size = sizeof(FLinePortal);
	lineportalstruct->Align = alignof(FLinePortal);

	auto playerclassstruct = NewStruct("PlayerClass", nullptr, true);
	playerclassstruct->Size = sizeof(FPlayerClass);
	playerclassstruct->Align = alignof(FPlayerClass);

	auto playerskinstruct = NewStruct("PlayerSkin", nullptr, true);
	playerskinstruct->Size = sizeof(FPlayerSkin);
	playerskinstruct->Align = alignof(FPlayerSkin);

	auto teamstruct = NewStruct("Team", nullptr, true);
	teamstruct->Size = sizeof(FTeam);
	teamstruct->Align = alignof(FTeam);

	auto terraindefstruct = NewStruct("TerrainDef", nullptr, true);
	terraindefstruct->Size = sizeof(FTerrainDef);
	terraindefstruct->Align = alignof(FTerrainDef);

	PStruct *pstruct = NewStruct("PlayerInfo", nullptr, true);
	pstruct->Size = sizeof(player_t);
	pstruct->Align = alignof(player_t);
	NewPointer(pstruct, false)->InstallHandlers(
		[](FSerializer &ar, const char *key, const void *addr)
		{
			ar(key, *(player_t **)addr);
		},
			[](FSerializer &ar, const char *key, void *addr)
		{
			Serialize<player_t>(ar, key, *(player_t **)addr, nullptr);
			return true;
		}
	);

	auto wbplayerstruct = NewStruct("WBPlayerStruct", nullptr, true);
	wbplayerstruct->Size = sizeof(wbplayerstruct_t);
	wbplayerstruct->Align = alignof(wbplayerstruct_t);

	AutoSegs::TypeInfos.ForEach([](ClassReg* typeInfo)
	{
		if (typeInfo->InitNatives)
			typeInfo->InitNatives();
	});
	
	// Sort the flag lists
	for (size_t i = 0; i < NUM_FLAG_LISTS; ++i)
	{
		qsort (FlagLists[i].Defs, FlagLists[i].NumDefs, sizeof(FFlagDef), flagcmp);
	}

	// Create a sorted list of properties
	if (properties.Size() == 0)
	{
		AutoSegs::Properties.ForEach([](FPropertyInfo* propertyInfo)
		{
			properties.Push(propertyInfo);
		});

		properties.ShrinkToFit();
		qsort(&properties[0], properties.Size(), sizeof(properties[0]), propcmp);
	}

	InitImports();

	// Add the constructor and destructor to FCheckPosition.
	auto fcp = NewStruct("FCheckPosition", nullptr);
	fcp->mConstructor = *FindFunction(fcp, "_Constructor")->VMPointer;
	fcp->mDestructor = *FindFunction(fcp, "_Destructor")->VMPointer;
	static const uint8_t reguse[] = { REGT_POINTER };
	fcp->mConstructor->RegTypes = fcp->mDestructor->RegTypes = reguse;
	fcp->Size = sizeof(FCheckPosition);
	fcp->Align = alignof(FCheckPosition);

	//This must also have its size set.
	auto frp = NewStruct("FRailParams", nullptr);
	frp->Size = sizeof(FRailParams);
	frp->Align = alignof(FRailParams);

	auto netcmdstruct = NewStruct("NetworkCommand", nullptr, true);
	netcmdstruct->Size = sizeof(FNetworkCommand);
	netcmdstruct->Align = alignof(FNetworkCommand);

	auto fltd = NewStruct("FLineTraceData", nullptr);
	fltd->Size = sizeof(FLineTraceData);
	fltd->Align = alignof(FLineTraceData);

	auto fspp = NewStruct("FSpawnParticleParams", nullptr);
	fspp->Size = sizeof(FSpawnParticleParams);
	fspp->Align = alignof(FSpawnParticleParams);

	auto cvst = NewStruct("CVar", nullptr, true);
	NewPointer(cvst, false)->InstallHandlers(
		[](FSerializer &arc, const char *key, const void *addr)
		{
			const FBaseCVar * self = *(const FBaseCVar**)addr;
			
			if(self)
			{
				arc.BeginObject(key);


				if(self->pnum != -1)
				{
					int32_t pnum = self->pnum;
					FName name = self->userinfoName;
					arc("name", name);
					arc("player", pnum);
				}
				else
				{
					FString name = self->GetName();
					arc("name", name);
				}

				arc.EndObject();
			}
		},
		[](FSerializer &arc, const char *key, void *addr)
		{
			FBaseCVar ** self = (FBaseCVar**)addr;

			FString name;
			arc.BeginObject(key);

			arc("name", name);

			FBaseCVar * backing = FindCVar(name.GetChars(), nullptr);
			if(!backing)
			{
				I_Error("Attempt to load pointer to inexisted CVar '%s'", name.GetChars());
			}
			else if((backing->GetFlags()  & (CVAR_USERINFO|CVAR_IGNORE)) == CVAR_USERINFO)
			{
				if(int pnum; arc.ReadOptionalInt("player", pnum))
				{
					*self = nullptr;
					LoadGameUserInfoCVars.Push({self, name, pnum}); // this needs to be done later, since userinfo isn't loaded yet
					arc.EndObject();
					return true;
				}
			}
			
			*self = backing;

			arc.EndObject();

			return true;
		}
	);
}

void SetupLoadingCVars()
{
	LoadGameUserInfoCVars.Clear();
}

void FinishLoadingCVars()
{
	for(UserInfoCVarNamePlayer &cvar : LoadGameUserInfoCVars)
	{
		(*cvar.addr) = GetCVar(cvar.pnum, cvar.name.GetChars());
	}
}

void SynthesizeFlagFields()
{
	// synthesize a symbol for each flag from the flag name tables to avoid redundant declaration of them.
	for (auto &fl : FlagLists)
	{
		auto cls = const_cast<PClass*>(*fl.Type);
		if (fl.Use & 2)
		{
			for (int i = 0; i < fl.NumDefs; i++)
			{
				if (fl.Defs[i].structoffset > 0) // skip the deprecated entries in this list
				{
					cls->VMType->AddNativeField(FStringf("b%s", fl.Defs[i].name), (fl.Defs[i].fieldsize == 4 ? TypeSInt32 : TypeSInt16), fl.Defs[i].structoffset, fl.Defs[i].varflags, fl.Defs[i].flagbit);
				}
			}
		}
	}
}

DEFINE_ACTION_FUNCTION(DObject, BAM)
{
	PARAM_PROLOGUE;
	PARAM_ANGLE(ang);
	ACTION_RETURN_INT(ang.BAMs());
}

