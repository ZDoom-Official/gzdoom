//-----------------------------------------------------------------------------
//
// Copyright 1993-1996 id Software
// Copyright 1994-1996 Raven Software
// Copyright 1998-1998 Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
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
//		Enemy thinking, AI.
//		Action Pointer Functions
//		that are associated with states/frames. 
//
//-----------------------------------------------------------------------------


#include <stdlib.h>


#include "m_random.h"

#include "doomdef.h"
#include "p_local.h"
#include "p_maputl.h"
#include "d_player.h"
#include "p_lnspec.h"
#include "g_game.h"
#include "p_enemy.h"
#include "a_sharedglobal.h"
#include "p_blockmap.h"
#include "teaminfo.h"
#include "p_spec.h"
#include "p_checkposition.h"
#include "g_levellocals.h"
#include "vm.h"
#include "actorinlines.h"
#include "a_ceiling.h"
#include "shadowinlines.h"

#include "gi.h"

static FRandom pr_checkmissilerange ("CheckMissileRange");
static FRandom pr_opendoor ("OpenDoor");
static FRandom pr_trywalk ("TryWalk");
static FRandom pr_newchasedir ("NewChaseDir");
static FRandom pr_lookformonsters ("LookForMonsters");
static FRandom pr_scaredycat ("Anubis");
	   FRandom pr_chase ("Chase");
	   FRandom pr_facetarget ("FaceTarget");
	   FRandom pr_railface ("RailFace");
static FRandom pr_look2 ("LookyLooky");
static FRandom pr_look3 ("IGotHooky");
static FRandom pr_slook ("SlooK");
static FRandom pr_dropoff ("Dropoff");
static FRandom pr_defect ("Defect");
static FRandom pr_avoidcrush("AvoidCrush");
static FRandom pr_stayonlift("StayOnLift");

static FRandom pr_skiptarget("SkipTarget");
static FRandom pr_enemystrafe("EnemyStrafe");

// movement interpolation is fine for objects that are moved by their own
// velocity. But for monsters it is problematic.
// 1. They don't move every tic
// 2. Their animation is not designed for movement interpolation
// The result is that they tend to 'glide' across the floor
// so this CVAR allows to switch it off.
CVAR(Bool, nomonsterinterpolation, false, CVAR_GLOBALCONFIG|CVAR_ARCHIVE)
CVAR(Int, sv_dropstyle, 0, CVAR_SERVERINFO | CVAR_ARCHIVE);

//
// P_NewChaseDir related LUT.
//

dirtype_t opposite[9] =
{
	DI_WEST, DI_SOUTHWEST, DI_SOUTH, DI_SOUTHEAST,
	DI_EAST, DI_NORTHEAST, DI_NORTH, DI_NORTHWEST, DI_NODIR
};

dirtype_t diags[4] =
{
	DI_NORTHWEST, DI_NORTHEAST, DI_SOUTHWEST, DI_SOUTHEAST
};

#define SQRTHALF 0.7071075439453125
double xspeed[8] = {1,SQRTHALF,0,-SQRTHALF,-1,-SQRTHALF,0,SQRTHALF};
double yspeed[8] = {0,SQRTHALF,1,SQRTHALF,0,-SQRTHALF,-1,-SQRTHALF};

//
// ENEMY THINKING
// Enemies are always spawned
// with targetplayer = -1, threshold = 0
// Most monsters are spawned unaware of all players,
// but some can be made preaware
//


//----------------------------------------------------------------------------
//
// PROC P_RecursiveSound
//
// Called by P_NoiseAlert.
// Traverses adjacent sectors,
// sound blocking lines cut off traversal.
//----------------------------------------------------------------------------

struct NoiseTarget
{
	sector_t *sec;
	int soundblocks;
};
static TArray<NoiseTarget> NoiseList(128);

static void NoiseMarkSector(sector_t *sec, AActor *soundtarget, bool splash, AActor *emitter, int soundblocks, double maxdist)
{
	// wake up all monsters in this sector
	if (sec->validcount == validcount
		&& sec->soundtraversed <= soundblocks + 1)
	{
		return; 		// already flooded
	}

	sec->validcount = validcount;
	sec->soundtraversed = soundblocks + 1;
	sec->SoundTarget = soundtarget;

	// [RH] Set this in the actors in the sector instead of the sector itself.
	for (AActor *actor = sec->thinglist; actor != NULL; actor = actor->snext)
	{
		if (actor != soundtarget && (!splash || !(actor->flags4 & MF4_NOSPLASHALERT)) &&
			(!maxdist || (actor->Distance2D(emitter) <= maxdist)))
		{
			actor->LastHeard = soundtarget;
		}
	}
	NoiseList.Push({ sec, soundblocks });
}


static void P_RecursiveSound(sector_t *sec, AActor *soundtarget, bool splash, AActor *emitter, int soundblocks, double maxdist)
{
	bool checkabove = !sec->PortalBlocksSound(sector_t::ceiling);
	bool checkbelow = !sec->PortalBlocksSound(sector_t::floor);

	for (auto check : sec->Lines)
	{
		// check sector portals
		// I wish there was a better method to do this than randomly looking through the portal at a few places...
		if (checkabove)
		{
			sector_t *upper = sec->Level->PointInSector(check->v1->fPos() + check->Delta() / 2 + sec->GetPortalDisplacement(sector_t::ceiling));
			NoiseMarkSector(upper, soundtarget, splash, emitter, soundblocks, maxdist);
		}
		if (checkbelow)
		{
			sector_t *lower = sec->Level->PointInSector(check->v1->fPos() + check->Delta() / 2 + sec->GetPortalDisplacement(sector_t::floor));
			NoiseMarkSector(lower, soundtarget, splash, emitter, soundblocks, maxdist);
		}

		// ... and line portals;
		FLinePortal *port = check->getPortal();
		if (port && (port->mFlags & PORTF_SOUNDTRAVERSE))
		{
			if (port->mDestination)
			{
				NoiseMarkSector(port->mDestination->frontsector, soundtarget, splash, emitter, soundblocks, maxdist);
			}
		}


		if (check->sidedef[1] == NULL ||
			!(check->flags & ML_TWOSIDED))
		{
			continue;
		}

		// Early out for intra-sector lines
		if (check->sidedef[0]->sector == check->sidedef[1]->sector) continue;

		sector_t *other;
		if (check->sidedef[0]->sector == sec)
			other = check->sidedef[1]->sector;
		else
			other = check->sidedef[0]->sector;

		// check for closed door
		if ((sec->floorplane.ZatPoint(check->v1->fPos()) >=
			other->ceilingplane.ZatPoint(check->v1->fPos()) &&
			sec->floorplane.ZatPoint(check->v2->fPos()) >=
			other->ceilingplane.ZatPoint(check->v2->fPos()))
			|| (other->floorplane.ZatPoint(check->v1->fPos()) >=
				sec->ceilingplane.ZatPoint(check->v1->fPos()) &&
				other->floorplane.ZatPoint(check->v2->fPos()) >=
				sec->ceilingplane.ZatPoint(check->v2->fPos()))
			|| (other->floorplane.ZatPoint(check->v1->fPos()) >=
				other->ceilingplane.ZatPoint(check->v1->fPos()) &&
				other->floorplane.ZatPoint(check->v2->fPos()) >=
				other->ceilingplane.ZatPoint(check->v2->fPos())))
		{
			continue;
		}

		if (check->flags & ML_SOUNDBLOCK)
		{
			if (!soundblocks)
				NoiseMarkSector(other, soundtarget, splash, emitter, 1, maxdist);
		}
		else
		{
			NoiseMarkSector(other, soundtarget, splash, emitter, soundblocks, maxdist);
		}
	}
}




//----------------------------------------------------------------------------
//
// PROC P_NoiseAlert
//
// If a monster yells at a player, it will alert other monsters to the
// player.
//
//----------------------------------------------------------------------------

void P_NoiseAlert (AActor *emitter, AActor *target, bool splash, double maxdist)
{
	if (emitter == NULL)
		return;

	if (target != NULL && target->player && (target->player->cheats & CF_NOTARGET))
		return;

	validcount++;
	NoiseList.Clear();
	NoiseMarkSector(emitter->Sector, target, splash, emitter, 0, maxdist);
	for (unsigned i = 0; i < NoiseList.Size(); i++)
	{
		P_RecursiveSound(NoiseList[i].sec, target, splash, emitter, NoiseList[i].soundblocks, maxdist);
	}
}

//----------------------------------------------------------------------------
//
// AActor :: CheckMeleeRange
//
//----------------------------------------------------------------------------

int P_CheckMeleeRange (AActor* actor, double range)
{
	AActor *pl = actor->target;

	double dist;
	
	if (!pl)
		return false;

	dist = actor->Distance2D (pl);
	if (range < 0) range = actor->meleerange;

	if (dist >= range + pl->radius)
		return false;

	// [RH] If moving toward goal, then we've reached it.
	if (pl == actor->goal)
		return true;

	if (actor->Sector->Flags & SECF_NOATTACK)
		return false;

	// [RH] Don't melee things too far above or below actor.
	if (!(actor->flags5 & MF5_NOVERTICALMELEERANGE))
	{
		if (pl->Z() > actor->Top())
			return false;
		if (pl->Top() < actor->Z())
			return false;
	}

	// killough 7/18/98: friendly monsters don't attack other friends
	if (actor->IsFriend(pl))
		return false;
		
	if (!P_CheckSight (actor, pl, 0))
		return false;
														
	return true;				
}

DEFINE_ACTION_FUNCTION_NATIVE(AActor, CheckMeleeRange, P_CheckMeleeRange)
{
	PARAM_SELF_PROLOGUE(AActor);
	PARAM_FLOAT(range);
	ACTION_RETURN_INT(P_CheckMeleeRange(self, range));
}

//=============================================================================
//
// P_CheckMissileRange
//
//=============================================================================

static int P_CheckMissileRange (AActor *actor)
{
	double dist;
		
	if ((actor->Sector->Flags & SECF_NOATTACK)) return false;

	if (!P_CheckSight (actor, actor->target, SF_SEEPASTBLOCKEVERYTHING))
		return false;
		
	if (actor->flags & MF_JUSTHIT)
	{
		// the target just hit the enemy, so fight back!
		actor->flags &= ~MF_JUSTHIT;

		// killough 7/18/98: no friendly fire at corpses
		// killough 11/98: prevent too much infighting among friends
		// Cleaned up and made readable
		if (!(actor->flags & MF_FRIENDLY)) return true;
		if (actor->target->health <= 0) return false;
		if (!actor->IsFriend(actor->target)) return true;
		if (actor->target->player != NULL)
		{
			return (pr_defect() >128);
		}
		else
		{
			return !(actor->target->flags & MF_JUSTHIT) && pr_defect() >128;
		}
	}
		
	if (actor->reactiontime)
		return false;	// do not attack yet

	// killough 7/18/98: friendly monsters don't attack other friendly
	// monsters or players (except when attacked, and then only once)
	if (actor->IsFriend(actor->target))
		return false;
			
	if (actor->flags & MF_FRIENDLY && P_HitFriend(actor))
		return false;

	// OPTIMIZE: get this from a global checksight
	// [RH] What?
	dist = actor->Distance2D (actor->target) - 64;
	
	if (actor->MeleeState == NULL)
		dist -= 128;	// no melee attack, so fire more


	if (actor->maxtargetrange > 0 && dist > actor->maxtargetrange)
		return false;	// The Arch Vile's special behavior turned into a property

	if (actor->MeleeState != nullptr && dist < actor->meleethreshold)
		return false;	// From the Revenant: close enough for fist attack

	dist *= actor->missilechancemult;

	int mmc = int(actor->MinMissileChance * G_SkillProperty(SKILLP_Aggressiveness));
	return pr_checkmissilerange() >= min(int(dist), mmc);
}

DEFINE_ACTION_FUNCTION_NATIVE(AActor, CheckMissileRange, P_CheckMissileRange)
{
	PARAM_SELF_PROLOGUE(AActor);
	ACTION_RETURN_BOOL(P_CheckMissileRange(self));
}

//=============================================================================
//
// P_HitFriend()
//
// killough 12/98
// This function tries to prevent shooting at friends that get in the line of fire
//
// [GrafZahl] Taken from MBF but this has been cleaned up to make it readable.
//
//=============================================================================

int P_HitFriend(AActor * self)
{
	FTranslatedLineTarget t;

	if (self->flags&MF_FRIENDLY && self->target != NULL)
	{
		DAngle angle = self->AngleTo(self->target);
		double dist = self->Distance2D(self->target);
		P_AimLineAttack (self, angle, dist, &t, nullAngle, true);
		if (t.linetarget != NULL && t.linetarget != self->target)
		{
			return self->IsFriend (t.linetarget);
		}
	}
	return false;
}

/*
 * P_IsOnLift
 *
 * killough 9/9/98:
 *
 * Returns true if the object is on a lift. Used for AI,
 * since it may indicate the need for crowded conditions,
 * or that a monster should stay on the lift for a while
 * while it goes up or down.
 */

