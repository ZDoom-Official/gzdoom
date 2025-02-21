// 
//---------------------------------------------------------------------------
//
// Copyright(C) 2002-2016 Christoph Oelckers
// All rights reserved.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//--------------------------------------------------------------------------
//

#include "a_sharedglobal.h"
#include "r_sky.h"
#include "r_state.h"
#include "r_utility.h"
#include "doomdata.h"
#include "g_levellocals.h"
#include "p_lnspec.h"
#include "texturemanager.h"
#include "hwrenderer/scene/hw_drawinfo.h"
#include "hwrenderer/scene/hw_drawstructs.h"
#include "hwrenderer/scene/hw_portal.h"
#include "hw_lighting.h"
#include "hw_material.h"
#include "hw_walldispatcher.h"

CVAR(Bool,gl_noskyboxes, false, 0)

//===========================================================================
//
// 
//
//===========================================================================

FTextureID GetSkyTexture(sector_t* sec, int plane, int second)
{
	auto tex = sec->planes[plane].skytexture[second];
	if (tex.isValid()) return tex;
	return second ? sec->Level->skytexture2 : sec->Level->skytexture1;
}

//==========================================================================
//
//  Set up the skyinfo struct
//
//==========================================================================

void HWSkyInfo::init(HWDrawInfo *di, sector_t* sec, int skypos, int sky1, PalEntry FadeColor)
{
	memset(this, 0, sizeof(*this));
	if ((sky1 & PL_SKYFLAT) && (sky1 & (PL_SKYFLAT - 1)))
	{
		const line_t *l = &di->Level->lines[(sky1&(PL_SKYFLAT - 1)) - 1];
		const side_t *s = l->sidedef[0];
		int pos;

		if (di->Level->flags & LEVEL_SWAPSKIES && s->GetTexture(side_t::bottom).isValid())
		{
			pos = side_t::bottom;
		}
		else
		{
			pos = side_t::top;
		}

		FTextureID texno = s->GetTexture(pos);
		auto tex = TexMan.GetGameTexture(texno, true);
		if (!tex || !tex->isValid()) goto normalsky;
		texture[0] = tex;
		skytexno1 = texno;
		x_offset[0] = s->GetTextureXOffset(pos) * (360.f/65536.f);
		y_offset = s->GetTextureYOffset(pos);
		mirrored = !l->args[2];
	}
	else
	{
	normalsky:
		auto skytex1 = GetSkyTexture(sec, skypos, false);
		auto skytex2 = GetSkyTexture(sec, skypos, true);
		if (di->Level->flags&LEVEL_DOUBLESKY)
		{
			auto tex1 = TexMan.GetGameTexture(skytex1);
			texture[1] = tex1;
			x_offset[1] = di->Level->hw_sky1pos;
			doublesky = true;
		}

		if ((di->Level->flags&LEVEL_SWAPSKIES || (sky1 == PL_SKYFLAT) || (di->Level->flags&LEVEL_DOUBLESKY)) &&
			skytex2 != skytex1)	// If both skies are equal use the scroll offset of the first!
		{
			texture[0] = TexMan.GetGameTexture(skytex2, true);
			skytexno1 = skytex2;
			sky2 = true;
			x_offset[0] = di->Level->hw_sky2pos;
		}
		else if (!doublesky)
		{
			texture[0] = TexMan.GetGameTexture(skytex1, true);
			skytexno1 = di->Level->skytexture1;
			x_offset[0] = di->Level->hw_sky1pos;
		}
	}
	if (di->Level->skyfog > 0)
	{
		fadecolor = FadeColor;
		fadecolor.a = 0;
	}
	else fadecolor = 0;

}


//==========================================================================
//
//  Calculate sky texture for ceiling or floor
//
//==========================================================================

