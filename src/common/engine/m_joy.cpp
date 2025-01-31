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

#include <math.h>
#include "vectors.h"
#include "m_joy.h"
#include "configfile.h"
#include "i_interface.h"
#include "d_eventbase.h"
#include "cmdlib.h"
#include "printf.h"

// MACROS ------------------------------------------------------------------

// TYPES -------------------------------------------------------------------

// EXTERNAL FUNCTION PROTOTYPES --------------------------------------------

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

EXTERN_CVAR(Bool, joy_ps2raw)
EXTERN_CVAR(Bool, joy_dinput)
EXTERN_CVAR(Bool, joy_xinput)

// PUBLIC DATA DEFINITIONS -------------------------------------------------

CUSTOM_CVARD(Bool, use_joystick, false, CVAR_ARCHIVE|CVAR_GLOBALCONFIG|CVAR_NOINITCALL, "enables input from the joystick if it is present") 
{
#ifdef _WIN32
	joy_ps2raw->Callback();
	joy_dinput->Callback();
	joy_xinput->Callback();
#endif
}

// PRIVATE DATA DEFINITIONS ------------------------------------------------

// Bits 0 is X+, 1 is X-, 2 is Y+, and 3 is Y-.
static uint8_t JoyAngleButtons[8] = { 1, 1+4, 4, 2+4, 2, 2+8, 8, 1+8 };

// CODE --------------------------------------------------------------------

//==========================================================================
//
// IJoystickConfig - Virtual Destructor
//
//==========================================================================

IJoystickConfig::~IJoystickConfig()
{
}

//==========================================================================
//
// M_SetJoystickConfigSection
//
// Sets up the config for reading or writing this controller's axis config. 
//
//==========================================================================

static bool M_SetJoystickConfigSection(IJoystickConfig *joy, bool create, FConfigFile* GameConfig)
{
	FString id = "Joy:";
	id += joy->GetIdentifier();
	if (!GameConfig) return false;
	return GameConfig->SetSection(id.GetChars(), create);
}

//==========================================================================
//
// M_LoadJoystickConfig
//
//==========================================================================

bool M_LoadJoystickConfig(IJoystickConfig *joy)
{
	FConfigFile* GameConfig = sysCallbacks.GetConfig ? sysCallbacks.GetConfig() : nullptr;
	char key[32];
	const char *value;
	int axislen;
	int numaxes;

	joy->SetDefaultConfig();
	if (!M_SetJoystickConfigSection(joy, false, GameConfig))
	{
		return false;
	}

	assert(GameConfig);

	value = GameConfig->GetValueForKey("Enabled");
	if (value)
	{
		joy->SetEnabled((bool)atoi(value));
	}

	if(joy->AllowsEnabledInBackground())
	{
		value = GameConfig->GetValueForKey("EnabledInBackground");
		if (value)
		{
			joy->SetEnabledInBackground((bool)atoi(value));
		}
	}

	value = GameConfig->GetValueForKey("Sensitivity");
	if (value)
	{
		joy->SetSensitivity((float)atof(value));
	}

	numaxes = joy->GetNumAxes();
	for (int i = 0; i < numaxes; ++i)
	{
		axislen = mysnprintf(key, countof(key), "Axis%u", i);

		mysnprintf(key + axislen, countof(key) - axislen, "deadzone");
		value = GameConfig->GetValueForKey(key);
		if (value)
		{
			joy->SetAxisDeadZone(i, (float)atof(value));
		}

		mysnprintf(key + axislen, countof(key) - axislen, "scale");
		value = GameConfig->GetValueForKey(key);
		if (value)
		{
			joy->SetAxisScale(i, (float)atof(value));
		}
	}
	return true;
}

//==========================================================================
//
// M_SaveJoystickConfig
//
// Only saves settings that are not at their defaults.
//
//==========================================================================