static bool P_IsOnLift(const AActor* actor)
{
	sector_t* sec = actor->Sector;

	// Short-circuit: it's on a lift which is active.
	DSectorEffect* e = sec->floordata;
	if (e && e->IsKindOf(RUNTIME_CLASS(DPlat)))
		return true;

	// Check to see if it's in a sector which can be activated as a lift.
	// This is a bit more restrictive than MBF as it only considers repeatable lifts moving from A->B->A and stop.
	// Other types of movement are not easy to detect with the more complex map setup 
	// and also do not really make sense in this context unless they are actually active
	return !!(sec->MoreFlags & SECMF_LIFT);
}

/*
 * P_IsUnderDamage
 *
 * killough 9/9/98:
 *
 * Returns nonzero if the object is under damage based on
 * their current position. Returns 1 if the damage is moderate,
 * -1 if it is serious. Used for AI.
 */

static int P_IsUnderDamage(AActor* actor)
{
	msecnode_t* seclist;
	int dir = 0;
	for (seclist = actor->touching_sectorlist; seclist; seclist = seclist->m_tnext)
	{
		DSectorEffect* e = seclist->m_sector->ceilingdata;
		if (e && e->IsKindOf(RUNTIME_CLASS(DCeiling)))
		{
			auto cl = (DCeiling*)e;
			if (cl->getCrush() > 0) // unlike MBF we need to consider non-crushing ceiling movers here.
				dir |= cl->getDirection();
		}
		// Q: consider crushing 3D floors too?
		// [inkoalawetrust] Check for sectors that can harm the actor.
		if (!(actor->flags9 & MF9_NOSECTORDAMAGE) && seclist->m_sector->damageamount > 0)
		{
			if (seclist->m_sector->MoreFlags & SECMF_HARMINAIR || actor->isAtZ(seclist->m_sector->LowestFloorAt(actor)) || actor->waterlevel)
				return (actor->player || (actor->player == nullptr && seclist->m_sector->MoreFlags & SECMF_HURTMONSTERS))  ? -1 : 0;
		}
	}
	return dir;
}

//
// P_CheckTags
// Checks if 2 sectors share the same primary activation tag
//

bool P_CheckTags(sector_t* sec1, sector_t* sec2)
{
	auto Level = sec1->Level;
	if (!Level->SectorHasTags(sec1) || !Level->SectorHasTags(sec2)) return sec1 == sec2;
	if (Level->GetFirstSectorTag(sec1) == Level->GetFirstSectorTag(sec2)) return true;
	// todo: check secondary tags as well.
	return false;
}

//
// P_Move
// Move in the current direction,
// returns false if the move is blocked.
//

static int P_Move (AActor *actor)
{

	double tryx, tryy, deltax, deltay, origx, origy;
	bool try_ok;
	double speed = actor->Speed;
	double movefactor = ORIG_FRICTION_FACTOR;
	double friction = ORIG_FRICTION;
	int dropoff = 0;

	if (actor->flags2 & MF2_BLASTED)
	{
		return true;
	}

	if (actor->movedir >= DI_NODIR)
	{
		actor->movedir = DI_NODIR;	// make sure it's valid.
		return false;
	}

	// [RH] Walking actors that are not on the ground cannot walk. We don't
	// want to yank them to the ground here as Doom did, since that makes
	// it difficult to thrust them vertically in a reasonable manner.
	// [GZ] Let jumping actors jump.
	if (!((actor->flags & MF_NOGRAVITY) || CanJump(actor))
		&& actor->Z() > actor->floorz && !(actor->flags2 & MF2_ONMOBJ))
	{
		return false;
	}

	// killough 10/98: allow dogs to drop off of taller ledges sometimes.
	// dropoff==1 means always allow it, dropoff==2 means only up to 128 high,
	// and only if the target is immediately on the other side of the line.
	AActor *target = actor->target;

	if ((actor->flags6 & MF6_JUMPDOWN) && target && !(actor->Level->flags3 & LEVEL3_NOJUMPDOWN) &&
			!(target->IsFriend(actor)) &&
			actor->Distance2D(target) < 144 &&
			pr_dropoff() < 235)
	{
		dropoff = 2;
	}

	// [RH] I'm not so sure this is such a good idea
	// [GZ] That's why it's compat-optioned.
	if (actor->Level->i_compatflags & COMPATF_MBFMONSTERMOVE && !(actor->flags8 & MF8_NOFRICTION))
	{
		// killough 10/98: make monsters get affected by ice and sludge too:
		movefactor = P_GetMoveFactor (actor, &friction);

		if (friction < ORIG_FRICTION)
		{ // sludge
			speed = speed * ((ORIG_FRICTION_FACTOR - (ORIG_FRICTION_FACTOR-movefactor)/2)) / ORIG_FRICTION_FACTOR;
			if (speed == 0)
			{ // always give the monster a little bit of speed
				speed = actor->Speed;
			}
		}
	}

	tryx = (origx = actor->X()) + (deltax = (speed * xspeed[actor->movedir]));
	tryy = (origy = actor->Y()) + (deltay = (speed * yspeed[actor->movedir]));

	// Like P_XYMovement this should do multiple moves if the step size is too large

	double maxmove = actor->radius - 1;
	int steps = 1;

	if (maxmove > 0)
	{ 
		const double xspeed = fabs (deltax);
		const double yspeed = fabs (deltay);

		if (xspeed > yspeed)
		{
			if (xspeed > maxmove)
			{
				steps = 1 + int(xspeed / maxmove);
			}
		}
		else
		{
			if (yspeed > maxmove)
			{
				steps = 1 + int(yspeed / maxmove);
			}
		}
	}
	FCheckPosition tm;

	tm.FromPMove = true;

	DVector2 start = { origx, origy };
	DVector2 move = { deltax, deltay };
	DAngle oldangle = actor->Angles.Yaw;

	try_ok = true;
	for (int i = 1; i <= steps; i++)
	{
		DVector2 ptry = start + move * i / steps;
		// killough 3/15/98: don't jump over dropoffs:
		try_ok = P_TryMove(actor, ptry, dropoff, NULL, tm);
		if (!try_ok) break;

		// Handle portal transitions just like P_XYMovement.
		if (steps > 1 && actor->Pos().XY() != ptry)
		{
			DAngle anglediff = deltaangle(oldangle, actor->Angles.Yaw);

			if (anglediff != nullAngle)
			{
				move = move.Rotated(anglediff);
				oldangle = actor->Angles.Yaw;
			}
			start = actor->Pos().XY() - move * i / steps;
		}
	}

	// [GrafZahl] Interpolating monster movement as it is done here just looks bad
	// so make it switchable
	if (nomonsterinterpolation)
	{
		actor->ClearInterpolation();
	}

	if (try_ok && friction > ORIG_FRICTION)
	{
		actor->SetOrigin(origx, origy, actor->Z(), false);
		movefactor *= 1.f / ORIG_FRICTION_FACTOR / 4;
		actor->Vel.X += deltax * movefactor;
		actor->Vel.Y += deltay * movefactor;
	}

	// [RH] If a walking monster is no longer on the floor, move it down
	// to the floor if it is within MaxStepHeight, presuming that it is
	// actually walking down a step.
	if (try_ok &&
		!((actor->flags & MF_NOGRAVITY) || CanJump(actor))
			&& !(actor->flags2 & MF2_ONMOBJ))

	{
		// account for imprecisions with slopes. A walking actor should never be below its own floorz.
		if (actor->Z() < actor->floorz)
			actor->SetZ(actor->floorz);

		else if (actor->Z() <= actor->floorz + actor->MaxStepHeight)
		{
			double savedz = actor->Z();
			actor->SetZ(actor->floorz);
			// Make sure that there isn't some other actor between us and
			// the floor we could get stuck in. The old code did not do this.
			if (!P_TestMobjZ(actor))
			{
				actor->SetZ(savedz);
			}
			else
			{ // The monster just hit the floor, so trigger any actions.
				if (actor->floorsector->SecActTarget != NULL &&
					actor->floorz == actor->floorsector->floorplane.ZatPoint(actor->PosRelative(actor->floorsector)))
				{
					actor->floorsector->TriggerSectorActions(actor, SECSPAC_HitFloor);
				}
				P_CheckFor3DFloorHit(actor, actor->Z(), true);
			}
		}
	}

	if (!try_ok)
	{
		if ((CanJump(actor) || (actor->flags & MF_FLOAT)) && tm.floatok)
		{ // must adjust height
			double savedz = actor->Z();

			if (actor->Z() < tm.floorz)
				actor->AddZ(actor->FloatSpeed);
			else
				actor->AddZ(-actor->FloatSpeed);


			// [RH] Check to make sure there's nothing in the way of the float
			if (P_TestMobjZ(actor))
			{
				actor->flags |= MF_INFLOAT;
				return true;
			}
			actor->SetZ(savedz);
		}

		if (!spechit.Size ())
		{
			return false;
		}

		// open any specials
		actor->movedir = DI_NODIR;

		// if the special is not a door that can be opened, return false
		//
		// killough 8/9/98: this is what caused monsters to get stuck in
		// doortracks, because it thought that the monster freed itself
		// by opening a door, even if it was moving towards the doortrack,
		// and not the door itself.
		//
		// killough 9/9/98: If a line blocking the monster is activated,
		// return true 90% of the time. If a line blocking the monster is
		// not activated, but some other line is, return false 90% of the
		// time. A bit of randomness is needed to ensure it's free from
		// lockups, but for most cases, it returns the correct result.
		//
		// Do NOT simply return false 1/4th of the time (causes monsters to
		// back out when they shouldn't, and creates secondary stickiness).

		spechit_t spec;
		int good = 0;
		
		if (!(actor->flags6 & MF6_NOTRIGGER))
		{
			while (spechit.Pop (spec))
			{
				// [RH] let monsters push lines, as well as use them
				if (((actor->flags4 & MF4_CANUSEWALLS) && P_ActivateLine (spec.line, actor, 0, SPAC_Use)) ||
					((actor->flags2 & MF2_PUSHWALL) && P_ActivateLine (spec.line, actor, 0, SPAC_Push)))
				{
					good |= spec.line == actor->BlockingLine ? 1 : 2;
				}
			}
		}
		else spechit.Clear();
		return good && ((pr_opendoor() >= 203) ^ (good & 1));
	}
	else
	{
		actor->flags &= ~MF_INFLOAT;
	}
	return true; 
}

//
// P_SmartMove
//
// killough 9/12/98: Same as P_Move, except smarter
//

int P_SmartMove(AActor* actor)
{
	AActor* target = actor->target;
	int on_lift = false, dropoff = false, under_damage;
	bool monster_avoid_hazards = (actor->Level->i_compatflags2 & COMPATF2_AVOID_HAZARDS) || (actor->flags8 & MF8_AVOIDHAZARDS);

	  /* killough 9/12/98: Stay on a lift if target is on one */
	on_lift = ((actor->flags8 & MF8_STAYONLIFT) || (actor->Level->i_compatflags2 & COMPATF2_STAYONLIFT))
		&& target && target->health > 0 && P_IsOnLift(actor)
		&& P_CheckTags(target->Sector, actor->Sector);

	under_damage = monster_avoid_hazards && P_IsUnderDamage(actor) != 0;//e6y

	if (!P_Move(actor))
		return false;

	// killough 9/9/98: avoid crushing ceilings or other damaging areas
	if (
		(on_lift && pr_stayonlift() < 230 &&      // Stay on lift
			!P_IsOnLift(actor))
		||
		(monster_avoid_hazards && !under_damage &&	//e6y  // Get away from damage
			(under_damage = P_IsUnderDamage(actor)) &&
			(under_damage < 0 || pr_avoidcrush() < 200))
		)
		actor->movedir = DI_NODIR;    // avoid the area (most of the time anyway)

	if (actor->flags2 & MF2_FLOORCLIP)
	{
		actor->AdjustFloorClip();
	}
	return true;
}

//=============================================================================
//
// TryWalk
// Attempts to move actor on
// in its current (ob->moveangle) direction.
// If blocked by either a wall or an actor
// returns FALSE
// If move is either clear or blocked only by a door,
// returns TRUE and sets...
// If a door is in the way,
// an OpenDoor call is made to start it opening.
//
//=============================================================================

bool P_TryWalk (AActor *actor)
{
	if (!P_SmartMove (actor))
	{
		return false;
	}
	actor->movecount = pr_trywalk() & 15;
	return true;
}

//=============================================================================
//
// P_DoNewChaseDir
//
// killough 9/8/98:
//
// Most of P_NewChaseDir(), except for what
// determines the new direction to take
//
//=============================================================================

