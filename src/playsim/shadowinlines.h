#pragma once

#include "actor.h"
#include "r_defs.h"
#include "m_random.h"

//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//		Handling of MF_SHADOW related code for attack and aiming functions.
//
//-----------------------------------------------------------------------------


// RNG VARIABLES ------------------------------------------------
extern FRandom pr_spawnmissile;
extern FRandom pr_facetarget;
extern FRandom pr_railface;
extern FRandom pr_crailgun;
inline FRandom pr_shadowaimz("VerticalShadowAim");

//==========================================================================
//
// Generic checks
//
//==========================================================================

// Generic actor sight check data.
struct SightCheckData
{
	AActor* HitShadow;
	AActor* HitSightBlocker;
};

inline ETraceStatus CheckForShadowBlockers(FTraceResults& res, void* userdata)
{
	SightCheckData* output = (SightCheckData*)userdata;
	if (res.HitType == TRACE_HitActor && res.Actor && (res.Actor->flags9 & MF9_SHADOWBLOCK))
	{
		output->HitShadow = res.Actor;
		return TRACE_Stop;
	}

	if (res.HitType != TRACE_HitActor)
	{
		return TRACE_Stop;
	}

	return TRACE_Continue;
}

// [inkoalawetrust] Check if an MF9_SHADOWBLOCK actor is standing between t1 and t2.
inline AActor* P_CheckForShadowBlock(AActor* t1, AActor* t2, DVector3 pos, double& penaltyFactor)
{
	FTraceResults result;
	SightCheckData ShadowCheck;
	ShadowCheck.HitShadow = nullptr;
	DVector3 dir;
	double dist;
	if (t2)
	{
		dir = t1->Vec3To(t2);
		dist = dir.Length();
	}
	//No second actor, fall back to shooting at facing direction.
	else
	{
		dir = DRotator(-(t1->Angles.Pitch), t1->Angles.Yaw, t1->Angles.Yaw);
		dist = 65536.0; //Arbitrary large value.
	}

	Trace(pos, t1->Sector, dir, dist, ActorFlags::FromInt(0xFFFFFFFF), ML_BLOCKEVERYTHING, t1, result, 0, CheckForShadowBlockers, &ShadowCheck);

	//Use the penalty factor of the shadowblocker that was hit. Otherwise, use the factor passed by PerformShadowChecks().
	if (ShadowCheck.HitShadow)
	{
		penaltyFactor = ShadowCheck.HitShadow->ShadowPenaltyFactor;
	}

	return ShadowCheck.HitShadow;
}

inline bool AffectedByShadows(AActor* self)
{
	return (!(self->flags6 & MF6_SEEINVISIBLE) || self->flags9 & MF9_SHADOWAIM);
}

inline AActor* CheckForShadows(AActor* self, AActor* other, DVector3 pos, double& penaltyFactor)
{
	if ((other && (other->flags & MF_SHADOW)) || (self->flags9 & MF9_DOSHADOWBLOCK))
	{
		AActor* shadowBlock = P_CheckForShadowBlock(self, other, pos, penaltyFactor);
		return shadowBlock ? shadowBlock : other;
	}
	return nullptr;
}

inline AActor* PerformShadowChecks(AActor* self, AActor* other, DVector3 pos, double& penaltyFactor)
{
    if (other != nullptr) penaltyFactor = other->ShadowPenaltyFactor; //Use target penalty factor by default.
    else penaltyFactor = 1.0;
    return AffectedByShadows(self) ? CheckForShadows(self, other, pos, penaltyFactor) : nullptr;
}

//==========================================================================
//
// Function-specific inlines.
//
//==========================================================================

inline void P_SpawnMissileXYZ_ShadowHandling(AActor* source, AActor* target, AActor* missile, DVector3 pos)
{
	double penaltyFactor;
	// invisible target: rotate velocity vector in 2D
	// [RC] Now monsters can aim at invisible player as if they were fully visible.
	if (PerformShadowChecks(source, target, pos, penaltyFactor))
	{
		DAngle an = DAngle::fromDeg(pr_spawnmissile.Random2() * (22.5 / 256)) * source->ShadowAimFactor * penaltyFactor;
		double c = an.Cos();
		double s = an.Sin();

		double newx = missile->Vel.X * c - missile->Vel.Y * s;
		double newy = missile->Vel.X * s + missile->Vel.Y * c;

		missile->Vel.X = newx;
		missile->Vel.Y = newy;

		if (source->flags9 & MF9_SHADOWAIMVERT)
		{
			DAngle pitch = DAngle::fromDeg(pr_spawnmissile.Random2() * (22.5 / 256)) * source->ShadowAimFactor * penaltyFactor;
			double newz = -pitch.Sin() * missile->Speed;
			missile->Vel.Z = newz;
		}
	}
	return;
}

