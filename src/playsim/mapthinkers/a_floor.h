#pragma once

//
// P_FLOOR
//

class DFloor : public DMovingFloor
{
	DECLARE_CLASS (DFloor, DMovingFloor)
public:
	enum EFloor
	{
		floorLowerToLowest,
		floorLowerToNearest,
		floorLowerToHighest,
		floorLowerByValue,
		floorRaiseByValue,
		floorRaiseToHighest,
		floorRaiseToNearest,
		floorRaiseAndCrush,
		floorRaiseAndCrushDoom,
		floorCrushStop,
		floorLowerInstant,
		floorRaiseInstant,
		floorMoveToValue,
		floorRaiseToLowestCeiling,
		floorRaiseByTexture,

		floorLowerAndChange,
		floorRaiseAndChange,

		floorRaiseToLowest,
		floorRaiseToCeiling,
		floorLowerToLowestCeiling,
		floorLowerByTexture,
		floorLowerToCeiling,

		donutRaise,

		buildStair,
		waitStair,
		resetStair,

		// Not to be used as parameters to EV_DoFloor()
		genFloorChg0,
		genFloorChgT,
		genFloorChg
	};

	// [RH] Changed to use Hexen-ish specials
	enum EStair
	{
		buildUp,
		buildDown
	};

	enum EStairType
	{
		stairUseSpecials = 1,
		stairSync = 2,
		stairCrush = 4,
	};

	void Construct(sector_t *sec);

	void Serialize(FSerializer &arc);
	void Tick ();

//protected:
	EFloor	 	m_Type;
	int 		m_Crush;
	bool		m_Hexencrush;
	bool		m_Instant;
	int 		m_Direction;
	secspecial_t m_NewSpecial{};
	FTextureID	m_Texture;
	double	 	m_FloorDestDist;
	double	 	m_Speed;

	// [RH] New parameters used to reset and delay stairs
	double		m_OrgDist;
	int			m_ResetCount;
	int			m_Delay;
	int			m_PauseTime;
	int			m_StepTime;
	int			m_PerStepTime;

	void StartFloorSound ();
	void SetFloorChangeType (sector_t *sec, int change);
	friend struct FLevelLocals;
};

class DElevator : public DMover
{
	DECLARE_CLASS (DElevator, DMover)
	HAS_OBJECT_POINTERS
public:
	enum EElevator
	{
		elevateUp,
		elevateDown,
		elevateCurrent,
		// [RH] For FloorAndCeiling_Raise/Lower
		elevateRaise,
		elevateLower
	};

	EElevator	m_Type;
	int			m_Direction;
	double		m_FloorDestDist;
	double		m_CeilingDestDist;
	double		m_Speed;


	void Construct(sector_t *sec);

	void OnDestroy() override;
	void Serialize(FSerializer &arc);
	void Tick ();

protected:
	TObjPtr<DInterpolation*> m_Interp_Ceiling;
	TObjPtr<DInterpolation*> m_Interp_Floor;

	void StartFloorSound ();
	friend struct FLevelLocals;
};


class DWaggleBase : public DMover
{
	DECLARE_CLASS (DWaggleBase, DMover)
	HAS_OBJECT_POINTERS
public:
	void Construct(sector_t *sec);

	void Serialize(FSerializer &arc);

protected:
	double m_OriginalDist;
	double m_Accumulator;
	double m_AccDelta;
	double m_TargetScale;
	double m_Scale;
	double m_ScaleDelta;
	int m_Ticker;
	int m_State;

	friend struct FLevelLocals;
	void DoWaggle (bool ceiling);
};

class DFloorWaggle : public DWaggleBase
{
	DECLARE_CLASS (DFloorWaggle, DWaggleBase)
public:
	void Construct(sector_t *sec);
	void Tick ();
};

class DCeilingWaggle : public DWaggleBase
{
	DECLARE_CLASS (DCeilingWaggle, DWaggleBase)
public:
	void Construct(sector_t *sec);
	void Tick ();
};

//jff 3/15/98 pure texture/type change for better generalized support
enum EChange
{
	trigChangeOnly,
	numChangeOnly,
};

