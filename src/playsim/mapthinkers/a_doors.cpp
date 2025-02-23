//-----------------------------------------------------------------------------
//
// Copyright 1993-1996 id Software
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
// DESCRIPTION: Door animation code (opening/closing)
//		[RH] Removed sliding door code and simplified for Hexen-ish specials
//
//-----------------------------------------------------------------------------



#include "doomdef.h"
#include "p_local.h"
#include "s_sound.h"
#include "s_sndseq.h"
#include "doomstat.h"
#include "r_state.h"
#include "gi.h"
#include "a_keys.h"
#include "serializer_doom.h"
#include "d_player.h"
#include "p_spec.h"
#include "g_levellocals.h"
#include "animations.h"
#include "texturemanager.h"
#include "vm.h"

//============================================================================
//
// VERTICAL DOORS
//
//============================================================================

IMPLEMENT_CLASS(DDoor, false, false)

void DDoor::Serialize(FSerializer &arc)
{
	Super::Serialize (arc);
	arc.Enum("type", m_Type)
		("topdist", m_TopDist)
		("botspot", m_BotSpot)
		("botdist", m_BotDist)
		("oldfloordist", m_OldFloorDist)
		("speed", m_Speed)
		("direction", m_Direction)
		("topwait", m_TopWait)
		("topcountdown", m_TopCountdown)
		("lighttag", m_LightTag);
}

DEFINE_FIELD(DDoor, m_Type)
DEFINE_FIELD(DDoor, m_TopDist)
DEFINE_FIELD(DDoor, m_BotSpot)
DEFINE_FIELD(DDoor, m_BotDist)
DEFINE_FIELD(DDoor, m_OldFloorDist)
DEFINE_FIELD(DDoor, m_Speed)
DEFINE_FIELD(DDoor, m_Direction)
DEFINE_FIELD(DDoor, m_TopWait)
DEFINE_FIELD(DDoor, m_TopCountdown)
DEFINE_FIELD(DDoor, m_LightTag)

//============================================================================
//
// T_VerticalDoor
//
//============================================================================