void P_DoNewChaseDir (AActor *actor, double deltax, double deltay)
{
	dirtype_t	d[2];
	int			tdir;
	dirtype_t	olddir, turnaround;
	bool		attempts[NUMDIRS-1]; // We don't need to attempt DI_NODIR.

	memset(&attempts, false, sizeof(attempts));
	olddir = (dirtype_t)actor->movedir;
	turnaround = opposite[olddir];

	if (deltax > 10)
		d[0] = DI_EAST;
	else if (deltax < -10)
		d[0] = DI_WEST;
	else
		d[0] = DI_NODIR;

	if (deltay < -10)
		d[1] = DI_SOUTH;
	else if (deltay>10)
		d[1] = DI_NORTH;
	else
		d[1] = DI_NODIR; 

	// try direct route
	if (d[0] != DI_NODIR && d[1] != DI_NODIR)
	{
		actor->movedir = diags[((deltay<0)<<1) + (deltax>0)];
		if (actor->movedir != turnaround)
		{
			attempts[actor->movedir] = true;
			if (P_TryWalk(actor))
				return;
		}
	}

	// try other directions
	if (!(actor->flags5 & MF5_AVOIDINGDROPOFF))
	{
		if ((pr_newchasedir() > 200 || fabs(deltay) > fabs(deltax)))
		{
			std::swap (d[0], d[1]);
		}

		if (d[0] == turnaround)
			d[0] = DI_NODIR;
		if (d[1] == turnaround)
			d[1] = DI_NODIR;
	}
		
	if (d[0] != DI_NODIR && attempts[d[0]] == false)
	{
		actor->movedir = d[0];
		attempts[d[0]] = true;
		if (P_TryWalk (actor))
		{
			// either moved forward or attacked
			return;
		}
	}

	if (d[1] != DI_NODIR && attempts[d[1]] == false)
	{
		actor->movedir = d[1];
		attempts[d[1]] = true;
		if (P_TryWalk (actor))
			return;
	}

	if (!(actor->flags5 & MF5_AVOIDINGDROPOFF))
	{
		// there is no direct path to the player, so pick another direction.
		if (olddir != DI_NODIR && attempts[olddir] == false)
		{
			actor->movedir = olddir;
			attempts[olddir] = true;
			if (P_TryWalk (actor))
				return;
		}
	}

	// randomly determine direction of search
	if (pr_newchasedir() & 1)	
	{
		for (tdir = DI_EAST; tdir <= DI_SOUTHEAST; tdir++)
		{
			if (tdir != turnaround && attempts[tdir] == false)
			{
				actor->movedir = tdir;
				attempts[tdir] = true;
				if ( P_TryWalk(actor) )
					return;
			}
		}
	}
	else
	{
		for (tdir = DI_SOUTHEAST; tdir != (DI_EAST-1); tdir--)
		{
			if (tdir != turnaround && attempts[tdir] == false)
			{
				actor->movedir = tdir;
				attempts[tdir] = true;
				if ( P_TryWalk(actor) )
					return;
			}
		}
	}

	if (turnaround != DI_NODIR && attempts[turnaround] == false)
	{
		actor->movedir =turnaround;
		if ( P_TryWalk(actor) )
			return;
	}

	actor->movedir = DI_NODIR;	// cannot move
}


//=============================================================================
//
// killough 11/98:
//
// Monsters try to move away from tall dropoffs.
//
// In Doom, they were never allowed to hang over dropoffs,
// and would remain stuck if involuntarily forced over one.
// This logic, combined with p_map.c (P_TryMove), allows
// monsters to free themselves without making them tend to
// hang over dropoffs.
//=============================================================================

//=============================================================================
//
// P_NewChaseDir
//
// killough 9/8/98: Split into two functions
//
//=============================================================================

void P_NewChaseDir(AActor * actor)
{
	DVector2 delta;

	actor->strafecount = 0;

	if ((actor->flags5&MF5_CHASEGOAL || actor->goal == actor->target) && actor->goal!=NULL)
	{
		delta = actor->Vec2To(actor->goal);
	}
	else if (actor->target != NULL)
	{
		delta = actor->Vec2To(actor->target);

		if (!(actor->flags6 & MF6_NOFEAR))
		{
			if ((actor->target->player != NULL && (actor->target->player->cheats & CF_FRIGHTENING)) || 
				(actor->flags4 & MF4_FRIGHTENED) ||
				(actor->target->flags8 & MF8_FRIGHTENING))
			{
				delta = -delta;
			}
		}
	}
	else
	{
		// Don't abort if this happens.
		Printf ("P_NewChaseDir: called with no target\n");
		P_RandomChaseDir(actor);
		return;
	}
	
	// Try to move away from a dropoff
	if (actor->floorz - actor->dropoffz > actor->MaxDropOffHeight && 
		actor->Z() <= actor->floorz && !(actor->flags & MF_DROPOFF) && 
		!(actor->flags2 & MF2_ONMOBJ) &&
		!(actor->flags & MF_FLOAT) && !(actor->Level->i_compatflags & COMPATF_DROPOFF))
	{
		FBoundingBox box(actor->X(), actor->Y(), actor->radius);
		FBlockLinesIterator it(actor->Level, box);
		line_t *line;

		double deltax = 0;
		double deltay = 0;
		while ((line = it.Next()))
		{
			if (line->backsector  && // Ignore one-sided linedefs
				inRange(box, line) &&
				BoxOnLineSide(box, line) == -1)
		    {
				double front = line->frontsector->floorplane.ZatPoint(actor->PosRelative(line));
				double back  = line->backsector->floorplane.ZatPoint(actor->PosRelative(line));
				DAngle angle;
		
				// The monster must contact one of the two floors,
				// and the other must be a tall dropoff.
				
				if (back == actor->Z() && front < actor->Z() - actor->MaxDropOffHeight)
				{
					angle = line->Delta().Angle();   // front side dropoff
				}
				else if (front == actor->Z() && back < actor->Z() - actor->MaxDropOffHeight)
				{
					angle = line->Delta().Angle() + DAngle::fromDeg(180.); // back side dropoff
				}
				else continue;
		
				// Move away from dropoff at a standard speed.
				// Multiple contacted linedefs are cumulative (e.g. hanging over corner)
				deltax -= 32 * angle.Sin();
				deltay += 32 * angle.Cos();
			}
		}

		if (deltax != 0 || deltay != 0) 
		{
			// [Graf Zahl] I have changed P_TryMove to only apply this logic when
			// being called from here. AVOIDINGDROPOFF activates the code that
			// allows monsters to move away from a dropoff. This is different from
			// MBF which requires unconditional use of the altered logic and therefore
			// forcing a massive change in the monster behavior to use this.

			// use different dropoff movement logic in P_TryMove
			actor->flags5|=MF5_AVOIDINGDROPOFF;
			P_DoNewChaseDir(actor, deltax, deltay);
			actor->flags5&=~MF5_AVOIDINGDROPOFF;
		
			// If moving away from dropoff, set movecount to 1 so that 
			// small steps are taken to get monster away from dropoff.
			actor->movecount = 1;
			return;
		}
	}

#if 0
	// Move away from friends when too close, except
	// in certain situations (e.g. a crowded lift)

	// MBF code for friends. Cannot be done in ZDoom but left here as a reminder for later implementation.

	if (actor->flags & target->flags & MF_FRIEND &&
	    distfriend > dist && 
	    !P_IsOnLift(target) && !P_IsUnderDamage(actor))
	  deltax = -deltax, deltay = -deltay;
	else
#endif

	// MBF's monster_backing option. Made an actor flag instead. Also cleaned the code up to make it readable.
	// Todo: implement the movement logic
	AActor *target = actor->target;
	if (target->health > 0 && !actor->IsFriend(target) && target != actor->goal)
    {   // Live enemy target

		if ((actor->flags3 & MF3_AVOIDMELEE) || (actor->Level->flags3 & LEVEL3_AVOIDMELEE))
		{
			bool ismeleeattacker = false;
			double dist = actor->Distance2D(target);
			if (target->player == NULL)
			{
				ismeleeattacker = (target->MissileState == NULL && dist < (target->meleerange + target->radius)*2);
			}
			else if (target->player->ReadyWeapon != NULL)
			{
				// melee range of player weapon is a parameter of the action function and cannot be checked here.
				// Add a new weapon property?
				ismeleeattacker = ((target->player->ReadyWeapon->IntVar(NAME_WeaponFlags) & WIF_MELEEWEAPON) && dist < 192);
			}
			if (ismeleeattacker)
			{
				actor->strafecount = pr_enemystrafe() & 15;
				delta = -delta;
			}
	    }
	}

	P_DoNewChaseDir(actor, delta.X, delta.Y);

	// If strafing, set movecount to strafecount so that old Doom
	// logic still works the same, except in the strafing part

	if (actor->strafecount)
		actor->movecount = actor->strafecount;

}

//=============================================================================
//
// P_RandomChaseDir
//
//=============================================================================

void P_RandomChaseDir (AActor *actor)
{
	dirtype_t olddir, turnaround;
	int tdir, i;

	olddir = (dirtype_t)actor->movedir;
	turnaround = opposite[olddir];
	int turndir;

	// Friendly monsters like to head toward a player
	if (actor->flags & MF_FRIENDLY && !(actor->flags8 & MF8_DONTFOLLOWPLAYERS))
	{
		AActor *player;
		DVector2 delta;
		dirtype_t d[3];

		if (actor->FriendPlayer != 0)
		{
			player = actor->Level->Players[i = actor->FriendPlayer - 1]->mo;
		}
		else
		{
			if (!multiplayer)
			{
				i = 0;
			}
			else for (i = pr_newchasedir() & (MAXPLAYERS-1); !actor->Level->PlayerInGame(i); i = (i+1) & (MAXPLAYERS-1))
			{
			}
			player = actor->Level->Players[i]->mo;
		}
		if (player != NULL && actor->Level->PlayerInGame(i))
		{
			if (pr_newchasedir() & 1 || !P_CheckSight (actor, player))
			{
				delta = actor->Vec2To(player);

				if (delta.X>128)
					d[1]= DI_EAST;
				else if (delta.X<-128)
					d[1]= DI_WEST;
				else
					d[1]=DI_NODIR;

				if (delta.Y<-128)
					d[2]= DI_SOUTH;
				else if (delta.Y>128)
					d[2]= DI_NORTH;
				else
					d[2]=DI_NODIR;

				// try direct route
				if (d[1] != DI_NODIR && d[2] != DI_NODIR)
				{
					actor->movedir = diags[((delta.Y<0)<<1) + (delta.X>0)];
					if (actor->movedir != turnaround && P_TryWalk(actor))
						return;
				}

				// try other directions
				if (pr_newchasedir() > 200 || fabs(delta.Y) > fabs(delta.X))
				{
					std::swap (d[1], d[2]);
				}

				if (d[1] == turnaround)
					d[1] = DI_NODIR;
				if (d[2] == turnaround)
					d[2] = DI_NODIR;
					
				if (d[1] != DI_NODIR)
				{
					actor->movedir = d[1];
					if (P_TryWalk (actor))
					{
						// either moved forward or attacked
						return;
					}
				}

				if (d[2] != DI_NODIR)
				{
					actor->movedir = d[2];
					if (P_TryWalk (actor))
						return;
				}
			}
		}
	}

	// If the actor elects to continue in its current direction, let it do
	// so unless the way is blocked. Then it must turn.
	if (pr_newchasedir() < 150)
	{
		if (P_TryWalk (actor))
			return;
	}

	turndir = (pr_newchasedir() & 1) ? -1 : 1;

	if (olddir == DI_NODIR)
	{
		olddir = (dirtype_t)(pr_newchasedir() & 7);
	}
	for (tdir = (olddir + turndir) & 7; tdir != olddir; tdir = (tdir + turndir) & 7)
	{
		if (tdir != turnaround)
		{
			actor->movedir = tdir;
			if (P_TryWalk (actor))
				return;
		}
	}
/*
	if (pr_newchasedir() & 1)
	{
		for (tdir = olddir; tdir <= DI_SOUTHEAST; ++tdir)
		{
			if (tdir != turnaround)
			{
				actor->movedir = tdir;
				if (P_TryWalk (actor))
					return;
			}
		}
	}
	else
	{
		for (tdir = DI_SOUTHEAST; tdir >= DI_EAST; --tdir)
		{
			if (tdir != turnaround)
			{
				actor->movedir = tdir;
				if (P_TryWalk (actor))
					return;
			}
		}
	}
*/
	if (turnaround != DI_NODIR)
	{
		actor->movedir = turnaround;
		if (P_TryWalk (actor))
		{
			actor->movecount = pr_newchasedir() & 15;
			return;
		}
	}

	actor->movedir = DI_NODIR;	// cannot move
}

//---------------------------------------------------------------------------
//
// P_IsVisible
//
// killough 9/9/98: whether a target is visible to a monster
// Extended to handle all A_LookEx related checking, too.
//
//---------------------------------------------------------------------------

