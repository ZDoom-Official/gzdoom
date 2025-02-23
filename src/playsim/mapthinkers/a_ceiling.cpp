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
//
// DESCRIPTION:  Ceiling animation (lowering, crushing, raising)
//
//-----------------------------------------------------------------------------



#include "doomdef.h"
#include "p_local.h"
#include "s_sound.h"
#include "s_sndseq.h"
#include "doomstat.h"
#include "r_state.h"
#include "serializer.h"
#include "p_spec.h"
#include "g_levellocals.h"
#include "vm.h"

//============================================================================
//
// CEILINGS
//
//============================================================================

IMPLEMENT_CLASS(DCeiling, false, false)

//============================================================================
//
// 
//
//============================================================================

void DCeiling::Serialize(FSerializer &arc)
{
	Super::Serialize (arc);
	arc.Enum("type", m_Type)
		("bottomheight", m_BottomHeight)
		("topheight", m_TopHeight)
		("speed", m_Speed)
		("speed1", m_Speed1)
		("speed2", m_Speed2)
		("crush", m_Crush)
		("silent", m_Silent)
		("direction", m_Direction)
		("texture", m_Texture)
		("newspecial", m_NewSpecial)
		("tag", m_Tag)
		("olddirecton", m_OldDirection)
		.Enum("crushmode", m_CrushMode);
}

DEFINE_FIELD(DCeiling, m_Type)
DEFINE_FIELD(DCeiling, m_BottomHeight)
DEFINE_FIELD(DCeiling, m_TopHeight)
DEFINE_FIELD(DCeiling, m_Speed)
DEFINE_FIELD(DCeiling, m_Speed1)
DEFINE_FIELD(DCeiling, m_Speed2)
DEFINE_FIELD(DCeiling, m_Silent)
DEFINE_FIELD(DCeiling, m_CrushMode)

DEFINE_ACTION_FUNCTION(DCeiling, getCrush)
{
	PARAM_SELF_PROLOGUE(DCeiling);
	ACTION_RETURN_INT(self->getCrush());
}
DEFINE_ACTION_FUNCTION(DCeiling, getDirection)
{
	PARAM_SELF_PROLOGUE(DCeiling);
	ACTION_RETURN_INT(self->getDirection());
}
DEFINE_ACTION_FUNCTION(DCeiling, getOldDirection)
{
	PARAM_SELF_PROLOGUE(DCeiling);
	ACTION_RETURN_INT(self->getOldDirection());
}

//============================================================================
//
// 
//
//============================================================================

void DCeiling::PlayCeilingSound ()
{
	if (m_Sector->Flags & SECF_SILENTMOVE) return;
	if (m_Sector->seqType >= 0)
	{
		SN_StartSequence (m_Sector, CHAN_CEILING, m_Sector->seqType, SEQ_PLATFORM, 0, false);
	}
	else if (m_Sector->SeqName != NAME_None)
	{
		SN_StartSequence (m_Sector, CHAN_CEILING, m_Sector->SeqName, 0);
	}
	else
	{
		if (m_Silent == 2)
			SN_StartSequence (m_Sector, CHAN_CEILING, "Silence", 0);
		else if (m_Silent == 1)
			SN_StartSequence (m_Sector, CHAN_CEILING, "CeilingSemiSilent", 0);
		else
			SN_StartSequence (m_Sector, CHAN_CEILING, "CeilingNormal", 0);
	}
}

//============================================================================
//
// DCeiling :: Tick
//
//============================================================================