void HWWall::SkyPlane(HWWallDispatcher *di, sector_t *sector, int plane, bool allowreflect)
{
	int ptype = -1;

	FSectorPortal *sportal = sector->ValidatePortal(plane);
	if (sportal != nullptr && sportal->mFlags & PORTSF_INSKYBOX) sportal = nullptr;	// no recursions, delete it here to simplify the following code

	// Either a regular sky or a skybox with skyboxes disabled
	if ((sportal == nullptr && sector->GetTexture(plane) == skyflatnum) || (gl_noskyboxes && sportal != nullptr && sportal->mType == PORTS_SKYVIEWPOINT))
	{
		HWSkyInfo skyinfo;
		if (di->di)
		{
			skyinfo.init(di->di, sector, plane, sector->skytransfer, Colormap.FadeColor);
			ptype = PORTALTYPE_SKY;
			sky = &skyinfo;
		}
		PutPortal(di, ptype, plane);
	}
	else if (sportal != nullptr)
	{
		switch (sportal->mType)
		{
		case PORTS_STACKEDSECTORTHING:
		case PORTS_PORTAL:
		case PORTS_LINKEDPORTAL:
		{
			if (di->di && di->di->Viewpoint.IsAllowedOoB())
			{
				secplane_t myplane = plane ? sector->ceilingplane : sector->floorplane;
				if (di->di->Viewpoint.IsOrtho() && di->di->Viewpoint.ViewVector3D.dot(myplane.Normal()) > 0.0) return;
				else if (plane==1 && di->di->Viewpoint.Pos.Z >= myplane.ZatPoint(di->di->Viewpoint.Pos)) return;
				else if (plane==0 && di->di->Viewpoint.Pos.Z <= myplane.ZatPoint(di->di->Viewpoint.Pos)) return;
			}
			auto glport = sector->GetPortalGroup(plane);
			if (glport != NULL)
			{
				if (sector->PortalBlocksView(plane)) return;
				if (di->di && screen->instack[1 - plane]) return;
				ptype = PORTALTYPE_SECTORSTACK;
				portal = glport;
			}
			break;
		}

		case PORTS_SKYVIEWPOINT:
		case PORTS_HORIZON:
		case PORTS_PLANE:
			ptype = PORTALTYPE_SKYBOX;
			secportal = sportal;
			break;
		}
	}
	else if (allowreflect && sector->GetReflect(plane) > 0 && !(di->Level->ib_compatflags & BCOMPATF_NOMIRRORS))
	{
		if (di->di)
		{
			auto vpz = di->di->Viewpoint.Pos.Z;
			if ((plane == sector_t::ceiling && vpz > sector->ceilingplane.fD()) ||
				(plane == sector_t::floor && vpz < -sector->floorplane.fD())) return;
			planemirror = plane == sector_t::ceiling ? &sector->ceilingplane : &sector->floorplane;
		}
		ptype = PORTALTYPE_PLANEMIRROR;
	}
	if (ptype != -1)
	{
		PutPortal(di, ptype, plane);
	}
}


//==========================================================================
//
//  Calculate sky texture for a line
//
//==========================================================================

void HWWall::SkyLine(HWWallDispatcher *di, sector_t *fs, line_t *line)
{
	FSectorPortal *secport = line->GetTransferredPortal();
	HWSkyInfo skyinfo;
	int ptype;

	// JUSTHIT is used as an indicator that a skybox is in use.
	// This is to avoid recursion

	if (!gl_noskyboxes && secport && (secport->mSkybox == nullptr || !(secport->mFlags & PORTSF_INSKYBOX)))
	{
		ptype = PORTALTYPE_SKYBOX;
		secportal = secport;
	}
	else
	{
		if (di->di) skyinfo.init(di->di, fs, sector_t::ceiling, fs->skytransfer, Colormap.FadeColor);
		ptype = PORTALTYPE_SKY;
		sky = &skyinfo;
	}
	ztop[0] = zceil[0];
	ztop[1] = zceil[1];
	zbottom[0] = zfloor[0];
	zbottom[1] = zfloor[1];
	PutPortal(di, ptype, -1);
}