int P_IsVisible(AActor *lookee, AActor *other, INTBOOL allaround, FLookExParams *params)
{
	double maxdist;
	double mindist;
	DAngle fov;

	if (other == nullptr || (other->flags8 & MF8_MVISBLOCKED))
	{
		return false;
	}

	if (params != NULL)
	{
		maxdist = params->maxDist;
		mindist = params->minDist;
		fov = allaround ? DAngle::fromDeg(0.) : params->Fov; // [RK] Account for LOOKALLAROUND flag.
	}
	else
	{
		mindist = maxdist = 0;
		fov = DAngle::fromDeg(allaround ? 0. : 180.);
	}

	double dist = lookee->Distance2D (other);

	if (maxdist && dist > maxdist)
		return false;			// [KS] too far

	if (mindist && dist < mindist)
		return false;			// [KS] too close

	if (fov != nullAngle)
	{
		DAngle an = absangle(lookee->AngleTo(other), lookee->Angles.Yaw);

		if (an > (fov / 2))
		{
			// if real close, react anyway
			// [KS] but respect minimum distance rules
			if (mindist || dist > lookee->meleerange + lookee->radius)
				return false;	// outside of fov
		}
	}

	// P_CheckSight is by far the most expensive operation in here so let's do it last.
	return P_CheckSight(lookee, other, SF_SEEPASTSHOOTABLELINES);
}

//---------------------------------------------------------------------------
//
// FUNC P_LookForMonsters
//
//---------------------------------------------------------------------------

#define MONS_LOOK_RANGE (20*64)
#define MONS_LOOK_LIMIT 64

int P_LookForMonsters (AActor *actor)
{
	int count;
	AActor *mo;
	auto iterator = actor->Level->GetThinkerIterator<AActor>();

	if (!P_CheckSight (actor->Level->Players[0]->mo, actor, SF_SEEPASTBLOCKEVERYTHING))
	{ // Player can't see monster
		return false;
	}
	count = 0;
	while ( (mo = iterator.Next ()) )
	{
		if (!(mo->flags3 & MF3_ISMONSTER) || (mo == actor) || (mo->health <= 0))
		{ // Not a valid monster
			continue;
		}
		if (mo->Distance2D (actor) > MONS_LOOK_RANGE)
		{ // Out of range
			continue;
		}
		if (pr_lookformonsters() < 16)
		{ // Skip
			continue;
		}
		if (++count >= MONS_LOOK_LIMIT)
		{ // Stop searching
			return false;
		}
		if (mo->GetSpecies() == actor->GetSpecies())
		{ // [RH] Don't go after same species
			continue;
		}
		if (!P_CheckSight (actor, mo, SF_SEEPASTBLOCKEVERYTHING))
		{ // Out of sight
			continue;
		}
		// Found a target monster
		actor->target = mo;
		return true;
	}
	return false;
}

//============================================================================
//
// LookForTIDinBlock
//
// Finds a target with the specified TID in a mapblock. Alternatively, it
// can find a target with a specified TID if something in this mapblock is
// already targetting it.
//
//============================================================================

AActor *LookForTIDInBlock (AActor *lookee, int index, void *extparams)
{
	FLookExParams *params = (FLookExParams *)extparams;
	FBlockNode *block;
	AActor *link;
	AActor *other;
	
	for (block = lookee->Level->blockmap.blocklinks[index]; block != NULL; block = block->NextActor)
	{
		link = block->Me;

        if (!(link->flags & MF_SHOOTABLE))
			continue;			// not shootable (observer or dead)

		if (link == lookee)
			continue;

		if (link->health <= 0)
			continue;			// dead

		if (link->flags2 & MF2_DORMANT)
			continue;			// don't target dormant things

		if (link->tid == lookee->TIDtoHate)
		{
			other = link;
		}
		else if (link->target != NULL && link->target->tid == lookee->TIDtoHate)
		{
			other = link->target;
			if (!(other->flags & MF_SHOOTABLE) ||
				other->health <= 0 ||
				(other->flags2 & MF2_DORMANT))
			{
				continue;
			}
		}
		else
		{
			continue;
		}

		if (!(lookee->flags3 & MF3_NOSIGHTCHECK))
		{
			if (!P_IsVisible(lookee, other, true, params))
				continue;			// out of sight
		}

		return other;
	}
	return NULL;
}

//============================================================================
//
// P_LookForTID
//
// Selects a live monster with the given TID
//
//============================================================================

int P_LookForTID (AActor *actor, INTBOOL allaround, FLookExParams *params)
{
	AActor *other;
	bool reachedend = false;
	bool chasegoal = params? (!(params->flags & LOF_DONTCHASEGOAL)) : true;

	other = P_BlockmapSearch (actor, 0, LookForTIDInBlock, params);

	if (other != NULL)
	{
		if (actor->goal && actor->target == actor->goal)
			actor->reactiontime = 0;

		actor->target = other;
		actor->LastLookActor = other;
		return true;
	}

	// The actor's TID could change because of death or because of
	// Thing_ChangeTID. If it's not what we expect, then don't use
	// it as a base for the iterator.
	if (actor->LastLookActor != NULL &&
		actor->LastLookActor->tid != actor->TIDtoHate)
	{
		actor->LastLookActor = nullptr;
	}

	auto iterator = actor->Level->GetActorIterator(actor->TIDtoHate, actor->LastLookActor);
	int c = (pr_look3() & 31) + 7;	// Look for between 7 and 38 hatees at a time
	while ((other = iterator.Next()) != actor->LastLookActor)
	{
		if (other == NULL)
		{
			if (reachedend)
			{
				// we have cycled through the entire list at least once
				// so let's abort because even if we continue nothing can
				// be found.
				break;
			}
			reachedend = true;
			continue;
		}

		if (!(other->flags & MF_SHOOTABLE))
			continue;			// not shootable (observer or dead)

		if (other == actor)
			continue;			// don't hate self

		if (other->health <= 0)
			continue;			// dead

		if (other->flags2 & MF2_DORMANT)
			continue;			// don't target dormant things

		if (--c == 0)
			break;

		if (!(actor->flags3 & MF3_NOSIGHTCHECK))
		{
			if (!P_IsVisible (actor, other, !!allaround, params))
				continue;			// out of sight
		}
		
		// [RH] Need to be sure the reactiontime is 0 if the monster is
		//		leaving its goal to go after something else.
		if (actor->goal && actor->target == actor->goal)
			actor->reactiontime = 0;

		actor->target = other;
		actor->LastLookActor = other;
		return true;
	}
	actor->LastLookActor = other;
	if (actor->target == NULL)
	{
		// [RH] use goal as target
		if (actor->goal != NULL && chasegoal)
		{
			actor->target = actor->goal;
			return true;
		}
		// Use last known enemy if no hatee sighted -- killough 2/15/98:
		if (actor->lastenemy != NULL && actor->lastenemy->health > 0)
		{
			if (!actor->IsFriend(actor->lastenemy))
			{
				actor->target = actor->lastenemy;
				actor->lastenemy = nullptr;
				return true;
			}
			else
			{
				actor->lastenemy = nullptr;
			}
		}
	}
	return false;
}

//============================================================================
//
// LookForEnemiesinBlock
//
// Finds a non-friendly monster in a mapblock. It can also use targets of
// friendlies in this mapblock to find non-friendlies in other mapblocks.
//
//============================================================================

AActor *LookForEnemiesInBlock (AActor *lookee, int index, void *extparam)
{
	FBlockNode *block;
	AActor *link;
	AActor *other;
	FLookExParams *params = (FLookExParams *)extparam;
	
	for (block = lookee->Level->blockmap.blocklinks[index]; block != NULL; block = block->NextActor)
	{
		link = block->Me;

        if (!(link->flags & MF_SHOOTABLE))
			continue;			// not shootable (observer or dead)

		if (link == lookee)
			continue;

		if (link->health <= 0)
			continue;			// dead

		if (link->flags2 & MF2_DORMANT)
			continue;			// don't target dormant things

		if (!(link->flags3 & MF3_ISMONSTER))
			continue;			// don't target it if it isn't a monster (could be a barrel)

		if (link->flags7 & MF7_NEVERTARGET)
			continue;

		other = NULL;
		if (lookee->flags & MF_FRIENDLY)
		{
			if (link->flags & MF_FRIENDLY)
			{
				if (!lookee->IsFriend(link))
				{
					// This is somebody else's friend, so go after it
					other = link;
				}
				else if (link->target != NULL && !(link->target->flags & MF_FRIENDLY))
				{
					other = link->target;
					if (!(other->flags & MF_SHOOTABLE) ||
						other->health <= 0 ||
						(other->flags2 & MF2_DORMANT))
					{
						other = NULL;;
					}
				}
			}
			else
			{
				other = link;
			}
		}
		else if (lookee->flags8 & MF8_SEEFRIENDLYMONSTERS && link->flags & MF_FRIENDLY)
		{
			other = link;
		}

		// [MBF] If the monster is already engaged in a one-on-one attack
		// with a healthy friend, don't attack around 60% the time.
		
		// [GrafZahl] This prevents friendlies from attacking all the same 
		// target.
		
		if (other)
		{
			AActor *targ = other->target;
			if (targ && targ->target == other && pr_skiptarget() > 100 && lookee->IsFriend (targ) &&
				targ->health*2 >= targ->SpawnHealth())
			{
				continue;
			}
		}

		// [KS] Hey, shouldn't there be a check for MF3_NOSIGHTCHECK here?

		if (other == NULL || !P_IsVisible (lookee, other, true, params))
			continue;			// out of sight


		return other;
	}
	return NULL;
}

//============================================================================
//
// P_LookForEnemies
//
// Selects a live enemy monster
//
//============================================================================

int P_LookForEnemies (AActor *actor, INTBOOL allaround, FLookExParams *params)
{
	AActor *other;

	other = P_BlockmapSearch(actor, actor->friendlyseeblocks, LookForEnemiesInBlock, params);

	if (other != NULL)
	{
		if (actor->goal && actor->target == actor->goal)
			actor->reactiontime = 0;

		actor->target = other;
//		actor->LastLook.Actor = other;
		return true;
	}

	if (actor->target == NULL)
	{
		// [RH] use goal as target
		if (actor->goal != NULL)
		{
			actor->target = actor->goal;
			return true;
		}
		// Use last known enemy if no hatee sighted -- killough 2/15/98:
		if (actor->lastenemy != NULL && actor->lastenemy->health > 0)
		{
			if (!actor->IsFriend(actor->lastenemy))
			{
				actor->target = actor->lastenemy;
				actor->lastenemy = nullptr;
				return true;
			}
			else
			{
				actor->lastenemy = nullptr;
			}
		}
	}
	return false;
}


/*
================
=
= P_LookForPlayers
=
= If allaround is false, only look 180 degrees in front
= returns true if a player is targeted
================
*/

int P_LookForPlayers (AActor *actor, INTBOOL allaround, FLookExParams *params)
{
	int 		c;
	int			pnum;
	player_t*	player;
	bool chasegoal = params? (!(params->flags & LOF_DONTCHASEGOAL)) : true;

	if (actor->TIDtoHate != 0)
	{
		if (P_LookForTID (actor, allaround, params))
		{
			return true;
		}
		if (!(actor->flags3 & MF3_HUNTPLAYERS))
		{
			return false;
		}
	}
	else if (actor->flags & MF_FRIENDLY)
	{
		bool result = P_LookForEnemies (actor, allaround, params);

#ifdef MBF_FRIENDS
		if (!result && (actor->flags & MF_FRIEND_MBF))
		{  // killough 9/9/98: friendly monsters go about players differently

			// Go back to a player, no matter whether it's visible or not
			for (int anyone=0; anyone<=1; anyone++)
			{
				auto Level = actor->Level;
				for (int c=0; c<MAXPLAYERS; c++)
				{
					auto p = Level->Players[i];
					if (Level->PlayerInGame(c) && p->playerstate==PST_LIVE &&
						actor->IsFriend(p->mo) &&
						(anyone || P_IsVisible(actor, p->mo, allaround)))
					{
						actor->target = Level->Players[c]->mo;

						// killough 12/98:
						// get out of refiring loop, to avoid hitting player accidentally

						if (actor->MissileState != NULL)
						{
							actor->SetState(actor->SeeState, true);
							actor->flags &= ~MF_JUSTHIT;
						}

						return true;
					}
				}
			}
		}
#endif
		// [SP] If you don't see any enemies in deathmatch, look for players (but only when friend to a specific player.)
		if (actor->FriendPlayer == 0 && (!teamplay || actor->GetTeam() == TEAM_NONE)) return result;
		if (result || !deathmatch) return true;


	}	// [SP] if false, and in deathmatch, intentional fall-through
	else if (actor->flags8 & MF8_SEEFRIENDLYMONSTERS)
	{
		bool result = P_LookForEnemies (actor, allaround, params);

		if (result) return true;
	}

	if (!(gameinfo.gametype & (GAME_DoomStrifeChex)) &&
		actor->Level->isPrimaryLevel() &&
		!multiplayer &&
		actor->Level->Players[0]->health <= 0 &&
		actor->goal == NULL &&
		gamestate != GS_TITLELEVEL
		)
	{ // Single player game and player is dead; look for monsters
		return P_LookForMonsters (actor);
	}

	c = 0;
	if (actor->TIDtoHate != 0)
	{
		pnum = pr_look2() & (MAXPLAYERS-1);
	}
	else
	{
		pnum = actor->LastLookPlayerNumber;
	}
		
	for (;;)
	{
		// [ED850] Each and every player should only ever be checked once.
		if (c++ < MAXPLAYERS)
		{
			pnum = (pnum + 1) & (MAXPLAYERS - 1);
			if (!actor->Level->PlayerInGame(pnum))
				continue;

			if (actor->TIDtoHate == 0)
			{
				actor->LastLookPlayerNumber = pnum;
			}
		}
		else
		{
			// done looking
			if (actor->target == NULL)
			{
				// [RH] use goal as target
				// [KS] ...unless we're ignoring goals and we don't already have one
				if (actor->goal != NULL && chasegoal)
				{
					actor->target = actor->goal;
					return true;
				}
				// Use last known enemy if no players sighted -- killough 2/15/98:
				if (actor->lastenemy != NULL && actor->lastenemy->health > 0)
				{
					if (!actor->IsFriend(actor->lastenemy))
					{
						actor->target = actor->lastenemy;
						actor->lastenemy = nullptr;
						return true;
					}
					else
					{
						actor->lastenemy = nullptr;
					}
				}
			}
			return actor->target == actor->goal && actor->goal != NULL;
		}

		player = actor->Level->Players[pnum];

		if (!(player->mo->flags & MF_SHOOTABLE))
			continue;			// not shootable (observer or dead)

		if (actor->IsFriend(player->mo))
			continue;			// same +MF_FRIENDLY, ignore

		if (player->cheats & CF_NOTARGET)
			continue;			// no target

		if (player->health <= 0)
			continue;			// dead

		if (!P_IsVisible (actor, player->mo, allaround, params))
			continue;			// out of sight


		if ( !actor->CallCanDetect(player->mo) || !player->mo->CallCanBeDetectedBy(actor) )
		{
			continue;
		}
		
		// [RH] Need to be sure the reactiontime is 0 if the monster is
		//		leaving its goal to go after a player.
		if (actor->goal && actor->target == actor->goal)
			actor->reactiontime = 0;

		actor->target = player->mo;
		return true;
	}
}