void DCeiling::Tick ()
{
	EMoveResult res;
		
	switch (m_Direction)
	{
	case 0:
		// IN STASIS
		break;
	case 1:
		// UP
		res = m_Sector->MoveCeiling (m_Speed, m_TopHeight, m_Direction);
		
		if (res == EMoveResult::pastdest)
		{
			switch (m_Type)
			{
			case DCeiling::ceilCrushAndRaise:
				m_Direction = -1;
				m_Speed = m_Speed1;
				if (!SN_IsMakingLoopingSound (m_Sector))
					PlayCeilingSound ();
				break;
				
			// movers with texture change, change the texture then get removed
			case genCeilingChgT:
			case genCeilingChg0:
				m_Sector->SetSpecial(&m_NewSpecial);
				// fall through
			case genCeilingChg:
				m_Sector->SetTexture(sector_t::ceiling, m_Texture);
				// fall through
			default:
				SN_StopSequence (m_Sector, CHAN_CEILING);
				Destroy ();
				break;
			}
		}
		break;
		
	case -1:
		// DOWN
		res = m_Sector->MoveCeiling (m_Speed, m_BottomHeight, m_Crush, m_Direction, m_CrushMode == ECrushMode::crushHexen);
		
		if (res == EMoveResult::pastdest)
		{
			switch (m_Type)
			{
			case DCeiling::ceilCrushAndRaise:
			case DCeiling::ceilCrushRaiseAndStay:
				m_Speed = m_Speed2;
				m_Direction = 1;
				if (!SN_IsMakingLoopingSound (m_Sector))
					PlayCeilingSound ();
				break;

			// in the case of ceiling mover/changer, change the texture
			// then remove the active ceiling
			case genCeilingChgT:
			case genCeilingChg0:
				m_Sector->SetSpecial(&m_NewSpecial);
				// fall through
			case genCeilingChg:
				m_Sector->SetTexture(sector_t::ceiling, m_Texture);
				// fall through
			default:
				SN_StopSequence (m_Sector, CHAN_CEILING);
				Destroy ();
				break;
			}
		}
		else // ( res != pastdest )
		{
			if (res == EMoveResult::crushed)
			{
				switch (m_Type)
				{
				case DCeiling::ceilCrushAndRaise:
				case DCeiling::ceilLowerAndCrush:
					if (m_CrushMode == ECrushMode::crushSlowdown)
						m_Speed = 1. / 8;
					break;

				default:
					break;
				}
			}
		}
		break;
	}
}

//============================================================================
//
// 
//
//============================================================================

void DCeiling::Construct(sector_t *sec)
{
	Super::Construct(sec);
}

void DCeiling::Construct(sector_t *sec, double speed1, double speed2, int silent)
{
	Super::Construct(sec);
	m_Crush = -1;
	m_CrushMode = ECrushMode::crushDoom;
	m_Speed = m_Speed1 = speed1;
	m_Speed2 = speed2;
	m_Silent = silent;
	m_BottomHeight = 0;
	m_TopHeight = 0;
	m_Direction = 0;
	m_Texture = FNullTextureID();
	m_Tag = 0;
	m_OldDirection = 0;
}

//============================================================================
//
// 
//
//============================================================================

