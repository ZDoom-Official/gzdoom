/*
** thingdef.h
**
** Actor definitions
**
**---------------------------------------------------------------------------
** Copyright 2002-2008 Christoph Oelckers
** Copyright 2004-2008 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
** 4. When not used as part of ZDoom or a ZDoom derivative, this code will be
**    covered by the terms of the GNU General Public License as published by
**    the Free Software Foundation; either version 2 of the License, or (at
**    your option) any later version.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/


#ifndef __THINGDEF_H
#define __THINGDEF_H

#include "doomtype.h"
#include "info.h"
#include "s_sound.h"
#include "sc_man.h"
#include "cmdlib.h"
#include "vm.h"


class FScanner;


//==========================================================================
//
// A flag descriptor
//
//==========================================================================

struct FFlagDef
{
	unsigned int flagbit;
	const char *name;
	int structoffset;
	int fieldsize;
	int varflags;
	VersionInfo deprecationVersion;
};

void FinalizeClass(PClass *cls, FStateDefinitions &statedef);
FFlagDef *FindFlag (const PClass *type, const char *part1, const char *part2, bool strict = false);
void HandleDeprecatedFlags(AActor *defaults, int set, int index);
int CheckDeprecatedFlags(AActor *actor, int index);
const char *GetFlagName(unsigned int flagnum, int flagoffset);
void ModActorFlag(AActor *actor, FFlagDef *fd, bool set);
bool ModActorFlag(AActor *actor, const FString &flagname, bool set, bool printerror = true);
INTBOOL CheckActorFlag(AActor *actor, FFlagDef *fd);
INTBOOL CheckActorFlag(AActor *owner, const char *flagname, bool printerror = true);

#define FLAG_NAME(flagnum, flagvar) GetFlagName(flagnum, myoffsetof(AActor, flagvar))


//==========================================================================
//
// State parser
//
//==========================================================================
class FxExpression;

struct FStateLabels;

struct FStateDefine
{
	FName Label;
	TArray<FStateDefine> Children;
	FState *State;
	uint8_t DefineFlags;
};

class FStateDefinitions
{
	TArray<FStateDefine> StateLabels;
	FState *laststate;
	FState *laststatebeforelabel;
	intptr_t lastlabel;
	TArray<FState> StateArray;
	TArray<FScriptPosition> SourceLines;

	static FStateDefine *FindStateLabelInList(TArray<FStateDefine> &list, FName name, bool create);
	static FStateLabels *CreateStateLabelList(TArray<FStateDefine> &statelist);
	static void MakeStateList(const FStateLabels *list, TArray<FStateDefine> &dest);
	static void RetargetStatePointers(intptr_t count, const char *target, TArray<FStateDefine> & statelist);
	FStateDefine *FindStateAddress(const char *name);
	FState *FindState(const char *name);

	FState *ResolveGotoLabel(PClassActor *mytype, char *name);
	static void FixStatePointers(PClassActor *actor, TArray<FStateDefine> & list);
	void ResolveGotoLabels(PClassActor *actor, TArray<FStateDefine> & list);
public:

	FStateDefinitions()
	{
		laststate = NULL;
		laststatebeforelabel = NULL;
		lastlabel = -1;
	}

	void SetStateLabel(const char *statename, FState *state, uint8_t defflags = SDF_STATE);
	void AddStateLabel(const char *statename);
	int GetStateLabelIndex (FName statename);
	void InstallStates(PClassActor *info, AActor *defaults);
	int FinishStates(PClassActor *actor);

	void MakeStateDefines(const PClassActor *cls);
	void AddStateDefines(const FStateLabels *list);
	void RetargetStates (intptr_t count, const char *target);

	bool SetGotoLabel(const char *string);
	bool SetStop();
	bool SetWait();
	bool SetLoop();
	int AddStates(FState *state, const char *framechars, const FScriptPosition &sc);
	int GetStateCount() const { return StateArray.Size(); }
};


void SaveStateSourceLines(FState *firststate, TArray<FScriptPosition> &positions);
FScriptPosition & GetStateSource(FState *state);

//==========================================================================
//
// Extra info maintained while defining an actor.
//
//==========================================================================
struct FDropItem;

struct Baggage
{
#ifdef _DEBUG
	FString ClassName;	// This is here so that during debugging the class name can be seen
#endif
	PNamespace *Namespace;
	PClassActor *Info;
	bool DropItemSet;
	bool StateSet;
	bool fromDecorate;
	int CurrentState;
	int Lumpnum;
	VersionInfo Version;
	FStateDefinitions statedef;

	FDropItem *DropItemList;

	FScriptPosition ScriptPosition;
};

inline void ResetBaggage (Baggage *bag, PClassActor *stateclass)
{
	bag->DropItemList = nullptr;
	bag->DropItemSet = false;
	bag->CurrentState = 0;
	bag->fromDecorate = true;
	bag->StateSet = false;
	bag->statedef.MakeStateDefines(stateclass);
}

//==========================================================================
//
// Action function lookup
//
//==========================================================================

FxExpression *ParseExpression(FScanner &sc, PClassActor *cls, PNamespace *resolvenspc = nullptr);
void ParseStates(FScanner &sc, PClassActor *actor, AActor *defaults, Baggage &bag);
void ParseFunctionParameters(FScanner &sc, PClassActor *cls, TArray<FxExpression *> &out_params,
	PFunction *afd, FString statestring, FStateDefinitions *statedef);
FxExpression *ParseActions(FScanner &sc, FState state, FString statestring, Baggage &bag, bool &endswithret);
FxExpression *ParseAction(FScanner &sc, FState state, FString statestring, Baggage &bag);
FName CheckCastKludges(FName in);
void SetImplicitArgs(TArray<PType *> *args, TArray<uint32_t> *argflags, TArray<FName> *argnames, PContainerType *cls, uint32_t funcflags, int useflags);
PFunction *CreateAnonymousFunction(PContainerType *containingclass, PType *returntype, int flags);
PFunction *FindClassMemberFunction(PContainerType *cls, PContainerType *funccls, FName name, FScriptPosition &sc, bool *error, const VersionInfo &version, bool nodeprecated = false);
void CreateDamageFunction(PNamespace *ns, const VersionInfo &ver, PClassActor *info, AActor *defaults, FxExpression *id, bool fromDecorate, int lumpnum);

//==========================================================================
//
// Property parser
//
//==========================================================================

void HandleActorFlag(FScanner &sc, Baggage &bag, const char *part1, const char *part2, int mod);
FxExpression *ParseParameter(FScanner &sc, PClassActor *cls, PType *type);


enum 
{
	DEPF_UNUSED = 0,
	DEPF_FIREDAMAGE = 1,
	DEPF_ICEDAMAGE = 2,
	DEPF_LOWGRAVITY = 3,
	DEPF_LONGMELEERANGE = 4,
	DEPF_SHORTMISSILERANGE = 5,
	DEPF_PICKUPFLASH = 6,
	DEPF_QUARTERGRAVITY = 7,
	DEPF_FIRERESIST = 8,
	DEPF_HERETICBOUNCE = 9,
	DEPF_HEXENBOUNCE = 10,
	DEPF_DOOMBOUNCE = 11,
	DEPF_INTERHUBSTRIP = 12,
	DEPF_HIGHERMPROB = 13,
	// all that follow need ZScript emulation!
	DEPF_MISSILEMORE = 14,
	DEPF_MISSILEEVENMORE = 15
};

// Types of old style decorations
enum EDefinitionType
{
	DEF_Decoration,
	DEF_BreakableDecoration,
	DEF_Pickup,
	DEF_Projectile,
};

union FPropParam
{
	int i;
	double d;
	const char *s;
	VMFunction* fu;
	FxExpression *exp;
};

typedef void (*PropHandler)(AActor *defaults, PClassActor *info, Baggage &bag, FPropParam *params);

enum ECategory
{
	CAT_PROPERTY,	// Inheritable property
	CAT_INFO		// non-inheritable info (spawn ID, Doomednum, game filter, conversation ID, not usable in ZScript)
};

struct FPropertyInfo : FAutoSegEntry<FPropertyInfo>
{
	const char *name;
	const char *params;
	const char *clsname;
	PropHandler Handler;
	int category;
	
	FPropertyInfo(const char * n, const char * p, const char * cn, PropHandler h, int c)
	: FAutoSegEntry(AutoSegs::Properties, this), name(n), params(p), clsname(cn), Handler(h), category(c) {}

};

FPropertyInfo *FindProperty(const char * string);
int MatchString (const char *in, const char **strings);


#define DEFINE_PROPERTY_BASE(name, paramlist, clas, cat) \
	static void Handler_##name##_##paramlist##_##clas(A##clas *defaults, PClassActor *info, Baggage &bag, FPropParam *params); \
	static FPropertyInfo Prop_##name##_##paramlist##_##clas = \
		{ #name, #paramlist, #clas, (PropHandler)Handler_##name##_##paramlist##_##clas, cat }; \
	static void Handler_##name##_##paramlist##_##clas(A##clas *defaults, PClassActor *info, Baggage &bag, FPropParam *params)

#define DEFINE_PREFIXED_PROPERTY_BASE(prefix, name, paramlist, clas, cat) \
	static void Handler_##name##_##paramlist##_##clas(A##clas *defaults, PClassActor *info, Baggage &bag, FPropParam *params); \
	static FPropertyInfo Prop_##name##_##paramlist##_##clas = \
{ #prefix"."#name, #paramlist, #clas, (PropHandler)Handler_##name##_##paramlist##_##clas, cat }; \
	static void Handler_##name##_##paramlist##_##clas(A##clas *defaults, PClassActor *info, Baggage &bag, FPropParam *params)

#define DEFINE_PREFIXED_SCRIPTED_PROPERTY_BASE(prefix, name, paramlist, clas, cat) \
	static void Handler_##name##_##paramlist##_##clas(AActor *defaults, PClassActor *info, Baggage &bag, FPropParam *params); \
	static FPropertyInfo Prop_##name##_##paramlist##_##clas = \
{ #prefix"."#name, #paramlist, #clas, (PropHandler)Handler_##name##_##paramlist##_##clas, cat }; \
	static void Handler_##name##_##paramlist##_##clas(AActor *defaults, PClassActor *info, Baggage &bag, FPropParam *params)


#define DEFINE_PROPERTY(name, paramlist, clas) DEFINE_PROPERTY_BASE(name, paramlist, clas, CAT_PROPERTY)
#define DEFINE_INFO_PROPERTY(name, paramlist, clas) DEFINE_PROPERTY_BASE(name, paramlist, clas, CAT_INFO)

#define DEFINE_CLASS_PROPERTY(name, paramlist, clas) DEFINE_PREFIXED_SCRIPTED_PROPERTY_BASE(clas, name, paramlist, clas, CAT_PROPERTY)
#define DEFINE_CLASS_PROPERTY_PREFIX(prefix, name, paramlist, clas) DEFINE_PREFIXED_SCRIPTED_PROPERTY_BASE(prefix, name, paramlist, clas, CAT_PROPERTY)

#define PROP_PARM_COUNT (params[0].i)

#define PROP_STRING_PARM(var, no) \
	const char *var = params[(no)+1].s;

#define PROP_NAME_PARM(var, no) \
	FName var = params[(no)+1].s;

#define PROP_EXP_PARM(var, no) \
	FxExpression *var = params[(no)+1].exp;

#define PROP_INT_PARM(var, no) \
	int var = params[(no)+1].i;

#define PROP_FLOAT_PARM(var, no) \
	float var = float(params[(no)+1].d);

#define PROP_DOUBLE_PARM(var, no) \
	double var = params[(no)+1].d;

#define PROP_FUNC_PARM(var, no) \
	auto var = params[(no)+1].fu;

#define PROP_COLOR_PARM(var, no, scriptpos) \
	int var = params[(no)+1].i== 0? params[(no)+2].i : V_GetColor(params[(no)+2].s, scriptpos);

#endif
