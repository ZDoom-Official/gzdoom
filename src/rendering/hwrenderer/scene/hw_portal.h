#pragma once

#include "tarray.h"
#include "r_utility.h"
#include "r_sections.h"
#include "actor.h"
#include "hwrenderer/scene/hw_drawinfo.h"
#include "hwrenderer/scene/hw_drawstructs.h"
#include "hw_renderstate.h"
#include "hw_material.h"

class FSkyBox;

struct HWSkyInfo
{
	float x_offset[2];
	float y_offset;		// doubleskies don't have a y-offset
	FGameTexture * texture[2];
	FTextureID skytexno1;
	bool mirrored;
	bool doublesky;
	bool sky2;
	PalEntry fadecolor;

	bool operator==(const HWSkyInfo & inf)
	{
		return !memcmp(this, &inf, sizeof(*this));
	}
	bool operator!=(const HWSkyInfo & inf)
	{
		return !!memcmp(this, &inf, sizeof(*this));
	}
	void init(HWDrawInfo *di, sector_t* sec, int skypos, int sky1, PalEntry fadecolor);
};

struct HWHorizonInfo
{
	HWSectorPlane plane;
	int lightlevel;
	FColormap colormap;
	PalEntry specialcolor;
};

struct FPortalSceneState;

class HWPortal
{
	friend struct FPortalSceneState;

	enum
	{
		STP_Stencil,
		STP_DepthClear,
		STP_DepthRestore,
		STP_AllInOne
	};

	ActorRenderFlags savedvisibility;
	TArray<unsigned int> mPrimIndices;
	unsigned int mTopCap = ~0u, mBottomCap = ~0u;

	virtual void DrawPortalStencil(FRenderState &state, int pass);

public:
	FPortalSceneState * mState;
	TArray<HWWall> lines;
	BoundingRect boundingBox;
	int planesused = 0;
	HWFlat flat;

    HWPortal(FPortalSceneState *s, bool local = false) : mState(s), boundingBox(false)
    {
    }
    virtual ~HWPortal() {}
    virtual int ClipSeg(seg_t *seg, const DVector3 &viewpos) { return PClip_Inside; }
    virtual int ClipSubsector(subsector_t *sub) { return PClip_Inside; }
    virtual int ClipPoint(const DVector2 &pos) { return PClip_Inside; }
    virtual linebase_t *ClipLine() { return nullptr; }
	virtual void * GetSource() const = 0;	// GetSource MUST be implemented!
	virtual const char *GetName() = 0;
	virtual bool AllowSSAO() { return true; }
	virtual bool IsSky() { return false; }
	virtual bool NeedCap() { return true; }
	virtual bool NeedDepthBuffer() { return true; }
	virtual void DrawContents(HWDrawInfo *di, FRenderState &state) = 0;
	virtual void RenderAttached(HWDrawInfo *di) {}
	void SetupStencil(HWDrawInfo *di, FRenderState &state, bool usestencil);
	void RemoveStencil(HWDrawInfo *di, FRenderState &state, bool usestencil);

	void AddLine(HWWall * l)
	{
		lines.Push(*l);
		boundingBox.addVertex(l->glseg.x1, l->glseg.y1);
		boundingBox.addVertex(l->glseg.x2, l->glseg.y2);
	}


};


struct FPortalSceneState
{
	int MirrorFlag = 0;
	int PlaneMirrorFlag = 0;
	int renderdepth = 0;

	int PlaneMirrorMode = 0;
	bool inskybox = 0;

	UniqueList<HWSkyInfo> UniqueSkies;
	UniqueList<HWHorizonInfo> UniqueHorizons;
	UniqueList<secplane_t> UniquePlaneMirrors;

	int skyboxrecursion = 0;

	VSMatrix tempmatrix;

	void BeginScene()
	{
		UniqueSkies.Clear();
		UniqueHorizons.Clear();
		UniquePlaneMirrors.Clear();
	}