//==========================================================================
//
//  Skies on one sided walls
//
//==========================================================================

void HWWall::SkyNormal(HWWallDispatcher *di, sector_t * fs,vertex_t * v1,vertex_t * v2)
{
	ztop[0]=ztop[1]=32768.0f;
	zbottom[0]=zceil[0];
	zbottom[1]=zceil[1];
	SkyPlane(di, fs, sector_t::ceiling, true);

	ztop[0]=zfloor[0];
	ztop[1]=zfloor[1];
	zbottom[0]=zbottom[1]=-32768.0f;
	SkyPlane(di, fs, sector_t::floor, true);
}

//==========================================================================
//
//  Upper Skies on two sided walls
//
//==========================================================================

void HWWall::SkyTop(HWWallDispatcher *di, seg_t * seg,sector_t * fs,sector_t * bs,vertex_t * v1,vertex_t * v2)
{
	if (fs->GetTexture(sector_t::ceiling)==skyflatnum)
	{
		if (bs->special == GLSector_NoSkyDraw || (bs->MoreFlags & SECMF_NOSKYWALLS) != 0 || (seg->linedef->flags & ML_NOSKYWALLS) != 0) return;
		if (bs->GetTexture(sector_t::ceiling)==skyflatnum)
		{
			// if the back sector is closed the sky must be drawn!
			if (bs->ceilingplane.ZatPoint(v1) > bs->floorplane.ZatPoint(v1) ||
				bs->ceilingplane.ZatPoint(v2) > bs->floorplane.ZatPoint(v2) || bs->transdoor)
					return;

			// one more check for some ugly transparent door hacks
			if (!bs->floorplane.isSlope() && !fs->floorplane.isSlope())
			{
				if (bs->GetPlaneTexZ(sector_t::floor)==fs->GetPlaneTexZ(sector_t::floor)+1.)
				{
					auto tex = TexMan.GetGameTexture(seg->sidedef->GetTexture(side_t::bottom), true);
					if (!tex || !tex->isValid()) return;

					// very, very, very ugly special case (See Icarus MAP14)
					// It is VERY important that this is only done for a floor height difference of 1
					// or it will cause glitches elsewhere.
					tex = TexMan.GetGameTexture(seg->sidedef->GetTexture(side_t::mid), true);
					if (tex != NULL && !(seg->linedef->flags & ML_DONTPEGTOP) &&
						seg->sidedef->GetTextureYOffset(side_t::mid) > 0)
					{
						ztop[0]=ztop[1]=32768.0f;
						zbottom[0]=zbottom[1]= 
							bs->ceilingplane.ZatPoint(v2) + seg->sidedef->GetTextureYOffset(side_t::mid);
						SkyPlane(di, fs, sector_t::ceiling, false);
						return;
					}
				}
			}
		}

		ztop[0]=ztop[1]=32768.0f;

		auto tex = TexMan.GetGameTexture(seg->sidedef->GetTexture(side_t::top), true);
		if (bs->GetTexture(sector_t::ceiling) != skyflatnum)

		{
			zbottom[0]=zceil[0];
			zbottom[1]=zceil[1];
		}
		else
		{
			zbottom[0] = bs->ceilingplane.ZatPoint(v1);
			zbottom[1] = bs->ceilingplane.ZatPoint(v2);
			flags|=HWF_SKYHACK;	// mid textures on such lines need special treatment!
		}
	}
	else 
	{
		float frontreflect = fs->GetReflect(sector_t::ceiling);
		if (frontreflect > 0)
		{
			float backreflect = bs->GetReflect(sector_t::ceiling);
			if (backreflect > 0 && bs->ceilingplane.fD() == fs->ceilingplane.fD() && !bs->isClosed())
			{
				// Don't add intra-portal line to the portal.
				return;
			}
		}
		else
		{
			int type = fs->GetPortalType(sector_t::ceiling);
			if (type == PORTS_STACKEDSECTORTHING || type == PORTS_PORTAL || type == PORTS_LINKEDPORTAL)
			{
				auto pfront = fs->GetPortalGroup(sector_t::ceiling);
				auto pback = bs->GetPortalGroup(sector_t::ceiling);
				if (pfront == NULL || fs->PortalBlocksView(sector_t::ceiling)) return;
				if (pfront == pback && !bs->PortalBlocksView(sector_t::ceiling)) return;
			}
		}

		// stacked sectors
		ztop[0] = ztop[1] = 32768.0f;
		zbottom[0] = fs->ceilingplane.ZatPoint(v1);
		zbottom[1] = fs->ceilingplane.ZatPoint(v2);

	}

	SkyPlane(di, fs, sector_t::ceiling, true);
}


