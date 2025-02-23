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
//		Floor animation: raising stairs.
//
//-----------------------------------------------------------------------------

#include "doomdef.h"
#include "p_local.h"
#include "p_lnspec.h"
#include "s_sound.h"
#include "s_sndseq.h"
#include "doomstat.h"
#include "r_state.h"
#include "serializer.h"
#include "serialize_obj.h"
#include "p_3dmidtex.h"
#include "p_spec.h"
#include "r_data/r_interpolate.h"
#include "g_levellocals.h"
#include "vm.h"
#include "r_utility.h"
#include "actorinlines.h"

//==========================================================================
//
//
//
//==========================================================================

static void StartFloorSound (sector_t *sec)
{
	if (sec->Flags & SECF_SILENTMOVE) return;

	if (sec->seqType >= 0)
	{
		SN_StartSequence (sec, CHAN_FLOOR, sec->seqType, SEQ_PLATFORM, 0);
	}
	else if (sec->SeqName != NAME_None)
	{
		SN_StartSequence (sec, CHAN_FLOOR, sec->SeqName, 0);
	}
	else
	{
		SN_StartSequence (sec, CHAN_FLOOR, "Floor", 0);
	}
}


//==========================================================================
//
// FLOORS
//
//==========================================================================

IMPLEMENT_CLASS(DFloor, false, false)

void DFloor::Serialize(FSerializer &arc)
{
	Super::Serialize (arc);
	arc.Enum("type", m_Type)
		("crush", m_Crush)
		("direction", m_Direction)
		("newspecial", m_NewSpecial)
		("texture", m_Texture)
		("floordestdist", m_FloorDestDist)
		("speed", m_Speed)
		("resetcount", m_ResetCount)
		("orgdist", m_OrgDist)
		("delay", m_Delay)
		("pausetime", m_PauseTime)
		("steptime", m_StepTime)
		("persteptime", m_PerStepTime)
		("crushmode", m_Hexencrush)
		("instant", m_Instant);
}

DEFINE_FIELD(DFloor, m_Type)
DEFINE_FIELD(DFloor, m_Crush)
DEFINE_FIELD(DFloor, m_Direction)
DEFINE_FIELD(DFloor, m_NewSpecial)
DEFINE_FIELD(DFloor, m_Texture)
DEFINE_FIELD(DFloor, m_FloorDestDist)
DEFINE_FIELD(DFloor, m_Speed)
DEFINE_FIELD(DFloor, m_ResetCount)
DEFINE_FIELD(DFloor, m_OrgDist)
DEFINE_FIELD(DFloor, m_Delay)
DEFINE_FIELD(DFloor, m_PauseTime)
DEFINE_FIELD(DFloor, m_StepTime)
DEFINE_FIELD(DFloor, m_PerStepTime)
DEFINE_FIELD(DFloor, m_Hexencrush)
DEFINE_FIELD(DFloor, m_Instant)

//==========================================================================
//
// MOVE A FLOOR TO ITS DESTINATION (UP OR DOWN)
//
//==========================================================================

void DFloor::Tick ()
{
	EMoveResult res;

	// [RH] Handle resetting stairs
	if (m_Type == buildStair || m_Type == waitStair)
	{
		if (m_ResetCount)
		{
			if (--m_ResetCount == 0)
			{
				m_Type = resetStair;
				m_Direction = (m_Direction > 0) ? -1 : 1;
				m_FloorDestDist = m_OrgDist;
			}
		}
		if (m_PauseTime)
		{
			m_PauseTime--;
			return;
		}
		else if (m_StepTime)
		{
			if (--m_StepTime == 0)
			{
				m_PauseTime = m_Delay;
				m_StepTime = m_PerStepTime;
			}
		}
	}

	if (m_Type == waitStair)
		return;

	res = m_Sector->MoveFloor (m_Speed, m_FloorDestDist, m_Crush, m_Direction, m_Hexencrush, m_Instant);
	
	if (res == EMoveResult::pastdest)
	{
		SN_StopSequence (m_Sector, CHAN_FLOOR);

		if (m_Type == buildStair)
			m_Type = waitStair;

		if (m_Type != waitStair || m_ResetCount == 0)
		{
			if (m_Direction == 1)
			{
				switch (m_Type)
				{
				case donutRaise:
				case genFloorChgT:
				case genFloorChg0:
					m_Sector->SetSpecial(&m_NewSpecial);
					[[fallthrough]];
				case genFloorChg:
					m_Sector->SetTexture(sector_t::floor, m_Texture);
					break;
				default:
					break;
				}
			}
			else if (m_Direction == -1)
			{
				switch (m_Type)
				{
				case floorLowerAndChange:
				case genFloorChgT:
				case genFloorChg0:
					m_Sector->SetSpecial(&m_NewSpecial);
					[[fallthrough]];
				case genFloorChg:
					m_Sector->SetTexture(sector_t::floor, m_Texture);
					break;
				default:
					break;
				}
			}

			m_Sector->floordata = nullptr; //jff 2/22/98
			StopInterpolation();

			//jff 2/26/98 implement stair retrigger lockout while still building
			// note this only applies to the retriggerable generalized stairs

			if (m_Sector->stairlock == -2)		// if this sector is stairlocked
			{
				sector_t *sec = m_Sector;
				sec->stairlock = -1;				// thinker done, promote lock to -1

				while (sec->prevsec != -1 && Level->sectors[sec->prevsec].stairlock != -2)
					sec = &Level->sectors[sec->prevsec];	// search for a non-done thinker
				if (sec->prevsec == -1)				// if all thinkers previous are done
				{
					sec = m_Sector;			// search forward
					while (sec->nextsec != -1 && Level->sectors[sec->nextsec].stairlock != -2)
						sec = &Level->sectors[sec->nextsec];
					if (sec->nextsec == -1)			// if all thinkers ahead are done too
					{
						while (sec->prevsec != -1)	// clear all locks
						{
							sec->stairlock = 0;
							sec = &Level->sectors[sec->prevsec];
						}
						sec->stairlock = 0;
					}
				}
			}

			Destroy ();
		}
	}
}

