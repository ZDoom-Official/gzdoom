//-----------------------------------------------------------------------------
//
// Copyright 1993-1994 id Software
// Copyright 1994-1996 Raven Software
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
#include <assert.h>

#include "doomdef.h"

#include "p_local.h"
#include "p_maputl.h"
#include "p_blockmap.h"
#include "m_random.h"
#include "p_lnspec.h"
#include "po_man.h"
#include "b_bot.h"
#include "p_spec.h"
#include "vm.h"

#include "g_levellocals.h"
#include "actorinlines.h"
#include "shadowinlines.h"

static FRandom pr_botchecksight ("BotCheckSight");
static FRandom pr_checksight ("CheckSight");

/*
==============================================================================

							P_CheckSight

This uses specialized forms of the maputils routines for optimized performance

==============================================================================
*/

// Performance meters
static int sightcounts[6];
static cycle_t SightCycles;
static cycle_t MaxSightCycles;

enum
{
	SO_TOPFRONT = 1,
	SO_TOPBACK = 2,
	SO_BOTTOMFRONT = 4,
	SO_BOTTOMBACK = 8,

};

struct SightOpening
{
	double top;
	double bottom;
	int range;
	int portalflags;

	void SwapSides()
	{
		portalflags = ((portalflags & (SO_TOPFRONT | SO_BOTTOMFRONT)) << 1) | ((portalflags & (SO_TOPBACK | SO_BOTTOMBACK)) >> 1);
	}
};

struct SightTask
{
	double Frac;
	double topslope;
	double bottomslope;
	int direction;
	int portalgroup;
};


static TArray<intercept_t> intercepts (128);
static TArray<SightTask> portals(32);

class SightCheck
{
	FLevelLocals *Level;
	DVector3 sightstart;
	DVector2 sightend;
	double Startfrac;

	AActor * seeingthing;
	double Lastztop;				// z at last line
	double Lastzbottom;				// z at last line
	sector_t * lastsector;			// last sector being entered by trace
	double topslope, bottomslope;	// slopes to top and bottom of target
	int Flags;
	divline_t Trace;
	int portaldir;
	int portalgroup;
	bool portalfound;
	unsigned int myseethrough;

	void P_SightOpening(SightOpening &open, const line_t *linedef, double x, double y);
	bool PTR_SightTraverse (intercept_t *in);
	bool P_SightCheckLine (line_t *ld);
	int P_SightBlockLinesIterator (int x, int y);
	bool P_SightTraverseIntercepts ();
	bool LineBlocksSight(line_t *ld);

public:
	SightCheck(FLevelLocals *l)
	{
		Level = l;
	}

	bool P_SightPathTraverse ();

	void init(AActor * t1, AActor * t2, sector_t *startsector, SightTask *task, int flags)
	{
		sightstart = t1->PosRelative(task->portalgroup);
		sightend = t2->PosRelative(task->portalgroup).XY();
		sightstart.Z += t1->Height * 0.75;

		portalgroup = task->portalgroup;
		Startfrac = task->Frac;
		Trace = { sightstart.X, sightstart.Y, sightend.X - sightstart.X, sightend.Y - sightstart.Y };
		Lastztop = Lastzbottom = sightstart.Z;
		lastsector = startsector;
		seeingthing=t2;
		topslope = task->topslope;
		bottomslope = task->bottomslope;
		Flags = flags;
		portaldir = task->direction;
		portalfound = false;

		myseethrough = FF_SEETHROUGH;
	}
};

//==========================================================================
//
// P_SightOpening
//
// Simplified version that removes everything not needed for a sight check
//
//==========================================================================