//P_SpawnMissileZAimed uses a local variable for the angle it passes on.
inline DAngle P_SpawnMissileZAimed_ShadowHandling(AActor* source, AActor* target, double& vz, double speed, DVector3 pos)
{
	double penaltyFactor;
	if (PerformShadowChecks(source, target, pos, penaltyFactor))
	{
		if (source->flags9 & MF9_SHADOWAIMVERT)
		{
			DAngle pitch = DAngle::fromDeg(pr_spawnmissile.Random2() * (16. / 360.)) * source->ShadowAimFactor * penaltyFactor;
			vz += -pitch.Sin() * speed; //Modify the Z velocity pointer that is then passed to P_SpawnMissileAngleZSpeed.
		}
		return DAngle::fromDeg(pr_spawnmissile.Random2() * (16. / 360.)) * source->ShadowAimFactor * penaltyFactor;
	}
	return nullAngle;
}

inline void A_Face_ShadowHandling(AActor* self, AActor* other, DAngle max_turn, DAngle other_angle, bool vertical)
{
	double penaltyFactor;
	if (!vertical)
	{
		// This will never work well if the turn angle is limited.
		if (max_turn == nullAngle && (self->Angles.Yaw == other_angle) && PerformShadowChecks(self, other, self->PosAtZ(self->Center()), penaltyFactor))
		{
			self->Angles.Yaw += DAngle::fromDeg(pr_facetarget.Random2() * (45 / 256.)) * self->ShadowAimFactor * penaltyFactor;
		}
	}
	else
	{
		//Randomly offset the pitch when looking at shadows.
		if (self->flags9 & MF9_SHADOWAIMVERT && max_turn == nullAngle && (self->Angles.Pitch == other_angle) && PerformShadowChecks(self, other, self->PosAtZ(self->Center()), penaltyFactor))
		{
			self->Angles.Pitch += DAngle::fromDeg(pr_facetarget.Random2() * (45 / 256.)) * self->ShadowAimFactor * penaltyFactor;
		}
	}
	return;
}

inline void A_MonsterRail_ShadowHandling(AActor* self)
{
	double penaltyFactor;
	double shootZ = self->Center() - self->FloatSpeed - self->Floorclip; // The formula P_RailAttack uses, minus offset_z since A_MonsterRail doesn't use it.
	
	if (PerformShadowChecks(self, self->target, self->PosAtZ(shootZ), penaltyFactor))
	{
		self->Angles.Yaw += DAngle::fromDeg(pr_railface.Random2() * 45. / 256) * self->ShadowAimFactor * penaltyFactor;
		if (self->flags9 & MF9_SHADOWAIMVERT)
			self->Angles.Pitch += DAngle::fromDeg(pr_railface.Random2() * 45. / 256) * self->ShadowAimFactor * penaltyFactor;
	}
	return;
}

//Also passes parameters to determine a firing position for the SHADOWBLOCK check.
inline void A_CustomRailgun_ShadowHandling(AActor* self, double spawnofs_xy, double spawnofs_z, DAngle spread_xy, int flags)
{
	double penaltyFactor;
	// [inkoalawetrust] The exact formula P_RailAttack uses to determine where the railgun trace should spawn from.
	DVector2 shootXY = (self->Vec2Angle(spawnofs_xy, (self->Angles.Yaw + spread_xy) - DAngle::fromDeg(90.)));
	double shootZ = self->Center() - self->FloatSpeed + spawnofs_z - self->Floorclip;
	if (flags & 16) shootZ += self->AttackOffset(); //16 is RGF_CENTERZ 
	DVector3 checkPos;
	checkPos.X = shootXY.X;
	checkPos.Y = shootXY.Y;
	checkPos.Z = shootZ;

	if (PerformShadowChecks(self, self->target, checkPos, penaltyFactor))
	{
		self->Angles.Yaw += DAngle::fromDeg(pr_crailgun.Random2() * (45. / 256.)) * self->ShadowAimFactor * penaltyFactor;
		if (self->flags9 & MF9_SHADOWAIMVERT)
		{
			self->Angles.Pitch += DAngle::fromDeg(pr_crailgun.Random2() * (45. / 256.)) * self->ShadowAimFactor * penaltyFactor;
		}
	}
	return;
}

//If anything is returned, then AimLineAttacks' result pitch is changed to that value.
inline DAngle P_AimLineAttack_ShadowHandling(AActor*source, AActor* target, AActor* linetarget, double shootZ)
{
	double penaltyFactor;
	AActor* mo;
	if (target)
		mo = target;
	else
		mo = linetarget;

	// [inkoalawetrust] Randomly offset the vertical aim of monsters. Roughly uses the SSG vertical spread.
	if (source->player == NULL && source->flags9 & MF9_SHADOWAIMVERT && PerformShadowChecks (source, mo, source->PosAtZ (shootZ), penaltyFactor))
	{
		if (linetarget)
			return DAngle::fromDeg(pr_shadowaimz.Random2() * (28.388 / 256.)) * source->ShadowAimFactor * penaltyFactor; //Change the autoaims' pitch to this.
		else
			source->Angles.Pitch = DAngle::fromDeg(pr_shadowaimz.Random2() * (28.388 / 256.)) * source->ShadowAimFactor * penaltyFactor;
	}
	return nullAngle;
}

//A_WolfAttack directly harms the target instead of firing a hitscan or projectile. So it handles shadows by lowering the chance of harming the target.
inline bool A_WolfAttack_ShadowHandling(AActor* self)
{
	double p; //Does nothing.
	return (PerformShadowChecks(self, self->target, self->PosAtZ(self->Center()), p));
}