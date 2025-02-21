// 
//---------------------------------------------------------------------------
//
// Copyright(C) 2004-2018 Christoph Oelckers
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
/*
** hw_portal.cpp
**   portal maintenance classes for skyboxes, horizons etc. (API independent parts)
**
*/

#include "c_dispatch.h"
#include "p_maputl.h"
#include "hw_portal.h"
#include "hw_clipper.h"
#include "d_player.h"
#include "r_sky.h"
#include "g_levellocals.h"
#include "hw_renderstate.h"
#include "flatvertices.h"
#include "hw_clock.h"
#include "hw_lighting.h"
#include "texturemanager.h"

EXTERN_CVAR(Int, r_mirror_recursions)
EXTERN_CVAR(Bool, gl_portals)

void SetPlaneTextureRotation(FRenderState& state, HWSectorPlane* plane, FGameTexture* texture);

//-----------------------------------------------------------------------------
//
// StartFrame
//
//-----------------------------------------------------------------------------

void FPortalSceneState::StartFrame()
{
	if (renderdepth == 0)
	{
		inskybox = false;
		screen->instack[sector_t::floor] = screen->instack[sector_t::ceiling] = 0;
	}
	renderdepth++;
}

//-----------------------------------------------------------------------------
//
// printing portal info
//
//-----------------------------------------------------------------------------

static bool gl_portalinfo;

CCMD(gl_portalinfo)
{
	gl_portalinfo = true;
}

static FString indent;
FPortalSceneState portalState;

//-----------------------------------------------------------------------------
//
// EndFrame
//
//-----------------------------------------------------------------------------

void FPortalSceneState::EndFrame(HWDrawInfo *di, FRenderState &state)
{
	HWPortal * p;

	if (gl_portalinfo)
	{
		Printf("%s%d portals, depth = %d\n%s{\n", indent.GetChars(), di->Portals.Size(), renderdepth, indent.GetChars());
		indent += "  ";
	}

	while (di->Portals.Pop(p) && p)
	{
		if (gl_portalinfo) 
		{
			Printf("%sProcessing %s, depth = %d\n", indent.GetChars(), p->GetName(), renderdepth);
		}
		if (p->lines.Size() > 0)
		{
			RenderPortal(p, state, true, di);
		}
		delete p;
	}
	renderdepth--;

	if (gl_portalinfo)
	{
		indent.Truncate(indent.Len()-2);
		Printf("%s}\n", indent.GetChars());
		if (indent.Len() == 0) gl_portalinfo = false;
	}
}


//-----------------------------------------------------------------------------
//
// Renders one sky portal without a stencil.
// In more complex scenes using a stencil for skies can severely stall
// the GPU and there's rarely more than one sky visible at a time.
//
//-----------------------------------------------------------------------------
bool FPortalSceneState::RenderFirstSkyPortal(int recursion, HWDrawInfo *outer_di, FRenderState &state)
{
	HWPortal * best = nullptr;
	unsigned bestindex = 0;
	bool usestencil = outer_di->Viewpoint.IsAllowedOoB();

	// Find the one with the highest amount of lines.
	// Normally this is also the one that saves the largest amount
	// of time by drawing it before the scene itself.
	auto &portals = outer_di->Portals;
	for (int i = portals.Size() - 1; i >= 0; --i)
	{
		auto p = portals[i];
		if (p->lines.Size() > 0 && p->IsSky())
		{
			// Cannot clear the depth buffer inside a portal recursion
			if (recursion && p->NeedDepthBuffer()) continue;

			if (!best || p->lines.Size() > best->lines.Size())
			{
				best = p;
				bestindex = i;
			}

			// If the portal area contains the current camera viewpoint, let's always use it because it's likely to give the largest area.
			if (p->boundingBox.contains(usestencil ? outer_di->Viewpoint.OffPos : outer_di->Viewpoint.Pos))
			{
				best = p;
				bestindex = i;
				break;
			}
		}
	}

	if (best)
	{
		portals.Delete(bestindex);
		if (usestencil && ((strcmp(best->GetName(), "Sky") == 0) || (strcmp(best->GetName(), "Skybox") == 0)))
		{
			tempmatrix = outer_di->VPUniforms.mProjectionMatrix; // ensure perspective projection matrix for skies
			outer_di->VPUniforms.mProjectionMatrix = outer_di->ProjectionMatrix2;
		}
		RenderPortal(best, state, usestencil, outer_di);
		if (usestencil && ((strcmp(best->GetName(), "Sky") == 0) || (strcmp(best->GetName(), "Skybox") == 0)))
			outer_di->VPUniforms.mProjectionMatrix = tempmatrix;
		delete best;
		return true;
	}
	return false;
}


void FPortalSceneState::RenderPortal(HWPortal *p, FRenderState &state, bool usestencil, HWDrawInfo *outer_di)
{
	if (gl_portals) outer_di->RenderPortal(p, state, usestencil);
}