void DDoor::Tick ()
{
	EMoveResult res;

	// Adjust bottom height - but only if there isn't an active lift attached to the floor.
	if (m_Sector->floorplane.fD() != m_OldFloorDist)
	{
		if (!m_Sector->floordata || !m_Sector->floordata->IsKindOf(RUNTIME_CLASS(DPlat)) ||
			!(barrier_cast<DPlat*>(m_Sector->floordata))->IsLift())
		{
			m_OldFloorDist = m_Sector->floorplane.fD();
			m_BotDist = m_Sector->ceilingplane.PointToDist (m_BotSpot, m_Sector->floorplane.ZatPoint (m_BotSpot));
		}
	}

	switch (m_Direction)
	{
	case 0:
		// WAITING
		if (!--m_TopCountdown)
		{
			switch (m_Type)
			{
			case doorRaise:
				m_Direction = -1; // time to go back down
				DoorSound (false);
				break;
				
			case doorCloseWaitOpen:
				m_Direction = 1;
				DoorSound (true);
				break;
				
			default:
				break;
			}
		}
		break;
		
	case 2:
		//	INITIAL WAIT
		if (!--m_TopCountdown)
		{
			switch (m_Type)
			{
			case doorWaitRaise:
				m_Direction = 1;
				m_Type = doorRaise;
				DoorSound (true);
				break;
				
			default:
				break;
			}
		}
		break;
		
	case -1:
		// DOWN
		res = m_Sector->MoveCeiling (m_Speed, m_BotDist, -1, m_Direction, false);

		// killough 10/98: implement gradual lighting effects
		if (m_LightTag != 0 && m_TopDist != -m_Sector->floorplane.fD())
		{
			Level->EV_LightTurnOnPartway (m_LightTag, 
				(m_Sector->ceilingplane.fD() + m_Sector->floorplane.fD()) / (m_TopDist + m_Sector->floorplane.fD()));
		}

		if (res == EMoveResult::pastdest)
		{
			SN_StopSequence (m_Sector, CHAN_CEILING);
			switch (m_Type)
			{
			case doorRaise:
			case doorClose:
				m_Sector->ceilingdata = nullptr;	//jff 2/22/98
				Destroy ();						// unlink and free
				break;
				
			case doorCloseWaitOpen:
				m_Direction = 0;
				m_TopCountdown = m_TopWait;
				break;
				
			default:
				break;
			}
		}
		else if (res == EMoveResult::crushed)
		{
			switch (m_Type)
			{
			case doorClose:				// DO NOT GO BACK UP!
				break;
				
			default:
				m_Direction = 1;
				DoorSound (true);
				break;
			}
		}
		break;
		
	case 1:
		// UP
		res = m_Sector->MoveCeiling (m_Speed, m_TopDist, -1, m_Direction, false);
		
		// killough 10/98: implement gradual lighting effects
		if (m_LightTag != 0 && m_TopDist != -m_Sector->floorplane.fD())
		{
			Level->EV_LightTurnOnPartway (m_LightTag,
				(m_Sector->ceilingplane.fD() + m_Sector->floorplane.fD()) / (m_TopDist + m_Sector->floorplane.fD()));
		}

		if (res == EMoveResult::pastdest)
		{
			SN_StopSequence (m_Sector, CHAN_CEILING);
			switch (m_Type)
			{
			case doorRaise:
				m_Direction = 0; // wait at top
				m_TopCountdown = m_TopWait;
				break;
				
			case doorCloseWaitOpen:
			case doorOpen:
				m_Sector->ceilingdata = nullptr;	//jff 2/22/98
				Destroy ();						// unlink and free
				break;
				
			default:
				break;
			}
		}
		else if (res == EMoveResult::crushed)
		{
			switch (m_Type)
			{
			case doorRaise:
			case doorWaitRaise:
				m_Direction = -1;
				DoorSound(false);
				break;

			default:
				break;
			}
		}
		break;
	}
}

//============================================================================
//
// [RH] DoorSound: Plays door sound depending on direction and speed
//
// If curseq is non-NULL, then it will check if the desired sound sequence
// will result in a different command stream than the current one. If not,
// then it does nothing.
//
//============================================================================

void DDoor::DoorSound(bool raise, DSeqNode *curseq) const
{
	int choice;

	// For multiple-selection sound sequences, the following choices are used:
	//  0  Opening
	//  1  Closing
	//  2  Opening fast
	//  3  Closing fast

	choice = !raise;

	if (m_Sector->Flags & SECF_SILENTMOVE) return;

	if (m_Speed >= 8)
	{
		choice += 2;
	}

	if (m_Sector->seqType >= 0)
	{
		if (curseq == NULL || !SN_AreModesSame(m_Sector->seqType, SEQ_DOOR, choice, curseq->GetModeNum()))
		{
			SN_StartSequence(m_Sector, CHAN_CEILING, m_Sector->seqType, SEQ_DOOR, choice);
		}
	}
	else if (m_Sector->SeqName != NAME_None)
	{
		if (curseq == NULL || !SN_AreModesSame(m_Sector->SeqName, choice, curseq->GetModeNum()))
		{
			SN_StartSequence(m_Sector, CHAN_CEILING, m_Sector->SeqName, choice);
		}
	}
	else
	{
		const char *snd;

		switch (gameinfo.gametype)
		{
		default:	/* Doom and Hexen */
			snd = "DoorNormal";
			break;
			
		case GAME_Heretic:
			snd = "HereticDoor";
			break;

		case GAME_Strife:
			snd = "DoorSmallMetal";

			// Search the front top textures of 2-sided lines on the door sector
			// for a door sound to use.
			for (auto line : m_Sector->Lines)
			{
				const char *texname;

				if (line->backsector == NULL)
					continue;

				auto tex = TexMan.GetGameTexture(line->sidedef[0]->GetTexture(side_t::top));
				texname = tex ? tex->GetName().GetChars() : NULL;
				if (texname != NULL && texname[0] == 'D' && texname[1] == 'O' && texname[2] == 'R')
				{
					switch (texname[3])
					{
					case 'S':
						snd = "DoorStone";
						break;

					case 'M':
						if (texname[4] == 'L')
						{
							snd = "DoorLargeMetal";
						}
						break;

					case 'W':
						if (texname[4] == 'L')
						{
							snd = "DoorLargeWood";
						}
						else
						{
							snd = "DoorSmallWood";
						}
						break;
					}
				}
			}
			break;
		}
		if (curseq == NULL || !SN_AreModesSame(snd, choice, curseq->GetModeNum()))
		{
			SN_StartSequence(m_Sector, CHAN_CEILING, snd, choice);
		}
	}
}