//==========================================================================
//
//
//
//==========================================================================

void DFloor::SetFloorChangeType (sector_t *sec, int change)
{
	m_Texture = sec->GetTexture(sector_t::floor);

	switch (change & 3)
	{
	case 1:
		m_NewSpecial = {};
		m_Type = DFloor::genFloorChg0;
		break;
	case 2:
		m_Type = DFloor::genFloorChg;
		break;
	case 3:
		sec->GetSpecial(&m_NewSpecial);
		m_Type = DFloor::genFloorChgT;
		break;
	}
}

//==========================================================================
//
//
//
//==========================================================================

void DFloor::StartFloorSound ()
{
	::StartFloorSound (m_Sector);
}

//==========================================================================
//
//
//
//==========================================================================

void DFloor::Construct(sector_t *sec)
{
	Super::Construct(sec);
}

//==========================================================================
//
//
//
//==========================================================================

bool FLevelLocals::CreateFloor(sector_t *sec, DFloor::EFloor floortype, line_t *line,
	double speed, double height, int crush, int change, bool hexencrush, bool hereticlower)
{
	bool 		rtn;
	DFloor*		floor;
	double		ceilingheight;
	double		newheight;
	vertex_t	*spot, *spot2;

	// ALREADY MOVING?	IF SO, KEEP GOING...
	if (sec->PlaneMoving(sector_t::floor))
	{
		return false;
	}

	// new floor thinker
	rtn = true;
	floor = CreateThinker<DFloor>(sec);
	floor->m_Type = floortype;
	floor->m_Crush = crush;
	floor->m_Hexencrush = hexencrush;
	floor->m_Speed = speed;
	floor->m_ResetCount = 0;				// [RH]
	floor->m_OrgDist = sec->floorplane.fD();	// [RH]
	floor->m_Instant = false;

	switch (floortype)
	{
	case DFloor::floorLowerToHighest:
		floor->m_Direction = -1;
		newheight = FindHighestFloorSurrounding(sec, &spot);
		floor->m_FloorDestDist = sec->floorplane.PointToDist(spot, newheight);
		// [RH] DOOM's turboLower type did this. I've just extended it
		//		to be applicable to all LowerToHighest types.
		if (hereticlower || floor->m_FloorDestDist != sec->floorplane.fD())
			floor->m_FloorDestDist = sec->floorplane.PointToDist(spot, newheight + height);
		break;

	case DFloor::floorLowerToLowest:
		floor->m_Direction = -1;
		newheight = FindLowestFloorSurrounding(sec, &spot);
		floor->m_FloorDestDist = sec->floorplane.PointToDist(spot, newheight);
		break;

	case DFloor::floorLowerToNearest:
		//jff 02/03/30 support lowering floor to next lowest floor
		floor->m_Direction = -1;
		newheight = FindNextLowestFloor(sec, &spot);
		floor->m_FloorDestDist = sec->floorplane.PointToDist(spot, newheight);
		break;

	case DFloor::floorLowerInstant:
		floor->m_Speed = height;
		[[fallthrough]];
	case DFloor::floorLowerByValue:
		floor->m_Direction = -1;
		newheight = sec->CenterFloor() - height;
		floor->m_FloorDestDist = sec->floorplane.PointToDist(sec->centerspot, newheight);
		break;

	case DFloor::floorRaiseInstant:
		floor->m_Speed = height;
		[[fallthrough]];
	case DFloor::floorRaiseByValue:
		floor->m_Direction = 1;
		newheight = sec->CenterFloor() + height;
		floor->m_FloorDestDist = sec->floorplane.PointToDist(sec->centerspot, newheight);
		break;

	case DFloor::floorMoveToValue:
		FindHighestFloorPoint(sec, &spot);
		floor->m_FloorDestDist = sec->floorplane.PointToDist(spot, height);
		floor->m_Direction = (floor->m_FloorDestDist > sec->floorplane.fD()) ? -1 : 1;
		break;

	case DFloor::floorRaiseAndCrushDoom:
		height = 8;
		[[fallthrough]];
	case DFloor::floorRaiseToLowestCeiling:
		floor->m_Direction = 1;
		newheight = FindLowestCeilingSurrounding(sec, &spot) - height;
		ceilingheight = FindLowestCeilingPoint(sec, &spot2);
		floor->m_FloorDestDist = sec->floorplane.PointToDist(spot, newheight);
		if (sec->floorplane.ZatPointDist(spot2, floor->m_FloorDestDist) > ceilingheight)
			floor->m_FloorDestDist = sec->floorplane.PointToDist(spot2,	ceilingheight - height);
		break;

	case DFloor::floorRaiseToHighest:
		floor->m_Direction = 1;
		newheight = FindHighestFloorSurrounding(sec, &spot);
		floor->m_FloorDestDist = sec->floorplane.PointToDist(spot, newheight);
		break;

	case DFloor::floorRaiseToNearest:
		floor->m_Direction = 1;
		newheight = FindNextHighestFloor(sec, &spot);
		floor->m_FloorDestDist = sec->floorplane.PointToDist(spot, newheight);
		break;

	case DFloor::floorRaiseToLowest:
		floor->m_Direction = 1;
		newheight = FindLowestFloorSurrounding(sec, &spot);
		floor->m_FloorDestDist = sec->floorplane.PointToDist(spot, newheight);
		break;

	case DFloor::floorRaiseAndCrush:
		floor->m_Direction = 1;
		newheight = FindLowestCeilingPoint(sec, &spot) - 8;
		floor->m_FloorDestDist = sec->floorplane.PointToDist(spot, newheight);
		break;

	case DFloor::floorRaiseToCeiling:
		floor->m_Direction = 1;
		newheight = FindLowestCeilingPoint(sec, &spot) - height;
		floor->m_FloorDestDist = sec->floorplane.PointToDist(spot, newheight);
		break;

	case DFloor::floorLowerToLowestCeiling:
		floor->m_Direction = -1;
		newheight = FindLowestCeilingSurrounding(sec, &spot);
		floor->m_FloorDestDist = sec->floorplane.PointToDist(spot, newheight);
		break;

	case DFloor::floorLowerByTexture:
		floor->m_Direction = -1;
		newheight = sec->CenterFloor() - FindShortestTextureAround(sec);
		floor->m_FloorDestDist = sec->floorplane.PointToDist(sec->centerspot, newheight);
		break;

	case DFloor::floorLowerToCeiling:
		// [RH] Essentially instantly raises the floor to the ceiling
		floor->m_Direction = -1;
		newheight = FindLowestCeilingPoint(sec, &spot) - height;
		floor->m_FloorDestDist = sec->floorplane.PointToDist(spot, newheight);
		break;

	case DFloor::floorRaiseByTexture:
		floor->m_Direction = 1;
		// [RH] Use P_FindShortestTextureAround from BOOM to do this
		//		since the code is identical to what was here. (Oddly
		//		enough, BOOM preserved the code here even though it
		//		also had this function.)
		newheight = sec->CenterFloor() + FindShortestTextureAround(sec);
		floor->m_FloorDestDist = sec->floorplane.PointToDist(sec->centerspot, newheight);
		break;

	case DFloor::floorRaiseAndChange:
		floor->m_Direction = 1;
		newheight = sec->CenterFloor() + height;
		floor->m_FloorDestDist = sec->floorplane.PointToDist(sec->centerspot, newheight);
		if (line != NULL)
		{
			FTextureID oldpic = sec->GetTexture(sector_t::floor);
			sec->SetTexture(sector_t::floor, line->frontsector->GetTexture(sector_t::floor));
			sec->TransferSpecial(line->frontsector);
		}
		else
		{
			sec->ClearSpecial();
		}
		break;

	case DFloor::floorLowerAndChange:
		floor->m_Direction = -1;
		newheight = FindLowestFloorSurrounding(sec, &spot);
		floor->m_FloorDestDist = sec->floorplane.PointToDist(spot, newheight);
		floor->m_Texture = sec->GetTexture(sector_t::floor);
		// jff 1/24/98 make sure floor->m_NewSpecial gets initialized
		// in case no surrounding sector is at floordestheight
		sec->GetSpecial(&floor->m_NewSpecial);

		//jff 5/23/98 use model subroutine to unify fixes and handling
		sector_t *modelsec;
		modelsec = FindModelFloorSector(sec, newheight);
		if (modelsec != NULL)
		{
			floor->m_Texture = modelsec->GetTexture(sector_t::floor);
			modelsec->GetSpecial(&floor->m_NewSpecial);
		}
		break;

	default:
		break;
	}

	// Do not interpolate instant movement floors.
	bool silent = false;

	if ((floor->m_Direction > 0 && floor->m_FloorDestDist > sec->floorplane.fD()) ||	// moving up but going down
		(floor->m_Direction < 0 && floor->m_FloorDestDist < sec->floorplane.fD()) ||	// moving down but going up
		(floor->m_Speed >= fabs(sec->floorplane.fD() - floor->m_FloorDestDist)))	// moving in one step
	{
		floor->StopInterpolation(true);
		floor->m_Instant = true;

		// [Graf Zahl]
		// Don't make sounds for instant movement hacks but make an exception for
		// switches that activate their own back side. 
		if (!(sec->Level->i_compatflags & COMPATF_SILENT_INSTANT_FLOORS))
		{
			if (!line || !(line->activation & (SPAC_Use | SPAC_Push)) || line->backsector != sec)
				silent = true;
		}
	}
	if (!silent) floor->StartFloorSound();

	if (change & 3)
	{
		// [RH] Need to do some transferring
		if (change & 4)
		{
			// Numeric model change
			sector_t *modelsec;

			modelsec = (floortype == DFloor::floorRaiseToLowestCeiling ||
				floortype == DFloor::floorLowerToLowestCeiling ||
				floortype == DFloor::floorRaiseToCeiling ||
				floortype == DFloor::floorLowerToCeiling) ?
				FindModelCeilingSector(sec, -floor->m_FloorDestDist) :
				FindModelFloorSector(sec, -floor->m_FloorDestDist);

			if (modelsec != NULL)
			{
				floor->SetFloorChangeType(modelsec, change);
			}
		}
		else if (line)
		{
			// Trigger model change
			floor->SetFloorChangeType(line->frontsector, change);
		}
	}
	return true;
}