void SightCheck::P_SightOpening(SightOpening &open, const line_t *linedef, double x, double y)
{
	open.portalflags = 0;
	open.bottom = open.top = 0;
	sector_t *front = linedef->frontsector;
	sector_t *back = linedef->backsector;

	if (back == NULL)
	{
		// single sided line
		if (linedef->flags & ML_PORTALCONNECT)
		{
			if (!front->PortalBlocksSight(sector_t::ceiling)) open.top = LINEOPEN_MAX, open.portalflags |= SO_TOPFRONT;
			if (!front->PortalBlocksSight(sector_t::floor)) open.bottom = LINEOPEN_MIN, open.portalflags |= SO_BOTTOMFRONT;
			if (open.top == 0) open.top = front->floorplane.ZatPoint(x, y);
			if (open.bottom == 0) open.bottom = front->ceilingplane.ZatPoint(x, y);
		}

		open.range = 0;
		return;
	}


	double fc = 0, ff = 0, bc = 0, bf = 0;

	if (linedef->flags & ML_PORTALCONNECT)
	{
		if (!front->PortalBlocksSight(sector_t::ceiling)) fc = LINEOPEN_MAX, open.portalflags |= SO_TOPFRONT;
		if (!back->PortalBlocksSight(sector_t::ceiling)) bc = LINEOPEN_MAX, open.portalflags |= SO_TOPBACK;
		if (!front->PortalBlocksSight(sector_t::floor)) ff = LINEOPEN_MIN, open.portalflags |= SO_BOTTOMFRONT;
		if (!back->PortalBlocksSight(sector_t::floor)) bf = LINEOPEN_MIN, open.portalflags |= SO_BOTTOMBACK;
	}

	if (fc == 0) fc = front->ceilingplane.ZatPoint(x, y);
	if (bc == 0) bc = back->ceilingplane.ZatPoint(x, y);
	if (ff == 0) ff = front->floorplane.ZatPoint(x, y);
	if (bf == 0) bf = back->floorplane.ZatPoint(x, y);

	open.bottom = max(ff, bf);
	open.top = min(fc, bc);

	// we only want to know if there is an opening, not how large it is.
	open.range = open.bottom < open.top;
}


/*
==============
=
= PTR_SightTraverse
=
==============
*/