bool FLevelLocals::CreateCeiling(sector_t *sec, DCeiling::ECeiling type, line_t *line, int tag, 
				   double speed, double speed2, double height,
				   int crush, int silent, int change, DCeiling::ECrushMode hexencrush)
{
	double		targheight = 0;	// Silence, GCC

	// if ceiling already moving, don't start a second function on it
	if (sec->PlaneMoving(sector_t::ceiling))
	{
		return false;
	}
	
	// new door thinker
	DCeiling *ceiling = CreateThinker<DCeiling> (sec, speed, speed2, silent & ~4);
	vertex_t *spot = sec->Lines[0]->v1;

	switch (type)
	{
	case DCeiling::ceilCrushAndRaise:
	case DCeiling::ceilCrushRaiseAndStay:
		ceiling->m_TopHeight = sec->ceilingplane.fD();
		[[fallthrough]];
	case DCeiling::ceilLowerAndCrush:
		targheight = FindHighestFloorPoint (sec, &spot);
		targheight += height;
		ceiling->m_BottomHeight = sec->ceilingplane.PointToDist (spot, targheight);
		ceiling->m_Direction = -1;
		break;

	case DCeiling::ceilRaiseToHighest:
		targheight = FindHighestCeilingSurrounding (sec, &spot);
		ceiling->m_TopHeight = sec->ceilingplane.PointToDist (spot, targheight);
		ceiling->m_Direction = 1;
		break;

	case DCeiling::ceilLowerByValue:
		targheight = sec->ceilingplane.ZatPoint (spot) - height;
		ceiling->m_BottomHeight = sec->ceilingplane.PointToDist (spot, targheight);
		ceiling->m_Direction = -1;
		break;

	case DCeiling::ceilRaiseByValue:
		targheight = sec->ceilingplane.ZatPoint (spot) + height;
		ceiling->m_TopHeight = sec->ceilingplane.PointToDist (spot, targheight);
		ceiling->m_Direction = 1;
		break;

	case DCeiling::ceilMoveToValue:
		{
			double diff = height - sec->ceilingplane.ZatPoint (spot);

			targheight = height;
			if (diff < 0)
			{
				ceiling->m_BottomHeight = sec->ceilingplane.PointToDist (spot, height);
				ceiling->m_Direction = -1;
			}
			else
			{
				ceiling->m_TopHeight = sec->ceilingplane.PointToDist (spot, height);
				ceiling->m_Direction = 1;
			}
		}
		break;

	case DCeiling::ceilLowerToHighestFloor:
		targheight = FindHighestFloorSurrounding (sec, &spot) + height;
		ceiling->m_BottomHeight = sec->ceilingplane.PointToDist (spot, targheight);
		ceiling->m_Direction = -1;
		break;

	case DCeiling::ceilRaiseToHighestFloor:
		targheight = FindHighestFloorSurrounding (sec, &spot);
		ceiling->m_TopHeight = sec->ceilingplane.PointToDist (spot, targheight);
		ceiling->m_Direction = 1;
		break;

	case DCeiling::ceilLowerInstant:
		targheight = sec->ceilingplane.ZatPoint (spot) - height;
		ceiling->m_BottomHeight = sec->ceilingplane.PointToDist (spot, targheight);
		ceiling->m_Direction = -1;
		ceiling->m_Speed = height;
		break;

	case DCeiling::ceilRaiseInstant:
		targheight = sec->ceilingplane.ZatPoint (spot) + height;
		ceiling->m_TopHeight = sec->ceilingplane.PointToDist (spot, targheight);
		ceiling->m_Direction = 1;
		ceiling->m_Speed = height;
		break;

	case DCeiling::ceilLowerToNearest:
		targheight = FindNextLowestCeiling (sec, &spot);
		ceiling->m_BottomHeight = sec->ceilingplane.PointToDist (spot, targheight);
		ceiling->m_Direction = -1;
		break;

	case DCeiling::ceilRaiseToNearest:
		targheight = FindNextHighestCeiling (sec, &spot);
		ceiling->m_TopHeight = sec->ceilingplane.PointToDist (spot, targheight);
		ceiling->m_Direction = 1;
		break;

	case DCeiling::ceilLowerToLowest:
		targheight = FindLowestCeilingSurrounding (sec, &spot);
		ceiling->m_BottomHeight = sec->ceilingplane.PointToDist (spot, targheight);
		ceiling->m_Direction = -1;
		break;

	case DCeiling::ceilRaiseToLowest:
		targheight = FindLowestCeilingSurrounding (sec, &spot);
		ceiling->m_TopHeight = sec->ceilingplane.PointToDist (spot, targheight);
		ceiling->m_Direction = 1;
		break;

	case DCeiling::ceilLowerToFloor:
		targheight = FindHighestFloorPoint (sec, &spot) + height;
		ceiling->m_BottomHeight = sec->ceilingplane.PointToDist (spot, targheight);
		ceiling->m_Direction = -1;
		break;

	case DCeiling::ceilRaiseToFloor:	// [RH] What's this for?
		targheight = FindHighestFloorPoint (sec, &spot) + height;
		ceiling->m_TopHeight = sec->ceilingplane.PointToDist (spot, targheight);
		ceiling->m_Direction = 1;
		break;

	case DCeiling::ceilLowerToHighest:
		targheight = FindHighestCeilingSurrounding (sec, &spot);
		ceiling->m_BottomHeight = sec->ceilingplane.PointToDist (spot, targheight);
		ceiling->m_Direction = -1;
		break;

	case DCeiling::ceilLowerByTexture:
		targheight = sec->ceilingplane.ZatPoint (spot) - FindShortestUpperAround (sec);
		ceiling->m_BottomHeight = sec->ceilingplane.PointToDist (spot, targheight);
		ceiling->m_Direction = -1;
		break;

	case DCeiling::ceilRaiseByTexture:
		targheight = sec->ceilingplane.ZatPoint (spot) + FindShortestUpperAround (sec);
		ceiling->m_TopHeight = sec->ceilingplane.PointToDist (spot, targheight);
		ceiling->m_Direction = 1;
		break;

	default:
		break;	// Silence GCC
	}
			
	ceiling->m_Tag = tag;
	ceiling->m_Type = type;
	ceiling->m_Crush = crush;
	ceiling->m_CrushMode = hexencrush;

	// Do not interpolate instant movement ceilings.
	// Note for ZDoomGL: Check to make sure that you update the sector
	// after the ceiling moves, because it hasn't actually moved yet.
	double movedist;

	if (ceiling->m_Direction < 0)
	{
		movedist = sec->ceilingplane.fD() - ceiling->m_BottomHeight;
	}
	else
	{
		movedist = ceiling->m_TopHeight - sec->ceilingplane.fD();
	}
	if (ceiling->m_Speed >= movedist)
	{
		ceiling->StopInterpolation(true);
		if (silent & 4) ceiling->m_Silent = 2;
	}

	// set texture/type change properties
	if (change & 3)		// if a texture change is indicated
	{
		if (change & 4)	// if a numeric model change
		{
			sector_t *modelsec;

			//jff 5/23/98 find model with floor at target height if target
			//is a floor type
			modelsec = (/*type == ceilRaiseToHighest ||*/
				   type == DCeiling::ceilRaiseToFloor ||
				   /*type == ceilLowerToHighest ||*/
				   type == DCeiling::ceilLowerToFloor) ?
				FindModelFloorSector(sec, targheight) :
				FindModelCeilingSector(sec, targheight);
			if (modelsec != NULL)
			{
				ceiling->m_Texture = modelsec->GetTexture(sector_t::ceiling);
				switch (change & 3)
				{
					case 1:		// type is zeroed
						ceiling->m_NewSpecial = {};
						ceiling->m_Type = DCeiling::genCeilingChg0;
						break;
					case 2:		// type is copied
						sec->GetSpecial(&ceiling->m_NewSpecial);
						ceiling->m_Type = DCeiling::genCeilingChgT;
						break;
					case 3:		// type is left alone
						ceiling->m_Type = DCeiling::genCeilingChg;
						break;
				}
			}
		}
		else if (line)	// else if a trigger model change
		{
			ceiling->m_Texture = line->frontsector->GetTexture(sector_t::ceiling);
			switch (change & 3)
			{
				case 1:		// type is zeroed
					ceiling->m_NewSpecial = {};
					ceiling->m_Type = DCeiling::genCeilingChg0;
					break;
				case 2:		// type is copied
					line->frontsector->GetSpecial(&ceiling->m_NewSpecial);
					ceiling->m_Type = DCeiling::genCeilingChgT;
					break;
				case 3:		// type is left alone
					ceiling->m_Type = DCeiling::genCeilingChg;
					break;
			}
		}
	}

	ceiling->PlayCeilingSound ();
	return ceiling != NULL;
}