DEFINE_ACTION_FUNCTION(FLevelLocals, CreateFloor)
{
	PARAM_SELF_STRUCT_PROLOGUE(FLevelLocals);
	PARAM_POINTER_NOT_NULL(sec, sector_t);
	PARAM_INT(floortype);
	PARAM_POINTER(ln, line_t);
	PARAM_FLOAT(speed);
	PARAM_FLOAT(height);
	PARAM_INT(crush);
	PARAM_INT(change);
	PARAM_BOOL(hereticlower);
	PARAM_BOOL(hexencrush);
	ACTION_RETURN_BOOL(self->CreateFloor(sec, (DFloor::EFloor)floortype, ln, speed, height, crush, change, hexencrush, hereticlower));
}

//==========================================================================
//
// HANDLE FLOOR TYPES
// [RH] Added tag, speed, height, crush, change params.
// This functions starts too many different things.
//
//==========================================================================

bool FLevelLocals::EV_DoFloor (DFloor::EFloor floortype, line_t *line, int tag,
				 double speed, double height, int crush, int change, bool hexencrush, bool hereticlower)
{
	int 		secnum;
	bool 		rtn = false;

	// check if a manual trigger; if so do just the sector on the backside
	auto it = GetSectorTagIterator(tag, line);
	while ((secnum = it.Next()) >= 0)
	{
		rtn |= CreateFloor(&sectors[secnum], floortype, line, speed, height, crush, change, hexencrush, hereticlower);
	}
	return rtn;
}