//-----------------------------------------------------------------------------
//
// DrawPortalStencil
//
//-----------------------------------------------------------------------------

void HWPortal::DrawPortalStencil(FRenderState &state, int pass)
{
	if (mPrimIndices.Size() == 0)
	{
		mPrimIndices.Resize(2 * lines.Size());
		
		for (unsigned int i = 0; i < lines.Size(); i++)
		{
			mPrimIndices[i * 2] = lines[i].vertindex;
			mPrimIndices[i * 2 + 1] = lines[i].vertcount;
		}

		if (NeedCap() && lines.Size() > 1 && planesused != 0)
		{
			screen->mVertexData->Map();
			if (planesused & (1 << sector_t::floor))
			{
				auto verts = screen->mVertexData->AllocVertices(4);
				auto ptr = verts.first;
				ptr[0].Set((float)boundingBox.left, -32767.f, (float)boundingBox.top, 0, 0);
				ptr[1].Set((float)boundingBox.right, -32767.f, (float)boundingBox.top, 0, 0);
				ptr[2].Set((float)boundingBox.left, -32767.f, (float)boundingBox.bottom, 0, 0);
				ptr[3].Set((float)boundingBox.right, -32767.f, (float)boundingBox.bottom, 0, 0);
				mBottomCap = verts.second;
			}
			if (planesused & (1 << sector_t::ceiling))
			{
				auto verts = screen->mVertexData->AllocVertices(4);
				auto ptr = verts.first;
				ptr[0].Set((float)boundingBox.left, 32767.f, (float)boundingBox.top, 0, 0);
				ptr[1].Set((float)boundingBox.right, 32767.f, (float)boundingBox.top, 0, 0);
				ptr[2].Set((float)boundingBox.left, 32767.f, (float)boundingBox.bottom, 0, 0);
				ptr[3].Set((float)boundingBox.right, 32767.f, (float)boundingBox.bottom, 0, 0);
				mTopCap = verts.second;
			}
			screen->mVertexData->Unmap();
		}

	}
	
	for (unsigned int i = 0; i < mPrimIndices.Size(); i += 2)
	{
		state.Draw(DT_TriangleFan, mPrimIndices[i], mPrimIndices[i + 1], i == 0);
	}
	if (NeedCap() && lines.Size() > 1)
	{
		// The cap's depth handling needs special treatment so that it won't block further portal caps.
		if (pass == STP_DepthRestore) state.SetDepthRange(1, 1);

		if (mBottomCap != ~0u)
		{
			state.Draw(DT_TriangleStrip, mBottomCap, 4, false);
		}
		if (mTopCap != ~0u)
		{
			state.Draw(DT_TriangleStrip, mTopCap, 4, false);
		}

		if (pass == STP_DepthRestore) state.SetDepthRange(0, 1);
	}
}


//-----------------------------------------------------------------------------
//
// Start
//
//-----------------------------------------------------------------------------

void HWPortal::SetupStencil(HWDrawInfo *di, FRenderState &state, bool usestencil)
{
	Clocker c(PortalAll);

	rendered_portals++;
	
	if (usestencil)
	{
		// Create stencil
		state.SetStencil(0, SOP_Increment);	// create stencil, increment stencil of valid pixels
		state.SetColorMask(false);
		state.SetEffect(EFF_STENCIL);
		state.EnableTexture(false);
		state.ResetColor();
		state.SetDepthFunc(DF_Less);
			
		if (NeedDepthBuffer())
		{
			state.SetDepthMask(false);							// don't write to Z-buffer!
				
			DrawPortalStencil(state, STP_Stencil);
				
			// Clear Z-buffer
			state.SetStencil(1, SOP_Keep); // draw sky into stencil. This stage doesn't modify the stencil.
			state.SetDepthMask(true);							// enable z-buffer again
			state.SetDepthRange(1, 1);
			state.SetDepthFunc(DF_Always);
			DrawPortalStencil(state, STP_DepthClear);
				
			// set normal drawing mode
			state.EnableTexture(true);
			state.SetDepthRange(0, 1);
			state.SetDepthFunc(DF_Less);
			state.SetColorMask(true);
			state.SetEffect(EFF_NONE);
		}
		else
		{
			// No z-buffer is needed therefore we can skip all the complicated stuff that is involved
			// Note: We must draw the stencil with z-write enabled here because there is no second pass!
				
			state.SetDepthMask(true);
			DrawPortalStencil(state, STP_AllInOne);
			state.SetStencil(1, SOP_Keep); // draw sky into stencil. This stage doesn't modify the stencil.
			state.EnableTexture(true);
			state.SetColorMask(true);
			state.SetEffect(EFF_NONE);
			state.EnableDepthTest(false);
			state.SetDepthMask(false);							// don't write to Z-buffer!
		}
		screen->stencilValue++;
		
		
	}
	else
	{
		if (!NeedDepthBuffer())
		{
			state.SetDepthMask(false);
			state.EnableDepthTest(false);
		}
	}

	// save viewpoint
	savedvisibility = di->Viewpoint.camera ? di->Viewpoint.camera->renderflags & RF_MAYBEINVISIBLE : ActorRenderFlags::FromInt(0);
}