bool SightCheck::PTR_SightTraverse (intercept_t *in)
{
	line_t  *li;
	double slope;
	SightOpening open;
	int  frontflag = -1;

	li = in->d.line;

//
// crosses a two sided line
//

	// ignore self referencing sectors if COMPAT_TRACE is on
	if ((Level->i_compatflags & COMPATF_TRACE) && li->frontsector == li->backsector)
		return true;

	double trX = Trace.x + Trace.dx * in->frac;
	double trY = Trace.y + Trace.dy * in->frac;

	P_SightOpening(open, li, trX, trY);
	if (LineBlocksSight(in->d.line))
	{
		// This may not skip P_SightOpening, but only reduce the open range to 0.
		open.range = 0;
		if (open.bottom != LINEOPEN_MIN) open.bottom = open.top;
	}

	FLinePortal *lport = li->getPortal();

	if (open.range == 0 && open.portalflags == 0 && (lport == nullptr || lport->mType != PORTT_LINKED))		// quick test for totally closed doors (must be delayed if portal checks are needed, though)
		return false;		// stop

	if (in->frac == 0)
		return true;

	// check bottom
	if (open.bottom > LINEOPEN_MIN)
	{
		slope = (open.bottom - sightstart.Z) / in->frac;
		if (slope > bottomslope)
			bottomslope = slope;
	}

	// check top
	if (open.top < LINEOPEN_MAX)
	{
		slope = (open.top - sightstart.Z) / in->frac;
		if (slope < topslope)
			topslope = slope;
	}

	if (open.portalflags)
	{
		sector_t *frontsec, *backsec;
		frontflag = P_PointOnLineSidePrecise(sightstart, li);
		if (!frontflag)
		{
			frontsec = li->frontsector;
			backsec = li->backsector;
		}
		else
		{
			frontsec = li->backsector;
			if (!frontsec) return false;	// We are looking through the backside of a one-sided line. Just abort if that happens.
			backsec = li->frontsector;
			open.SwapSides();				// swap flags to make the next checks simpler.
		}

		if (portaldir != sector_t::floor && (open.portalflags & SO_TOPBACK) && !(open.portalflags & SO_TOPFRONT))
		{
			portals.Push({ in->frac, topslope, bottomslope, sector_t::ceiling, backsec->GetOppositePortalGroup(sector_t::ceiling) });
		}
		if (portaldir != sector_t::ceiling && (open.portalflags & SO_BOTTOMBACK) && !(open.portalflags & SO_BOTTOMFRONT))
		{
			portals.Push({ in->frac, topslope, bottomslope, sector_t::floor, backsec->GetOppositePortalGroup(sector_t::floor) });
		}
	}
	if (lport != nullptr && lport->mDestination != nullptr)
	{
		portals.Push({ in->frac, topslope, bottomslope, portaldir, lport->mDestination->frontsector->PortalGroup });
		return false;
	}

	if (topslope <= bottomslope || open.range == 0)
		return false;		// stop

	// now handle 3D-floors
	if(li->frontsector->e->XFloor.ffloors.Size() || li->backsector->e->XFloor.ffloors.Size())
	{
		if (frontflag == -1) frontflag = P_PointOnLineSidePrecise(sightstart, li);
		
		//Check 3D FLOORS!
		for(int i=1;i<=2;i++)
		{
			sector_t * s=i==1? li->frontsector:li->backsector;
			double    highslope, lowslope;

			double topz= topslope * in->frac + sightstart.Z;
			double bottomz= bottomslope * in->frac + sightstart.Z;

			for (auto rover : s->e->XFloor.ffloors)
			{
				if ((rover->flags & FF_SEETHROUGH) == myseethrough || !(rover->flags & FF_EXISTS)) continue;
				if ((Flags & SF_IGNOREWATERBOUNDARY) && (rover->flags & FF_SOLID) == 0) continue;

				double ff_bottom = rover->bottom.plane->ZatPoint(trX, trY);
				double ff_top = rover->top.plane->ZatPoint(trX, trY);

				highslope = (ff_top - sightstart.Z) / in->frac;
				lowslope = (ff_bottom - sightstart.Z) / in->frac;

				if (highslope >= topslope)
				{
					// blocks completely
					if (lowslope <= bottomslope) return false;
					// blocks upper edge of view
					if (lowslope < topslope) topslope = lowslope;
				}
				else if (lowslope <= bottomslope)
				{
					// blocks lower edge of view
					if (highslope > bottomslope)  bottomslope = highslope;
				}
				else
				{
					// the 3D-floor is inside the viewing cone but neither clips the top nor the bottom so by 
					// itself it can't be view blocking.
					// However, if there's a 3D-floor on the other side that obstructs the same vertical range
					// the 2 together will block sight.
					sector_t * sb = i == 2 ? li->frontsector : li->backsector;

					for (auto rover2 : sb->e->XFloor.ffloors)
					{
						if ((rover2->flags & FF_SEETHROUGH) == myseethrough || !(rover2->flags & FF_EXISTS)) continue;
						if ((Flags & SF_IGNOREWATERBOUNDARY) && (rover->flags & FF_SOLID) == 0) continue;

						double ffb_bottom = rover2->bottom.plane->ZatPoint(trX, trY);
						double ffb_top = rover2->top.plane->ZatPoint(trX, trY);

						if ((ffb_bottom >= ff_bottom && ffb_bottom <= ff_top) ||
							(ffb_top <= ff_top && ffb_top >= ff_bottom) ||
							(ffb_top >= ff_top && ffb_bottom <= ff_bottom) ||
							(ffb_top <= ff_top && ffb_bottom >= ff_bottom))
						{
							return false;
						}
					}
				}
				// trace is leaving a sector with a 3d-floor
				if (s == lastsector && frontflag == i - 1)
				{
					// upper slope intersects with this 3d-floor
					if (Lastztop <= ff_bottom && topz > ff_top)
					{
						topslope = lowslope;
					}
					// lower slope intersects with this 3d-floor
					if (Lastzbottom >= ff_top && bottomz < ff_top)
					{
						bottomslope = highslope;
					}
				}
				if (topslope <= bottomslope) return false;		// stop
			}
		}
		lastsector = frontflag==0 ? li->backsector : li->frontsector;
	}
	else lastsector=NULL;	// don't need it if there are no 3D-floors

	Lastztop = (topslope * in->frac) + sightstart.Z;
	Lastzbottom = (bottomslope * in->frac) + sightstart.Z;

	return true;			// keep going
}