void DDoor::Construct(sector_t *sector)
{
	Super::Construct(sector);
}

//============================================================================
//
// [RH] SpawnDoor: Helper function for EV_DoDoor
//
//============================================================================

void DDoor::Construct(sector_t *sec, EVlDoor type, double speed, int delay, int lightTag, int topcountdown)
{
	vertex_t *spot;
	double height;

	Super::Construct(sec);

	m_Type = type;
	m_Speed = speed;
	m_TopWait = delay;
	m_TopCountdown = topcountdown;
	m_LightTag = lightTag;

	if (Level->i_compatflags & COMPATF_NODOORLIGHT)
	{
		m_LightTag = 0;
	}

	switch (type)
	{
	case doorClose:
		m_Direction = -1;
		height = FindLowestCeilingSurrounding (sec, &spot);
		m_TopDist = sec->ceilingplane.PointToDist (spot, height - 4);
		DoorSound (false);
		break;

	case doorOpen:
	case doorRaise:
		m_Direction = 1;
		height = FindLowestCeilingSurrounding (sec, &spot);
		m_TopDist = sec->ceilingplane.PointToDist (spot, height - 4);
		if (m_TopDist != sec->ceilingplane.fD())
			DoorSound (true);
		break;

	case doorCloseWaitOpen:
		m_TopDist = sec->ceilingplane.fD();
		m_Direction = -1;
		DoorSound (false);
		break;

	case doorWaitRaise:
		m_Direction = 2;
		height = FindLowestCeilingSurrounding (sec, &spot);
		m_TopDist = sec->ceilingplane.PointToDist (spot, height - 4);
		break;

	case doorWaitClose:
		m_Direction = 0;
		m_Type = DDoor::doorRaise;
		height = FindHighestFloorPoint (sec, &m_BotSpot);
		m_BotDist = sec->ceilingplane.PointToDist (m_BotSpot, height);
		m_OldFloorDist = sec->floorplane.fD();
		m_TopDist = sec->ceilingplane.fD();
		break;

	}

	if (!m_Sector->floordata || !m_Sector->floordata->IsKindOf(RUNTIME_CLASS(DPlat)) ||
		!(barrier_cast<DPlat*>(m_Sector->floordata))->IsLift())
	{
		height = FindHighestFloorPoint (sec, &m_BotSpot);
		m_BotDist = sec->ceilingplane.PointToDist (m_BotSpot, height);
	}
	else
	{
		height = FindLowestCeilingPoint(sec, &m_BotSpot);
		m_BotDist = sec->ceilingplane.PointToDist (m_BotSpot, height);
	}
	m_OldFloorDist = sec->floorplane.fD();
}

//============================================================================
//
// [RH] Merged EV_VerticalDoor and EV_DoLockedDoor into EV_DoDoor
//		and made them more general to support the new specials.
//
//============================================================================