//==========================================================================
//
// [RH]
// EV_FloorCrushStop
// Stop a floor from crushing!
//
//==========================================================================

bool FLevelLocals::EV_FloorCrushStop (int tag, line_t *line)
{
	int secnum;
	auto it = GetSectorTagIterator(tag, line);
	while ((secnum = it.Next()) >= 0)
	{
		sector_t *sec = &sectors[secnum];

		if (sec->floordata && sec->floordata->IsKindOf (RUNTIME_CLASS(DFloor)) &&
			barrier_cast<DFloor *>(sec->floordata)->m_Type == DFloor::floorRaiseAndCrush)
		{
			SN_StopSequence (sec, CHAN_FLOOR);
			sec->floordata->Destroy ();
			sec->floordata = nullptr;
		}
	}
	return true;
}

// same as above but stops any floor mover that was active on the given sector.
bool FLevelLocals::EV_StopFloor(int tag, line_t *line)
{
	int sec;
	auto it = GetSectorTagIterator(tag, line);
	while ((sec = it.Next()) >= 0)
	{
		if (sectors[sec].floordata)
		{
			SN_StopSequence(&sectors[sec], CHAN_FLOOR);
			sectors[sec].floordata->Destroy();
			sectors[sec].floordata = nullptr;
		}
	}
	return true;
}
//==========================================================================
//
// BUILD A STAIRCASE!
// [RH] Added stairsize, srcspeed, delay, reset, igntxt, usespecials parameters
//		If usespecials is non-zero, then each sector in a stair is determined
//		by its special. If usespecials is 2, each sector stays in "sync" with
//		the others.
//
//==========================================================================