//
// ACTION ROUTINES
//

//
// A_Look
// Stay in state until a player is sighted.
// [RH] Will also leave state to move to goal.
//
DEFINE_ACTION_FUNCTION(AActor, A_Look)
{
	PARAM_SELF_PROLOGUE(AActor);

	AActor *targ;

	if (self->flags5 & MF5_INCONVERSATION)
		return 0;

	// [RH] Set goal now if appropriate
	if (self->special == Thing_SetGoal && self->args[0] == 0) 
	{
		auto iterator = self->Level->GetActorIterator(NAME_PatrolPoint, self->args[1]);
		self->special = 0;
		self->goal = iterator.Next ();
		self->reactiontime = self->args[2] * TICRATE + self->Level->maptime;
		if (self->args[3] == 0) self->flags5 &= ~MF5_CHASEGOAL;
		else self->flags5 |= MF5_CHASEGOAL;
	}

	self->threshold = 0;		// any shot will wake up

	if (self->TIDtoHate != 0)
	{
		targ = self->target;
	}
	else
	{
		targ = (self->Level->i_compatflags & COMPATF_SOUNDTARGET || self->flags & MF_NOSECTOR)? 
			self->Sector->SoundTarget : self->LastHeard;

		// [RH] If the soundtarget is dead, don't chase it
		if (targ != NULL && targ->health <= 0)
		{
			targ = NULL;
		}

		if (targ && targ->player && ((targ->player->cheats & CF_NOTARGET) || !(targ->flags & MF_FRIENDLY)))
		{
			return 0;
		}
	}

	// [RH] Andy Baker's stealth monsters
	if (self->flags & MF_STEALTH)
	{
		self->visdir = -1;
	}

	if (targ && (targ->flags & MF_SHOOTABLE))
	{
		if (self->IsFriend (targ))	// be a little more precise!
		{
			// If we find a valid target here, the wandering logic should *not*
			// be activated! It would cause the seestate to be set twice.
			if (P_LookForPlayers (self, self->flags4 & MF4_LOOKALLAROUND, NULL))
				goto seeyou;

			// Let the self wander around aimlessly looking for a fight
			if (self->SeeState != NULL)
			{
				self->SetState (self->SeeState);
			}
			else
			{
				A_Wander(self);
			}
		}
		else
		{
			self->target = targ;

			if (self->flags & MF_AMBUSH)
			{
				if (P_CheckSight (self, self->target, SF_SEEPASTBLOCKEVERYTHING))
					goto seeyou;
			}
			else
				goto seeyou;
		}
	}
	
	if (!P_LookForPlayers (self, self->flags4 & MF4_LOOKALLAROUND, NULL))
		return 0;
				
	// go into chase state
  seeyou:
	// [RH] Don't start chasing after a goal if it isn't time yet.
	if (self->target == self->goal)
	{
		if (self->reactiontime > self->Level->maptime)
			self->target = nullptr;
	}
	else if (self->SeeSound.isvalid())
	{
		if ((self->flags2 & MF2_BOSS) || (self->flags8 & MF8_FULLVOLSEE))
		{ // full volume
			S_Sound (self, CHAN_VOICE, 0, self->SeeSound, 1, ATTN_NONE);
		}
		else
		{
			S_Sound (self, CHAN_VOICE, 0, self->SeeSound, 1, ATTN_NORM);
		}
	}

	if (self->target && self->SeeState)
	{
		self->SetState (self->SeeState);
	}
	return 0;
}


//==========================================================================
//
// A_LookEx (int flags, fixed minseedist, fixed maxseedist, fixed maxheardist, fixed fov, state wakestate)
// [KS] Borrowed the A_Look code to make a parameterized version.
//
//==========================================================================

DEFINE_ACTION_FUNCTION(AActor, A_LookEx)
{
	PARAM_SELF_PROLOGUE(AActor);
	PARAM_INT	(flags)			
	PARAM_FLOAT	(minseedist)	
	PARAM_FLOAT	(maxseedist)	
	PARAM_FLOAT (maxheardist)	
	PARAM_ANGLE (fov)			
	PARAM_STATE	(seestate)		

	AActor *targ = NULL; // Shuts up gcc
	double dist;
	if (fov == nullAngle) fov = DAngle::fromDeg(180.);
	FLookExParams params = { fov, minseedist, maxseedist, maxheardist, flags, seestate };

	if (self->flags5 & MF5_INCONVERSATION)
		return 0;

	// [RH] Set goal now if appropriate
	if (self->special == Thing_SetGoal && self->args[0] == 0) 
	{
		auto iterator = self->Level->GetActorIterator(NAME_PatrolPoint, self->args[1]);
		self->special = 0;
		self->goal = iterator.Next ();
		self->reactiontime = self->args[2] * TICRATE + self->Level->maptime;
		if (self->args[3] == 0)
			self->flags5 &= ~MF5_CHASEGOAL;
		else
			self->flags5 |= MF5_CHASEGOAL;
	}

	self->threshold = 0;		// any shot will wake up

	if (self->TIDtoHate != 0)
	{
		targ = self->target;
	}
	else
	{
		if (!(flags & LOF_NOSOUNDCHECK))
		{
			targ = (self->Level->i_compatflags & COMPATF_SOUNDTARGET || self->flags & MF_NOSECTOR)?
				self->Sector->SoundTarget : self->LastHeard;
			if (targ != NULL)
			{
				// [RH] If the soundtarget is dead, don't chase it
				if (targ->health <= 0)
				{
					targ = NULL;
				}
				else
				{
					dist = self->Distance2D (targ);

					// [KS] If the target is too far away, don't respond to the sound.
					if (maxheardist && dist > maxheardist)
					{
						targ = NULL;
						self->LastHeard = nullptr;
					}
				}
			}
        }
        
        if (targ && targ->player && (targ->player->cheats & CF_NOTARGET))
        {
            return 0;
        }
	}

	// [RH] Andy Baker's stealth monsters
	if (self->flags & MF_STEALTH)
	{
		self->visdir = -1;
	}

	if (targ && (targ->flags & MF_SHOOTABLE))
	{
		if (self->IsFriend (targ))	// be a little more precise!
		{
			if (!(self->flags4 & MF4_STANDSTILL))
			{
				if (!(flags & LOF_NOSIGHTCHECK))
				{
					// If we find a valid target here, the wandering logic should *not*
					// be activated! If would cause the seestate to be set twice.
					if (P_LookForPlayers(self, (self->flags4 & MF4_LOOKALLAROUND), &params)) // [RK] Passing true for allround should only occur if the flag is actually set.
						goto seeyou;
				}

				// Let the self wander around aimlessly looking for a fight
                if (!(self->flags7 & MF7_INCHASE))
                {
                    if (seestate)
                    {
                        self->SetState (seestate);
                    }
                    else
                    {
                        if (self->SeeState != NULL)
                        {
                            self->SetState (self->SeeState);
                        }
                        else
                        {
                            A_Wander(self);
                        }
                    }
                }
			}
		}
		else
		{
			self->target = targ; //We already have a target?
            
            // [KS] The target can become ourselves in rare circumstances (like
            // if we committed suicide), so if that's the case, just ignore it.
            if (self->target == self) self->target = nullptr;

			if (self->target != NULL)
			{
				if (self->flags & MF_AMBUSH)
				{
					dist = self->Distance2D (self->target);
					if (P_CheckSight (self, self->target, SF_SEEPASTBLOCKEVERYTHING) &&
						(!minseedist || dist > minseedist) &&
						(!maxseedist || dist < maxseedist))
					{
						goto seeyou;
					}
				}
				else
					goto seeyou;
			}
		}
	}

	if (!(flags & LOF_NOSIGHTCHECK))
	{
		if (!P_LookForPlayers(self, (self->flags4 & MF4_LOOKALLAROUND), &params)) // [RK] Account for the flag being set.
			return 0;
	}
	else
	{
		return 0;
	}
				
	// go into chase state
  seeyou:
	// [RH] Don't start chasing after a goal if it isn't time yet.
	if (self->target == self->goal)
	{
		if (self->reactiontime > self->Level->maptime)
			self->target = nullptr;
	}
	else if (self->SeeSound.isvalid() && !(flags & LOF_NOSEESOUND))
	{
		if (flags & LOF_FULLVOLSEESOUND)
		{ // full volume
			S_Sound (self, CHAN_VOICE, 0, self->SeeSound, 1, ATTN_NONE);
		}
		else
		{
			S_Sound (self, CHAN_VOICE, 0, self->SeeSound, 1, ATTN_NORM);
		}
	}

	if (self->target && !(self->flags7 & MF7_INCHASE))
	{
        if (!(flags & LOF_NOJUMP))
        {
            if (seestate)
            {
                self->SetState (seestate);
            }
            else
            {
                self->SetState (self->SeeState);
            }
        }
	}
	return 0;
}

// [KS] *** End additions by me ***

//==========================================================================
//
// A_ClearLastHeard
//
//==========================================================================

DEFINE_ACTION_FUNCTION(AActor, A_ClearLastHeard)
{
	PARAM_SELF_PROLOGUE(AActor);
	self->LastHeard = nullptr;
	return 0;
}

//==========================================================================
//
// A_Wander
//
//==========================================================================
enum ChaseFlags
{
	CHF_FASTCHASE = 1,
	CHF_NOPLAYACTIVE = 2,
	CHF_NIGHTMAREFAST = 4,
	CHF_RESURRECT = 8,
	CHF_DONTMOVE = 16,
	CHF_NORANDOMTURN = 32,
	CHF_NODIRECTIONTURN = 64,
	CHF_NOPOSTATTACKTURN = 128,
	CHF_STOPIFBLOCKED = 256,
	CHF_DONTIDLE = 512,
	CHF_DONTLOOKALLAROUND = 1024,
};

void A_Wander(AActor *self, int flags)
{
	// [RH] Strife probably clears this flag somewhere, but I couldn't find where.
	// This seems as good a place as any.
	self->flags4 &= ~MF4_INCOMBAT;

	if (self->flags5 & MF5_INCONVERSATION)
		return;

	if (self->flags4 & MF4_STANDSTILL)
		return;

	if (self->reactiontime != 0)
	{
		self->reactiontime--;
		return;
	}

	// turn towards movement direction if not there yet
	if (!(flags & CHF_NODIRECTIONTURN) && (self->movedir < DI_NODIR))
	{
		self->Angles.Yaw = DAngle::fromDeg(floor(self->Angles.Yaw.Degrees() / 45) * 45.);
		DAngle delta = deltaangle(self->Angles.Yaw, DAngle::fromDeg(self->movedir * 45));
		if (delta < nullAngle)
		{
			self->Angles.Yaw -= DAngle::fromDeg(45);
		}
		else if (delta > nullAngle)
		{
			self->Angles.Yaw += DAngle::fromDeg(45);
		}
	}

	if ((--self->movecount < 0 && !(flags & CHF_NORANDOMTURN)) || (!P_SmartMove(self) && !(flags & CHF_STOPIFBLOCKED)))
	{
		P_RandomChaseDir(self);
		self->movecount += 5;
	}
	return;
}