DEFINE_ACTION_FUNCTION(FLevelLocals, CreateCeiling)
{
	PARAM_SELF_STRUCT_PROLOGUE(FLevelLocals);
	PARAM_POINTER_NOT_NULL(sec, sector_t);
	PARAM_INT(type);
	PARAM_POINTER(ln, line_t);
	PARAM_FLOAT(speed);
	PARAM_FLOAT(speed2);
	PARAM_FLOAT(height);
	PARAM_INT(crush);
	PARAM_INT(silent);
	PARAM_INT(change);
	PARAM_INT(crushmode);
	ACTION_RETURN_BOOL(self->CreateCeiling(sec, (DCeiling::ECeiling)type, ln, 0, speed, speed2, height, crush, silent, change, (DCeiling::ECrushMode)crushmode));
}

//============================================================================
//
// EV_DoCeiling
// Move a ceiling up/down and all around!
//
// [RH] Added tag, speed, speed2, height, crush, silent, change params
//
//============================================================================

bool FLevelLocals::EV_DoCeiling (DCeiling::ECeiling type, line_t *line,
				   int tag, double speed, double speed2, double height,
				   int crush, int silent, int change, DCeiling::ECrushMode hexencrush)
{
	int 		secnum;
	bool 		rtn;
	sector_t*	sec;
		
	rtn = false;

	// check if a manual trigger, if so do just the sector on the backside
	if (tag == 0)
	{
		if (!line || !(sec = line->backsector))
			return rtn;
		secnum = sec->sectornum;
		// [RH] Hack to let manual crushers be retriggerable, too
		tag ^= secnum | 0x1000000;
		ActivateInStasisCeiling (tag);
		return CreateCeiling(sec, type, line, tag, speed, speed2, height, crush, silent, change, hexencrush);
	}
	
	//	Reactivate in-stasis ceilings...for certain types.
	// This restarts a crusher after it has been stopped
	if (type == DCeiling::ceilCrushAndRaise)
	{
		ActivateInStasisCeiling (tag);
	}

	// affects all sectors with the same tag as the linedef
	auto it = GetSectorTagIterator(tag);
	while ((secnum = it.Next()) >= 0)
	{
		rtn |= CreateCeiling(&sectors[secnum], type, line, tag, speed, speed2, height, crush, silent, change, hexencrush);
	}
	return rtn;
}