bool FLevelLocals::EV_BuildStairs (int tag, DFloor::EStair type, line_t *line, double stairsize, double speed, int delay, int reset, int igntxt, int usespecials)
{
	int 				secnum = -1;
	int					osecnum;	//jff 3/4/98 save old loop index
	double 				height;
	double				stairstep;
	int 				newsecnum = -1;
	FTextureID			texture;
	int 				ok;
	int					persteptime;
	bool 				rtn = false;
	
	sector_t*			sec;
	sector_t*			tsec = NULL;
	sector_t*			prev = NULL;

	DFloor*				floor;

	if (speed == 0)
		return false;

	persteptime = int(stairsize / speed);

	// check if a manual trigger, if so do just the sector on the backside
	auto itr = GetSectorTagIterator(tag, line);
	// The compatibility mode doesn't work with a hashing algorithm.
	// It needs the original linear search method. This was broken in Boom.
	bool compatible = tag != 0 && (i_compatflags & COMPATF_STAIRINDEX);
	while ((secnum = itr.NextCompat(compatible, secnum)) >= 0)
	{
		sec = &sectors[secnum];

		// ALREADY MOVING?	IF SO, KEEP GOING...
		//jff 2/26/98 add special lockout condition to wait for entire
		//staircase to build before retriggering
		if (sec->PlaneMoving(sector_t::floor) || sec->stairlock)
		{
			continue;
		}
		
		// new floor thinker
		rtn = true;
		floor = CreateThinker<DFloor> (sec);
		floor->m_Direction = (type == DFloor::buildUp) ? 1 : -1;
		stairstep = stairsize * floor->m_Direction;
		floor->m_Type = DFloor::buildStair;	//jff 3/31/98 do not leave uninited
		floor->m_ResetCount = reset;	// [RH] Tics until reset (0 if never)
		floor->m_OrgDist = sec->floorplane.fD();	// [RH] Height to reset to
		// [RH] Set up delay values
		floor->m_Delay = delay;
		floor->m_PauseTime = 0;
		floor->m_StepTime = floor->m_PerStepTime = persteptime;
		floor->m_Instant = false;

		floor->m_Crush = (usespecials & DFloor::stairCrush) ? 10 : -1; //jff 2/27/98 fix uninitialized crush field
		floor->m_Hexencrush = true;

		floor->m_Speed = speed;
		height = sec->CenterFloor() + stairstep;
		floor->m_FloorDestDist = sec->floorplane.PointToDist (sec->centerspot, height);

		texture = sec->GetTexture(sector_t::floor);
		osecnum = secnum;				//jff 3/4/98 preserve loop index
		
		// Find next sector to raise
		// 1. Find 2-sided line with same sector side[0] (lowest numbered)
		// 2. Other side is the next sector to raise
		// 3. Unless already moving, or different texture, then stop building
		validcount++;
		sec->validcount = validcount;
		do
		{
			ok = 0;

			if (usespecials & DFloor::stairUseSpecials)
			{
				// [RH] Find the next sector by scanning for Stairs_Special?
				tsec = P_NextSpecialSectorVC(sec,
						sec->special == Stairs_Special1 ?
							Stairs_Special2 : Stairs_Special1);

				if ( (ok = (tsec != nullptr)) )
				{
					tsec->validcount = validcount;
					height += stairstep;

					// if sector's floor already moving, look for another
					//jff 2/26/98 special lockout condition for retriggering
					if (tsec->PlaneMoving(sector_t::floor) || tsec->stairlock)
					{
						prev = sec;
						sec = tsec;
						continue;
					}
					newsecnum = tsec->Index();
				}
			}
			else
			{
				for (auto line : sec->Lines)
				{
					if ( !(line->flags & ML_TWOSIDED) )
						continue;

					tsec = line->frontsector;
					newsecnum = tsec->sectornum;

					if (secnum != newsecnum)
						continue;

					tsec = line->backsector;
					if (!tsec) continue;	//jff 5/7/98 if no backside, continue
					newsecnum = tsec->sectornum;

					if (!igntxt && tsec->GetTexture(sector_t::floor) != texture)
						continue;

					// Doom bug: Height was changed before discarding the sector as part of the stairs.
					// Needs to be compatibility optioned because some maps (Eternall MAP25) depend on it.
					if (i_compatflags & COMPATF_STAIRINDEX) height += stairstep;

					// if sector's floor already moving, look for another
					//jff 2/26/98 special lockout condition for retriggering
					if (tsec->PlaneMoving(sector_t::floor) || tsec->stairlock)
						continue;

					if (!(i_compatflags & COMPATF_STAIRINDEX)) height += stairstep;

					ok = true;
					break;
				}
			}

			if (ok)
			{
				// jff 2/26/98
				// link the stair chain in both directions
				// lock the stair sector until building complete
				sec->nextsec = newsecnum; // link step to next
				tsec->prevsec = secnum;   // link next back
				tsec->nextsec = -1;       // set next forward link as end
				tsec->stairlock = -2;     // lock the step

				prev = sec;
				sec = tsec;
				secnum = newsecnum;

				// create and initialize a thinker for the next step
				floor = CreateThinker<DFloor> (sec);
				floor->StartFloorSound ();
				floor->m_Direction = (type == DFloor::buildUp) ? 1 : -1;
				floor->m_FloorDestDist = sec->floorplane.PointToDist (DVector2(0, 0), height);
				// [RH] Set up delay values
				floor->m_Delay = delay;
				floor->m_PauseTime = 0;
				floor->m_StepTime = floor->m_PerStepTime = persteptime;

				if (usespecials & DFloor::stairSync)
				{
					// [RH]
					double rise = height - sec->CenterFloor();
					floor->m_Speed = speed * rise / stairstep;
				}
				else
				{
					floor->m_Speed = speed;
				}
				floor->m_Type = DFloor::buildStair;	//jff 3/31/98 do not leave uninited
				//jff 2/27/98 fix uninitialized crush field
				floor->m_Crush = (!(usespecials & DFloor::stairUseSpecials) && speed == 4) ? 10 : -1; //jff 2/27/98 fix uninitialized crush field
				floor->m_Hexencrush = false;
				floor->m_Instant = false;
				floor->m_ResetCount = reset;	// [RH] Tics until reset (0 if never)
				floor->m_OrgDist = sec->floorplane.fD();	// [RH] Height to reset to
			}
		} while (ok);
		// [RH] make sure the first sector doesn't point to a previous one, otherwise
		// it can infinite loop when the first sector stops moving.
		sectors[osecnum].prevsec = -1;
	}
	return rtn;
}

//==========================================================================
//
// [RH] Added pillarspeed and slimespeed parameters
//
//==========================================================================