void M_SaveJoystickConfig(IJoystickConfig *joy)
{
	FConfigFile* GameConfig = sysCallbacks.GetConfig ? sysCallbacks.GetConfig() : nullptr;
	char key[32], value[32];
	int axislen, numaxes;

	if (GameConfig != NULL && M_SetJoystickConfigSection(joy, true, GameConfig))
	{
		GameConfig->ClearCurrentSection();
		if (!joy->GetEnabled())
		{
			GameConfig->SetValueForKey("Enabled", "0");
		}

		if (!joy->AllowsEnabledInBackground() && joy->GetEnabledInBackground())
		{
			GameConfig->SetValueForKey("EnabledInBackground", "1");
		}
		
		if (!joy->IsSensitivityDefault())
		{
			mysnprintf(value, countof(value), "%g", joy->GetSensitivity());
			GameConfig->SetValueForKey("Sensitivity", value);
		}
		numaxes = joy->GetNumAxes();
		for (int i = 0; i < numaxes; ++i)
		{
			axislen = mysnprintf(key, countof(key), "Axis%u", i);

			if (!joy->IsAxisDeadZoneDefault(i))
			{
				mysnprintf(key + axislen, countof(key) - axislen, "deadzone");
				mysnprintf(value, countof(value), "%g", joy->GetAxisDeadZone(i));
				GameConfig->SetValueForKey(key, value);
			}
			if (!joy->IsAxisScaleDefault(i))
			{
				mysnprintf(key + axislen, countof(key) - axislen, "scale");
				mysnprintf(value, countof(value), "%g", joy->GetAxisScale(i));
				GameConfig->SetValueForKey(key, value);
			}
		}
		// If the joystick is entirely at its defaults, delete this section
		// so that we don't write out a lone section header.
		if (GameConfig->SectionIsEmpty())
		{
			GameConfig->DeleteCurrentSection();
		}
	}
}


//===========================================================================
//
// Joy_RemoveDeadZone
//
//===========================================================================

double Joy_RemoveDeadZone(double axisval, double deadzone, uint8_t *buttons)
{
	uint8_t butt;

	// Cancel out deadzone.
	if (fabs(axisval) < deadzone)
	{
		axisval = 0;
		butt = 0;
	}
	// Make the dead zone the new 0.
	else if (axisval < 0)
	{
		axisval = (axisval + deadzone) / (1.0 - deadzone);
		butt = 2;	// button minus
	}
	else
	{
		axisval = (axisval - deadzone) / (1.0 - deadzone);
		butt = 1;	// button plus
	}
	if (buttons != NULL)
	{
		*buttons = butt;
	}
	return axisval;
}

//===========================================================================
//
// Joy_XYAxesToButtons
//
// Given two axes, returns a button set for them. They should have already
// been sent through Joy_RemoveDeadZone. For axes that share the same
// physical stick, the angle the stick forms should determine whether or
// not the four component buttons are present. Going by deadzone alone gives
// you huge areas where you have to buttons pressed and thin strips where
// you only have one button pressed. For DirectInput gamepads, there is
// not much standard for how the right stick is presented, so we can only
// do this for the left stick for those, since X and Y axes are pretty
// standard. For XInput and Raw PS2 controllers, both sticks are processed
// through here.
//
//===========================================================================

int Joy_XYAxesToButtons(double x, double y)
{
	if (x == 0 && y == 0)
	{
		return 0;
	}
	double rad = atan2(y, x);
	if (rad < 0)
	{
		rad += 2*pi::pi();
	}
	// The circle is divided into eight segments for corresponding
	// button combinations. Each segment is pi/4 radians wide. We offset
	// by half this so that the segments are centered around the ideal lines
	// their buttons represent instead of being right on the lines.
	rad += pi::pi()/8;		// Offset
	rad *= 4/pi::pi();		// Convert range from [0,2pi) to [0,8)
	return JoyAngleButtons[int(rad) & 7];
}

//===========================================================================
//
// Joy_GenerateButtonEvents
//
// Provided two bitmasks for a set of buttons, generates events to reflect
// any changes from the old to new set, where base is the key for bit 0,
// base+1 is the key for bit 1, etc.
//
//===========================================================================

void Joy_GenerateButtonEvents(int oldbuttons, int newbuttons, int numbuttons, int base)
{
	int changed = oldbuttons ^ newbuttons;
	if (changed != 0)
	{
		event_t ev = { 0, 0, 0, 0, 0, 0, 0 };
		int mask = 1;
		for (int j = 0; j < numbuttons; mask <<= 1, ++j)
		{
			if (changed & mask)
			{
				ev.data1 = base + j;
				ev.type = (newbuttons & mask) ? EV_KeyDown : EV_KeyUp;
				D_PostEvent(&ev);
			}
		}
	}
}

void Joy_GenerateButtonEvents(int oldbuttons, int newbuttons, int numbuttons, const int *keys)
{
	int changed = oldbuttons ^ newbuttons;
	if (changed != 0)
	{
		event_t ev = { 0, 0, 0, 0, 0, 0, 0 };
		int mask = 1;
		for (int j = 0; j < numbuttons; mask <<= 1, ++j)
		{
			if (changed & mask)
			{
				ev.data1 = keys[j];
				ev.type = (newbuttons & mask) ? EV_KeyDown : EV_KeyUp;
				D_PostEvent(&ev);
			}
		}
	}
}