//==========================================================================
//
// A_Look2
//
//==========================================================================
DEFINE_ACTION_FUNCTION(AActor, A_Look2)
{
	PARAM_SELF_PROLOGUE(AActor);

	AActor *targ;

	if (self->flags5 & MF5_INCONVERSATION)
		return 0;

	self->threshold = 0;
	targ = self->LastHeard;

	if (targ != NULL && targ->health <= 0)
	{
		targ = NULL;
	}

	if (targ && (targ->flags & MF_SHOOTABLE))
	{
		if ((self->Level->flags & LEVEL_NOALLIES) ||
			(self->flags & MF_FRIENDLY) != (targ->flags & MF_FRIENDLY))
		{
			if (self->flags & MF_AMBUSH)
			{
				if (!P_CheckSight (self, targ, SF_SEEPASTBLOCKEVERYTHING))
					goto nosee;
			}
			self->target = targ;
			self->threshold = 10;
			self->SetState (self->SeeState);
			return 0;
		}
		else
		{
			if (!P_LookForPlayers (self, self->flags4 & MF4_LOOKALLAROUND, NULL))
				goto nosee;
			self->SetState (self->SeeState);
			self->flags4 |= MF4_INCOMBAT;
			return 0;
		}
	}
nosee:
	if (pr_look2() < 30)
	{
		self->SetState (self->SpawnState + (pr_look2() & 1) + 1);
	}
	if (!(self->flags4 & MF4_STANDSTILL) && pr_look2() < 40)
	{
		self->SetState (self->SpawnState + 3);
	}
	return 0;
}

//=============================================================================
//
// A_Chase
//
// Actor has a melee attack, so it tries to close as fast as possible
//
// [GrafZahl] integrated A_FastChase, A_SerpentChase and A_SerpentWalk into this
// to allow the monsters using those functions to take advantage of the
// enhancements.
//
//=============================================================================
#define CLASS_BOSS_STRAFE_RANGE	64*10

void A_DoChase (AActor *actor, bool fastchase, FState *meleestate, FState *missilestate, bool playactive, bool nightmarefast, bool dontmove, int flags)
{
	if (actor->flags5 & MF5_INCONVERSATION)
		return;

	if (actor->flags7 & MF7_INCHASE)
	{
		return;
	}
	actor->flags7 |= MF7_INCHASE;

	// [RH] Andy Baker's stealth monsters
	if (actor->flags & MF_STEALTH)
	{
		actor->visdir = -1;
	}

	if (actor->reactiontime)
	{
		actor->reactiontime--;
	}

	// [RH] Don't chase invisible targets
	if (actor->target != NULL &&
		((actor->target->renderflags & RF_INVISIBLE) || (actor->target->flags8 & MF8_MINVISIBLE)) &&
		actor->target != actor->goal)
	{
		actor->target = nullptr;
	}

	// modify target threshold
	if (actor->threshold)
	{
		if (actor->target == NULL || actor->target->health <= 0)
		{
			actor->threshold = 0;
		}
		else
		{
			actor->threshold--;
		}
	}

	if (nightmarefast && G_SkillProperty(SKILLP_FastMonsters))
	{ // Monsters move faster in nightmare mode
		if (actor->tics > 3)
		{
			actor->tics -= actor->tics / 2;
			if (actor->tics < 3)
			{
				actor->tics = 3;
			}
		}
	}

	// turn towards movement direction if not there yet
	if (actor->strafecount)
	{
		A_FaceTarget(actor);
	}
	else if (!(flags & CHF_NODIRECTIONTURN) && actor->movedir < 8)
	{
		actor->Angles.Yaw = DAngle::fromDeg(floor(actor->Angles.Yaw.Degrees() / 45) * 45.);
		DAngle delta = deltaangle(actor->Angles.Yaw, DAngle::fromDeg(actor->movedir * 45));
		if (delta < nullAngle)
		{
			actor->Angles.Yaw -= DAngle::fromDeg(45);
		}
		else if (delta > nullAngle)
		{
			actor->Angles.Yaw += DAngle::fromDeg(45);
		}
	}

	// [RH] If the target is dead or a friend (and not a goal), stop chasing it.
	if (actor->target && actor->target != actor->goal && (actor->target->health <= 0 || actor->IsFriend(actor->target)))
		actor->target = nullptr;

	// [RH] Friendly monsters will consider chasing whoever hurts a player if they
	// don't already have a target.
	if (actor->flags & MF_FRIENDLY && actor->target == NULL)
	{
		player_t *player;

		if (actor->FriendPlayer != 0)
		{
			player = actor->Level->Players[actor->FriendPlayer - 1];
		}
		else
		{
			int i;
			if (!multiplayer)
			{
				i = 0;
			}
			else for (i = pr_newchasedir() & (MAXPLAYERS-1); !actor->Level->PlayerInGame(i); i = (i+1) & (MAXPLAYERS-1))
			{
			}
			player = actor->Level->Players[i];
		}
		if (player->attacker && player->attacker->health > 0 && player->attacker->flags & MF_SHOOTABLE && pr_newchasedir() < 80)
		{
			if (!(player->attacker->flags & MF_FRIENDLY) ||
				(deathmatch && actor->FriendPlayer != 0 && player->attacker->FriendPlayer != 0 &&
				actor->FriendPlayer != player->attacker->FriendPlayer))
			{
				actor->target = player->attacker;
			} 
		}
	}
	if (!actor->target || !(actor->target->flags & MF_SHOOTABLE))
	{ // look for a new target
		if (actor->target != nullptr && (actor->target->flags2 & MF2_NONSHOOTABLE))
		{
			// Target is only temporarily unshootable, so remember it.
			actor->lastenemy = actor->target;
			// Switch targets faster, since we're only changing because we can't
			// hurt our old one temporarily.
			actor->threshold = 0;
		}
		if (P_LookForPlayers (actor, !(flags & CHF_DONTLOOKALLAROUND), nullptr) && actor->target != actor->goal)
		{ // got a new target
			actor->flags7 &= ~MF7_INCHASE;
			return;
		}
		if (actor->target == nullptr)
		{
			if (flags & CHF_DONTIDLE || actor->flags & MF_FRIENDLY)
			{
				//A_Look(actor);
				if (actor->target == nullptr)
				{
					if (!dontmove) A_Wander(actor);
					actor->flags7 &= ~MF7_INCHASE;
					return;
				}
			}
			else
			{
				actor->SetIdle();
				actor->flags7 &= ~MF7_INCHASE;
				return;
			}
		}
	}
	
	// do not attack twice in a row
	if (actor->flags & MF_JUSTATTACKED)
	{
		actor->flags &= ~MF_JUSTATTACKED;
		if (!actor->isFast() && !dontmove && !(flags & CHF_NOPOSTATTACKTURN) && !(flags & CHF_STOPIFBLOCKED))
		{
			P_NewChaseDir (actor);
		}
		//Because P_TryWalk would never be reached if the actor is stopped by a blocking object,
		//need to make sure the movecount is reset, otherwise they will just keep attacking
		//over and over again.
		if (flags & CHF_STOPIFBLOCKED)
			actor->movecount = pr_trywalk() & 15;
		actor->flags7 &= ~MF7_INCHASE;
		return;
	}
	
	// [RH] Don't attack if just moving toward goal
	if (actor->target == actor->goal || (actor->flags5&MF5_CHASEGOAL && actor->goal != NULL))
	{
		AActor * savedtarget = actor->target;
		actor->target = actor->goal;
		bool result = P_CheckMeleeRange(actor);
		actor->target = savedtarget;

		if (result)
		{
			// reached the goal
			auto iterator = actor->Level->GetActorIterator(NAME_PatrolPoint, actor->goal->args[0]);
			auto specit = actor->Level->GetActorIterator(NAME_PatrolSpecial, actor->goal->tid);
			AActor *spec;

			// Execute the specials of any PatrolSpecials with the same TID
			// as the goal.
			while ( (spec = specit.Next()) )
			{
				P_ExecuteSpecial(actor->Level, spec->special, NULL, actor, false, spec->args[0],
					spec->args[1], spec->args[2], spec->args[3], spec->args[4]);
			}

			DAngle lastgoalang = actor->goal->Angles.Yaw;
			int delay;
			AActor * newgoal = iterator.Next ();
			if (newgoal != NULL && actor->goal == actor->target)
			{
				delay = newgoal->args[1];
				actor->reactiontime = delay * TICRATE + actor->Level->maptime;
			}
			else
			{
				delay = 0;
				actor->reactiontime = actor->GetDefault()->reactiontime;
				actor->Angles.Yaw = lastgoalang;		// Look in direction of last goal
			}
			if (actor->target == actor->goal) actor->target = nullptr;
			actor->flags |= MF_JUSTATTACKED;
			if (newgoal != NULL && delay != 0)
			{
				actor->flags4 |= MF4_INCOMBAT;
				actor->SetIdle();
			}
			actor->flags7 &= ~MF7_INCHASE;
			actor->goal = newgoal;
			return;
		}
		if (actor->goal == actor->target) goto nomissile;
	}

	// Strafe	(Hexen's class bosses)
	// This was the sole reason for the separate A_FastChase function but
	// it can be just as easily handled by a simple flag so the monsters
	// can take advantage of all the other enhancements of A_Chase.

	if (fastchase && !dontmove)
	{
		if (actor->FastChaseStrafeCount > 0)
		{
			actor->FastChaseStrafeCount--;
		}
		else
		{
			actor->FastChaseStrafeCount = 0;
			actor->Vel.X = actor->Vel.Y = 0;
			double dist = actor->Distance2D (actor->target);
			if (dist < CLASS_BOSS_STRAFE_RANGE)
			{
				if (pr_chase() < 100)
				{
					DAngle ang = actor->AngleTo(actor->target);
					if (pr_chase() < 128) ang += DAngle::fromDeg(90.);
					else ang -= DAngle::fromDeg(90.);
					actor->VelFromAngle(13., ang);
					actor->FastChaseStrafeCount = 3;		// strafe time
				}
			}
		}
	}

	// [RH] Scared monsters attack less frequently
	if (((actor->target->player == nullptr ||
		!((actor->target->player->cheats & CF_FRIGHTENING) || (actor->target->flags8 & MF8_FRIGHTENING))) &&
		!(actor->flags4 & MF4_FRIGHTENED)) ||
		pr_scaredycat() < 43)
	{
		// check for melee attack
		if (meleestate && P_CheckMeleeRange(actor))
		{
			if (actor->AttackSound.isvalid())
				S_Sound (actor, CHAN_WEAPON, 0, actor->AttackSound, 1, ATTN_NORM);

			actor->SetState (meleestate);
			actor->flags7 &= ~MF7_INCHASE;
			return;
		}
		
		// check for missile attack
		if (missilestate)
		{
			if (!actor->isFast() && actor->movecount)
			{
				goto nomissile;
			}
			
			if (!P_CheckMissileRange (actor))
				goto nomissile;
			
			actor->SetState (missilestate);
			actor->flags |= MF_JUSTATTACKED;
			actor->flags4 |= MF4_INCOMBAT;
			actor->flags7 &= ~MF7_INCHASE;
			return;
		}
	}

 nomissile:
	// possibly choose another target
	if ((multiplayer || actor->TIDtoHate)
		&& !actor->threshold
		&& !P_CheckSight (actor, actor->target, 0) )
	{
		bool lookForBetter = false;
		bool gotNew;
		if (actor->flags3 & MF3_NOSIGHTCHECK)
		{
			actor->flags3 &= ~MF3_NOSIGHTCHECK;
			lookForBetter = true;
		}
		AActor * oldtarget = actor->target;
		gotNew = P_LookForPlayers (actor, !(flags & CHF_DONTLOOKALLAROUND), nullptr);
		if (lookForBetter)
		{
			actor->flags3 |= MF3_NOSIGHTCHECK;
		}
		if (gotNew && actor->target != oldtarget)
		{
			actor->flags7 &= ~MF7_INCHASE;
			return; 	// got a new target
		}
	}

	//
	// chase towards player
	//

	if (actor->strafecount)
		actor->strafecount--;
	
	// class bosses don't do this when strafing
	if ((!fastchase || !actor->FastChaseStrafeCount) && !dontmove)
	{
		// CANTLEAVEFLOORPIC handling was completely missing in the non-serpent functions.
		DVector2 old = actor->Pos().XY();
		int oldgroup = actor->PrevPortalGroup;
		FTextureID oldFloor = actor->floorpic;

		// chase towards player
		if ((--actor->movecount < 0 && !(flags & CHF_NORANDOMTURN)) || (!P_SmartMove(actor) && !(flags & CHF_STOPIFBLOCKED)))
		{
			P_NewChaseDir(actor);
		}
		// if the move was illegal, reset it 
		// (copied from A_SerpentChase - it applies to everything with CANTLEAVEFLOORPIC!)
		if (actor->flags2&MF2_CANTLEAVEFLOORPIC && actor->floorpic != oldFloor )
		{
			if (P_TryMove(actor, old, false))
			{
				if (nomonsterinterpolation)
				{
					actor->Prev.X = old.X;
					actor->Prev.Y = old.Y;
					actor->PrevPortalGroup = oldgroup;
				}
			}
			if (!(flags & CHF_STOPIFBLOCKED))
				P_NewChaseDir(actor);
		}
	}
	else if (dontmove && actor->movecount > 0) actor->movecount--;
	
	// make active sound
	if (playactive && pr_chase() < 3)
	{
		actor->PlayActiveSound ();
	}

	actor->flags7 &= ~MF7_INCHASE;
}