bool FLevelLocals::EV_DoDonut (int tag, line_t *line, double pillarspeed, double slimespeed)
{
	sector_t*			s1;
	sector_t*			s2;
	sector_t*			s3;
	int 				secnum;
	bool 				rtn;
	DFloor*				floor;
	vertex_t*			spot;
	double				height;
		
	rtn = false;

	auto itr = GetSectorTagIterator(tag, line);
	while ((secnum = itr.Next()) >= 0)
	{
		s1 = &sectors[secnum];					// s1 is pillar's sector

		// ALREADY MOVING?	IF SO, KEEP GOING...
		if (s1->PlaneMoving(sector_t::floor))
			continue; // safe now, because we check that tag is non-0 in the looping condition [fdari]
						
		rtn = true;
		s2 = getNextSector (s1->Lines[0], s1);	// s2 is pool's sector
		if (!s2)								// note lowest numbered line around
			continue;							// pillar must be two-sided

		if (!(i_compatflags2 & COMPATF2_FLOORMOVE) && s2->PlaneMoving(sector_t::floor))
			continue;

		for (auto ln : s2->Lines)
		{
			if (ln->backsector == nullptr || ln->backsector == s1)
				continue;
			s3 = ln->backsector;
			
			//	Spawn rising slime
			floor = CreateThinker<DFloor> (s2);
			floor->m_Type = DFloor::donutRaise;
			floor->m_Crush = -1;
			floor->m_Hexencrush = false;
			floor->m_Direction = 1;
			floor->m_Sector = s2;
			floor->m_Speed = slimespeed;
			floor->m_Instant = false;
			floor->m_Texture = s3->GetTexture(sector_t::floor);
			floor->m_NewSpecial = {};
			height = FindHighestFloorPoint (s3, &spot);
			floor->m_FloorDestDist = s2->floorplane.PointToDist (spot, height);
			floor->StartFloorSound ();
			
			//	Spawn lowering donut-hole
			floor = CreateThinker<DFloor> (s1);
			floor->m_Type = DFloor::floorLowerToNearest;
			floor->m_Crush = -1;
			floor->m_Hexencrush = false;
			floor->m_Direction = -1;
			floor->m_Sector = s1;
			floor->m_Speed = pillarspeed;
			floor->m_Instant = false;
			height = FindHighestFloorPoint (s3, &spot);
			floor->m_FloorDestDist = s1->floorplane.PointToDist (spot, height);
			floor->StartFloorSound ();
			break;
		}
	}
	return rtn;
}

//==========================================================================
//
// Elevators
//
//==========================================================================

IMPLEMENT_CLASS(DElevator, false, true)

IMPLEMENT_POINTERS_START(DElevator)
	IMPLEMENT_POINTER(m_Interp_Floor)
	IMPLEMENT_POINTER(m_Interp_Ceiling)
IMPLEMENT_POINTERS_END

void DElevator::Construct(sector_t *sec)
{
	Super::Construct(sec);
	sec->floordata = this;
	sec->ceilingdata = this;
	m_Interp_Floor = sec->SetInterpolation(sector_t::FloorMove, true);
	m_Interp_Ceiling = sec->SetInterpolation(sector_t::CeilingMove, true);
}

void DElevator::Serialize(FSerializer &arc)
{
	Super::Serialize (arc);
	arc.Enum("type", m_Type)
		("direction", m_Direction)
		("floordestdist", m_FloorDestDist)
		("ceilingdestdist", m_CeilingDestDist)
		("speed", m_Speed)
		("interp_floor", m_Interp_Floor)
		("interp_ceiling", m_Interp_Ceiling);
}

DEFINE_FIELD(DElevator, m_Type)
DEFINE_FIELD(DElevator, m_Direction)
DEFINE_FIELD(DElevator, m_FloorDestDist)
DEFINE_FIELD(DElevator, m_CeilingDestDist)
DEFINE_FIELD(DElevator, m_Speed)

//==========================================================================
//
//
//
//==========================================================================

void DElevator::OnDestroy()
{
	if (m_Interp_Ceiling != NULL)
	{
		m_Interp_Ceiling->DelRef();
		m_Interp_Ceiling = nullptr;
	}
	if (m_Interp_Floor != NULL)
	{
		m_Interp_Floor->DelRef();
		m_Interp_Floor = nullptr;
	}
	Super::OnDestroy();
}

//==========================================================================
//
// T_MoveElevator()
//
// Move an elevator to it's destination (up or down)
// Called once per tick for each moving floor.
//
// Passed an elevator_t structure that contains all pertinent info about the
// move. See P_SPEC.H for fields.
// No return.
//
// jff 02/22/98 added to support parallel floor/ceiling motion
//
//==========================================================================

void DElevator::Tick ()
{
	EMoveResult res;

	double oldfloor, oldceiling;

	oldfloor = m_Sector->floorplane.fD();
	oldceiling = m_Sector->ceilingplane.fD();

	if (m_Direction < 0)	// moving down
	{
		res = m_Sector->MoveFloor (m_Speed, m_FloorDestDist, m_Direction);
		if (res == EMoveResult::ok || res == EMoveResult::pastdest)
		{
			res = m_Sector->MoveCeiling (m_Speed, m_CeilingDestDist, m_Direction);
			if (res == EMoveResult::crushed)
			{
				m_Sector->MoveFloor (m_Speed, oldfloor, -m_Direction);
			}
		}
	}
	else // up
	{
		res = m_Sector->MoveCeiling (m_Speed, m_CeilingDestDist, m_Direction);
		if (res == EMoveResult::ok || res == EMoveResult::pastdest)
		{
			res = m_Sector->MoveFloor (m_Speed, m_FloorDestDist, m_Direction);
			if (res == EMoveResult::crushed)
			{
				m_Sector->MoveCeiling (m_Speed, oldceiling, -m_Direction);
			}
		}
	}

	if (res == EMoveResult::pastdest)	// if destination height achieved
	{
		// make floor stop sound
		SN_StopSequence (m_Sector, CHAN_FLOOR);

		m_Sector->floordata = nullptr;		//jff 2/22/98
		m_Sector->ceilingdata = nullptr;	//jff 2/22/98
		Destroy ();		// remove elevator from actives
	}
}

//==========================================================================
//
//
//
//==========================================================================

void DElevator::StartFloorSound ()
{
	::StartFloorSound (m_Sector);
}

