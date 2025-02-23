#pragma once

//
// P_CEILING
//

// [RH] Changed these
class DCeiling : public DMovingCeiling
{
	DECLARE_CLASS (DCeiling, DMovingCeiling)
public:
	enum ECeiling
	{
		ceilLowerByValue,
		ceilRaiseByValue,
		ceilMoveToValue,
		ceilLowerToHighestFloor,
		ceilLowerInstant,
		ceilRaiseInstant,
		ceilCrushAndRaise,
		ceilLowerAndCrush,
		ceil_placeholder,
		ceilCrushRaiseAndStay,
		ceilRaiseToNearest,
		ceilLowerToLowest,
		ceilLowerToFloor,

		// The following are only used by Generic_Ceiling
		ceilRaiseToHighest,
		ceilLowerToHighest,
		ceilRaiseToLowest,
		ceilLowerToNearest,
		ceilRaiseToHighestFloor,
		ceilRaiseToFloor,
		ceilRaiseByTexture,
		ceilLowerByTexture,

		genCeilingChg0,
		genCeilingChgT,
		genCeilingChg
	};

	enum class ECrushMode
	{
		crushDoom = 0,
		crushHexen = 1,
		crushSlowdown = 2
	};

	ECeiling	m_Type;
	double	 	m_BottomHeight;
	double	 	m_TopHeight;
	double	 	m_Speed;
	double		m_Speed1;		// [RH] dnspeed of crushers
	double		m_Speed2;		// [RH] upspeed of crushers
	ECrushMode	m_CrushMode;
	int			m_Silent;

	void Construct(sector_t *sec);
	void Construct(sector_t *sec, double speed1, double speed2, int silent);

	void Serialize(FSerializer &arc);
	void Tick ();

	int getCrush() const { return m_Crush; }
	int getDirection() const { return m_Direction; }
	int getOldDirection() const { return m_OldDirection; }

protected:
	int 		m_Crush;
	int 		m_Direction;	// 1 = up, 0 = waiting, -1 = down

	// [RH] Need these for BOOM-ish transferring ceilings
	FTextureID	m_Texture;
	secspecial_t m_NewSpecial{};

	// ID
	int 		m_Tag;
	int 		m_OldDirection;

	void PlayCeilingSound ();

	friend struct FLevelLocals;
};