//==========================================================================
//
// CanResurrect
//
// Checks if an actor can resurrect with another one, calling virtual script
// functions to check.
// 
//==========================================================================
// [MC] Code is almost a duplicate of CanCollideWith but with changes to
// accommodate checking of just one actor.
bool P_CanResurrect(AActor *raiser, AActor *thing)
{
	if (raiser == nullptr)
		return false;

	static unsigned VIndex = ~0u;
	if (VIndex == ~0u)
	{
		VIndex = GetVirtualIndex(RUNTIME_CLASS(AActor), "CanResurrect");
		assert(VIndex != ~0u);
	}

	VMValue params[3] = { raiser, thing, false };
	VMReturn ret;
	int retval;
	ret.IntAt(&retval);

	auto clss = raiser->GetClass();
	VMFunction *func = clss->Virtuals.Size() > VIndex ? clss->Virtuals[VIndex] : nullptr;
	if (func != nullptr)
	{
		VMCall(func, params, 3, &ret, 1);
		if (!retval) return false;
	}

	// Pointless to be running it again if it's just self.
	if (thing == nullptr || thing == raiser)
		return true;

	std::swap(params[0].a, params[1].a);
	params[2].i = true;

	// re-get for the other actor.
	clss = thing->GetClass();
	func = clss->Virtuals.Size() > VIndex ? clss->Virtuals[VIndex] : nullptr;
	if (func != nullptr)
	{
		VMCall(func, params, 3, &ret, 1);
		if (!retval) return false;
	}
	return true;
}

//==========================================================================
//
// P_CheckForResurrection (formerly part of A_VileChase)
// Check for resurrecting a body
//
//==========================================================================

bool P_CheckForResurrection(AActor* self, bool usevilestates, FState* state = nullptr, FSoundID sound = NO_SOUND)
{
	const AActor *info;
	AActor *temp;
		
	if (self->movedir != DI_NODIR)
	{
		const double absSpeed = fabs (self->Speed);
		DVector2 viletry = self->Vec2Offset(
			absSpeed * xspeed[self->movedir],
			absSpeed * yspeed[self->movedir], true);

		FPortalGroupArray check(FPortalGroupArray::PGA_Full3d);

		FMultiBlockThingsIterator it(check, self->Level, viletry.X, viletry.Y, self->Z() - 64, self->Top() + 64, 32., false, NULL);
		FMultiBlockThingsIterator::CheckResult cres;
		while (it.Next(&cres))
		{
			AActor *corpsehit = cres.thing;
			FState *raisestate = corpsehit->GetRaiseState();
			if (raisestate != NULL)
			{
				// use the current actor's radius instead of the Arch Vile's default.
				double maxdist = corpsehit->GetDefault()->radius + self->radius;

				if (fabs(corpsehit->X() - cres.Position.X) > maxdist ||
					fabs(corpsehit->Y() - cres.Position.Y) > maxdist)
					continue;			// not actually touching
				// Let's check if there are floors in between the archvile and its target

				if (corpsehit->Sector->PortalGroup != self->Sector->PortalGroup)
				{
					// if in a different section of the map, only consider possible if a line of sight exists.
					if (!P_CheckSight(self, corpsehit))
						continue;
				}
				else
				{
					sector_t *vilesec = self->Sector;
					sector_t *corpsec = corpsehit->Sector;
					// We only need to test if at least one of the sectors has a 3D floor.
					sector_t *testsec = vilesec->e->XFloor.ffloors.Size() ? vilesec :
						(vilesec != corpsec && corpsec->e->XFloor.ffloors.Size()) ? corpsec : NULL;
					if (testsec)
					{
						double zdist1, zdist2;
						if (P_Find3DFloor(testsec, corpsehit->Pos(), false, true, zdist1)
							!= P_Find3DFloor(testsec, self->Pos(), false, true, zdist2))
						{
							// Not on same floor
							if (vilesec == corpsec || fabs(zdist1 - self->Z()) > self->Height)
								continue;
						}
					}
				}

				corpsehit->Vel.X = corpsehit->Vel.Y = 0;
				// [RH] Check against real height and radius

				double oldheight = corpsehit->Height;
				double oldradius = corpsehit->radius;
				ActorFlags oldflags = corpsehit->flags;

				corpsehit->flags |= MF_SOLID;
				corpsehit->Height = corpsehit->GetDefault()->Height;
				bool check = P_CheckPosition(corpsehit, corpsehit->Pos().XY());
				corpsehit->flags = oldflags;
				corpsehit->radius = oldradius;
				corpsehit->Height = oldheight;
				if (!check || !P_CanResurrect(self, corpsehit)) continue;

				// got one!
				temp = self->target;
				self->target = corpsehit;
				A_FaceTarget(self);
				if (self->flags & MF_FRIENDLY)
				{
					// If this is a friendly Arch-Vile (which is turning the resurrected monster into its friend)
					// and the Arch-Vile is currently targetting the resurrected monster the target must be cleared.
					if (self->lastenemy == temp) self->lastenemy = nullptr;
					if (self->lastenemy == corpsehit) self->lastenemy = nullptr;
					if (temp == self->target) temp = NULL;
				}
				self->target = temp;

				// Make the state the monster enters customizable.
				if (state == nullptr) state = self->FindState(NAME_Heal);
				if (state != nullptr)
				{
					self->SetState(state);
				}
				else if (usevilestates)
				{
					// For Dehacked compatibility this has to use the Arch Vile's
					// heal state as a default if the actor doesn't define one itself.
					PClassActor *archvile = PClass::FindActor(NAME_Archvile);
					if (archvile != NULL)
					{
						self->SetState(archvile->FindState(NAME_Heal));
					}
				}
				if (sound == NO_SOUND) sound = S_FindSound("vile/raise");
				S_Sound(corpsehit, CHAN_BODY, 0, sound, 1, ATTN_IDLE);
				info = corpsehit->GetDefault();

				if (GetTranslationType(corpsehit->Translation) == TRANSLATION_Blood)
				{
					corpsehit->Translation = info->Translation; // Clean up bloodcolor translation from crushed corpses
				}
				if (self->Level->i_compatflags & COMPATF_VILEGHOSTS)
				{
					corpsehit->Height *= 4;
					// [GZ] This was a commented-out feature, so let's make use of it,
					// but only for ghost monsters so that they are visibly different.
					if (corpsehit->Height == 0)
					{
						// Make raised corpses look ghostly
						if (corpsehit->Alpha > 0.5)
						{
							corpsehit->Alpha /= 2;
						}
						// This will only work if the render style is changed as well.
						if (corpsehit->RenderStyle == LegacyRenderStyles[STYLE_Normal])
						{
							corpsehit->RenderStyle = STYLE_Translucent;
						}
					}
				}
				else
				{
					corpsehit->Height = info->Height;	// [RH] Use real mobj height
					corpsehit->radius = info->radius;	// [RH] Use real radius
				}

				corpsehit->Revive();

				// You are the Archvile's minion now, so hate what it hates
				corpsehit->CopyFriendliness(self, false);
				corpsehit->SetState(raisestate);

				return true;
			}
		}
	}
	return false;
}


// for internal use
void A_Chase(AActor *self)
{
	A_DoChase(self, false, self->MeleeState, self->MissileState, true, gameinfo.nightmarefast, false, 0);
}

void A_ChaseNative(AActor * self, int meleelabel, int missilelabel, int flags)
{
	FName meleename = ENamedName(meleelabel - 0x10000000);
	FName missilename = ENamedName(missilelabel - 0x10000000);
	if (meleename != NAME__a_chase_default || missilename != NAME__a_chase_default)
	{
		FState *melee = StateLabels.GetState(meleelabel, self->GetClass());
		FState *missile = StateLabels.GetState(missilelabel, self->GetClass());
		if ((flags & CHF_RESURRECT) && P_CheckForResurrection(self, false))
			return;

		A_DoChase(self, !!(flags&CHF_FASTCHASE), melee, missile, !(flags&CHF_NOPLAYACTIVE),
			!!(flags&CHF_NIGHTMAREFAST), !!(flags&CHF_DONTMOVE), flags & 0x3fffffff);
	}
	else // this is the old default A_Chase
	{
		A_DoChase(self, false, self->MeleeState, self->MissileState, true, gameinfo.nightmarefast, false, 0);
	}
}

DEFINE_ACTION_FUNCTION_NATIVE(AActor, A_Chase, A_ChaseNative)
{
	PARAM_SELF_PROLOGUE(AActor);
	PARAM_STATELABEL(meleelabel);
	PARAM_STATELABEL(missilelabel);
	PARAM_INT(flags);

	A_ChaseNative(self, meleelabel, missilelabel, flags);

	return 0;
}

void A_DoChaseNative(AActor * self, FState *melee, FState *missile, int flags)
{
	if ((flags & CHF_RESURRECT) && P_CheckForResurrection(self, false))
		return;
	A_DoChase(self, !!(flags&CHF_FASTCHASE), melee, missile, !(flags&CHF_NOPLAYACTIVE), !!(flags&CHF_NIGHTMAREFAST), !!(flags&CHF_DONTMOVE), flags & 0x3fffffff);
}

DEFINE_ACTION_FUNCTION_NATIVE(AActor, A_DoChase, A_DoChaseNative)
{
	PARAM_SELF_PROLOGUE(AActor);
	PARAM_STATE(melee);
	PARAM_STATE(missile);
	PARAM_INT(flags);

	A_DoChaseNative(self, melee, missile, flags);

	return 0;
}

//=============================================================================
//
// A_FaceTarget
// A_FaceMaster
// A_FaceTracer
//
//=============================================================================
enum FAF_Flags
{
	FAF_BOTTOM = 1,
	FAF_MIDDLE = 2,
	FAF_TOP = 4,
	FAF_NODISTFACTOR = 8,	// deprecated
};
void A_Face(AActor *self, AActor *other, DAngle max_turn, DAngle max_pitch, DAngle ang_offset, DAngle pitch_offset, int flags, double z_add)
{
	if (!other)
		return;

	// [RH] Andy Baker's stealth monsters
	if (self->flags & MF_STEALTH)
	{
		self->visdir = 1;
	}

	self->flags &= ~MF_AMBUSH;

	DAngle other_angle = self->AngleTo(other);
	DAngle delta = -deltaangle(self->Angles.Yaw, other_angle);

	// 0 means no limit. Also, if we turn in a single step anyways, no need to go through the algorithms.
	// It also means that there is no need to check for going past the other.
	if (max_turn != nullAngle && (max_turn < fabs(delta)))
	{
		if (delta > nullAngle)
		{
			self->Angles.Yaw -= max_turn + ang_offset;
		}
		else
		{
			self->Angles.Yaw += max_turn + ang_offset;
		}
	}
	else
	{
		self->Angles.Yaw = other_angle + ang_offset;
	}

	// [DH] Now set pitch. In order to maintain compatibility, this can be
	// disabled and is so by default.
	if (max_pitch <= DAngle::fromDeg(180.))
	{
		DVector2 dist = self->Vec2To(other);
		
		// Positioning ala missile spawning, 32 units above foot level
		double source_z = self->Z() + 32 + self->GetBobOffset();
		double target_z = other->Z() + 32 + other->GetBobOffset();

		// If the target z is above the target's head, reposition to the middle of
		// its body.		
		if (target_z >= other->Top())
		{
			target_z = other->Center();
		}
		if (source_z >= self->Top())
		{
			source_z = self->Center();
		}

		//Note there is no +32 on purpose. This is for customization sake. 
		//If one doesn't want this behavior, just don't use FAF_BOTTOM.
		if (flags & FAF_BOTTOM)
			target_z = other->Z() + other->GetBobOffset(); 
		if (flags & FAF_MIDDLE)
			target_z = other->Center() + other->GetBobOffset();
		if (flags & FAF_TOP)
			target_z = other->Top() + other->GetBobOffset();

		target_z += z_add;

		double dist_z = target_z - source_z;
		double ddist = g_sqrt(dist.X*dist.X + dist.Y*dist.Y + dist_z*dist_z);

		DAngle other_pitch = -DAngle::fromRad(g_asin(dist_z / ddist)).Normalized180();
		
		if (max_pitch != nullAngle)
		{
			if (self->Angles.Pitch > other_pitch)
			{
				max_pitch = min(max_pitch, (self->Angles.Pitch - other_pitch).Normalized360());
				self->Angles.Pitch -= max_pitch;
			}
			else
			{
				max_pitch = min(max_pitch, (other_pitch - self->Angles.Pitch).Normalized360());
				self->Angles.Pitch += max_pitch;
			}
		}
		else
		{
			self->Angles.Pitch = other_pitch;
		}
		self->Angles.Pitch += pitch_offset;
		A_Face_ShadowHandling(self, other, max_pitch, other_pitch, true);
	}
	

	A_Face_ShadowHandling(self,other,max_turn,other_angle,false);
}