// performs trivial visibility checks.
bool SightCheck::LineBlocksSight(line_t *ld)
{
	// try to early out the check
	if (!ld->backsector || !(ld->flags & ML_TWOSIDED) || (ld->flags & ML_BLOCKSIGHT))
		return true;	// stop checking

						// [RH] don't see past block everything lines
	if (ld->flags & ML_BLOCKEVERYTHING)
	{
		if (!(Flags & SF_SEEPASTBLOCKEVERYTHING))
		{
			return true;
		}
		// Pretend the other side is invisible if this is not an impact line
		// that runs a script on the current map. Used to prevent monsters
		// from trying to attack through a block everything line unless
		// there's a chance their attack will make it nonblocking.
		if (!(Flags & SF_SEEPASTSHOOTABLELINES))
		{
			if (!(ld->activation & SPAC_Impact))
			{
				return true;
			}
			if (ld->special != ACS_Execute && ld->special != ACS_ExecuteAlways)
			{
				return true;
			}
			if (ld->args[1] != 0 && ld->args[1] != Level->levelnum)
			{
				return true;
			}
		}
	}
	return false;
}

/*
==================
=
= P_SightCheckLine
=
===================
*/

bool SightCheck::P_SightCheckLine (line_t *ld)
{
	divline_t dl;

	if (ld->validcount == validcount)
	{
		return true;
	}
	ld->validcount = validcount;
	if (P_PointOnDivlineSide (ld->v1->fPos(), &Trace) ==
		P_PointOnDivlineSide (ld->v2->fPos(), &Trace))
	{
		return true;		// line isn't crossed
	}
	P_MakeDivline (ld, &dl);
	if (P_PointOnDivlineSide (Trace.x, Trace.y, &dl) ==
		P_PointOnDivlineSide (Trace.x+Trace.dx, Trace.y+Trace.dy, &dl))
	{
		return true;		// line isn't crossed
	}

	if (!portalfound)	// when portals come into play, the quick-outs here may not be performed
	{
		if (LineBlocksSight(ld)) return false;
	}

	sightcounts[3]++;
	// store the line for later intersection testing
	intercept_t newintercept;
	newintercept.isaline = true;
	newintercept.d.line = ld;
	intercepts.Push (newintercept);

	return true;
}

/*
==================
=
= P_SightBlockLinesIterator
=
===================
*/

int SightCheck::P_SightBlockLinesIterator (int x, int y)
{
	int offset;
	int *list;
	int res = 1;

	polyblock_t *polyLink;
	unsigned int i;

	offset = y* Level->blockmap.bmapwidth+x;

	// if any of the previous blocks may contain a portal we may abort the collection of lines here, but we may not abort the sight check.
	// (We still try to delay activating this for as long as possible.)
	portalfound = portalfound || Level->PortalBlockmap(x, y).containsLinkedPortals;

	polyLink = Level->PolyBlockMap[offset];
	portalfound |= (polyLink && Level->PortalBlockmap.hasLinkedPolyPortals);
	while (polyLink)
	{
		if (polyLink->polyobj)
		{ // only check non-empty links
			if (polyLink->polyobj->validcount != validcount)
			{
				polyLink->polyobj->validcount = validcount;
				for (i = 0; i < polyLink->polyobj->Linedefs.Size(); i++)
				{
					if (!P_SightCheckLine(polyLink->polyobj->Linedefs[i]))
					{
						if (!portalfound) return 0;
						else res = -1;
					}
				}
			}
		}
		polyLink = polyLink->next;
	}

	for (list = Level->blockmap.GetLines(x, y); *list != -1; list++)
	{
		if (!P_SightCheckLine (&Level->lines[*list]))
		{
			if (!portalfound) return 0;
			else res = -1;
		}
	}

	return res;			// everything was checked
}