//==========================================================================
//
//  Lower Skies on two sided walls
//
//==========================================================================

void HWWall::SkyBottom(HWWallDispatcher *di, seg_t * seg,sector_t * fs,sector_t * bs,vertex_t * v1,vertex_t * v2)
{
	if (fs->GetTexture(sector_t::floor)==skyflatnum)
	{
		if (bs->special == GLSector_NoSkyDraw || (bs->MoreFlags & SECMF_NOSKYWALLS) != 0 || (seg->linedef->flags & ML_NOSKYWALLS) != 0) return;
		auto tex = TexMan.GetGameTexture(seg->sidedef->GetTexture(side_t::bottom), true);
		
		// For lower skies the normal logic only applies to walls with no lower texture.
		if (!tex->isValid())
		{
			if (bs->GetTexture(sector_t::floor)==skyflatnum)
			{
				// if the back sector is closed the sky must be drawn!
				if (bs->ceilingplane.ZatPoint(v1) > bs->floorplane.ZatPoint(v1) ||
					bs->ceilingplane.ZatPoint(v2) > bs->floorplane.ZatPoint(v2))
						return;

			}
			else
			{
				// Special hack for Vrack2b. For mesh based rendering this check needs to be done in the actual render pass!
				if (di->di && bs->floorplane.ZatPoint(di->di->Viewpoint.Pos) > di->di->Viewpoint.Pos.Z) return;
			}
		}
		zbottom[0]=zbottom[1]=-32768.0f;

		if ((tex && tex->isValid()) || bs->GetTexture(sector_t::floor) != skyflatnum)
		{
			ztop[0] = zfloor[0];
			ztop[1] = zfloor[1];
		}
		else
		{
			ztop[0] = bs->floorplane.ZatPoint(v1);
			ztop[1] = bs->floorplane.ZatPoint(v2);
			flags |= HWF_SKYHACK;	// mid textures on such lines need special treatment!
		}
	}
	else 
	{
		float frontreflect = fs->GetReflect(sector_t::floor);
		if (frontreflect > 0)
		{
			float backreflect = bs->GetReflect(sector_t::floor);
			if (backreflect > 0 && bs->floorplane.fD() == fs->floorplane.fD() && !bs->isClosed())
			{
				// Don't add intra-portal line to the portal.
				return;
			}
		}
		else
		{
			int type = fs->GetPortalType(sector_t::floor);
			if (type == PORTS_STACKEDSECTORTHING || type == PORTS_PORTAL || type == PORTS_LINKEDPORTAL)
			{
				auto pfront = fs->GetPortalGroup(sector_t::floor);
				auto pback = bs->GetPortalGroup(sector_t::floor);
				if (pfront == NULL || fs->PortalBlocksView(sector_t::floor)) return;
				if (pfront == pback && !bs->PortalBlocksView(sector_t::floor)) return;
			}
		}

		// stacked sectors
		zbottom[0]=zbottom[1]=-32768.0f;
		ztop[0] = fs->floorplane.ZatPoint(v1);
		ztop[1] = fs->floorplane.ZatPoint(v2);
	}

	SkyPlane(di, fs, sector_t::floor, true);
}

