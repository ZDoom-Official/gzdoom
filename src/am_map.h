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
#ifndef __AMMAP_H__
#define __AMMAP_H__

#include "dobject.h"

struct event_t;
class FSerializer;
struct FLevelLocals;

class DAutomapBase : public DObject
{
	DECLARE_ABSTRACT_CLASS(DAutomapBase, DObject);
public:
	FLevelLocals *Level;	// temporary location so that it can be set from the outside.

	// Called by main loop.
	virtual bool Responder(event_t* ev, bool last) = 0;

	// Called by main loop.
	virtual void Ticker(void) = 0;

	// Called by main loop,
	// called instead of view drawer if automap active.
	virtual void Drawer(int bottom) = 0;

	// Used for am_path drawing to calculate distance between ticks.
	DVector2 last_tick_pos;

	virtual void NewResolution() = 0;
	virtual void LevelInit() = 0;
	virtual void UpdateShowAllLines() = 0;
	virtual void GoBig() = 0;
	virtual void ResetFollowLocation() = 0;
	virtual int addMark() = 0;
	virtual bool clearMarks() = 0;
	virtual DVector2 GetPosition() = 0;
	virtual void startDisplay() = 0;

};

void AM_StaticInit();
void AM_ClearColorsets();	// reset data for a restart.
DAutomapBase *AM_Create(FLevelLocals *Level);
void AM_Stop();
void AM_ToggleMap();

#endif