//============================================================================
//
// Restart a ceiling that's in-stasis
// [RH] Passed a tag instead of a line and rewritten to use a list
//
//============================================================================

void FLevelLocals::ActivateInStasisCeiling (int tag)
{
	DCeiling *scan;
	auto iterator = GetThinkerIterator<DCeiling>();

	while ( (scan = iterator.Next ()) )
	{
		if (scan->m_Tag == tag && scan->m_Direction == 0)
		{
			scan->m_Direction = scan->m_OldDirection;
			scan->PlayCeilingSound ();
		}
	}
}

//============================================================================
//
// EV_CeilingCrushStop
// Stop a ceiling from crushing!
// [RH] Passed a tag instead of a line and rewritten to use a list
//
//============================================================================

bool FLevelLocals::EV_CeilingCrushStop (int tag, bool remove)
{
	bool rtn = false;
	auto iterator = GetThinkerIterator<DCeiling>();

	auto scan = iterator.Next();
	while (scan != nullptr)
	{
		DCeiling *next = iterator.Next();
		if (scan->m_Tag == tag && scan->m_Direction != 0)
		{
			if (!remove)
			{
				SN_StopSequence(scan->m_Sector, CHAN_CEILING);
				scan->m_OldDirection = scan->m_Direction;
				scan->m_Direction = 0;		// in-stasis;
			}
			else
			{
				scan->Destroy();
			}
			rtn = true;
		}
		scan = next;
	}

	return rtn;
}

bool FLevelLocals::EV_StopCeiling(int tag, line_t *line)
{
	int sec;
	auto it = GetSectorTagIterator(tag, line);
	while ((sec = it.Next()) >= 0)
	{
		if (sectors[sec].ceilingdata)
		{
			SN_StopSequence(&sectors[sec], CHAN_CEILING);
			sectors[sec].ceilingdata->Destroy();
			sectors[sec].ceilingdata = nullptr;
		}
	}
	return true;
}
