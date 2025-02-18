/*
** joystickmenu.cpp
** The joystick configuration menus
**
**---------------------------------------------------------------------------
** Copyright 2010-2017 Christoph Oelckers
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

//=============================================================================
//
//
//
//=============================================================================

class OptionMenuSliderJoySensitivity : OptionMenuSliderBase
{
	JoystickConfig mJoy;

	OptionMenuSliderJoySensitivity Init(String label, double min, double max, double step, int showval, JoystickConfig joy)
	{
		Super.Init(label, min, max, step, showval);
		mJoy = joy;
		return self;
	}

	override double GetSliderValue()
	{
		return mJoy.GetSensitivity();
	}

	override void SetSliderValue(double val)
	{
		mJoy.SetSensitivity(val);
	}
}

//=============================================================================
//
//
//
//=============================================================================

class OptionMenuSliderJoyScale : OptionMenuSliderBase
{
	int mAxis;
	int mNeg;
	JoystickConfig mJoy;

	OptionMenuSliderJoyScale Init(String label, int axis, double min, double max, double step, int showval, JoystickConfig joy)
	{
		Super.Init(label, min, max, step, showval);
		mAxis = axis;
		mNeg = 1;
		mJoy = joy;
		return self;
	}

	override double GetSliderValue()
	{
		double d = mJoy.GetAxisScale(mAxis);
		mNeg = d < 0? -1:1;
		return d;
	}

	override void SetSliderValue(double val)
	{
		mJoy.SetAxisScale(mAxis, val * mNeg);
	}
}

//=============================================================================
//
//
//
//=============================================================================

class OptionMenuSliderJoyDeadZone : OptionMenuSliderBase
{
	int mAxis;
	int mNeg;
	JoystickConfig mJoy;

	OptionMenuSliderJoyDeadZone Init(String label, int axis, double min, double max, double step, int showval, JoystickConfig joy)
	{
		Super.Init(label, min, max, step, showval);
		mAxis = axis;
		mNeg = 1;
		mJoy = joy;
		return self;
	}

	override double GetSliderValue()
	{
		double d = mJoy.GetAxisDeadZone(mAxis);
		mNeg = d < 0? -1:1;
		return d;
	}

	override void SetSliderValue(double val)
	{
		mJoy.SetAxisDeadZone(mAxis, val * mNeg);
	}
}

//=============================================================================
//
// 
//
//=============================================================================

class OptionMenuItemJoyMap : OptionMenuItemOptionBase
{
	int mAxis;
	JoystickConfig mJoy;

	OptionMenuItemJoyMap Init(String label, int axis, Name values, int center, JoystickConfig joy)
	{
		Super.Init(label, 'none', values, null, center);
		mAxis = axis;
		mJoy = joy;
		return self;
	}

	override int GetSelection()
	{
		double f = mJoy.GetAxisMap(mAxis);
		let opt = OptionValues.GetCount(mValues);
		if (opt > 0)
		{
			// Map from joystick axis to menu selection.
			for(int i = 0; i < opt; i++)
			{
				if (f ~== OptionValues.GetValue(mValues, i))
				{
					return i;
				}
			}
		}
		return -1;
	}

	override void SetSelection(int selection)
	{
		let opt = OptionValues.GetCount(mValues);
		// Map from menu selection to joystick axis.
		if (opt == 0 || selection >= opt)
		{
			selection = JoystickConfig.JOYAXIS_None;
		}
		else
		{
			selection = int(OptionValues.GetValue(mValues, selection));
		}
		mJoy.SetAxisMap(mAxis, selection);
	}
}

//=============================================================================
//
// 
//
//=============================================================================

class OptionMenuItemInverter : OptionMenuItemOptionBase
{
	int mAxis;
	JoystickConfig mJoy;

	OptionMenuItemInverter Init(String label, int axis, int center, JoystickConfig joy)
	{
		Super.Init(label, "none", "YesNo", NULL, center);
		mAxis = axis;
		mJoy = joy;
		return self;
	}

	override int GetSelection()
	{
		float f = mJoy.GetAxisScale(mAxis);
		return f > 0? 0:1;
	}

	override void SetSelection(int Selection)
	{
		let f = abs(mJoy.GetAxisScale(mAxis));
		if (Selection) f*=-1;
		mJoy.SetAxisScale(mAxis, f);
	}
}

//=============================================================================
//
// Executes a CCMD, action is a CCMD name
//
//=============================================================================


class OptionMenuJoyEnable : OptionMenuItemOptionBase
{
	JoystickConfig mJoy;

	OptionMenuJoyEnable Init(String label, JoystickConfig joy)
	{
		Super.Init(label,"none","YesNo",null,0);
		mJoy = joy;
		return self;
	}

	override int GetSelection()
	{
		return mJoy.GetEnabled() ? 1 : 0;
	}

	override void SetSelection(int Selection)
	{
		mJoy.SetEnabled(Selection);
	}
}


class OptionMenuJoyEnableInBackground : OptionMenuItemOptionBase
{
	JoystickConfig mJoy;

	OptionMenuJoyEnableInBackground Init(String label, JoystickConfig joy)
	{
		Super.Init(label,"none","YesNo",null,0);
		mJoy = joy;
		return self;
	}

	override int GetSelection()
	{
		return mJoy.GetEnabledInBackground() ? 1 : 0;
	}

	override void SetSelection(int Selection)
	{
		mJoy.SetEnabledInBackground(Selection);
	}
}

class OptionMenuItemJoyConfigMenu : OptionMenuItemSubmenu
{
	JoystickConfig mJoy;

	OptionMenuItemJoyConfigMenu Init(String label, JoystickConfig joy)
	{
		Super.Init(label, "JoystickConfigMenu");
		mJoy = joy;
		return self;
	}

	override bool Activate()
	{
		let desc = OptionMenuDescriptor(MenuDescriptor.GetDescriptor('JoystickConfigMenu'));
		if (desc != NULL)
		{
			SetController(OptionMenuDescriptor(desc), mJoy);
		}
		let res = Super.Activate();
		let joymenu = JoystickConfigMenu(Menu.GetCurrentMenu());
		if (res && joymenu != null) joymenu.mJoy = mJoy;
		return res;
	}

	static void SetController(OptionMenuDescriptor opt, JoystickConfig joy)
	{
		OptionMenuItem it;
		opt.mItems.Clear();
		if (joy == NULL)
		{
			opt.mTitle = "$JOYMNU_CONFIG";
			it = new("OptionMenuItemStaticText").Init("$JOYMNU_INVALID", false);
			opt.mItems.Push(it);
		}
		else
		{
			/*
			it = new("OptionMenuItemStaticText").Init(joy.GetName(), false);
			opt.mItems.Push(it);

			it = new("OptionMenuItemStaticText").Init("", false);
			opt.mItems.Push(it);
			*/

			it = new("OptionMenuJoyEnable").Init("$JOYMNU_JOYENABLE", joy);
			opt.mItems.Push(it);

			if(joy.AllowsEnabledInBackground())
			{
				it = new("OptionMenuJoyEnableInBackground").Init("$JOYMNU_JOYENABLEINBACKGROUND", joy);
				opt.mItems.Push(it);
			}

			it = new("OptionMenuSliderJoySensitivity").Init("$JOYMNU_OVRSENS", 0, 2, 0.1, 3, joy);
			opt.mItems.Push(it);
			it = new("OptionMenuItemStaticText").Init(" ", false);
			opt.mItems.Push(it);

			if (joy.GetNumAxes() > 0)
			{
				it = new("OptionMenuItemStaticText").Init("$JOYMNU_AXIS", true);
				opt.mItems.Push(it);

				for (int i = 0; i < joy.GetNumAxes(); ++i)
				{
					it = new("OptionMenuItemStaticText").Init(" ", false);
					opt.mItems.Push(it);

					it = new("OptionMenuSliderJoyScale").Init("$JOYMNU_OVRSENS", i, 0, 4, 0.1, 3, joy);
					opt.mItems.Push(it);
					it = new("OptionMenuItemInverter").Init("$JOYMNU_INVERT", i, false, joy);
					opt.mItems.Push(it);
					it = new("OptionMenuSliderJoyDeadZone").Init("$JOYMNU_DEADZONE", i, 0, 0.9, 0.05, 3, joy);
					opt.mItems.Push(it);
				}
			}
			else
			{
				it = new("OptionMenuItemStaticText").Init("$JOYMNU_NOAXES", false);
				opt.mItems.Push(it);
			}
		}
		opt.mScrollPos = 0;
		opt.mSelectedItem = -1;
		opt.mIndent = 0;
		opt.mPosition = -25;
		opt.CalcIndent();
	}

}

//=============================================================================
//
//
//
//=============================================================================

class JoystickConfigMenu : OptionMenu
{
	JoystickConfig mJoy;
}