/*
====================
=
= P_SightTraverseIntercepts
=
= Returns true if the traverser function returns true for all lines
====================
*/

bool SightCheck::P_SightTraverseIntercepts ()
{
	unsigned count;
	double dist;
	intercept_t *scan, *in;
	unsigned scanpos;
	divline_t dl;

	count = intercepts.Size ();
//
// calculate intercept distance
//
	for (scanpos = 0; scanpos < intercepts.Size (); scanpos++)
	{
		scan = &intercepts[scanpos];
		P_MakeDivline (scan->d.line, &dl);
		scan->frac = P_InterceptVector (&Trace, &dl);
		if (scan->frac < Startfrac)
		{
			scan->frac = INT_MAX;
			count--;
		}
	}

//
// go through in order
// proper order is needed to handle 3D floors and portals.
//
	in = NULL;

	while (count--)
	{
		dist = INT_MAX;
		for (scanpos = 0; scanpos < intercepts.Size (); scanpos++)
		{
			scan = &intercepts[scanpos];
			if (scan->frac < dist)
			{
				dist = scan->frac;
				in = scan;
			}
		}

		if (in != NULL)
		{
			if (!PTR_SightTraverse (in))
				return false;					// don't bother going farther
			in->frac = INT_MAX;
		}
	}

	if (lastsector == seeingthing->Sector && lastsector->e->XFloor.ffloors.Size())
	{
		// we must do one last check whether the trace has crossed a 3D floor in the last sector

		double topz = topslope + sightstart.Z;
		double bottomz = bottomslope + sightstart.Z;

		for (auto rover : lastsector->e->XFloor.ffloors)
		{
			if ((rover->flags & FF_SEETHROUGH) == myseethrough || !(rover->flags & FF_EXISTS)) continue;
			if ((Flags & SF_IGNOREWATERBOUNDARY) && (rover->flags & FF_SOLID) == 0) continue;

			double ff_bottom = rover->bottom.plane->ZatPoint(seeingthing);
			double ff_top = rover->top.plane->ZatPoint(seeingthing);

			if (Lastztop <= ff_bottom && topz > ff_bottom && Lastzbottom <= ff_bottom && bottomz > ff_bottom) return false;
			if (Lastzbottom >= ff_top && bottomz < ff_top && Lastztop >= ff_top && topz < ff_top) return false;
		}
	}
	return true;			// everything was traversed
}



/*
==================
=
= P_SightPathTraverse
=
= Traces a line from x1,y1 to x2,y2, calling the traverser function for each block
= Returns true if the traverser function returns true for all lines
==================
*/

