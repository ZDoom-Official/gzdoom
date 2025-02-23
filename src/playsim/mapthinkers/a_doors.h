#pragma once

//
// P_DOORS
//
class DDoor : public DMovingCeiling
{
	DECLARE_CLASS (DDoor, DMovingCeiling)
public:
	enum EVlDoor
	{
		doorClose,
		doorOpen,
		doorRaise,
		doorWaitRaise,
		doorCloseWaitOpen,
		doorWaitClose,
	};

	EVlDoor		m_Type;
	double	 	m_TopDist;
	double		m_BotDist, m_OldFloorDist;
	vertex_t* m_BotSpot;
	double	 	m_Speed;

	// 1 = up, 0 = waiting at top, -1 = down
	int 		m_Direction;

	// tics to wait at the top
	int 		m_TopWait;
	// (keep in case a door going down is reset)
	// when it reaches 0, start going down
	int 		m_TopCountdown;

	int			m_LightTag;

	void Construct(sector_t *sector);
	void Construct(sector_t *sec, EVlDoor type, double speed, int delay, int lightTag, int topcountdown);

	void Serialize(FSerializer &arc);
	void Tick ();
protected:
	void DoorSound (bool raise, class DSeqNode *curseq=NULL) const;

private:
	friend struct FLevelLocals;
};

class DAnimatedDoor : public DMovingCeiling
{
	DECLARE_CLASS (DAnimatedDoor, DMovingCeiling)
public:

	enum EADType
	{
		adOpenClose,
		adClose
	};

	void Construct(sector_t *sector);
	void Construct(sector_t *sec, line_t *line, int speed, int delay, FDoorAnimation *anim, EADType type);

	void Serialize(FSerializer &arc);
	void Tick ();

	bool StartClosing ();
protected:
	line_t *m_Line1, *m_Line2;
	int m_Frame;
	FDoorAnimation *m_DoorAnim;
	int m_Timer;
	double m_BotDist;
	int m_Status;
	int m_Type;
	enum
	{
		Opening,
		Waiting,
		Closing,
		Dead
	};
	int m_Speed;
	int m_Delay;
	bool m_SetBlocking1, m_SetBlocking2;

	friend struct FLevelLocals;
};