//-----------------------------------------------------------------------------
//
// End
//
//-----------------------------------------------------------------------------
void HWPortal::RemoveStencil(HWDrawInfo *di, FRenderState &state, bool usestencil)
{
	Clocker c(PortalAll);
	bool needdepth = NeedDepthBuffer();

	// Restore the old view

	auto &vp = di->Viewpoint;
	if (vp.camera != nullptr) vp.camera->renderflags = (vp.camera->renderflags & ~RF_MAYBEINVISIBLE) | savedvisibility;

	if (usestencil)
	{
		
		state.SetColorMask(false);						// no graphics
		state.SetEffect(EFF_NONE);
		state.ResetColor();
		state.EnableTexture(false);
		
		if (needdepth)
		{
			// first step: reset the depth buffer to max. depth
			state.SetDepthRange(1, 1);							// always
			state.SetDepthFunc(DF_Always);						// write the farthest depth value
			DrawPortalStencil(state, STP_DepthClear);
		}
		else
		{
			state.EnableDepthTest(true);
		}
		
		// second step: restore the depth buffer to the previous values and reset the stencil
		state.SetDepthFunc(DF_LEqual);
		state.SetDepthRange(0, 1);
		state.SetStencil(0, SOP_Decrement);
		DrawPortalStencil(state, STP_DepthRestore);
		state.SetDepthFunc(DF_Less);
		
		
		state.EnableTexture(true);
		state.SetEffect(EFF_NONE);
		state.SetColorMask(true);
		screen->stencilValue--;
		
		// restore old stencil op.
		state.SetStencil(0, SOP_Keep);
	}
	else
	{
		if (needdepth)
		{
			state.Clear(CT_Depth);
		}
		else
		{
			state.EnableDepthTest(true);
			state.SetDepthMask(true);
		}
		
		// This draws a valid z-buffer into the stencil's contents to ensure it
		// doesn't get overwritten by the level's geometry.
		
		state.ResetColor();
		state.SetDepthFunc(DF_LEqual);
		state.SetDepthRange(0, 1);
		state.SetColorMask(0, 0, 0, 1); // mark portal in alpha channel but don't touch color
		state.SetEffect(EFF_STENCIL);
		state.EnableTexture(false);
		state.SetRenderStyle(STYLE_Source);
		DrawPortalStencil(state, STP_DepthRestore);
		state.SetEffect(EFF_NONE);
		state.EnableTexture(true);
		state.SetColorMask(true);
		state.SetDepthFunc(DF_Less);
	}
}


//-----------------------------------------------------------------------------
//
// 
//
//-----------------------------------------------------------------------------

