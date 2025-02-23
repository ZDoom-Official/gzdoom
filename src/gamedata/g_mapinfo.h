/*
** g_level.h
**
**---------------------------------------------------------------------------
** Copyright 1998-2006 Randy Heit
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

#ifndef __G_LEVEL_H__
#define __G_LEVEL_H__

#include "autosegs.h"
#include "doomtype.h"
#include "vectors.h"
#include "sc_man.h"
#include "screenjob.h"
#include "hwrenderer/postprocessing/hw_postprocess.h"
#include "hw_viewpointuniforms.h"

struct level_info_t;
struct cluster_info_t;
class FSerializer;

// The structure used to control scripts between maps
struct acsdefered_t
{
	enum EType
	{
		defexecute,
		defexealways,
		defsuspend,
		defterminate
	} type;
	int script;
	int args[3];
	int playernum;
};

FSerializer &Serialize(FSerializer &arc, const char *key, acsdefered_t &defer, acsdefered_t *def);


struct FIntermissionDescriptor;
struct FIntermissionAction;
struct CutsceneDef;

struct FMapInfoParser
{
	FMapInfoParser(FScanner* parent)
		: sc(parent ? &parent->GetSymbols() : nullptr)
	{
	}
	enum EFormatType
	{
		FMT_Unknown,
		FMT_Old,
		FMT_New
	};

	FScanner sc;
	int format_type;
	bool HexenHack;

	FMapInfoParser(int format = FMT_Unknown)
	{
		format_type = format;
		HexenHack = false;
	}

	bool ParseLookupName(FString &dest);
	void ParseMusic(FString &name, int &order);
	void ParseCutscene(CutsceneDef& cdef, bool allow_function = true);

	//void ParseLumpOrTextureName(char *name);
	void ParseLumpOrTextureName(FString &name);
	void ParseExitText(FName formap, level_info_t *info);
	void ParseExitMusic(FName formap, level_info_t *info);
	void ParseExitBackdrop(FName formap, level_info_t *info, bool ispic);

	void ParseCluster();
	void ParseNextMap(FString &mapname);
	level_info_t *ParseMapHeader(level_info_t &defaultinfo);
	void ParseMapDefinition(level_info_t &leveldef);
	void ParseGameInfo();
	void ParseEpisodeInfo ();
	void ParseSkill ();
	void ParseMapInfo (int lump, level_info_t &gamedefaults, level_info_t &defaultinfo);

	void ParseOpenBrace();
	bool ParseCloseBrace();
	bool CheckAssign();
	void ParseAssign();
	void MustParseAssign();
	void ParseComma();
	bool CheckNumber();
	bool CheckFloat();
	void SkipToNext();
	void CheckEndOfFile(const char *block);

	void ParseIntermissionAction(FIntermissionDescriptor *Desc);
	void ParseIntermission();
	void ParseDoomEdNums();
	void ParseSpawnNums();
	void ParseConversationIDs();
	void ParseAMColors(bool);
	FName CheckEndSequence();
	FName ParseEndGame();
	void ParseDamageDefinition();
};

#define DEFINE_MAP_OPTION(name, old) \
	static void MapOptHandler_##name(FMapInfoParser &parse, level_info_t *info); \
	static FMapOptInfo MapOpt_##name = \
		{ #name, MapOptHandler_##name, old }; \
	static void MapOptHandler_##name(FMapInfoParser &parse, level_info_t *info)


struct FMapOptInfo : FAutoSegEntry<FMapOptInfo>
{
	const char *name;
	void (*handler) (FMapInfoParser &parse, level_info_t *levelinfo);
	bool old;

	FMapOptInfo(const char *n, void (*h) (FMapInfoParser &, level_info_t *), bool o)
	: FAutoSegEntry(AutoSegs::MapInfoOptions, this), name(n), handler(h), old(o) {}
};

enum ELevelFlags : unsigned int
{
	LEVEL_NOINTERMISSION		= 0x00000001,
	LEVEL_NOINVENTORYBAR		= 0x00000002,	// This effects Doom only, since it's the only one without a standard inventory bar.
	LEVEL_DOUBLESKY				= 0x00000004,
	LEVEL_HASFADETABLE			= 0x00000008,	// Level uses Hexen's fadetable mapinfo to get fog

	LEVEL_MAP07SPECIAL			= 0x00000010,
	LEVEL_BRUISERSPECIAL		= 0x00000020,
	LEVEL_CYBORGSPECIAL			= 0x00000040,
	LEVEL_SPIDERSPECIAL			= 0x00000080,

	LEVEL_SPECLOWERFLOOR		= 0x00000100,
	LEVEL_SPECOPENDOOR			= 0x00000200,
	LEVEL_SPECLOWERFLOORTOHIGHEST=0x00000300,
	LEVEL_SPECACTIONSMASK		= 0x00000300,

	LEVEL_MONSTERSTELEFRAG		= 0x00000400,
	LEVEL_ACTOWNSPECIAL			= 0x00000800,
	LEVEL_SNDSEQTOTALCTRL		= 0x00001000,
	LEVEL_FORCETILEDSKY		= 0x00002000,

	LEVEL_CROUCH_NO				= 0x00004000,
	LEVEL_JUMP_NO				= 0x00008000,
	LEVEL_FREELOOK_NO			= 0x00010000,
	LEVEL_FREELOOK_YES			= 0x00020000,

	// The absence of both of the following bits means that this level does not
	// use falling damage (though damage can be forced with dmflags,.
	LEVEL_FALLDMG_ZD			= 0x00040000,	// Level uses ZDoom's falling damage
	LEVEL_FALLDMG_HX			= 0x00080000,	// Level uses Hexen's falling damage

	LEVEL_HEADSPECIAL			= 0x00100000,	// Heretic episode 1/4
	LEVEL_MINOTAURSPECIAL		= 0x00200000,	// Heretic episode 2/5
	LEVEL_SORCERER2SPECIAL		= 0x00400000,	// Heretic episode 3
	LEVEL_SPECKILLMONSTERS		= 0x00800000,

	LEVEL_STARTLIGHTNING		= 0x01000000,	// Automatically start lightning
	LEVEL_FILTERSTARTS			= 0x02000000,	// Apply mapthing filtering to player starts
	LEVEL_LOOKUPLEVELNAME		= 0x04000000,	// Level name is the name of a language string
	LEVEL_USEPLAYERSTARTZ		= 0x08000000,	// Use the Z position of player starts

	LEVEL_SWAPSKIES				= 0x10000000,	// Used by lightning
	LEVEL_NOALLIES				= 0x20000000,	// i.e. Inside Strife's front base
	LEVEL_CHANGEMAPCHEAT		= 0x40000000,	// Don't display cluster messages
	LEVEL_VISITED				= 0x80000000,	// Used for intermission map

	// The flags uint64_t is now split into 2 DWORDs 
	LEVEL2_RANDOMPLAYERSTARTS	= 0x00000001,	// Select single player starts randomnly (no voodoo dolls)
	LEVEL2_ALLMAP				= 0x00000002,	// The player picked up a map on this level

	LEVEL2_LAXMONSTERACTIVATION	= 0x00000004,	// Monsters can open doors depending on the door speed
	LEVEL2_LAXACTIVATIONMAPINFO	= 0x00000008,	// LEVEL_LAXMONSTERACTIVATION is not a default.

	LEVEL2_MISSILESACTIVATEIMPACT=0x00000010,	// Missiles are the activators of SPAC_IMPACT events, not their shooters
	LEVEL2_NEEDCLUSTERTEXT		= 0x00000020,	// A map with this flag needs to retain its cluster intermission texts when being redefined in UMAPINFO

	LEVEL2_KEEPFULLINVENTORY	= 0x00000040,	// doesn't reduce the amount of inventory items to 1

	LEVEL2_PRERAISEWEAPON		= 0x00000080,	// players should spawn with their weapons fully raised (but not when respawning it multiplayer)
	LEVEL2_MONSTERFALLINGDAMAGE	= 0x00000100,
	LEVEL2_CLIPMIDTEX			= 0x00000200,
	LEVEL2_WRAPMIDTEX			= 0x00000400,

	LEVEL2_CHECKSWITCHRANGE		= 0x00000800,	

	LEVEL2_PAUSE_MUSIC_IN_MENUS	= 0x00001000,
	LEVEL2_TOTALINFIGHTING		= 0x00002000,
	LEVEL2_NOINFIGHTING			= 0x00004000,

	LEVEL2_NOMONSTERS			= 0x00008000,
	LEVEL2_INFINITE_FLIGHT		= 0x00010000,

	LEVEL2_ALLOWRESPAWN			= 0x00020000,

	LEVEL2_FORCETEAMPLAYON		= 0x00040000,
	LEVEL2_FORCETEAMPLAYOFF		= 0x00080000,

	LEVEL2_CONV_SINGLE_UNFREEZE	= 0x00100000,
	LEVEL2_NOCLUSTERTEXT		= 0x00200000,	// ignore intermission texts fro clusters. This gets set when UMAPINFO is used to redefine its properties.
	LEVEL2_DUMMYSWITCHES		= 0x00400000,
	LEVEL2_HEXENHACK			= 0x00800000,	// Level was defined in a Hexen style MAPINFO

	LEVEL2_SMOOTHLIGHTING		= 0x01000000,	// Level uses the smooth lighting feature.
	LEVEL2_POLYGRIND			= 0x02000000,	// Polyobjects grind corpses to gibs.
	LEVEL2_RESETINVENTORY		= 0x04000000,	// Resets player inventory when starting this level (unless in a hub)
	LEVEL2_RESETHEALTH			= 0x08000000,	// Resets player health when starting this level (unless in a hub)

	LEVEL2_NOSTATISTICS			= 0x10000000,	// This level should not have statistics collected
	LEVEL2_ENDGAME				= 0x20000000,	// This is an epilogue level that cannot be quit.
	LEVEL2_NOAUTOSAVEHINT		= 0x40000000,	// tell the game that an autosave for this level does not need to be kept
	LEVEL2_FORGETSTATE			= 0x80000000,	// forget this map's state in a hub
	
	// More flags!
	LEVEL3_FORCEFAKECONTRAST	= 0x00000001,	// forces fake contrast even with fog enabled
	LEVEL3_REMOVEITEMS			= 0x00000002,	// kills all INVBAR items on map change.
	LEVEL3_ATTENUATE			= 0x00000004,	// attenuate lights?
	LEVEL3_NOLIGHTFADE			= 0x00000008,	// no light fading to black.
	LEVEL3_NOCOLOREDSPRITELIGHTING = 0x00000010,	// draw sprites only with color-less light
	LEVEL3_EXITNORMALUSED		= 0x00000020,
	LEVEL3_EXITSECRETUSED		= 0x00000040,
	LEVEL3_FORCEWORLDPANNING	= 0x00000080,	// Forces the world panning flag for all textures, even those without it explicitly set.
	LEVEL3_HIDEAUTHORNAME		= 0x00000100,
	LEVEL3_PROPERMONSTERFALLINGDAMAGE	= 0x00000200,	// Properly apply falling damage to the monsters
	LEVEL3_SKYBOXAO				= 0x00000400,	// Apply SSAO to sector skies
	LEVEL3_E1M8SPECIAL			= 0x00000800,
	LEVEL3_E2M8SPECIAL			= 0x00001000,
	LEVEL3_E3M8SPECIAL			= 0x00002000,
	LEVEL3_E4M8SPECIAL			= 0x00004000,
	LEVEL3_E4M6SPECIAL			= 0x00008000,
	LEVEL3_NOSHADOWMAP			= 0x00010000,	// disables shadowmaps for a given level.
	LEVEL3_AVOIDMELEE			= 0x00020000,	// global flag needed for proper MBF support.
	LEVEL3_NOJUMPDOWN			= 0x00040000,	// only for MBF21. Inverse of MBF's dog_jumping flag.
	LEVEL3_LIGHTCREATED			= 0x00080000,	// a light had been created in the last frame
	LEVEL3_NOFOGOFWAR			= 0x00100000,	// disables effect of r_radarclipper CVAR on this map
	LEVEL3_SECRET				= 0x00200000,   // level is a secret level
};


struct FSpecialAction
{
	FName Type;					// this is initialized before the actors...
	int Action;
	int Args[5];
};

class DScroller;

class FScanner;
struct level_info_t;

typedef TMap<int, FName> FMusicMap;

enum EMapType : int
{
	MAPTYPE_UNKNOWN = 0,
	MAPTYPE_DOOM,
	MAPTYPE_HEXEN,
	MAPTYPE_BUILD,
	MAPTYPE_UDMF	// This does not distinguish between namespaces.
};

struct FExitText
{
	enum EDefined
	{
		DEF_TEXT = 1,
		DEF_MUSIC = 2,
		DEF_BACKDROP = 4,
		DEF_LOOKUP = 8,
		DEF_PIC = 16
	};
	int16_t mDefined = 0;
	int16_t mOrder = -1;
	FString mText;
	FString mMusic;
	FString mBackdrop;

	FExitText(int def = 0, int order = -1, const FString &text = "", const FString &backdrop = "", const FString &music = "")
		: mDefined(int16_t(def)), mOrder(int16_t(order)), mText(text), mMusic(music), mBackdrop(backdrop)
	{
	}
};


struct level_info_t
{
	int			levelnum;
	
	FString		MapName;
	FString		NextMap;
	FString		NextSecretMap;
	FString		PName;
	FString		SkyPic1;
	FString		SkyPic2;
	FString		FadeTable;
	FString		CustomColorMap;
	FString		F1Pic;
	FString		BorderTexture;
	FString		MapBackground;

	TMap<FName, FExitText> ExitMapTexts;

	int			cluster;
	int			partime;
	int			sucktime;
	int32_t		flags;
	uint32_t	flags2;
	uint32_t	flags3;

	FString		LightningSound = "world/thunder";
	FString		Music;
	FString		LevelName;
	FString		MapLabel;
	FString		AuthorName;
	int8_t		WallVertLight, WallHorizLight;
	int			musicorder;
	FileSys::FCompressedBuffer	Snapshot;
	TArray<acsdefered_t> deferred;
	float		skyspeed1;
	float		skyspeed2;
	uint32_t	fadeto;
	uint32_t	outsidefog;
	int			cdtrack;
	unsigned int cdid;
	double		gravity;
	double		aircontrol;
	int			WarpTrans;
	int			airsupply;
	uint32_t	compatflags, compatflags2;
	uint32_t	compatmask, compatmask2;
	FString		Translator;	// for converting Doom-format linedef and sector types.
	int			DefaultEnvironment;	// Default sound environment for the map.
	FName		Intermission;
	FName		deathsequence;
	FName		slideshow;
	uint32_t	hazardcolor;
	uint32_t	hazardflash;
	int			fogdensity;
	int			outsidefogdensity;
	int			skyfog;
	float		pixelstretch;

	// Redirection: If any player is carrying the specified item, then
	// you go to the RedirectMap instead of this one.
	FName		RedirectType;
	FString		RedirectMapName;

	// CVAR Redirection: If the CVAR Bool returns true, then
	// you go to the RedirectMap instead of this one.
	FName		RedirectCVAR;
	FString		RedirectCVARMapName;

	FString		EnterPic;
	FString		ExitPic;
	FString		EnterAnim;
	FString		ExitAnim;
	FString 	InterMusic;
	int			intermusicorder;
	TMap <FName, std::pair<FString, int> > MapInterMusic;

	FString		SoundInfo;
	FString		SndSeq;

	double		teamdamage;

	FMusicMap	MusicMap;

	TArray<FSpecialAction> specialactions;

	TArray<FSoundID> PrecacheSounds;
	TArray<FString> PrecacheTextures;
	TArray<FName> PrecacheClasses;
	
	TArray<FString> EventHandlers;

	ELightMode	lightmode;
	int8_t		brightfog;
	int8_t		lightadditivesurfaces;
	int8_t		notexturefill;
	FVector3	skyrotatevector;
	FVector3	skyrotatevector2;

	FString		EDName;
	FString		acsName;
	bool		fs_nocheckposition;
	ELightBlendMode lightblendmode;
	ETonemapMode tonemap;
	
	CutsceneDef intro, outro;


	level_info_t() 
	{ 
		Reset(); 
	}
	~level_info_t()
	{
		Snapshot.Clean();
		ClearDefered();
	}
	void Reset();
	bool isValid();
	FString LookupLevelName (uint32_t *langtable = nullptr);
	void ClearDefered()
	{
		deferred.Clear();
	}
	level_info_t *CheckLevelRedirect ();
};


struct cluster_info_t
{
	int			cluster;
	FString		FinaleFlat;
	FString		ExitText;
	FString		EnterText;
	FString		MessageMusic;
	CutsceneDef intro;		// plays when entering this cluster, aside from starting a new game
	CutsceneDef outro;		// plays when leaving this cluster
	CutsceneDef gameover;	// when defined, plays when the player dies in this cluster
	int			musicorder;
	int			flags;
	int			cdtrack;
	FString		ClusterName;
	unsigned int cdid;

	void Reset();

};

// Cluster flags
enum
{
	CLUSTER_HUB				= 0x00000001,	// Cluster uses hub behavior
	CLUSTER_EXITTEXTINLUMP	= 0x00000002,	// Exit text is the name of a lump
	CLUSTER_ENTERTEXTINLUMP	= 0x00000004,	// Enter text is the name of a lump
	CLUSTER_FINALEPIC		= 0x00000008,	// Finale "flat" is actually a full-sized image
	CLUSTER_LOOKUPEXITTEXT	= 0x00000010,	// Exit text is the name of a language string
	CLUSTER_LOOKUPENTERTEXT	= 0x00000020,	// Enter text is the name of a language string
	CLUSTER_LOOKUPNAME		= 0x00000040,	// Name is the name of a language string
	CLUSTER_LOOKUPCLUSTERNAME = 0x00000080,	// Cluster name is the name of a language string
	CLUSTER_ALLOWINTERMISSION = 0x00000100  // Allow intermissions between levels in a hub.
};

extern TArray<level_info_t> wadlevelinfos;

// mapname will be changed if it is a valid warptrans
bool CheckWarpTransMap (FString &mapname, bool substitute);

cluster_info_t *FindClusterInfo (int cluster);
level_info_t *FindLevelInfo (const char *mapname, bool allowdefault=true);
level_info_t *FindLevelByNum (int num);
level_info_t *CheckLevelRedirect (level_info_t *info);

bool SecretLevelVisited();

FString CalcMapName (int episode, int level);

void G_ClearMapinfo();
void G_ParseMapInfo (FString basemapinfo);

enum ESkillProperty
{
	SKILLP_FastMonsters,
	SKILLP_Respawn,
	SKILLP_RespawnLimit,
	SKILLP_DisableCheats,
	SKILLP_AutoUseHealth,
	SKILLP_SpawnFilter,
	SKILLP_EasyBossBrain,
	SKILLP_ACSReturn,
	SKILLP_NoPain,
	SKILLP_EasyKey,
	SKILLP_SlowMonsters,
	SKILLP_Infight,
	SKILLP_PlayerRespawn,
	SKILLP_SpawnMulti,
	SKILLP_InstantReaction,
	SKILLP_SpawnMultiCoopOnly,
};
enum EFSkillProperty	// floating point properties
{
	SKILLP_AmmoFactor,
	SKILLP_DropAmmoFactor,
	SKILLP_ArmorFactor,
	SKILLP_HealthFactor,
	SKILLP_DamageFactor,
	SKILLP_Aggressiveness,
	SKILLP_MonsterHealth,
	SKILLP_FriendlyHealth,
	SKILLP_KickbackFactor,
};

int G_SkillProperty(ESkillProperty prop);
double G_SkillProperty(EFSkillProperty prop);
const char * G_SkillName();

typedef TMap<FName, FString> SkillMenuNames;

typedef TMap<FName, FName> SkillActorReplacement;

struct FSkillInfo
{
	FName Name = NAME_None;
	double AmmoFactor, DoubleAmmoFactor, DropAmmoFactor;
	double DamageFactor;
	double ArmorFactor;
	double HealthFactor;
	double KickbackFactor;

	bool FastMonsters;
	bool SlowMonsters;
	bool DisableCheats;
	bool AutoUseHealth;

	bool EasyBossBrain;
	bool EasyKey;
	bool NoMenu;
	int RespawnCounter;
	int RespawnLimit;
	double Aggressiveness;
	int SpawnFilter;
	bool SpawnMulti;
	bool InstantReaction;
	bool SpawnMultiCoopOnly;
	int ACSReturn;
	FString MenuName;
	FString PicName;
	SkillMenuNames MenuNamesForPlayerClass;
	bool MustConfirm;
	FString MustConfirmText;
	char Shortcut;
	FString TextColor;
	SkillActorReplacement Replace;
	SkillActorReplacement Replaced;
	double MonsterHealth;
	double FriendlyHealth;
	bool NoPain;
	int Infighting;
	bool PlayerRespawn;

	FSkillInfo() = default;
	FSkillInfo(const FSkillInfo &other)
	{
		operator=(other);
	}
	FSkillInfo &operator=(const FSkillInfo &other);
	int GetTextColor() const;

	void SetReplacement(FName a, FName b);
	FName GetReplacement(FName a);
	void SetReplacedBy(FName b, FName a);
	FName GetReplacedBy(FName b);
};

extern TArray<FSkillInfo> AllSkills;
extern int DefaultSkill;

struct FEpisode
{
	FString mEpisodeName;
	FString mEpisodeMap;
	FString mPicName;
	char mShortcut;
	bool mNoSkill;
	CutsceneDef mIntro;
};

extern TArray<FEpisode> AllEpisodes;

int ParseUMapInfo(int lumpnum);
void CommitUMapinfo(level_info_t *defaultinfo);


#endif //__G_LEVEL_H__