bool SightCheck::P_SightPathTraverse ()
{
	double x1, x2, y1, y2;
	double xt1,yt1,xt2,yt2;
	double xstep,ystep;
	double partialx, partialy;
	double xintercept, yintercept;
	int mapx, mapy, mapxstep, mapystep;
	int count;

	validcount++;
	intercepts.Clear ();
	x1 = sightstart.X + Startfrac * Trace.dx;
	y1 = sightstart.Y + Startfrac * Trace.dy;
	x2 = sightend.X;
	y2 = sightend.Y;
	if (lastsector == NULL) lastsector = Level->PointInSector(x1, y1);

	// for FF_SEETHROUGH the following rule applies:
	// If the viewer is in an area without FF_SEETHROUGH he can only see into areas without this flag
	// If the viewer is in an area with FF_SEETHROUGH he can only see into areas with this flag
	bool checkfloor = true, checkceiling = true;
	for(auto rover : lastsector->e->XFloor.ffloors)
	{
		if(!(rover->flags & FF_EXISTS)) continue;
		if ((Flags & SF_IGNOREWATERBOUNDARY) && (rover->flags & FF_SOLID) == 0) continue;

		double ff_bottom=rover->bottom.plane->ZatPoint(sightstart);
		double ff_top=rover->top.plane->ZatPoint(sightstart);

		if (sightstart.Z < ff_top) checkceiling = false;
		if (sightstart.Z >= ff_bottom) checkfloor = false;

		if (sightstart.Z < ff_top && sightstart.Z >= ff_bottom) 
		{
			myseethrough = rover->flags & FF_SEETHROUGH;
			break;
		}
	}

	// We also must check if the starting sector contains  portals, and start sight checks in those as well.
	if (portaldir != sector_t::floor && checkceiling && !lastsector->PortalBlocksSight(sector_t::ceiling))
	{
		portals.Push({ 0, topslope, bottomslope, sector_t::ceiling, lastsector->GetOppositePortalGroup(sector_t::ceiling) });
	}
	if (portaldir != sector_t::ceiling && checkfloor && !lastsector->PortalBlocksSight(sector_t::floor))
	{
		portals.Push({ 0, topslope, bottomslope, sector_t::floor, lastsector->GetOppositePortalGroup(sector_t::floor) });
	}

	x1 -= Level->blockmap.bmaporgx;
	y1 -= Level->blockmap.bmaporgy;
	xt1 = x1 / FBlockmap::MAPBLOCKUNITS;
	yt1 = y1 / FBlockmap::MAPBLOCKUNITS;

	x2 -= Level->blockmap.bmaporgx;
	y2 -= Level->blockmap.bmaporgy;
	xt2 = x2 / FBlockmap::MAPBLOCKUNITS;
	yt2 = y2 / FBlockmap::MAPBLOCKUNITS;

	mapx = xs_FloorToInt(xt1);
	mapy = xs_FloorToInt(yt1);
	int mapex = xs_FloorToInt(xt2);
	int mapey = xs_FloorToInt(yt2);


	if (mapex > mapx)
	{
		mapxstep = 1;
		partialx = 1. - xt1 + xs_FloorToInt(xt1);
		ystep = (y2 - y1) / fabs(x2 - x1);
	}
	else if (mapex < mapx)
	{
		mapxstep = -1;
		partialx = xt1 - xs_FloorToInt(xt1);
		ystep = (y2 - y1) / fabs(x2 - x1);
	}
	else
	{
		mapxstep = 0;
		partialx = 1.;
		ystep = 256;
	}
	yintercept = yt1 + partialx * ystep;

	if (mapey > mapy)
	{
		mapystep = 1;
		partialy = 1. - yt1 + xs_FloorToInt(yt1);
		xstep = (x2 - x1) / fabs(y2 - y1);
	}
	else if (mapey < mapy)
	{
		mapystep = -1;
		partialy = yt1 - xs_FloorToInt(yt1);
		xstep = (x2 - x1) / fabs(y2 - y1);
	}
	else
	{
		mapystep = 0;
		partialy = 1;
		xstep = 256;
	}
	xintercept = xt1 + partialy * xstep;

	// [RH] Fix for traces that pass only through blockmap corners. In that case,
	// xintercept and yintercept can both be set ahead of mapx and mapy, so the
	// for loop would never advance anywhere.

	if (fabs(xstep) == 1. && fabs(ystep) == 1.)
	{
		if (ystep < 0)
		{
			partialx = 1. - partialx;
		}
		if (xstep < 0)
		{
			partialy = 1. - partialy;
		}
		if (partialx == partialy)
		{
			xintercept = xt1;
			yintercept = yt1;
		}
	}

//
// step through map blocks
// Count is present to prevent a round off error from skipping the break

	int itres = -1;
	for (count = 0 ; count < 1000 ; count++)
	{
		// end traversing when reaching the end of the blockmap
		// an early out is not possible because with portals a trace can easily land outside the map's bounds.
		if (!Level->blockmap.isValidBlock(mapx, mapy))
		{
			break;
		}
		itres = P_SightBlockLinesIterator(mapx, mapy);
		if (itres == 0)
		{
			sightcounts[1]++;
			return false;	// early out
		}

		// either reached the end or had an early-out condition with portals left to check,
		if (itres == -1 || (mapxstep | mapystep) == 0)
			break;

		switch (((xs_FloorToInt(yintercept) == mapy) << 1) | (xs_FloorToInt(xintercept) == mapx))
		{
		case 0:		// neither xintercept nor yintercept match!
sightcounts[5]++;
			// Continuing won't make things any better, so we might as well stop right here
			return false;

		case 1:		// xintercept matches
			xintercept += xstep;
			mapy += mapystep;
			if (mapy == mapey)
				mapystep = 0;
			break;

		case 2:		// yintercept matches
			yintercept += ystep;
			mapx += mapxstep;
			if (mapx == mapex)
				mapxstep = 0;
			break;

		case 3:		// xintercept and yintercept both match
			sightcounts[4]++;
			// The trace is exiting a block through its corner. Not only does the block
			// being entered need to be checked (which will happen when this loop
			// continues), but the other two blocks adjacent to the corner also need to
			// be checked.
			if (!P_SightBlockLinesIterator (mapx + mapxstep, mapy) ||
				!P_SightBlockLinesIterator (mapx, mapy + mapystep))
			{
sightcounts[1]++;
				return false;
			}
			xintercept += xstep;
			yintercept += ystep;
			mapx += mapxstep;
			mapy += mapystep;
			if (mapx == mapex)
				mapxstep = 0;
			if (mapy == mapey)
				mapystep = 0;
			break;
		}
	}


//
// couldn't early out, so go through the sorted list
//
sightcounts[2]++;

	bool traverseres = P_SightTraverseIntercepts ( );
	if (itres == -1) return false;	// if the iterator had an early out there was no line of sight. The traverser was only called to collect more portals.
	if (seeingthing->Sector->PortalGroup != portalgroup) return false;	// We are in a different group than the seeingthing, so this trace cannot determine visibility alone.
	return traverseres;
}