	bool isMirrored() const
	{
		return !!((MirrorFlag ^ PlaneMirrorFlag) & 1);
	}

	void StartFrame();
	bool RenderFirstSkyPortal(int recursion, HWDrawInfo *outer_di, FRenderState &state);
	void EndFrame(HWDrawInfo *outer_di, FRenderState &state);
	void RenderPortal(HWPortal *p, FRenderState &state, bool usestencil, HWDrawInfo *outer_di);
};

extern FPortalSceneState portalState;

    
class HWScenePortalBase : public HWPortal
{
protected:
    HWScenePortalBase(FPortalSceneState *state) : HWPortal(state, false)
    {
        
    }
public:
	void ClearClipper(HWDrawInfo *di, Clipper *clipper);
	virtual bool NeedDepthBuffer() { return true; }
	virtual void DrawContents(HWDrawInfo *di, FRenderState &state)
	{
		if (Setup(di, state, (di->Viewpoint.IsAllowedOoB() ? di->rClipper : di->mClipper)))
		{
			di->DrawScene(DM_PORTAL);
			Shutdown(di, state);
		}
		else state.ClearScreen();
	}
	virtual bool Setup(HWDrawInfo *di, FRenderState &rstate, Clipper *clipper) = 0;
	virtual void Shutdown(HWDrawInfo *di, FRenderState &rstate) {}
};

struct HWLinePortal : public HWScenePortalBase, public linebase_t
{
	angle_t		angv1, angv2;	// for quick comparisons with a line or subsector

	HWLinePortal(FPortalSceneState *state, line_t *line) : HWScenePortalBase(state)
	{
		v1 = line->v1;
		v2 = line->v2;
		CalcDelta();
	}

	HWLinePortal(FPortalSceneState *state, FLinePortalSpan *line) : HWScenePortalBase(state)
	{
		if (line->lines[0]->mType != PORTT_LINKED || line->v1 == nullptr)
		{
			// For non-linked portals we must check the actual linedef.
			line_t *lline = line->lines[0]->mDestination;
			v1 = lline->v1;
			v2 = lline->v2;
		}
		else
		{
			// For linked portals we can check the merged span.
			v1 = line->v1;
			v2 = line->v2;
		}
		CalcDelta();
	}

	void CalcDelta()
	{
		delta = v2->fPos() - v1->fPos();
	}

	int ClipSeg(seg_t *seg, const DVector3 &viewpos) override;
	int ClipSubsector(subsector_t *sub) override;
	int ClipPoint(const DVector2 &pos);
	bool NeedCap() override { return false; }
};

struct HWMirrorPortal : public HWLinePortal
{
	// mirror portals always consist of single linedefs!
	line_t * linedef;

protected:
	bool Setup(HWDrawInfo *di, FRenderState &rstate, Clipper *clipper) override;
	void Shutdown(HWDrawInfo *di, FRenderState &rstate) override;
	void * GetSource() const override { return linedef; }
	const char *GetName() override;

public:

	HWMirrorPortal(FPortalSceneState *state, line_t * line)
		: HWLinePortal(state, line)
	{
		linedef = line;
	}
};


struct HWLineToLinePortal : public HWLinePortal
{
	FLinePortalSpan *glport;
protected:
	bool Setup(HWDrawInfo *di, FRenderState &rstate, Clipper *clipper) override;
	virtual void * GetSource() const override { return glport; }
	virtual const char *GetName() override;
	virtual linebase_t *ClipLine() override { return this; }
	virtual void RenderAttached(HWDrawInfo *di) override;

public:

	HWLineToLinePortal(FPortalSceneState *state, FLinePortalSpan *ll)
		: HWLinePortal(state, ll)
	{
		glport = ll;
	}
};