bool FLevelLocals::EV_DoDoor (DDoor::EVlDoor type, line_t *line, AActor *thing,
				int tag, double speed, int delay, int lock, int lightTag, bool boomgen, int topcountdown)
{
	bool		rtn = false;
	int 		secnum;
	sector_t*	sec;

	if (lock != 0 && !P_CheckKeys (thing, lock, tag != 0))
		return false;

	if (tag == 0)
	{		// [RH] manual door
		if (!line)
			return false;

		// if the wrong side of door is pushed, give oof sound
		if (line->sidedef[1] == NULL)			// killough
		{
			S_Sound (thing, CHAN_VOICE, 0, "*usefail", 1, ATTN_NORM);
			return false;
		}

		// get the sector on the second side of activating linedef
		sec = line->sidedef[1]->sector;
		secnum = sec->sectornum;

		// if door already has a thinker, use it
		if (sec->PlaneMoving(sector_t::ceiling))
		{
			// Boom used remote door logic for generalized doors, even if they are manual
			if (boomgen)
				return false;
			if (sec->ceilingdata->IsKindOf (RUNTIME_CLASS(DDoor)))
			{
				DDoor *door = barrier_cast<DDoor *>(sec->ceilingdata);

				// ONLY FOR "RAISE" DOORS, NOT "OPEN"s
				if (door->m_Type == DDoor::doorRaise && type == DDoor::doorRaise)
				{
					if (door->m_Direction == -1)
					{
						door->m_Direction = 1;	// go back up
						door->DoorSound (true);	// [RH] Make noise
					}
					else if (!(line->activation & (SPAC_Push|SPAC_MPush)))
						// [RH] activate push doors don't go back down when you
						//		run into them (otherwise opening them would be
						//		a real pain).
					{
						if (!thing->player || thing->player->Bot != NULL)
							return false;	// JDC: bad guys never close doors
											//Added by MC: Neither do bots.

						door->m_Direction = -1;	// start going down immediately

						// Start the door close sequence.
						door->DoorSound(false, SN_CheckSequence(sec, CHAN_CEILING));
						return true;
					}
					else
					{
						return false;
					}
				}
			}
			return false;
		}
		if (CreateThinker<DDoor> (sec, type, speed, delay, lightTag, topcountdown))
			rtn = true;
	}
	else
	{	// [RH] Remote door

		auto it = GetSectorTagIterator(tag);
		while ((secnum = it.Next()) >= 0)
		{
			sec = &sectors[secnum];
			// if the ceiling is already moving, don't start the door action
			if (sec->PlaneMoving(sector_t::ceiling))
				continue;

			if (CreateThinker<DDoor>(sec, type, speed, delay, lightTag, topcountdown))
				rtn = true;
		}
				
	}
	return rtn;
}

//============================================================================
//
// animated doors
//
//============================================================================

IMPLEMENT_CLASS(DAnimatedDoor, false, false)

void DAnimatedDoor::Construct(sector_t *sec)
{
	Super::Construct(sec, false);
}

void DAnimatedDoor::Serialize(FSerializer &arc)
{
	Super::Serialize (arc);
	
	arc("line1", m_Line1)
		("line2", m_Line2)
		("frame", m_Frame)
		("timer", m_Timer)
		("botdist", m_BotDist)
		("status", m_Status)
		("speed", m_Speed)
		("delay", m_Delay)
		("dooranim", m_DoorAnim)
		("setblock1", m_SetBlocking1)
		("setblock2", m_SetBlocking2)
		("type", m_Type);
}

//============================================================================
//
// Starts a closing action on an animated door
//
//============================================================================

bool DAnimatedDoor::StartClosing ()
{
	// CAN DOOR CLOSE?
	if (m_Sector->touching_thinglist != NULL)
	{
		return false;
	}

	double topdist = m_Sector->ceilingplane.fD();
	if (m_Sector->MoveCeiling (2048., m_BotDist, 0, -1, false) == EMoveResult::crushed)
	{
		return false;
	}

	m_Sector->MoveCeiling (2048., topdist, 1);

	m_Line1->flags |= ML_BLOCKING;
	m_Line2->flags |= ML_BLOCKING;
	if (m_DoorAnim->CloseSound != NAME_None)
	{
		SN_StartSequence (m_Sector, CHAN_CEILING, m_DoorAnim->CloseSound, 1);
	}

	m_Status = Closing;
	m_Timer = m_Speed;
	return true;
}