void HWScenePortalBase::ClearClipper(HWDrawInfo *di, Clipper *clipper)
{
	auto outer_di = di->outer;
	DAngle angleOffset = deltaangle(outer_di->Viewpoint.Angles.Yaw, di->Viewpoint.Angles.Yaw);

	clipper->Clear();

	// Set the clipper to the minimal visible area
	clipper->SafeAddClipRange(0, 0xffffffff);
	auto outvp = outer_di->Viewpoint;
	DVector3 outPos = clipper->amRadar ? outvp.OffPos : outvp.Pos;
	angle_t padding = clipper->amRadar ? 0x00200000 : 0x00000000; // Make radar clipping more aggressive (reveal less)
	for (unsigned int i = 0; i < lines.Size(); i++)
	{
		DAngle startAngle = (DVector2(lines[i].glseg.x2, lines[i].glseg.y2) - outPos).Angle() + angleOffset;
		DAngle endAngle = (DVector2(lines[i].glseg.x1, lines[i].glseg.y1) - outPos).Angle() + angleOffset;

		if (deltaangle(endAngle, startAngle) < nullAngle)
		{
			clipper->SafeRemoveClipRangeRealAngles(startAngle.BAMs() + padding, endAngle.BAMs() - padding);
		}
	}

	// and finally clip it to the visible area
	angle_t a1 = di->FrustumAngle();
	if (!clipper->amRadar && a1 < ANGLE_180) clipper->SafeAddClipRangeRealAngles(di->Viewpoint.Angles.Yaw.BAMs() + a1, di->Viewpoint.Angles.Yaw.BAMs() - a1);

	// lock the parts that have just been clipped out.
	clipper->SetSilhouette();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//
//
// Common code for line to line and mirror portals
//
//
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

int HWLinePortal::ClipSeg(seg_t *seg, const DVector3 &viewpos)
{
	line_t *linedef = seg->linedef;
	if (!linedef)
	{
		return PClip_Inside;	// should be handled properly.
	}
	return P_ClipLineToPortal(linedef, this, viewpos.XY()) ? PClip_InFront : PClip_Inside;
}

int HWLinePortal::ClipSubsector(subsector_t *sub)
{
	// this seg is completely behind the mirror
	for (unsigned int i = 0; i<sub->numlines; i++)
	{
		if (P_PointOnLineSidePrecise(sub->firstline[i].v1->fPos(), this) == 0) return PClip_Inside;
	}
	return PClip_InFront;
}

int HWLinePortal::ClipPoint(const DVector2 &pos)
{
	if (P_PointOnLineSidePrecise(pos, this))
	{
		return PClip_InFront;
	}
	return PClip_Inside;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//
//
// Mirror Portal
//
//
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//
//
//
//-----------------------------------------------------------------------------

bool HWMirrorPortal::Setup(HWDrawInfo *di, FRenderState &rstate, Clipper *clipper)
{
	auto state = mState;
	if (state->renderdepth > r_mirror_recursions)
	{
		return false;
	}

	auto &vp = di->Viewpoint;
	di->UpdateCurrentMapSection();

	di->mClipPortal = this;
	DAngle StartAngle = vp.Angles.Yaw;
	DVector3 StartPos = vp.Pos;

	vertex_t *v1 = linedef->v1;
	vertex_t *v2 = linedef->v2;

	// the player is always visible in a mirror.
	vp.showviewer = true;
	// Reflect the current view behind the mirror.
	if (linedef->Delta().X == 0)
	{
		// vertical mirror
		vp.Pos.X = 2 * v1->fX() - StartPos.X;

		// Compensation for reendering inaccuracies
		if (StartPos.X < v1->fX()) vp.Pos.X -= 0.1;
		else vp.Pos.X += 0.1;
	}
	else if (linedef->Delta().Y == 0)
	{
		// horizontal mirror
		vp.Pos.Y = 2 * v1->fY() - StartPos.Y;

		// Compensation for reendering inaccuracies
		if (StartPos.Y < v1->fY())  vp.Pos.Y -= 0.1;
		else vp.Pos.Y += 0.1;
	}
	else
	{
		// any mirror--use floats to avoid integer overflow. 
		// Use doubles to avoid losing precision which is very important here.

		double dx = v2->fX() - v1->fX();
		double dy = v2->fY() - v1->fY();
		double x1 = v1->fX();
		double y1 = v1->fY();
		double x = StartPos.X;
		double y = StartPos.Y;

		// the above two cases catch len == 0
		double r = ((x - x1)*dx + (y - y1)*dy) / (dx*dx + dy * dy);

		vp.Pos.X = (x1 + r * dx) * 2 - x;
		vp.Pos.Y = (y1 + r * dy) * 2 - y;

		// Compensation for reendering inaccuracies
		FVector2 v(-dx, dy);
		v.MakeUnit();

		vp.Pos.X += v[1] * state->renderdepth / 2;
		vp.Pos.Y += v[0] * state->renderdepth / 2;
	}
	vp.Angles.Yaw = linedef->Delta().Angle() * 2. - StartAngle;

	vp.ViewActor = nullptr;

	state->MirrorFlag++;
	di->SetClipLine(linedef);
	di->SetupView(rstate, vp.Pos.X, vp.Pos.Y, vp.Pos.Z, !!(state->MirrorFlag & 1), !!(state->PlaneMirrorFlag & 1));

	clipper->Clear();

	angle_t af = di->FrustumAngle();
	if (af < ANGLE_180) clipper->SafeAddClipRangeRealAngles(vp.Angles.Yaw.BAMs() + af, vp.Angles.Yaw.BAMs() - af);

	if(!di->Viewpoint.IsAllowedOoB())
		clipper->SafeAddClipRange(linedef->v1, linedef->v2);
	return true;
}

void HWMirrorPortal::Shutdown(HWDrawInfo *di, FRenderState &rstate)
{
	mState->MirrorFlag--;
}

const char *HWMirrorPortal::GetName() { return "Mirror"; }

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//
//
// Line to line Portal
//
//
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//
//
//
//-----------------------------------------------------------------------------
bool HWLineToLinePortal::Setup(HWDrawInfo *di, FRenderState &rstate, Clipper *clipper)
{
	// TODO: Handle recursion more intelligently
	auto &state = mState;
	if (state->renderdepth>r_mirror_recursions)
	{
		return false;
	}
	auto &vp = di->Viewpoint;
	di->mClipPortal = this;

	line_t *origin = glport->lines[0]->mOrigin;
	P_TranslatePortalXY(origin, vp.Pos.X, vp.Pos.Y);
	P_TranslatePortalXY(origin, vp.ActorPos.X, vp.ActorPos.Y);
	P_TranslatePortalAngle(origin, vp.Angles.Yaw);
	P_TranslatePortalZ(origin, vp.Pos.Z);
	P_TranslatePortalXY(origin, vp.Path[0].X, vp.Path[0].Y);
	P_TranslatePortalXY(origin, vp.Path[1].X, vp.Path[1].Y);
	P_TranslatePortalXY(origin, vp.OffPos.X, vp.OffPos.Y);
	P_TranslatePortalZ(origin, vp.OffPos.Z);
	if (!vp.showviewer && vp.camera != nullptr && P_PointOnLineSidePrecise(vp.Path[0], glport->lines[0]->mDestination) != P_PointOnLineSidePrecise(vp.Path[1], glport->lines[0]->mDestination))
	{
		double distp = (vp.Path[0] - vp.Path[1]).Length();
		if (distp > EQUAL_EPSILON)
		{
			double dist1 = (vp.Pos - vp.Path[0]).Length();
			double dist2 = (vp.Pos - vp.Path[1]).Length();

			if (dist1 + dist2 < distp + 1)
			{
				vp.camera->renderflags |= RF_MAYBEINVISIBLE;
			}
		}
	}


	for (unsigned i = 0; i < lines.Size(); i++)
	{
		line_t *line = lines[i].seg->linedef->getPortalDestination();
		subsector_t *sub;
		if (line->sidedef[0]->Flags & WALLF_POLYOBJ)
			sub = di->Level->PointInRenderSubsector(line->v1->fixX(), line->v1->fixY());
		else sub = line->frontsector->subsectors[0];
		di->CurrentMapSections.Set(sub->mapsection);
	}

	vp.ViewActor = nullptr;
	di->SetClipLine(glport->lines[0]->mDestination);
	di->SetupView(rstate, vp.Pos.X, vp.Pos.Y, vp.Pos.Z, !!(state->MirrorFlag & 1), !!(state->PlaneMirrorFlag & 1));

	ClearClipper(di, clipper);
	return true;
}

void HWLineToLinePortal::RenderAttached(HWDrawInfo *di)
{
	di->ProcessActorsInPortal(glport, di->in_area);
}

const char *HWLineToLinePortal::GetName() { return "LineToLine"; }


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//
//
// Skybox Portal
//
//
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//
// GLSkyboxPortal::DrawContents
//
//-----------------------------------------------------------------------------

bool HWSkyboxPortal::Setup(HWDrawInfo *di, FRenderState &rstate, Clipper *clipper)
{
	auto state = mState;
	old_pm = state->PlaneMirrorMode;

	if (mState->skyboxrecursion >= 3)
	{
		return false;
	}
	auto &vp = di->Viewpoint;

	state->skyboxrecursion++;
	state->PlaneMirrorMode = 0;
	state->inskybox = true;

	AActor *origin = portal->mSkybox;
	portal->mFlags |= PORTSF_INSKYBOX;
	vp.extralight = 0;

	oldclamp = rstate.SetDepthClamp(false);
	vp.Pos = origin->InterpolatedPosition(vp.TicFrac);
	vp.ActorPos = origin->Pos();
	vp.Angles.Yaw += (origin->PrevAngles.Yaw + deltaangle(origin->PrevAngles.Yaw, origin->Angles.Yaw) * vp.TicFrac);

	// Don't let the viewpoint be too close to a floor or ceiling
	double floorh = origin->Sector->floorplane.ZatPoint(origin->Pos());
	double ceilh = origin->Sector->ceilingplane.ZatPoint(origin->Pos());
	if (vp.Pos.Z < floorh + 4) vp.Pos.Z = floorh + 4;
	if (vp.Pos.Z > ceilh - 4) vp.Pos.Z = ceilh - 4;

	vp.ViewActor = origin;

	di->SetupView(rstate, vp.Pos.X, vp.Pos.Y, vp.Pos.Z, !!(state->MirrorFlag & 1), !!(state->PlaneMirrorFlag & 1));
	di->SetViewArea();
	ClearClipper(di, clipper);
	di->UpdateCurrentMapSection();
	return true;
}


void HWSkyboxPortal::Shutdown(HWDrawInfo *di, FRenderState &rstate)
{
	rstate.SetDepthClamp(oldclamp);

	auto state = mState;
	portal->mFlags &= ~PORTSF_INSKYBOX;
	state->inskybox = false;
	state->skyboxrecursion--;
	state->PlaneMirrorMode = old_pm;
}

const char *HWSkyboxPortal::GetName() { return "Skybox"; }
bool HWSkyboxPortal::AllowSSAO() { return false; }	// [MK] sector skyboxes don't allow SSAO by default

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//
//
// Sector stack Portal
//
//
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//
// GLSectorStackPortal::SetupCoverage
//
//-----------------------------------------------------------------------------

static uint8_t SetCoverage(HWDrawInfo *di, void *node)
{
	if (di->Level->nodes.Size() == 0)
	{
		return 0;
	}
	if (!((size_t)node & 1))  // Keep going until found a subsector
	{
		node_t *bsp = (node_t *)node;
		uint8_t coverage = SetCoverage(di, bsp->children[0]) | SetCoverage(di, bsp->children[1]);
		di->no_renderflags[bsp->Index()] = coverage;
		return coverage;
	}
	else
	{
		subsector_t *sub = (subsector_t *)((uint8_t *)node - 1);
		return di->ss_renderflags[sub->Index()] & SSRF_SEEN;
	}
}

void HWSectorStackPortal::SetupCoverage(HWDrawInfo *di)
{
	for (unsigned i = 0; i<subsectors.Size(); i++)
	{
		subsector_t *sub = subsectors[i];
		int plane = origin->plane;
		for (int j = 0; j<sub->portalcoverage[plane].sscount; j++)
		{
			subsector_t *dsub = &di->Level->subsectors[sub->portalcoverage[plane].subsectors[j]];
			di->CurrentMapSections.Set(dsub->mapsection);
			di->ss_renderflags[dsub->Index()] |= SSRF_SEEN;
		}
	}
	SetCoverage(di, di->Level->HeadNode());
}

//-----------------------------------------------------------------------------
//
// GLSectorStackPortal::DrawContents
//
//-----------------------------------------------------------------------------
bool HWSectorStackPortal::Setup(HWDrawInfo *di, FRenderState &rstate, Clipper *clipper)
{
	auto state = mState;
	if (state->renderdepth > 100) // energency abort in case a map manages to set up a recursion.
	{
		return false;
	}

	FSectorPortalGroup *portal = origin;
	auto &vp = di->Viewpoint;

	vp.Pos += origin->mDisplacement;
	vp.ActorPos += origin->mDisplacement;
	vp.ViewActor = nullptr;
	vp.OffPos += origin->mDisplacement;

	// avoid recursions!
	if (origin->plane != -1) screen->instack[origin->plane]++;
	if (lines.Size() > 0)
	{
		flat.plane.GetFromSector(lines[0].sub->sector,
								 lines[0].sub->sector->GetPortal(sector_t::ceiling)->mType & (PORTS_STACKEDSECTORTHING | PORTS_PORTAL | PORTS_LINKEDPORTAL) ?
								 sector_t::ceiling : sector_t::floor);
		di->SetClipHeight(flat.plane.plane.ZatPoint(vp.Pos),
						  flat.plane.plane.Normal().Z > 0 ? -1.f : 1.f);
	}
	di->SetupView(rstate, vp.Pos.X, vp.Pos.Y, vp.Pos.Z, !!(state->MirrorFlag & 1), !!(state->PlaneMirrorFlag & 1));
	SetupCoverage(di);
	ClearClipper(di, clipper);

	// If the viewpoint is not within the portal, we need to invalidate the entire clip area.
	// The portal will re-validate the necessary parts when its subsectors get traversed.
	subsector_t *sub = di->Level->PointInRenderSubsector(vp.Pos);
	if (vp.IsAllowedOoB()) sub = di->Level->PointInRenderSubsector(vp.OffPos);
	if (!(di->ss_renderflags[sub->Index()] & SSRF_SEEN))
	{
		clipper->SafeAddClipRange(0, ANGLE_MAX);
		clipper->SetBlocked(true);
	}
	return true;
}


void HWSectorStackPortal::DrawPortalStencil(FRenderState &state, int pass)
{
	bool isceiling = planesused & (1 << sector_t::ceiling);
	for (unsigned int i = 0; i < lines.Size(); i++)
	{
		flat.section = lines[i].sub->section;
		flat.iboindex = lines[i].sub->sector->iboindex[isceiling ? sector_t::ceiling : sector_t::floor];
		flat.plane.GetFromSector(lines[i].sub->sector, isceiling ? sector_t::ceiling : sector_t::floor);
		// if (isceiling) flat.plane.plane.FlipVert(); // Doesn't do anything. Stencil is a screen-space projection

		state.SetNormal(flat.plane.plane.Normal().X, flat.plane.plane.Normal().Z, flat.plane.plane.Normal().Y);
		state.DrawIndexed(DT_Triangles, flat.iboindex + flat.section->vertexindex, flat.section->vertexcount, i == 0);
	}
}


void HWSectorStackPortal::Shutdown(HWDrawInfo *di, FRenderState &rstate)
{
	if (origin->plane != -1) screen->instack[origin->plane]--;
}

const char *HWSectorStackPortal::GetName() { return "Sectorstack"; }

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//
//
// Plane Mirror Portal
//
//
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//
// GLPlaneMirrorPortal::DrawContents
//
//-----------------------------------------------------------------------------

bool HWPlaneMirrorPortal::Setup(HWDrawInfo *di, FRenderState &rstate, Clipper *clipper)
{
	auto state = mState;
	if (state->renderdepth > r_mirror_recursions)
	{
		return false;
	}
	// A plane mirror needs to flip the portal exclusion logic because inside the mirror, up is down and down is up.
	std::swap(screen->instack[sector_t::floor], screen->instack[sector_t::ceiling]);

	auto &vp = di->Viewpoint;
	old_pm = state->PlaneMirrorMode;

	// the player is always visible in a mirror.
	vp.showviewer = true;

	double planez = origin->ZatPoint(vp.Pos);
	vp.Pos.Z = 2 * planez - vp.Pos.Z;
	vp.ViewActor = nullptr;
	state->PlaneMirrorMode = origin->fC() < 0 ? -1 : 1;

	state->PlaneMirrorFlag++;
	di->SetClipHeight(planez, state->PlaneMirrorMode < 0 ? -1.f : 1.f);
	di->SetupView(rstate, vp.Pos.X, vp.Pos.Y, vp.Pos.Z, !!(state->MirrorFlag & 1), !!(state->PlaneMirrorFlag & 1));
	vp.ViewVector3D.Z = - vp.ViewVector3D.Z;
	vp.OffPos.Z = 2 * planez - vp.OffPos.Z;
	ClearClipper(di, clipper);

	di->UpdateCurrentMapSection();
	return true;
}


void HWPlaneMirrorPortal::DrawPortalStencil(FRenderState &state, int pass)
{
	bool isceiling = planesused & (1 << sector_t::ceiling);
	for (unsigned int i = 0; i < lines.Size(); i++)
	{
		flat.section = lines[i].sub->section;
		flat.iboindex = lines[i].sub->sector->iboindex[isceiling ? sector_t::ceiling : sector_t::floor];
		flat.plane.GetFromSector(lines[i].sub->sector, isceiling ? sector_t::ceiling : sector_t::floor);
		// if (isceiling) flat.plane.plane.FlipVert(); // Doesn't do anything. Stencil is a screen-space projection

		state.SetNormal(flat.plane.plane.Normal().X, flat.plane.plane.Normal().Z, flat.plane.plane.Normal().Y);
		state.DrawIndexed(DT_Triangles, flat.iboindex + flat.section->vertexindex, flat.section->vertexcount, i == 0);
	}
}


void HWPlaneMirrorPortal::Shutdown(HWDrawInfo *di, FRenderState &rstate)
{
	auto state = mState;
	state->PlaneMirrorFlag--;
	state->PlaneMirrorMode = old_pm;
	std::swap(screen->instack[sector_t::floor], screen->instack[sector_t::ceiling]);
}

const char *HWPlaneMirrorPortal::GetName() { return origin->fC() < 0? "Planemirror ceiling" : "Planemirror floor"; }


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//
//
// Horizon Portal
//
//
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

HWHorizonPortal::HWHorizonPortal(FPortalSceneState *s, HWHorizonInfo * pt, FRenderViewpoint &vp, bool local)
: HWPortal(s, local)
{
	origin = pt;

	// create the vertex data for this horizon portal.
	HWSectorPlane * sp = &origin->plane;
	const float vx = vp.Pos.X;
	const float vy = vp.Pos.Y;
	const float vz = vp.Pos.Z;
	const float z = sp->Texheight;
	const float tz = (z - vz);

	// Draw to some far away boundary
	// This is not drawn as larger strips because it causes visual glitches.
	auto verts = screen->mVertexData->AllocVertices(1024 + 10);
	auto ptr = verts.first;
	for (int xx = -32768; xx < 32768; xx += 4096)
	{
		float x = xx + vx;
		for (int yy = -32768; yy < 32768; yy += 4096)
		{
			float y = yy + vy;
			ptr->Set(x, z, y, x / 64, -y / 64);
			ptr++;
			ptr->Set(x + 4096, z, y, x / 64 + 64, -y / 64);
			ptr++;
			ptr->Set(x, z, y + 4096, x / 64, -y / 64 - 64);
			ptr++;
			ptr->Set(x + 4096, z, y + 4096, x / 64 + 64, -y / 64 - 64);
			ptr++;
		}
	}

	// fill the gap between the polygon and the true horizon
	// Since I can't draw into infinity there can always be a
	// small gap
	ptr->Set(-32768 + vx, z, -32768 + vy, 512.f, 0);
	ptr++;
	ptr->Set(-32768 + vx, vz, -32768 + vy, 512.f, tz);
	ptr++;
	ptr->Set(-32768 + vx, z, 32768 + vy, -512.f, 0);
	ptr++;
	ptr->Set(-32768 + vx, vz, 32768 + vy, -512.f, tz);
	ptr++;
	ptr->Set(32768 + vx, z, 32768 + vy, 512.f, 0);
	ptr++;
	ptr->Set(32768 + vx, vz, 32768 + vy, 512.f, tz);
	ptr++;
	ptr->Set(32768 + vx, z, -32768 + vy, -512.f, 0);
	ptr++;
	ptr->Set(32768 + vx, vz, -32768 + vy, -512.f, tz);
	ptr++;
	ptr->Set(-32768 + vx, z, -32768 + vy, 512.f, 0);
	ptr++;
	ptr->Set(-32768 + vx, vz, -32768 + vy, 512.f, tz);
	ptr++;

	voffset = verts.second;
	vcount = 1024;

}

//-----------------------------------------------------------------------------
//
// HWHorizonPortal::DrawContents
//
//-----------------------------------------------------------------------------
void HWHorizonPortal::DrawContents(HWDrawInfo *di, FRenderState &state)
{
	Clocker c(PortalAll);

	HWSectorPlane * sp = &origin->plane;
	auto &vp = di->Viewpoint;

	auto texture = TexMan.GetGameTexture(sp->texture, true);

	if (!texture || !texture->isValid())
	{
		state.ClearScreen();
		return;
	}
	di->SetCameraPos(vp.Pos);


	if (texture->isFullbright())
	{
		// glowing textures are always drawn full bright without color
		SetColor(state, di->Level, di->lightmode, 255, 0, false, origin->colormap, 1.f);
		SetFog(state, di->Level, di->lightmode, 255, 0, false, &origin->colormap, false);
	}
	else
	{
		int rel = getExtraLight();
		SetColor(state, di->Level, di->lightmode, origin->lightlevel, rel, di->isFullbrightScene(), origin->colormap, 1.0f);
		SetFog(state, di->Level, di->lightmode, origin->lightlevel, rel, di->isFullbrightScene(), &origin->colormap, false);
	}


	state.EnableBrightmap(true);
	state.SetMaterial(texture, UF_Texture, 0, CLAMP_NONE, NO_TRANSLATION, -1);
	state.SetObjectColor(origin->specialcolor);

	SetPlaneTextureRotation(state, sp, texture);
	state.AlphaFunc(Alpha_GEqual, 0.f);
	state.SetRenderStyle(STYLE_Source);

	for (unsigned i = 0; i < vcount; i += 4)
	{
		state.Draw(DT_TriangleStrip, voffset + i, 4, true);// i == 0);
	}
	state.Draw(DT_TriangleStrip, voffset + vcount, 10, false);

	state.EnableTextureMatrix(false);
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//
//
// Eternity-style horizon portal
//
// To the rest of the engine these masquerade as a skybox portal
// Internally they need to draw two horizon or sky portals
// and will use the respective classes to achieve that.
//
//
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//
// 
//
//-----------------------------------------------------------------------------

void HWEEHorizonPortal::DrawContents(HWDrawInfo *di, FRenderState &state)
{
	auto &vp = di->Viewpoint;
	sector_t *sector = portal->mOrigin;
	if (sector->GetTexture(sector_t::floor) == skyflatnum ||
		sector->GetTexture(sector_t::ceiling) == skyflatnum)
	{
		HWSkyInfo skyinfo;
		skyinfo.init(di, sector, sector_t::ceiling, sector->skytransfer, 0);
		HWSkyPortal sky(screen->mSkyData, mState, &skyinfo, true);
		sky.DrawContents(di, state);
	}
	if (sector->GetTexture(sector_t::ceiling) != skyflatnum)
	{
		HWHorizonInfo horz;
		horz.plane.GetFromSector(sector, sector_t::ceiling);
		horz.lightlevel = hw_ClampLight(sector->GetCeilingLight());
		horz.colormap = sector->Colormap;
		horz.specialcolor = 0xffffffff;
		if (portal->mType == PORTS_PLANE)
		{
			horz.plane.Texheight = vp.Pos.Z + fabs(horz.plane.Texheight);
		}
		HWHorizonPortal ceil(mState, &horz, di->Viewpoint, true);
		ceil.DrawContents(di, state);
	}
	if (sector->GetTexture(sector_t::floor) != skyflatnum)
	{
		HWHorizonInfo horz;
		horz.plane.GetFromSector(sector, sector_t::floor);
		horz.lightlevel = hw_ClampLight(sector->GetFloorLight());
		horz.colormap = sector->Colormap;
		horz.specialcolor = 0xffffffff;
		if (portal->mType == PORTS_PLANE)
		{
			horz.plane.Texheight = vp.Pos.Z - fabs(horz.plane.Texheight);
		}
		HWHorizonPortal floor(mState, &horz, di->Viewpoint, true);
		floor.DrawContents(di, state);
	}
}

const char *HWHorizonPortal::GetName() { return "Horizon"; }
const char *HWEEHorizonPortal::GetName() { return "EEHorizon"; }