/*
=====================
=
= P_CheckSight
=
= Returns true if a straight line between t1 and t2 is unobstructed
= look from eyes of t1 to any part of t2
=
= killough 4/20/98: cleaned up, made to use new LOS struct
=
=====================
*/

ETraceStatus CheckForSightBlockers(FTraceResults& res, void* userdata)
{
	SightCheckData* output = (SightCheckData*)userdata;
	if (res.HitType == TRACE_HitActor && res.Actor && (res.Actor->flags9 & MF9_BLOCKSIGHT))
	{
		output->HitSightBlocker = res.Actor;
		return TRACE_Stop;
	}

	if (res.HitType != TRACE_HitActor)
	{
		return TRACE_Stop;
	}

	return TRACE_Continue;
}

// [inkoalawetrust] Check if an MF9_BLOCKSIGHT actor is standing between t1 and t2.
AActor* P_CheckForSightBlock(AActor* t1, AActor* t2)
{
	FTraceResults result;
	SightCheckData SightCheck;
	SightCheck.HitSightBlocker = nullptr;
	DVector3 dir = t1->Vec3To(t2);
	double dist = dir.Length();
	DVector3 origin = t1->PosPlusZ(t1->Height * 0.75); //Standard sight checks are done at this height too.

	Trace(origin, t1->Sector, dir, dist, ActorFlags::FromInt(0xFFFFFFFF), ML_BLOCKEVERYTHING, t1, result, 0, CheckForSightBlockers, &SightCheck);

	return SightCheck.HitSightBlocker;
}