struct HWSkyboxPortal : public HWScenePortalBase
{
	bool oldclamp;
	int old_pm;
	FSectorPortal * portal;

protected:
	bool Setup(HWDrawInfo *di, FRenderState &rstate, Clipper *clipper) override;
	void Shutdown(HWDrawInfo *di, FRenderState &rstate) override;
	virtual void * GetSource() const { return portal; }
	virtual bool IsSky() { return true; }
	virtual const char *GetName();
	virtual bool AllowSSAO() override;

public:


	HWSkyboxPortal(FPortalSceneState *state, FSectorPortal * pt) : HWScenePortalBase(state)
	{
		portal = pt;
	}

};


struct HWSectorStackPortal : public HWScenePortalBase
{
	TArray<subsector_t *> subsectors;
protected:
	bool Setup(HWDrawInfo *di, FRenderState &rstate, Clipper *clipper) override;
	void DrawPortalStencil(FRenderState &state, int pass) override;
	void Shutdown(HWDrawInfo *di, FRenderState &rstate) override;
	virtual void * GetSource() const { return origin; }
	virtual bool IsSky() { return true; }	// although this isn't a real sky it can be handled as one.
	virtual const char *GetName();
	FSectorPortalGroup *origin;

public:

	HWSectorStackPortal(FPortalSceneState *state, FSectorPortalGroup *pt) : HWScenePortalBase(state)
	{
		origin = pt;
	}
	void SetupCoverage(HWDrawInfo *di);
	void AddSubsector(subsector_t *sub)
	{
		subsectors.Push(sub);
	}

};

struct HWPlaneMirrorPortal : public HWScenePortalBase
{
	int old_pm;
protected:
	bool Setup(HWDrawInfo *di, FRenderState &rstate, Clipper *clipper) override;
	void Shutdown(HWDrawInfo *di, FRenderState &rstate) override;
	void DrawPortalStencil(FRenderState &state, int pass) override;
	virtual void * GetSource() const { return origin; }
	virtual const char *GetName();
	secplane_t * origin;

public:

	HWPlaneMirrorPortal(FPortalSceneState *state, secplane_t * pt) : HWScenePortalBase(state)
	{
		origin = pt;
	}

};


struct HWHorizonPortal : public HWPortal
{
	HWHorizonInfo * origin;
	unsigned int voffset;
	unsigned int vcount;
	friend struct HWEEHorizonPortal;

protected:
	virtual void DrawContents(HWDrawInfo *di, FRenderState &state);
	virtual void * GetSource() const { return origin; }
	virtual bool NeedDepthBuffer() { return false; }
	virtual bool NeedCap() { return false; }
	virtual const char *GetName();

public:
	
	HWHorizonPortal(FPortalSceneState *state, HWHorizonInfo * pt, FRenderViewpoint &vp, bool local = false);
};

struct HWEEHorizonPortal : public HWPortal
{
	FSectorPortal * portal;

protected:
	virtual void DrawContents(HWDrawInfo *di, FRenderState &state);
	virtual void * GetSource() const { return portal; }
	virtual bool NeedDepthBuffer() { return false; }
	virtual bool NeedCap() { return false; }
	virtual const char *GetName();

public:

	HWEEHorizonPortal(FPortalSceneState *state, FSectorPortal *pt) : HWPortal(state)
	{
		portal = pt;
	}

};


struct HWSkyPortal : public HWPortal
{
	HWSkyInfo * origin;
	FSkyVertexBuffer *vertexBuffer;
	friend struct HWEEHorizonPortal;

protected:
	virtual void DrawContents(HWDrawInfo *di, FRenderState &state);
	virtual void * GetSource() const { return origin; }
	virtual bool IsSky() { return true; }
	virtual bool NeedDepthBuffer() { return false; }
	virtual const char *GetName();

public:


	HWSkyPortal(FSkyVertexBuffer *vertexbuffer, FPortalSceneState *state, HWSkyInfo *  pt, bool local = false)
		: HWPortal(state, local)
	{
		origin = pt;
		vertexBuffer = vertexbuffer;
	}

};