//==========================================================================
//
// EV_DoElevator
//
// Handle elevator linedef types
//
// Passed the linedef that triggered the elevator and the elevator action
//
// jff 2/22/98 new type to move floor and ceiling in parallel
// [RH] Added speed, tag, and height parameters and new types.
//
//==========================================================================

bool FLevelLocals::EV_DoElevator (line_t *line, DElevator::EElevator elevtype,
					double speed, double height, int tag)
{
	int			secnum;
	bool		rtn;
	sector_t*	sec;
	DElevator*	elevator;
	double		floorheight, ceilingheight;
	double		newheight;
	vertex_t*	spot;

	if (!line && (elevtype == DElevator::elevateCurrent))
		return false;

	secnum = -1;
	rtn = false;

	auto itr = GetSectorTagIterator(tag, line);

	// act on all sectors with the same tag as the triggering linedef
	while ((secnum = itr.Next()) >= 0)
	{
		sec = &sectors[secnum];
		// If either floor or ceiling is already activated, skip it
		if (sec->PlaneMoving(sector_t::floor) || sec->ceilingdata) //jff 2/22/98
			continue; // the loop used to break at the end if tag were 0, but would miss that step if "continue" occured [FDARI]

		// create and initialize new elevator thinker
		rtn = true;
		elevator = CreateThinker<DElevator> (sec);
		elevator->m_Type = elevtype;
		elevator->m_Speed = speed;
		elevator->StartFloorSound ();

		floorheight = sec->CenterFloor ();
		ceilingheight = sec->CenterCeiling ();

		// set up the fields according to the type of elevator action
		switch (elevtype)
		{
		// elevator down to next floor
		case DElevator::elevateDown:
			elevator->m_Direction = -1;
			newheight = FindNextLowestFloor (sec, &spot);
			elevator->m_FloorDestDist = sec->floorplane.PointToDist (spot, newheight);
			newheight += sec->ceilingplane.ZatPoint(spot) - sec->floorplane.ZatPoint(spot);
			elevator->m_CeilingDestDist = sec->ceilingplane.PointToDist (spot, newheight);
			break;

		// elevator up to next floor
		case DElevator::elevateUp:
			elevator->m_Direction = 1;
			newheight = FindNextHighestFloor (sec, &spot);
			elevator->m_FloorDestDist = sec->floorplane.PointToDist (spot, newheight);
			newheight += sec->ceilingplane.ZatPoint(spot) - sec->floorplane.ZatPoint(spot);
			elevator->m_CeilingDestDist = sec->ceilingplane.PointToDist (spot, newheight);
			break;

		// elevator to floor height of activating switch's front sector
		case DElevator::elevateCurrent:
			newheight = line->frontsector->floorplane.ZatPoint (line->v1);
			elevator->m_FloorDestDist = sec->floorplane.PointToDist (line->v1, newheight);
			newheight += sec->ceilingplane.ZatPoint(line->v1) - sec->floorplane.ZatPoint(line->v1);
			elevator->m_CeilingDestDist = sec->ceilingplane.PointToDist (line->v1, newheight);

			elevator->m_Direction =
				elevator->m_FloorDestDist > sec->floorplane.fD() ? -1 : 1;
			break;

		// [RH] elevate up by a specific amount
		case DElevator::elevateRaise:
			elevator->m_Direction = 1;
			elevator->m_FloorDestDist = sec->floorplane.PointToDist (sec->centerspot, floorheight + height);
			elevator->m_CeilingDestDist = sec->ceilingplane.PointToDist (sec->centerspot, ceilingheight + height);
			break;

		// [RH] elevate down by a specific amount
		case DElevator::elevateLower:
			elevator->m_Direction = -1;
			elevator->m_FloorDestDist = sec->floorplane.PointToDist (sec->centerspot, floorheight - height);
			elevator->m_CeilingDestDist = sec->ceilingplane.PointToDist (sec->centerspot, ceilingheight - height);
			break;
		}
	}
	return rtn;
}


//==========================================================================
//
// EV_DoChange()
//
// Handle pure change types. These change floor texture and sector type
// by trigger or numeric model without moving the floor.
//
// The linedef causing the change and the type of change is passed
// Returns true if any sector changes
//
// jff 3/15/98 added to better support generalized sector types
// [RH] Added tag parameter.
//
//==========================================================================

bool FLevelLocals::EV_DoChange (line_t *line, EChange changetype, int tag)
{
	int			secnum;
	bool		rtn;
	sector_t	*sec;
	sector_t	*secm;

	rtn = false;
	// change all sectors with the same tag as the linedef
	auto it = GetSectorTagIterator(tag);
	while ((secnum = it.Next()) >= 0)
	{
		sec = &sectors[secnum];
              
		rtn = true;

		// handle trigger or numeric change type
		FTextureID oldpic = sec->GetTexture(sector_t::floor);

		switch(changetype)
		{
		case trigChangeOnly:
			if (line)
			{ // [RH] if no line, no change
				sec->SetTexture(sector_t::floor, line->frontsector->GetTexture(sector_t::floor));
				sec->TransferSpecial(line->frontsector);
			}
			break;
		case numChangeOnly:
			secm = FindModelFloorSector(sec, sec->CenterFloor());
			if (secm)
			{ // if no model, no change
				sec->SetTexture(sector_t::floor, secm->GetTexture(sector_t::floor));
				sec->TransferSpecial(secm);
			}
			break;
		default:
			break;
		}
	}
	return rtn;
}