void A_FaceTarget(AActor *self)
{
	A_Face(self, self->target);
}

//===========================================================================
//
// [RH] A_MonsterRail
//
// New function to let monsters shoot a railgun
//
//===========================================================================
DEFINE_ACTION_FUNCTION(AActor, A_MonsterRail)
{
	PARAM_SELF_PROLOGUE(AActor);

	if (!self->target)
		return 0;

	DAngle saved_pitch = self->Angles.Pitch;
	FTranslatedLineTarget t;

	// [RH] Andy Baker's stealth monsters
	if (self->flags & MF_STEALTH)
	{
		self->visdir = 1;
	}

	self->flags &= ~MF_AMBUSH;
		
	self->Angles.Yaw = self->AngleTo(self->target);

	self->Angles.Pitch = P_AimLineAttack (self, self->Angles.Yaw, MISSILERANGE, &t, DAngle::fromDeg(60.), 0, self->target);
	if (t.linetarget == NULL)
	{
		// We probably won't hit the target, but aim at it anyway so we don't look stupid.
		DVector2 xydiff = self->Vec2To(self->target);
		double zdiff = self->target->Center() - self->Center() - self->Floorclip;
		self->Angles.Pitch = -VecToAngle(xydiff.Length(), zdiff);
	}

	// Let the aim trail behind the player
	self->Angles.Yaw = self->AngleTo(self->target, -self->target->Vel.X * 3, -self->target->Vel.Y * 3);

	A_MonsterRail_ShadowHandling(self);

	FRailParams p;

	p.source = self;
	p.damage = self->GetMissileDamage(0, 1);
	P_RailAttack (&p);
	self->Angles.Pitch = saved_pitch;
	return 0;
}

//---------------------------------------------------------------------------
//
// PROC P_DropItem
//
//---------------------------------------------------------------------------

AActor *P_DropItem (AActor *source, PClassActor *type, int dropamount, int chance)
{
	IFVM(Actor, A_DropItem)
	{
		VMValue params[] = { source, type, dropamount, chance };
		AActor *retval;
		VMReturn ret((void**)&retval);
		VMCall(func, params, 4, &ret, 1);
		return retval;
	}
	return NULL;
}

DEFINE_ACTION_FUNCTION(AActor, A_Pain)
{
	PARAM_SELF_PROLOGUE(AActor);

	// [RH] Vary player pain sounds depending on health (ala Quake2)
	if (self->player && self->alternative == nullptr)
	{
		const char *pain_amount;
		FSoundID sfx_id = NO_SOUND;

		if (self->health < 25)
			pain_amount = "*pain25";
		else if (self->health < 50)
			pain_amount = "*pain50";
		else if (self->health < 75)
			pain_amount = "*pain75";
		else
			pain_amount = "*pain100";

		// Try for damage-specific sounds first.
		if (self->player->LastDamageType != NAME_None)
		{
			FString pain_sound = pain_amount;
			pain_sound += '-';
			pain_sound += self->player->LastDamageType.GetChars();
			sfx_id = S_FindSound(pain_sound);
			if (sfx_id == NO_SOUND)
			{
				// Try again without a specific pain amount.
				pain_sound = "*pain-";
				pain_sound += self->player->LastDamageType.GetChars();
				sfx_id = S_FindSound(pain_sound);
			}
		}
		if (sfx_id == NO_SOUND)
		{
			sfx_id = S_FindSound(pain_amount);
		}

		S_Sound (self, CHAN_VOICE, 0, sfx_id, 1, ATTN_NORM);
	}
	else if (self->PainSound.isvalid())
	{
		S_Sound (self, CHAN_VOICE, 0, self->PainSound, 1, ATTN_NORM);
	}
	return 0;
}

int CheckBossDeath (AActor *actor)
{
	int i;

	// make sure there is a player alive for victory
	for (i = 0; i < MAXPLAYERS; i++)
		if (actor->Level->PlayerInGame(i) && actor->Level->Players[i]->health > 0)
			break;
	
	if (i == MAXPLAYERS)
		return false; // no one left alive, so do not end game
	
	// Make sure all bosses are dead
	auto iterator = actor->Level->GetThinkerIterator<AActor>();
	AActor *other;

	PClassActor *cls = actor->GetClass();
	FName type = cls->GetReplacee(actor->Level)->TypeName;

	while ( (other = iterator.Next ()) )
	{
		if (other == actor)
			continue;

		PClassActor *ocls = other->GetClass();
		FName otype = ocls->GetReplacee(other->Level)->TypeName;

		if ((other->health > 0 || (other->flags & MF_ICECORPSE))
			&& (ocls == cls || otype == type))
		{ // Found a living boss
		  // [RH] Frozen bosses don't count as dead until they shatter
			return false;
		}
		
	}
	// The boss death is good
	return true;
}

//
// A_BossDeath
// Possibly trigger special effects if on a boss level
//
void A_BossDeath(AActor *self)
{
	FName mytype = self->GetClass()->TypeName;

	// Ugh...
	auto replacee = self->GetClass()->GetReplacee(self->Level);
	FName type = replacee->TypeName;
	int flags8 = self->flags8;
	
	if (type != mytype) flags8 |= ((AActor*)replacee->Defaults)->flags8;
	
	
	// Do generic special death actions first
	bool checked = false;
	auto Level = self->Level;
	for (unsigned i = 0; i < Level->info->specialactions.Size(); i++)
	{
		FSpecialAction *sa = &Level->info->specialactions[i];

		if (type == sa->Type || mytype == sa->Type)
		{
			if (!checked && !CheckBossDeath(self))
			{
				return;
			}
			checked = true;

			if (sa->Action & 0x40000000)
			{
				// This is a Doom format special from UMAPINFO. It must be translated here, because at parsing time there is not sufficient context to do it.
				maplinedef_t ml;
				ml.special = sa->Action & 0x3fffffff;
				ml.tag = sa->Args[0];
				line_t line;
				self->Level->TranslateLineDef(&line, &ml);
				P_ExecuteSpecial(self->Level, line.special, nullptr, self, false, line.args[0], line.args[1], line.args[2], line.args[3], line.args[4]);
			}
			else
			{
				P_ExecuteSpecial(self->Level, sa->Action, nullptr, self, false, sa->Args[0], sa->Args[1], sa->Args[2], sa->Args[3], sa->Args[4]);
			}
		}
	}

	// [RH] These all depend on the presence of level flags now
	//		rather than being hard-coded to specific levels/episodes.

	if (((Level->flags & (LEVEL_MAP07SPECIAL|
						LEVEL_BRUISERSPECIAL|
						LEVEL_CYBORGSPECIAL|
						LEVEL_SPIDERSPECIAL|
						LEVEL_HEADSPECIAL|
						LEVEL_MINOTAURSPECIAL|
						LEVEL_SORCERER2SPECIAL)) == 0) &&
		((Level->flags3 & (LEVEL3_E1M8SPECIAL | LEVEL3_E2M8SPECIAL | LEVEL3_E3M8SPECIAL | LEVEL3_E4M8SPECIAL | LEVEL3_E4M6SPECIAL)) == 0))
		return;

	if ((Level->i_compatflags & COMPATF_ANYBOSSDEATH) || ( // [GZ] Added for UAC_DEAD
		((Level->flags & LEVEL_MAP07SPECIAL) && (flags8 & (MF8_MAP07BOSS1|MF8_MAP07BOSS2))) ||
		((Level->flags & LEVEL_BRUISERSPECIAL) && (type == NAME_BaronOfHell)) ||
		((Level->flags & LEVEL_CYBORGSPECIAL) && (type == NAME_Cyberdemon)) ||
		((Level->flags & LEVEL_SPIDERSPECIAL) && (type == NAME_SpiderMastermind)) ||
		((Level->flags & LEVEL_HEADSPECIAL) && (type == NAME_Ironlich)) ||
		((Level->flags & LEVEL_MINOTAURSPECIAL) && (type == NAME_Minotaur)) ||
		((Level->flags & LEVEL_SORCERER2SPECIAL) && (type == NAME_Sorcerer2)) ||
		((Level->flags3 & LEVEL3_E1M8SPECIAL) && (flags8 & MF8_E1M8BOSS)) ||
		((Level->flags3 & LEVEL3_E2M8SPECIAL) && (flags8 & MF8_E2M8BOSS)) ||
		((Level->flags3 & LEVEL3_E3M8SPECIAL) && (flags8 & MF8_E3M8BOSS)) ||
		((Level->flags3 & LEVEL3_E4M8SPECIAL) && (flags8 & MF8_E4M8BOSS)) ||
		((Level->flags3 & LEVEL3_E4M6SPECIAL) && (flags8 & MF8_E4M6BOSS))
		))
		;
	else
		return;

	if (!CheckBossDeath (self))
	{
		return;
	}

	// victory!
	if (Level->flags & LEVEL_SPECKILLMONSTERS)
	{ // Kill any remaining monsters
		Level->Massacre ();
	}
	if (Level->flags & LEVEL_MAP07SPECIAL)
	{
		// samereplacement will only be considered if both Fatso and Arachnotron are flagged as MAP07 bosses and the current monster maps to one of them.
		PClassActor * fatso = PClass::FindActor(NAME_Fatso);
		PClassActor * arachnotron = PClass::FindActor(NAME_Arachnotron);
		bool samereplacement = (type == NAME_Fatso || type == NAME_Arachnotron) && 
			fatso && arachnotron && 
			(GetDefaultByType(fatso)->flags8 & MF8_MAP07BOSS1) && 
			(GetDefaultByType(arachnotron)->flags8 & MF8_MAP07BOSS2) &&
			fatso->GetReplacement(Level) == arachnotron->GetReplacement(Level);

		if ((flags8 & MF8_MAP07BOSS1) || samereplacement)
		{
			Level->EV_DoFloor (DFloor::floorLowerToLowest, NULL, 666, 1., 0, -1, 0, false);
		}
		
		if ((flags8 & MF8_MAP07BOSS2) || samereplacement)
		{
			Level->EV_DoFloor (DFloor::floorRaiseByTexture, NULL, 667, 1., 0, -1, 0, false);
		}
		return;
	}
	else
	{
		switch (Level->flags & LEVEL_SPECACTIONSMASK)
		{
		case LEVEL_SPECLOWERFLOOR:
			Level->EV_DoFloor (DFloor::floorLowerToLowest, NULL, 666, 1., 0, -1, 0, false);
			return;
		
		case LEVEL_SPECLOWERFLOORTOHIGHEST:
			Level->EV_DoFloor (DFloor::floorLowerToHighest, NULL, 666, 1., 0, -1, 0, false);
			return;
		
		case LEVEL_SPECOPENDOOR:
			Level->EV_DoDoor (DDoor::doorOpen, NULL, NULL, 666, 8., 0, 0, 0);
			return;
		}
	}

	// [RH] If noexit, then don't end the Level->
	if ((deathmatch || alwaysapplydmflags) && (dmflags & DF_NO_EXIT))
		return;

	Level->ExitLevel (0, false);
}

//----------------------------------------------------------------------------
//
// PROC P_Massacre
//
// Kills all monsters.
//
//----------------------------------------------------------------------------

int FLevelLocals::Massacre (bool baddies, FName cls)
{
	// jff 02/01/98 'em' cheat - kill all monsters
	// partially taken from Chi's .46 port
	//
	// killough 2/7/98: cleaned up code and changed to use dprintf;
	// fixed lost soul bug (LSs left behind when PEs are killed)

	int killcount = 0;
	AActor *actor;
	auto iterator = GetThinkerIterator<AActor>(cls);

	while ( (actor = iterator.Next ()) )
	{
		if (!(actor->flags2 & MF2_DORMANT) && (actor->flags3 & MF3_ISMONSTER) && (!baddies || !(actor->flags & MF_FRIENDLY)))
		{
			killcount += actor->Massacre();
		}
	}
	return killcount;
}

DEFINE_ACTION_FUNCTION(AActor, A_ClassBossHealth)
{
	PARAM_SELF_PROLOGUE(AActor);
	if (multiplayer && !deathmatch)		// co-op only
	{
		if (!self->special1)
		{
			self->health *= 5;
			self->special1 = true;   // has been initialized
		}
	}
	return 0;
}