//============================================================================
//
//
//
//============================================================================

void DAnimatedDoor::Tick ()
{
	if (m_DoorAnim == NULL)
	{
		// can only happen when a bad savegame is loaded.
		Destroy();
		return;
	}

	switch (m_Status)
	{
	case Dead:
		m_Sector->ceilingdata = nullptr;
		Destroy ();
		break;

	case Opening:
		if (!m_Timer--)
		{
			if (++m_Frame >= m_DoorAnim->NumTextureFrames)
			{
				// IF DOOR IS DONE OPENING...
				m_Line1->flags &= ~ML_BLOCKING;
				m_Line2->flags &= ~ML_BLOCKING;

				if (m_Delay == 0)
				{
					m_Sector->ceilingdata = nullptr;
					Destroy ();
					break;
				}

				m_Timer = m_Delay;
				m_Status = Waiting;
			}
			else
			{
				// IF DOOR NEEDS TO ANIMATE TO NEXT FRAME...
				m_Timer = m_Speed;

				m_Line1->sidedef[0]->SetTexture(side_t::mid, m_DoorAnim->TextureFrames[m_Frame]);
				m_Line1->sidedef[1]->SetTexture(side_t::mid, m_DoorAnim->TextureFrames[m_Frame]);
				m_Line2->sidedef[0]->SetTexture(side_t::mid, m_DoorAnim->TextureFrames[m_Frame]);
				m_Line2->sidedef[1]->SetTexture(side_t::mid, m_DoorAnim->TextureFrames[m_Frame]);
			}
		}
		break;

	case Waiting:
		// IF DOOR IS DONE WAITING...
		if (m_Type == adClose || !m_Timer--)
		{
			if (!StartClosing())
			{
				m_Timer = m_Delay;
			}
		}
		break;

	case Closing:
		if (!m_Timer--)
		{
			if (--m_Frame < 0)
			{
				// IF DOOR IS DONE CLOSING...
				m_Sector->MoveCeiling (2048., m_BotDist, -1);
				m_Sector->ceilingdata = nullptr;
				Destroy ();
				// Unset blocking flags on lines that didn't start with them. Since the
				// ceiling is down now, we shouldn't need this flag anymore to keep things
				// from getting through.
				if (!m_SetBlocking1)
				{
					m_Line1->flags &= ~ML_BLOCKING;
				}
				if (!m_SetBlocking2)
				{
					m_Line2->flags &= ~ML_BLOCKING;
				}
				break;
			}
			else
			{
				// IF DOOR NEEDS TO ANIMATE TO NEXT FRAME...
				m_Timer = m_Speed;

				m_Line1->sidedef[0]->SetTexture(side_t::mid, m_DoorAnim->TextureFrames[m_Frame]);
				m_Line1->sidedef[1]->SetTexture(side_t::mid, m_DoorAnim->TextureFrames[m_Frame]);
				m_Line2->sidedef[0]->SetTexture(side_t::mid, m_DoorAnim->TextureFrames[m_Frame]);
				m_Line2->sidedef[1]->SetTexture(side_t::mid, m_DoorAnim->TextureFrames[m_Frame]);
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

void DAnimatedDoor::Construct(sector_t *sec, line_t *line, int speed, int delay, FDoorAnimation *anim, DAnimatedDoor::EADType type)
{
	double topdist;
	FTextureID picnum;

	Super::Construct(sec, false);

	m_DoorAnim = anim;

	m_Line1 = line;
	m_Line2 = line;

	for (auto l : sec->Lines)
	{
		if (l == line)
			continue;

		if (l->sidedef[0]->GetTexture(side_t::top) == line->sidedef[0]->GetTexture(side_t::top))
		{
			m_Line2 = l;
			break;
		}
	}


	auto &tex1 = m_Line1->sidedef[0]->textures;
	tex1[side_t::mid].InitFrom(tex1[side_t::top]);

	auto &tex2 = m_Line2->sidedef[0]->textures;
	tex2[side_t::mid].InitFrom(tex2[side_t::top]);

	picnum = tex1[side_t::top].texture;

	auto tex = TexMan.GetGameTexture(picnum);
	topdist = tex ? tex->GetDisplayHeight() : 64;

	topdist = m_Sector->ceilingplane.fD() - topdist * m_Sector->ceilingplane.fC();

	m_Type = type;
	m_Status = type == adClose? Waiting : Opening;
	m_Speed = speed;
	m_Delay = delay;
	m_Timer = m_Speed;
	m_Frame = 0;
	m_SetBlocking1 = !!(m_Line1->flags & ML_BLOCKING);
	m_SetBlocking2 = !!(m_Line2->flags & ML_BLOCKING);
	m_Line1->flags |= ML_BLOCKING;
	m_Line2->flags |= ML_BLOCKING;
	m_BotDist = m_Sector->ceilingplane.fD();
	m_Sector->MoveCeiling (2048., topdist, 1);
	if (type == adOpenClose)
	{
		if (m_DoorAnim->OpenSound != NAME_None)
		{
			SN_StartSequence(m_Sector, CHAN_INTERIOR, m_DoorAnim->OpenSound, 1);
		}
	}
}

//============================================================================
//
// EV_SlidingDoor : slide a door horizontally
// (animate midtexture, then set noblocking line)
//
//============================================================================

bool FLevelLocals::EV_SlidingDoor (line_t *line, AActor *actor, int tag, int speed, int delay, DAnimatedDoor::EADType type)
{
	sector_t *sec;
	int secnum;
	bool rtn;

	secnum = -1;
	rtn = false;

	if (tag == 0)
	{
		// Manual sliding door
		sec = line->backsector;

		// Make sure door isn't already being animated
		if (sec->ceilingdata != NULL )
		{
			if (actor == NULL || actor->player == NULL)
				return false;

			if (sec->ceilingdata->IsA (RUNTIME_CLASS(DAnimatedDoor)))
			{
				DAnimatedDoor *door = barrier_cast<DAnimatedDoor *>(sec->ceilingdata);
				if (door->m_Status == DAnimatedDoor::Waiting)
				{
					return door->StartClosing();
				}
			}
			return false;
		}
		// Do not attempt to close the door if it already is
		else if (type == DAnimatedDoor::adClose)
			return false;
		FDoorAnimation *anim = TexAnim.FindAnimatedDoor (line->sidedef[0]->GetTexture(side_t::top));
		if (anim != NULL)
		{
			CreateThinker<DAnimatedDoor>(sec, line, speed, delay, anim, type);
			return true;
		}
		return false;
	}

	auto it = GetSectorTagIterator(tag);
	while ((secnum = it.Next()) >= 0)
	{
		sec = &sectors[secnum];
		if (sec->ceilingdata != NULL)
		{
			// Check if the animated door is already open and waiting, if so, close it.
			if (sec->ceilingdata->IsA (RUNTIME_CLASS(DAnimatedDoor)))
			{
				DAnimatedDoor *door = barrier_cast<DAnimatedDoor *>(sec->ceilingdata);
				if (door->m_Status == DAnimatedDoor::Waiting)
				{
					door->StartClosing();
				}
			}
			continue;
		}
		// Do not attempt to close the door if it already is
		else if (type == DAnimatedDoor::adClose)
			continue;

		for (auto line : sec->Lines)
		{
			if (line->backsector == NULL)
			{
				continue;
			}
			FDoorAnimation *anim = TexAnim.FindAnimatedDoor (line->sidedef[0]->GetTexture(side_t::top));
			if (anim != NULL)
			{
				rtn = true;
				CreateThinker<DAnimatedDoor>(sec, line, speed, delay, anim, type);
				break;
			}
		}
	}
	return rtn;
}