//==========================================================================
//
//
//
//==========================================================================

IMPLEMENT_CLASS(DWaggleBase, false, false)
IMPLEMENT_CLASS(DFloorWaggle, false, false)
IMPLEMENT_CLASS(DCeilingWaggle, false, false)

void DWaggleBase::Serialize(FSerializer &arc)
{
	Super::Serialize (arc);
	arc("originaldist", m_OriginalDist)
		("accumulator", m_Accumulator)
		("accdelta", m_AccDelta)
		("targetscale", m_TargetScale)
		("scale", m_Scale)
		("scaledelta", m_ScaleDelta)
		("ticker", m_Ticker)
		("state", m_State);
}

//==========================================================================
//
// WaggleBase
//
//==========================================================================

#define WGLSTATE_EXPAND 1
#define WGLSTATE_STABLE 2
#define WGLSTATE_REDUCE 3

void DWaggleBase::Construct(sector_t *sec)
{
	Super::Construct(sec);
}

//==========================================================================
//
//
//
//==========================================================================

void DWaggleBase::DoWaggle (bool ceiling)
{
	secplane_t *plane;
	int pos;
	double dist;

	if (ceiling)
	{
		plane = &m_Sector->ceilingplane;
		pos = sector_t::ceiling;
	}
	else
	{
		plane = &m_Sector->floorplane;
		pos = sector_t::floor;
	}

	switch (m_State)
	{
	case WGLSTATE_EXPAND:
		if ((m_Scale += m_ScaleDelta) >= m_TargetScale)
		{
			m_Scale = m_TargetScale;
			m_State = WGLSTATE_STABLE;
		}
		break;

	case WGLSTATE_REDUCE:
		if ((m_Scale -= m_ScaleDelta) <= 0)
		{ // Remove
			dist = (m_OriginalDist - plane->fD()) / plane->fC();
			m_Sector->ChangePlaneTexZ(pos, -plane->HeightDiff (m_OriginalDist));
			plane->setD(m_OriginalDist);
			P_ChangeSector (m_Sector, true, dist, ceiling, false);
			if (ceiling)
			{
				m_Sector->ceilingdata = nullptr;
			}
			else
			{
				m_Sector->floordata = nullptr;
			}
			Destroy ();
			return;
		}
		break;

	case WGLSTATE_STABLE:
		if (m_Ticker != -1)
		{
			if (!--m_Ticker)
			{
				m_State = WGLSTATE_REDUCE;
			}
		}
		break;
	}
	m_Accumulator += m_AccDelta;

	dist = plane->fD();
	plane->setD(m_OriginalDist + plane->PointToDist (DVector2(0, 0), BobSin(m_Accumulator) *m_Scale));
	m_Sector->ChangePlaneTexZ(pos, plane->HeightDiff (dist));
	dist = plane->HeightDiff (dist);

	// Interesting: Hexen passes 'true' for the crunch parameter which really is crushing damage here...
	// Also, this does not reset the move if it blocks.
	P_Scroll3dMidtex(m_Sector, 1, dist, ceiling);
	P_MoveLinkedSectors(m_Sector, 1, dist, ceiling);
	P_ChangeSector (m_Sector, 1, dist, ceiling, false);
}

//==========================================================================
//
// FloorWaggle
//
//==========================================================================

void DFloorWaggle::Construct(sector_t *sec)
{
	Super::Construct(sec);
	sec->floordata = this;
	interpolation = sec->SetInterpolation(sector_t::FloorMove, true);
}

void DFloorWaggle::Tick ()
{
	DoWaggle (false);
}

//==========================================================================
//
// CeilingWaggle
//
//==========================================================================

void DCeilingWaggle::Construct(sector_t *sec)
{
	Super::Construct(sec);
	sec->ceilingdata = this;
	interpolation = sec->SetInterpolation(sector_t::CeilingMove, true);
}

void DCeilingWaggle::Tick ()
{
	DoWaggle (true);
}

//==========================================================================
//
// EV_StartWaggle
//
//==========================================================================

bool FLevelLocals::EV_StartWaggle (int tag, line_t *line, int height, int speed, int offset, int timer, bool ceiling)
{
	int sectorIndex;
	sector_t *sector;
	DWaggleBase *waggle;
	bool retCode;

	retCode = false;

	auto itr = GetSectorTagIterator(tag, line);

	while ((sectorIndex = itr.Next()) >= 0)
	{
		sector = &sectors[sectorIndex];
		if ((!ceiling && sector->PlaneMoving(sector_t::floor)) || 
			(ceiling && sector->PlaneMoving(sector_t::ceiling)))
		{ // Already busy with another thinker
			continue;
		}
		retCode = true;
		if (ceiling)
		{
			waggle = CreateThinker<DCeilingWaggle> (sector);
			waggle->m_OriginalDist = sector->ceilingplane.fD();
		}
		else
		{
			waggle = CreateThinker<DFloorWaggle> (sector);
			waggle->m_OriginalDist = sector->floorplane.fD();
		}
		waggle->m_Accumulator = offset;
		waggle->m_AccDelta = speed / 64.;
		waggle->m_Scale = 0;
		waggle->m_TargetScale = height / 64.;
		waggle->m_ScaleDelta = waggle->m_TargetScale / (TICRATE + ((3 * TICRATE)*height) / 255);
		waggle->m_Ticker = timer ? timer*TICRATE : -1;
		waggle->m_State = WGLSTATE_EXPAND;
	}
	return retCode;
}