int P_CheckSight (AActor *t1, AActor *t2, int flags)
{
	SightCycles.Clock();

	bool res;

	if (t1 == nullptr || t2 == nullptr)
	{
		return false;
	}

	if ((t2->flags8 & MF8_MVISBLOCKED) && !(flags & SF_IGNOREVISIBILITY))
	{
		return false;
	}

	auto s1 = t1->Sector;
	auto s2 = t2->Sector;
	//
	// check for trivial rejection
	//
	if (!t1->Level->CheckReject(s1, s2))
	{
sightcounts[0]++;
		res = false;			// can't possibly be connected
		goto done;
	}

//
// check precisely
//
	// [RH] Andy Baker's stealth monsters:
	// Cannot see an invisible object
	if ((flags & SF_IGNOREVISIBILITY) == 0 &&
		((t2->renderflags & RF_INVISIBLE) ||
		(t2->flags8 & MF8_MINVISIBLE) ||
		!t2->RenderStyle.IsVisible(t2->Alpha)))
	{ // small chance of an attack being made anyway
		if ((t1->Level->BotInfo.m_Thinking ? pr_botchecksight() : pr_checksight()) > 50)
		{
			res = false;
			goto done;
		}
	}

	// killough 4/19/98: make fake floors and ceilings block monster view

	if (!(flags & SF_IGNOREWATERBOUNDARY))
	{
		if ((s1->GetHeightSec() &&
			((t1->Top() <= s1->heightsec->floorplane.ZatPoint(t1) &&
			  t2->Z() >= s1->heightsec->floorplane.ZatPoint(t2)) ||
			 (t1->Z() >= s1->heightsec->ceilingplane.ZatPoint(t1) &&
			  t2->Top() <= s1->heightsec->ceilingplane.ZatPoint(t2))))
			||
			(s2->GetHeightSec() &&
			 ((t2->Top() <= s2->heightsec->floorplane.ZatPoint(t2) &&
			   t1->Z() >= s2->heightsec->floorplane.ZatPoint(t1)) ||
			  (t2->Z() >= s2->heightsec->ceilingplane.ZatPoint(t2) &&
			   t1->Top() <= s2->heightsec->ceilingplane.ZatPoint(t1)))))
		{
			res = false;
			goto done;
		}
	}

	// [inkoalawetrust] All trivial checks passed, now check for MF9_BLOCKSIGHT
	if (t1->flags9 & MF9_DOBLOCKSIGHT)
	{
		AActor* blocker = P_CheckForSightBlock(t1, t2);

		if (blocker != nullptr)
		{
			res = false;
			goto done;
		}
	}

	// An unobstructed LOS is possible.
	// Now look from eyes of t1 to any part of t2.

	validcount++;
	portals.Clear();
	{
		sector_t *sec;
		double lookheight = t1->Z() + t1->Height*0.75;
		t1->GetPortalTransition(lookheight, &sec);

		double bottomslope = t2->Z() - lookheight;
		double topslope = bottomslope + t2->Height;
		SightTask task = { 0, topslope, bottomslope, -1, sec->PortalGroup };


		SightCheck s(t1->Level);
		s.init(t1, t2, sec, &task, flags);
		res = s.P_SightPathTraverse ();
		if (!res)
		{
			double dist = t1->Distance2D(t2);
			for (unsigned i = 0; i < portals.Size(); i++)
			{
				portals[i].Frac += 1 / dist;
				s.init(t1, t2, NULL, &portals[i], flags);
				if (s.P_SightPathTraverse())
				{
					res = true;
					break;
				}
			}
		}
	}

done:
	SightCycles.Unclock();
	return res;
}

ADD_STAT (sight)
{
	FString out;
	out.Format ("%04.1f ms (%04.1f max), %5d %2d%4d%4d%4d%4d\n",
		SightCycles.TimeMS(), MaxSightCycles.TimeMS(),
		sightcounts[3], sightcounts[0], sightcounts[1], sightcounts[2], sightcounts[4], sightcounts[5]);
	return out;
}

void P_ResetSightCounters (bool full)
{
	if (full)
	{
		MaxSightCycles.Reset();
	}
	if (SightCycles.Time() > MaxSightCycles.Time())
	{
		MaxSightCycles = SightCycles;
	}
	SightCycles.Reset();
	memset (sightcounts, 0, sizeof(sightcounts));
}
