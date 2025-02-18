/*
**
**
**---------------------------------------------------------------------------
** Copyright 2005-2016 Randy Heit
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

// HEADER FILES ------------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "i_input.h"
#include "d_eventbase.h"

#include "gameconfigfile.h"
#include "m_argv.h"
#include "cmdlib.h"
#include "keydef.h"

#include "i_mainwindow.h"

// MACROS ------------------------------------------------------------------

#define DEFAULT_DEADZONE			0.25f
#define STATUS_SWITCH_TIME			3

#define VID_PLAY_COM							0x0b43
#define PID_EMS_USB2_PS2_CONTROLLER_ADAPTER		0x0003

#define VID_GREENASIA							0x0e8f
#define PID_DRAGON_PS2_CONTROLLER_ADAPTER		0x0003
#define PID_PANTHERLORD_PS2_CONTROLLER_ADAPTER	0x0029

#define VID_PERSONAL_COMMUNICATION_SYSTEMS		0x0810
#define PID_TWIN_USB_VIBRATION_GAMEPAD			0x0001

#define STATUS_DISCONNECTED		0xFF
#define STATUS_DIGITAL			0x41
#define STATUS_ANALOG			0x73

// TYPES -------------------------------------------------------------------

enum EAdapterType
{
	ADAPTER_EMSUSB2,
	ADAPTER_DragonPlus,
	ADAPTER_PantherLord,
	ADAPTER_TwinUSB,
	ADAPTER_Unknown
};

struct FAdapterHandle
{
	HANDLE Handle;
	EAdapterType Type;
	int ControllerNumber;
	FString DeviceID;
};

class FRawPS2Controller : public IJoystickConfig
{
public:
	FRawPS2Controller(HANDLE handle, EAdapterType type, int sequence, int controller, FString devid);
	~FRawPS2Controller();

	bool ProcessInput(RAWHID *raw, int code);
	void AddAxes(float axes[NUM_KEYS]);
	bool IsConnected() { return Connected; }

	// IJoystickConfig interface
	FString GetName();
	float GetSensitivity();
	virtual void SetSensitivity(float scale);

	int GetNumAxes();
	float GetAxisDeadZone(int axis);
	const char *GetAxisName(int axis);
	float GetAxisScale(int axis);

	void SetAxisDeadZone(int axis, float deadzone);
	void SetAxisScale(int axis, float scale);

	bool IsSensitivityDefault();
	bool IsAxisDeadZoneDefault(int axis);
	bool IsAxisScaleDefault(int axis);

	bool GetEnabled();
	void SetEnabled(bool enabled);

	bool AllowsEnabledInBackground() { return false; }
	bool GetEnabledInBackground() { return false; }
	void SetEnabledInBackground(bool enabled) {}

	void SetDefaultConfig();
	FString GetIdentifier();

protected:
	struct AxisInfo
	{
		float Value;
		float DeadZone;
		float Multiplier;
		int Keys[2];
		uint8_t ButtonValue;
	};
	struct DefaultAxisConfig
	{
		float Multiplier;
	};
	enum
	{
		AXIS_ThumbLX,
		AXIS_ThumbLY,
		AXIS_ThumbRX,
		AXIS_ThumbRY,
		NUM_AXES
	};

	HANDLE Handle;
	FString DeviceID;
	int ControllerNumber;
	int Sequence;
	DWORD DisconnectCount;
	EAdapterType Type;
	float Multiplier;
	AxisInfo Axes[NUM_AXES];
	static DefaultAxisConfig DefaultAxes[NUM_AXES];
	int LastButtons;
	bool Connected;
	bool Marked;
	bool Active;
	bool Enabled;

	void Attached();
	void Detached();
	void NeutralInput();

	static void ProcessThumbstick(int value1, AxisInfo *axis1, int value2, AxisInfo *axis2, int base);

	friend class FRawPS2Manager;
};

class FRawPS2Manager : public FJoystickCollection
{
public:
	FRawPS2Manager();
	~FRawPS2Manager();

	bool GetDevice();
	bool ProcessRawInput(RAWINPUT *raw, int code);
	void AddAxes(float axes[NUM_KEYS]);
	void GetDevices(TArray<IJoystickConfig *> &sticks);
	IJoystickConfig *Rescan();

protected:
	TArray<FRawPS2Controller *> Devices;
	bool Registered;

	void DoRegister();
	FRawPS2Controller *EnumDevices();
	static int DeviceSort(const void *a, const void *b);
};

// Each entry is an offset to the corresponding data field in the
// adapter's data packet. Some of these fields are optional and are
// assigned negative values if the adapter does not include them.
struct PS2Descriptor
{
	const char *AdapterName;
	 uint8_t PacketSize;
	int8_t ControllerNumber;
	int8_t ControllerStatus;
	 uint8_t LeftX;
	 uint8_t LeftY;
	 uint8_t RightX;
	 uint8_t RightY;
	int8_t DPadHat;
	 uint8_t DPadButtonsNibble:1;
	int8_t DPadButtons:7;		// up, right, down, left
	 uint8_t ButtonSet1:7;			// triangle, circle, cross, square
	 uint8_t ButtonSet1Nibble:1;
	 uint8_t ButtonSet2:7;			// L2, R2, L1, R1
	 uint8_t ButtonSet2Nibble:1;
	 uint8_t ButtonSet3:7;			// select, start, lthumb, rthumb
	 uint8_t ButtonSet3Nibble:1;
};

// EXTERNAL FUNCTION PROTOTYPES --------------------------------------------

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

// PUBLIC DATA DEFINITIONS -------------------------------------------------

CUSTOM_CVAR(Bool, joy_ps2raw, true, CVAR_GLOBALCONFIG|CVAR_ARCHIVE|CVAR_NOINITCALL)
{
	I_StartupRawPS2();
	event_t ev = { EV_DeviceChange };
	D_PostEvent(&ev);
}

// PRIVATE DATA DEFINITIONS ------------------------------------------------

static const int ButtonKeys[16] =
{
	KEY_PAD_DPAD_UP,
	KEY_PAD_DPAD_RIGHT,
	KEY_PAD_DPAD_DOWN,
	KEY_PAD_DPAD_LEFT,

	KEY_PAD_Y,			// triangle
	KEY_PAD_B,			// circle
	KEY_PAD_A,			// cross
	KEY_PAD_X,			// square

	KEY_PAD_LTRIGGER,	// L2
	KEY_PAD_RTRIGGER,	// R2
	KEY_PAD_LSHOULDER,	// L1
	KEY_PAD_RSHOULDER,	// R1

	KEY_PAD_BACK,		// select
	KEY_PAD_START,
	KEY_PAD_LTHUMB,
	KEY_PAD_RTHUMB
};

static const uint8_t HatButtons[16] =
{
	1, 1+2, 2, 2+4, 4, 4+8, 8, 8+1,
	0, 0, 0, 0, 0, 0, 0, 0
};

static const PS2Descriptor Descriptors[ADAPTER_Unknown] =
{
 { // ADAPTER_EMS_USB2
	"EMS USB2 Adapter",
	 8, // packet size
	 0, // controller number
	 7, // controller status
	 3, // left x
	 4, // left y
	 5, // right x
	 6, // right y
	-1, // hat
	 1, // d-pad buttons nibble
	 2, // d-pad buttons
	 1, // buttons 1
	 0, // buttons 1 nibble
	 1, // buttons 2
	 1, // buttons 2 nibble
	 2, // buttons 3
	 0, // buttons 3 nibble
 },
 { // ADAPTER_DragonPlus
	"Dragon+ Adapter",
	 9, // packet size
	-1, // controller number
	-1, // controller status
	 3, // left x
	 4, // left y
	 2, // right x
	 1, // right y
	 6, // hat
	 0, // d-pad buttons nibble
	-1, // d-pad buttons
	 6, // buttons 1
	 1, // buttons 1 nibble
	 7, // buttons 2
	 0, // buttons 2 nibble
	 7, // buttons 3
	 1, // buttons 3 nibble
 },
 { // ADAPTER_PantherLord
	// Windows indentifies it as a ga451-USB device, but I call it a PantherLord
	// because there's a box in the middle of the cable with a PantherLord sticker
	// on it.
	"PantherLord Adapter",
	 9, // packet size
	-1, // controller number
	-1, // controller status
	 4, // left x
	 5, // left y
	 3, // right x
	 2, // right y
	-1, // hat			...		 This device has both a hat and d-pad buttons.
	 0, // d-pad buttons nibble  Since buttons are better, we just read those.
	 8, // d-pad buttons
	 6, // buttons 1
	 1, // buttons 1 nibble
	 7, // buttons 2
	 0, // buttons 2 nibble
	 7, // buttons 3
	 1, // buttons 3 nibble
 },
 { // ADAPTER_TwinUSB
	"Twin USB Gamepad",
	 8, // packet size
	 0, // controller number
	-1, // controller status
	 3, // left x
	 4, // left y
	 2, // right x
	 1, // right y
	 5, // hat
	 0, // d-pad buttons nibble
	-1, // d-pad buttons
	 5, // buttons 1
	 1, // buttons 1 nibble
	 6, // buttons 2
	 0, // buttons 2 nibble
	 6, // buttons 3
	 1, // buttons 3 nibble
 }
};

static const char *AxisNames[] =
{
	"Left Thumb X Axis",
	"Left Thumb Y Axis",
	"Right Thumb X Axis",
	"Right Thumb Y Axis",
};

FRawPS2Controller::DefaultAxisConfig FRawPS2Controller::DefaultAxes[NUM_AXES] =
{
	// multiplier
	{ 1 },		// ThumbLX
	{ 1 },		// ThumbLY
	{ 1 },		// ThumbRX
	{ 0.75 },	// ThumbRY
};

// CODE --------------------------------------------------------------------

//==========================================================================
//
// FRawPS2Controller - Constructor
//
// handle: The Raw Input handle for this device
// type: The adapter type
// sequence: The seqeunce number, for attaching numbers to names
// controller: The controller to check, for multi-controller adapters
//
//==========================================================================

FRawPS2Controller::FRawPS2Controller(HANDLE handle, EAdapterType type, int sequence, int controller, FString devid)
{
	Handle = handle;
	Type = type;
	ControllerNumber = controller;
	Sequence = sequence;
	DeviceID = devid;
	Enabled = true;

	// The EMS USB2 controller provides attachment status. The others do not.
	Connected = (Descriptors[type].ControllerStatus < 0);
	if (Connected)
	{
		Attached();
	}

	M_LoadJoystickConfig(this);
}

//==========================================================================
//
// FRawPS2Controller - Destructor
//
//==========================================================================

FRawPS2Controller::~FRawPS2Controller()
{
	// Make sure to send any key ups before destroying this.
	NeutralInput();
	M_SaveJoystickConfig(this);
}

//==========================================================================
//
// FRawPS2Controller :: ProcessInput
//
//==========================================================================

bool FRawPS2Controller::ProcessInput(RAWHID *raw, int code)
{
	// w32api has an incompatible definition of bRawData.
	// (But the version that comes with MinGW64 is fine.)
#if defined(__GNUC__) && !defined(__MINGW64_VERSION_MAJOR)
	uint8_t *rawdata = &raw->bRawData;
#else
	uint8_t *rawdata = raw->bRawData;
#endif
	const PS2Descriptor *desc = &Descriptors[Type];
	bool digital;

	// Ensure packet size is what we expect.
	if (raw->dwSizeHid != desc->PacketSize)
	{
		return false;
	}

#if 0
	// If this is a multi-controller device, check that this packet
	// is for the controller we were created for. We probably don't
	// need to do this, because the multi-controller adapters
	// send data for each controller to seperate device instances.
	if (desc->ControllerNumber >= 0 &&
		raw->bRawData[desc->ControllerNumber] != ControllerNumber)
	{
		return false;
	}
#endif

	// Check for disconnected controller
	if (desc->ControllerStatus >= 0)
	{
		if (rawdata[desc->ControllerStatus] == STATUS_DISCONNECTED)
		{
			// When you press the Analog button on a controller, the EMS
			// USB2 will briefly report the controller as disconnected.
			if (++DisconnectCount < STATUS_SWITCH_TIME)
			{
				NeutralInput();
				return true;
			}
			if (Connected)
			{
				Detached();
			}
			return true;
		}
		if (!Connected)
		{
			Attached();
		}
	}
	DisconnectCount = 0;

	if (code == RIM_INPUTSINK)
	{
		NeutralInput();
		return true;
	}

	// Check for digital controller
	digital = false;
	if (desc->ControllerStatus >= 0)
	{
		// The EMS USB2 is nice enough to actually tell us what type of
		// controller is attached.
		digital = (rawdata[desc->ControllerStatus] == STATUS_DIGITAL);
	}
	else
	{
		// The other adapters don't bother to tell us, but we can still
		// make an educated guess. In analog mode, the axes center at 0x80.
		// In digital mode, they center at 0x7F, and the right stick is
		// fixed at center because it gets translated to presses of the
		// four face buttons instead.
		digital = (rawdata[desc->RightX] == 0x7F && rawdata[desc->RightY] == 0x7F);
	}

	// Convert axes to floating point and cancel out deadzones.
	ProcessThumbstick(rawdata[desc->LeftX], &Axes[AXIS_ThumbLX],
					  rawdata[desc->LeftY], &Axes[AXIS_ThumbLY], KEY_PAD_LTHUMB_RIGHT);

	// If we know we are digital, ignore the right stick.
	if (digital)
	{
		ProcessThumbstick(0x80, &Axes[AXIS_ThumbRX],
						  0x80, &Axes[AXIS_ThumbRY], KEY_PAD_RTHUMB_RIGHT);
	}
	else
	{
		ProcessThumbstick(rawdata[desc->RightX], &Axes[AXIS_ThumbRX],
						  rawdata[desc->RightY], &Axes[AXIS_ThumbRY], KEY_PAD_RTHUMB_RIGHT);
	}

	// Generate events for buttons that have changed.
	int buttons = 0;

	// If we know we are digital, ignore the D-Pad.
	if (!digital)
	{
		if (desc->DPadButtons >= 0)
		{
			buttons = rawdata[desc->DPadButtons] >> (4 * desc->DPadButtonsNibble);
		}
		else if (desc->DPadHat >= 0)
		{
			buttons = HatButtons[rawdata[desc->DPadHat] & 15];
		}
	}
	buttons |= ((rawdata[desc->ButtonSet1] >> (4 * desc->ButtonSet1Nibble)) & 15) << 4;
	buttons |= ((rawdata[desc->ButtonSet2] >> (4 * desc->ButtonSet2Nibble)) & 15) << 8;
	buttons |= ((rawdata[desc->ButtonSet3] >> (4 * desc->ButtonSet3Nibble)) & 15) << 12;
	Joy_GenerateButtonEvents(LastButtons, buttons, 16, ButtonKeys);

	LastButtons = buttons;
	return true;
}

//==========================================================================
//
// FRawPS2Controller :: ProcessThumbstick							STATIC
//
// Converts both axie of a thumb stick to floating point, cancels out the
// deadzone, and generates button up/down events for them.
//
//==========================================================================

void FRawPS2Controller::ProcessThumbstick(int value1, AxisInfo *axis1, int value2, AxisInfo *axis2, int base)
{
	uint8_t buttonstate;
	double axisval1, axisval2;

	axis1->Keys[0] = base;
	axis1->Keys[1] = base + 1;
	axis2->Keys[0] = base + 2;
	axis2->Keys[1] = base + 3;

	axisval1 = value1 * (2.0 / 255) - 1.0;
	axisval2 = value2 * (2.0 / 255) - 1.0;
	axisval1 = Joy_RemoveDeadZone(axisval1, axis1->DeadZone, NULL);
	axisval2 = Joy_RemoveDeadZone(axisval2, axis2->DeadZone, NULL);
	axis1->Value = float(axisval1);
	axis2->Value = float(axisval2);

	// We store all four buttons in the first axis and ignore the second.
	buttonstate = Joy_XYAxesToButtons(axisval1, axisval2);
	Joy_GenerateButtonEvents(axis1->ButtonValue, buttonstate, 4, base);
	axis1->ButtonValue = buttonstate;
}

//==========================================================================
//
// FRawPS2Controller :: Attached
//
// This controller was just attached. Set all buttons and axes to 0.
//
//==========================================================================

void FRawPS2Controller::Attached()
{
	int i;

	Connected = true;
	DisconnectCount = 0;
	LastButtons = 0;
	for (i = 0; i < NUM_AXES; ++i)
	{
		Axes[i].Value = 0;
		Axes[i].ButtonValue = 0;
	}
	UpdateJoystickMenu(this);
}

//==========================================================================
//
// FRawPS2Controller :: Detached
//
// This controller was just detached. Send button ups for buttons that
// were pressed the last time we got input from it.
//
//==========================================================================

void FRawPS2Controller::Detached()
{
	Connected = false;
	NeutralInput();
	UpdateJoystickMenu(NULL);
}

//==========================================================================
//
// FRawPS2Controller :: NeutralInput
//
// Sets the controller's state to a neutral one. Either because the
// controller is disconnected or because we are in the background.
//
//==========================================================================

void FRawPS2Controller::NeutralInput()
{
	for (int i = 0; i < NUM_AXES; i += 2)
	{
		ProcessThumbstick(0x80, &Axes[i], 0x80, &Axes[i+1], KEY_PAD_LTHUMB_RIGHT + i*2);
	}
	Joy_GenerateButtonEvents(LastButtons, 0, 16, ButtonKeys);
	LastButtons = 0;
}

//==========================================================================
//
// FRawPS2Controller :: AddAxes
//
// Add the values of each axis to the game axes.
//
//==========================================================================

void FRawPS2Controller::AddAxes(float axes[NUM_KEYS])
{
	for (int i = 0; i < NUM_AXES; ++i)
	{
		// Add to the game axis.
		float axis_value = float(Axes[i].Value * Multiplier * Axes[i].Multiplier);
		int axis_key = 0;

		if (axis_value > 0.0f)
		{
			axis_key = Axes[i].Keys[0];
		}
		else if (axis_value < 0.0f)
		{
			axis_key = Axes[i].Keys[1];
		}

		if (axis_key > 0 && axis_key < NUM_KEYS)
		{
			axes[axis_key] += fabs(axis_value);
		}
	}
}

//==========================================================================
//
// FRawPS2Controller :: SetDefaultConfig
//
//==========================================================================

void FRawPS2Controller::SetDefaultConfig()
{
	Multiplier = 1;
	for (int i = 0; i < NUM_AXES; ++i)
	{
		Axes[i].DeadZone = DEFAULT_DEADZONE;
		Axes[i].Multiplier = DefaultAxes[i].Multiplier;
	}
}

//==========================================================================
//
// FRawPS2Controller :: GetIdentifier
//
//==========================================================================

FString FRawPS2Controller::GetIdentifier()
{
	FString id = "PS2:";
	id += DeviceID;
	return id;
}

//==========================================================================
//
// FRawPS2Controller :: GetName
//
//==========================================================================

FString FRawPS2Controller::GetName()
{
	FString res = Descriptors[Type].AdapterName;
	if (Sequence != 0)
	{
		res.AppendFormat(" #%d", Sequence);
	}
	return res;
}

//==========================================================================
//
// FRawPS2Controller :: GetSensitivity
//
//==========================================================================

float FRawPS2Controller::GetSensitivity()
{
	return Multiplier;
}

//==========================================================================
//
// FRawPS2Controller :: SetSensitivity
//
//==========================================================================

void FRawPS2Controller::SetSensitivity(float scale)
{
	Multiplier = scale;
}

//==========================================================================
//
// FRawPS2Controller :: IsSensitivityDefault
//
//==========================================================================

bool FRawPS2Controller::IsSensitivityDefault()
{
	return Multiplier == 1;
}

//==========================================================================
//
// FRawPS2Controller :: GetNumAxes
//
//==========================================================================

int FRawPS2Controller::GetNumAxes()
{
	return NUM_AXES;
}

//==========================================================================
//
// FRawPS2Controller :: GetAxisDeadZone
//
//==========================================================================

float FRawPS2Controller::GetAxisDeadZone(int axis)
{
	if (unsigned(axis) < NUM_AXES)
	{
		return Axes[axis].DeadZone;
	}
	return 0;
}

//==========================================================================
//
// FRawPS2Controller :: GetAxisName
//
//==========================================================================

const char *FRawPS2Controller::GetAxisName(int axis)
{
	if (unsigned(axis) < NUM_AXES)
	{
		return AxisNames[axis];
	}
	return "Invalid";
}

//==========================================================================
//
// FRawPS2Controller :: GetAxisScale
//
//==========================================================================

float FRawPS2Controller::GetAxisScale(int axis)
{
	if (unsigned(axis) < NUM_AXES)
	{
		return Axes[axis].Multiplier;
	}
	return 0;
}

//==========================================================================
//
// FRawPS2Controller :: SetAxisDeadZone
//
//==========================================================================

void FRawPS2Controller::SetAxisDeadZone(int axis, float deadzone)
{
	if (unsigned(axis) < NUM_AXES)
	{
		Axes[axis].DeadZone = clamp(deadzone, 0.f, 1.f);
	}
}

//==========================================================================
//
// FRawPS2Controller :: SetAxisScale
//
//==========================================================================

void FRawPS2Controller::SetAxisScale(int axis, float scale)
{
	if (unsigned(axis) < NUM_AXES)
	{
		Axes[axis].Multiplier = scale;
	}
}

//===========================================================================
//
// FRawPS2Controller :: IsAxisDeadZoneDefault
//
//===========================================================================

bool FRawPS2Controller::IsAxisDeadZoneDefault(int axis)
{
	if (unsigned(axis) < NUM_AXES)
	{
		return Axes[axis].DeadZone == DEFAULT_DEADZONE;
	}
	return true;
}

//===========================================================================
//
// FRawPS2Controller :: IsAxisScaleDefault
//
//===========================================================================

bool FRawPS2Controller::IsAxisScaleDefault(int axis)
{
	if (unsigned(axis) < NUM_AXES)
	{
		return Axes[axis].Multiplier == DefaultAxes[axis].Multiplier;
	}
	return true;
}

//===========================================================================
//
// FRawPS2Controller :: GetEnabled
//
//===========================================================================

bool FRawPS2Controller::GetEnabled()
{
	return Enabled;
}

//===========================================================================
//
// FRawPS2Controller :: SetEnabled
//
//===========================================================================

void FRawPS2Controller::SetEnabled(bool enabled)
{
	Enabled = enabled;
}

//==========================================================================
//
// FRawPS2Manager - Constructor
//
//==========================================================================

FRawPS2Manager::FRawPS2Manager()
{
	Registered = false;
}

//==========================================================================
//
// FRawPS2Manager - Destructor
//
//==========================================================================

FRawPS2Manager::~FRawPS2Manager()
{
	for (unsigned i = 0; i < Devices.Size(); ++i)
	{
		if (Devices[i] != NULL)
		{
			delete Devices[i];
		}
	}
}

//==========================================================================
//
// FRawPS2Manager :: GetDevice
//
//==========================================================================

bool FRawPS2Manager::GetDevice()
{
	RAWINPUTDEVICE rid;

	rid.usUsagePage = HID_GENERIC_DESKTOP_PAGE;
	rid.usUsage = HID_GDP_JOYSTICK;
	rid.dwFlags = RIDEV_INPUTSINK;
	rid.hwndTarget = mainwindow.GetHandle();
	if (!RegisterRawInputDevices(&rid, 1, sizeof(rid)))
	{
		return false;
	}
	rid.dwFlags = RIDEV_REMOVE;
	rid.hwndTarget = NULL;	// Must be NULL for RIDEV_REMOVE.
	RegisterRawInputDevices(&rid, 1, sizeof(rid));
	EnumDevices();
	return true;
}

//===========================================================================
//
// FRawPS2Manager :: AddAxes
//
// Adds the state of all attached device axes to the passed array.
//
//===========================================================================

void FRawPS2Manager::AddAxes(float axes[NUM_KEYS])
{
	for (unsigned i = 0; i < Devices.Size(); ++i)
	{
		if (Devices[i]->IsConnected())
		{
			Devices[i]->AddAxes(axes);
		}
	}
}

//===========================================================================
//
// FRawPS2Manager :: GetJoysticks
//
// Adds the IJoystick interfaces for each device we created to the sticks
// array, if they are detected as connected.
//
//===========================================================================

void FRawPS2Manager::GetDevices(TArray<IJoystickConfig *> &sticks)
{
	for (unsigned i = 0; i < Devices.Size(); ++i)
	{
		if (Devices[i]->IsConnected())
		{
			sticks.Push(Devices[i]);
		}
	}
}

//===========================================================================
//
// FRawPS2Manager :: ProcessRawInput
//
//===========================================================================

bool FRawPS2Manager::ProcessRawInput(RAWINPUT *raw, int code)
{
	if (raw->header.dwType != RIM_TYPEHID)
	{
		return false;
	}
	for (unsigned i = 0; i < Devices.Size(); ++i)
	{
		if (Devices[i]->Handle == raw->header.hDevice)
		{
			if (Devices[i]->Enabled && Devices[i]->ProcessInput(&raw->data.hid, code))
			{
				return true;
			}
		}
	}
	return false;
}

//===========================================================================
//
// FRawPS2Manager :: Rescan
//
//===========================================================================

IJoystickConfig *FRawPS2Manager::Rescan()
{
	return EnumDevices();
}

//===========================================================================
//
// FRawPS2Manager :: EnumDevices
//
// Find out what PS2 adaptors we understand are on the system and create
// FRawPS2Controller objects for them. May return a pointer to the first new
// device found.
//
//===========================================================================

FRawPS2Controller *FRawPS2Manager::EnumDevices()
{
	UINT nDevices, numDevices;
	RAWINPUTDEVICELIST *devices;
	UINT i, j;

	if (GetRawInputDeviceList(NULL, &nDevices, sizeof(RAWINPUTDEVICELIST)) != 0)
	{
		return NULL;
	}
	if ((devices = (RAWINPUTDEVICELIST *)malloc(sizeof(RAWINPUTDEVICELIST) * nDevices)) == NULL)
	{
		return NULL;
	}
	if ((numDevices = GetRawInputDeviceList(devices, &nDevices, sizeof(RAWINPUTDEVICELIST))) == (UINT)-1)
	{
		free(devices);
		return NULL;
	}

	TArray<FAdapterHandle> adapters;

	for (i = 0; i < numDevices; ++i)
	{
		if (devices[i].dwType == RIM_TYPEHID)
		{
			RID_DEVICE_INFO rdi;
			UINT cbSize;

			cbSize = rdi.cbSize = sizeof(rdi);
			if ((INT)GetRawInputDeviceInfoA(devices[i].hDevice, RIDI_DEVICEINFO, &rdi, &cbSize) >= 0)
			{
				// All the PS2 adapters report themselves as joysticks.
				// (By comparison, the 360 controller reports itself as a gamepad.)
				if (rdi.hid.usUsagePage == HID_GENERIC_DESKTOP_PAGE &&
					rdi.hid.usUsage == HID_GDP_JOYSTICK)
				{
					EAdapterType type = ADAPTER_Unknown;

					// Check vendor and product IDs to see if we know what this is.
					if (rdi.hid.dwVendorId == VID_PLAY_COM)
					{
						if (rdi.hid.dwProductId == PID_EMS_USB2_PS2_CONTROLLER_ADAPTER)
						{
							type = ADAPTER_EMSUSB2;
						}
					}
					else if (rdi.hid.dwVendorId == VID_GREENASIA)
					{
						if (rdi.hid.dwProductId == PID_DRAGON_PS2_CONTROLLER_ADAPTER)
						{
							type = ADAPTER_DragonPlus;
						}
						else if (rdi.hid.dwProductId == PID_PANTHERLORD_PS2_CONTROLLER_ADAPTER)
						{
							type = ADAPTER_PantherLord;
						}
					}
					else if (rdi.hid.dwVendorId == VID_PERSONAL_COMMUNICATION_SYSTEMS)
					{
						if (rdi.hid.dwProductId == PID_TWIN_USB_VIBRATION_GAMEPAD)
						{
							type = ADAPTER_TwinUSB;
						}
					}
					if (type != ADAPTER_Unknown)
					{
						// Get the device name. Part of this is a path under HKLM\CurrentControlSet\Enum
						// with \ characters replaced by # characters. It is not a human-friendly name.
						// The layout for the name string is:
						// <Enumerator>#<Device ID>#<Device Interface Class GUID>
						// The Device ID has multiple #-seperated parts and uniquely identifies
						// this device and which USB port it is connected to.
						wchar_t name[256];
						UINT namelen = countof(name);
						wchar_t *devid, *devidend;

						if (GetRawInputDeviceInfoW(devices[i].hDevice, RIDI_DEVICENAME, name, &namelen) == (UINT)-1)
						{ // Can't get name. Skip it, since there's stuff in there we need for config.
							continue;
						}

						devid = wcschr(name, '#');
						if (devid == NULL)
						{ // Should not happen
							continue;
						}
						devidend = wcsrchr(++devid, '#');
						if (devidend != NULL)
						{
							*devidend = '\0';
						}

						FAdapterHandle handle = { devices[i].hDevice, type, 0, FString(devid) };

						// Adapters that support more than one controller have a seperate device
						// entry for each controller. We can examine the name to determine which
						// controller this device is for.
						if (Descriptors[type].ControllerNumber >= 0)
						{
							wchar_t *col = wcsstr(devid, L"&Col");
							if (col != NULL)
							{
								// I have no idea if this number is base 16 or base 10. Every
								// other number in the name is base 16, so I assume this one is
								// too, but since I don't have anything that goes higher than 02,
								// I can't be sure.
								handle.ControllerNumber = wcstoul(col + 4, NULL, 16);
							}
						}
						adapters.Push(handle);
					}
				}
			}
		}
	}
	free(devices);

	// Sort the found devices so that we have a consistant ordering.
	qsort(&adapters[0], adapters.Size(), sizeof(FAdapterHandle), DeviceSort);

	// Compare the new list of devices with the one we previously instantiated.
	// Every device we currently hold is marked 0. Then scan through the new
	// list and try to find it there, if it's found, it is marked 1. At the end
	// of this, devices marked 1 existed before and are left alone. Devices
	// marked 0 are no longer present and should be destroyed. If a device is
	// present in the new list that we have not yet instantiated, we 
	// instantiate it now.
	FRawPS2Controller *newone = NULL;
	EAdapterType lasttype = ADAPTER_Unknown;
	int sequence = 0;	// Resets to 0 or 1 each time the adapter type changes

	for (j = 0; j < Devices.Size(); ++j)
	{
		Devices[j]->Marked = false;
	}
	for (i = 0; i < adapters.Size(); ++i)
	{
		FAdapterHandle *adapter = &adapters[i];

		if (adapter->Type != lasttype)
		{
			lasttype = adapter->Type;
			// Peak ahead. If the next adapter has the same type, use 1.
			// Otherwise, use 0. (0 means to not append a number to
			// the device name.)
			if (i == adapters.Size() - 1 || adapter->Type != adapters[i+1].Type)
			{
				sequence = 0;
			}
			else
			{
				sequence = 1;
			}
		}

		for (j = 0; j < Devices.Size(); ++j)
		{
			if (Devices[j]->Handle == adapter->Handle)
			{
				Devices[j]->Marked = true;
				break;
			}
		}
		if (j == Devices.Size())
		{ // Not found. Add it.
			FRawPS2Controller *device = new FRawPS2Controller(adapter->Handle, adapter->Type, sequence++,
				adapter->ControllerNumber, adapter->DeviceID);
			device->Marked = true;
			Devices.Push(device);
			if (newone == NULL)
			{
				newone = device;
			}
		}
	}
	// Remove detached devices and avoid holes in the list.
	for (i = j = 0; j < Devices.Size(); ++j)
	{
		if (!Devices[j]->Marked)
		{
			delete Devices[j];
		}
		else
		{
			if (i != j)
			{
				Devices[i] = Devices[j];
			}
			++i;
		}
	}
	Devices.Resize(i);
	DoRegister();
	return newone;
}

//===========================================================================
//
// FRawPS2Manager :: DeviceSort										STATIC
//
// Sorts first by device type, then by ID, then by controller number.
//
//===========================================================================

int FRawPS2Manager::DeviceSort(const void *a, const void *b)
{
	const FAdapterHandle *ha = (const FAdapterHandle *)a;
	const FAdapterHandle *hb = (const FAdapterHandle *)b;
	int lex = ha->Type - hb->Type;
	if (lex == 0)
	{
		// Skip device part of the ID and sort the connection part
		const char *ca = strchr(ha->DeviceID.GetChars(), '#');
		const char *cb = strchr(hb->DeviceID.GetChars(), '#');
		const char *ea, *eb;
		// The last bit looks like a controller number. Strip it out to be safe
		// if this is a multi-controller adapter.
		if (ha->ControllerNumber != 0)
		{
			ea = strrchr(ca, '&');
			eb = strrchr(cb, '&');
		}
		else
		{
			ea = strlen(ca) + ca;
			eb = strlen(cb) + cb;
		}
		for (; ca < ea && cb < eb && lex == 0; ++ca, ++cb)
		{
			lex = *ca - *cb;
		}
		if (lex == 0)
		{
			lex = ha->ControllerNumber - hb->ControllerNumber;
		}
	}
	return lex;
}

//===========================================================================
//
// FRawPS2Manager :: DoRegister
//
// Ensure that we are only listening for input if devices we care about
// are attached to the system.
//
//===========================================================================

void FRawPS2Manager::DoRegister()
{
	RAWINPUTDEVICE rid;
	rid.usUsagePage = HID_GENERIC_DESKTOP_PAGE;
	rid.usUsage = HID_GDP_JOYSTICK;
	if (Devices.Size() == 0)
	{
		if (Registered)
		{
			rid.dwFlags = RIDEV_REMOVE;
			rid.hwndTarget = NULL;
			if (RegisterRawInputDevices(&rid, 1, sizeof(rid)))
			{
				Registered = false;
			}
		}
	}
	else
	{
		if (!Registered)
		{
			rid.dwFlags = RIDEV_INPUTSINK;
			rid.hwndTarget = mainwindow.GetHandle();
			if (RegisterRawInputDevices(&rid, 1, sizeof(rid)))
			{
				Registered = true;
			}
		}
	}
}

//===========================================================================
//
// I_StartupRawPS2
//
//===========================================================================

void I_StartupRawPS2()
{
	if (!joy_ps2raw || !use_joystick || Args->CheckParm("-nojoy"))
	{
		if (JoyDevices[INPUT_RawPS2] != NULL)
		{
			delete JoyDevices[INPUT_RawPS2];
			JoyDevices[INPUT_RawPS2] = NULL;
			UpdateJoystickMenu(NULL);
		}
	}
	else
	{
		if (JoyDevices[INPUT_RawPS2] == NULL)
		{
			FJoystickCollection *joys = new FRawPS2Manager;
			if (joys->GetDevice())
			{
				JoyDevices[INPUT_RawPS2] = joys;
			}
			else
			{
				delete joys;
			}
		}
	}
}

//===========================================================================
//
// I_IsPS2Adapter
//
// The Data1 part of a DirectInput product GUID contains the device's vendor
// and product IDs. Returns true if we know what this device is.
//
//===========================================================================

bool I_IsPS2Adapter(DWORD vidpid)
{
	return (vidpid == MAKELONG(VID_PLAY_COM, PID_EMS_USB2_PS2_CONTROLLER_ADAPTER) ||
			vidpid == MAKELONG(VID_GREENASIA, PID_DRAGON_PS2_CONTROLLER_ADAPTER) ||
			vidpid == MAKELONG(VID_GREENASIA, PID_PANTHERLORD_PS2_CONTROLLER_ADAPTER) ||
			vidpid == MAKELONG(VID_PERSONAL_COMMUNICATION_SYSTEMS, PID_TWIN_USB_VIBRATION_GAMEPAD));
}
