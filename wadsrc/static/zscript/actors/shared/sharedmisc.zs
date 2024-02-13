
// Default class for unregistered doomednums -------------------------------

class Unknown : Actor
{
	Default
	{
		Radius 32;
		Height 56;
		+NOGRAVITY
		+NOBLOCKMAP
		+DONTSPLASH
	}
	States
	{
	Spawn:
		UNKN A -1;
		Stop;
	}
}

// Route node for monster patrols -------------------------------------------

class PatrolPoint : Actor
{
	Default
	{
		Radius 8;
		Height 8;
		Mass 10;
		+NOGRAVITY
		+NOBLOCKMAP
		+DONTSPLASH
		+NOTONAUTOMAP
		RenderStyle "None";
	}
}	

// A special to execute when a monster reaches a matching patrol point ------

class PatrolSpecial : Actor
{
	Default
	{
		Radius 8;
		Height 8;
		Mass 10;
		+NOGRAVITY
		+NOBLOCKMAP
		+DONTSPLASH
		+NOTONAUTOMAP
		RenderStyle "None";
	}
}	

// Map spot ----------------------------------------------------------------

class MapSpot : Actor
{
	Default
	{
		+NOBLOCKMAP
		+NOSECTOR
		+NOGRAVITY
		+DONTSPLASH
		+NOTONAUTOMAP
		RenderStyle "None";
		CameraHeight 0;
	}
}	

// same with different editor number for Legacy maps -----------------------

class FS_Mapspot : Mapspot 
{
}

// Map spot with gravity ---------------------------------------------------

class MapSpotGravity : MapSpot
{
	Default
	{
		-NOBLOCKMAP
		-NOSECTOR
		-NOGRAVITY
		+NOTONAUTOMAP
	}
}

// Point Pushers -----------------------------------------------------------

class PointPusher : Actor
{
	Default
	{
		+NOBLOCKMAP
		+INVISIBLE
		+NOCLIP
		+NOTONAUTOMAP
	}
}

class PointPuller : Actor
{
	Default
	{
		+NOBLOCKMAP
		+INVISIBLE
		+NOCLIP
		+NOTONAUTOMAP
	}
}

// Bloody gibs -------------------------------------------------------------

class RealGibs : Actor
{
	Default
	{
		+DROPOFF
		+CORPSE
		+NOTELEPORT
		+DONTGIB
	}
	States
	{
	Spawn:
		goto GenericCrush;
	}
}

// Gibs that can be placed on a map. ---------------------------------------
//
// These need to be a separate class from the above, in case someone uses
// a deh patch to change the gibs, since ZDoom actually creates a gib class
// for actors that get crushed instead of changing their state as Doom did.

class Gibs : RealGibs
{
	Default
	{
		ClearFlags;
	}
}

// Needed for loading Build maps -------------------------------------------

class CustomSprite : Actor
{
	Default
	{
		+NOBLOCKMAP
		+NOGRAVITY
	}
	States
	{
	Spawn:
		TNT1 A -1;
		Stop;
	}
	
	override void BeginPlay ()
	{
		Super.BeginPlay ();

		String name = String.Format("BTIL%04d", args[0] & 0xffff);
		picnum = TexMan.CheckForTexture (name, TexMan.TYPE_Build);
		if (!picnum.Exists())
		{
			Destroy();
			return;
		}

		Scale.X = args[2] / 64.;
		Scale.Y = args[3] / 64.;

		int cstat = args[4];
		if (cstat & 2)
		{
			A_SetRenderStyle((cstat & 512) ? 0.6666 : 0.3333, STYLE_Translucent);
		}
		if (cstat & 4) bXFlip = true;
		if (cstat & 8) bYFlip = true;
		if (cstat & 16) bWallSprite = true;
		if (cstat & 32) bFlatSprite = true;
	}
}

// SwitchableDecoration: Activate and Deactivate change state --------------

class SwitchableDecoration : Actor
{
	override void Activate (Actor activator)
	{
		SetStateLabel("Active");
	}

	override void Deactivate (Actor activator)
	{
		SetStateLabel("Inactive");
	}
	
}

class SwitchingDecoration : SwitchableDecoration
{
	override void Deactivate (Actor activator)
	{
	}
}

// Sector flag setter ------------------------------------------------------

class SectorFlagSetter : Actor
{
	Default
	{
		+NOBLOCKMAP
		+NOGRAVITY
		+DONTSPLASH
		RenderStyle "None";
	}
	
	override void BeginPlay ()
	{
		Super.BeginPlay ();
		CurSector.Flags |= args[0];
		Destroy();
	}
}

// Marker for sounds : Actor -------------------------------------------------------

class SpeakerIcon : Unknown
{
	States
	{
	Spawn:
		SPKR A -1 BRIGHT;
		Stop;
	}
	Default
	{
		Scale 0.125;
	}
}

//===============================================================
// Path Nodes
//===============================================================

class PathNode : Actor
{
	// For non-connected paths. Stamina will be used to set this. Necessary for tele/portals.
	private int group; 

	Array<Actor> neighbors;
	
	Default
	{
		//$Arg0 "TID 1"
		//$Arg1 "TID 2"
		//$Arg2 "TID 3"
		//$Arg3 "TID 4"
		//$Arg4 "TID 5"
		//$Arg0Type 14
		//$Arg1Type 14
		//$Arg2Type 14
		//$Arg3Type 14
		//$Arg4Type 14
		+NOBLOCKMAP
		+INVISIBLE
		+DONTSPLASH
		+NOTONAUTOMAP
		+NOGRAVITY // TO DO: Look into 3D variant for traversing up and down 3D floors and floating monsters.
		RenderStyle "None";
		MeleeRange 2048; // Sight checks limited to this. 0 = infinite.
	}
	
	// Args are TIDs. Can be one way to force single directions.
	override void PostBeginPlay()
	{
		Super.PostBeginPlay();
		for (int i = 0; i < Args.Size(); i++)
		{
			if (!Args[i])	continue;
			
			let it = level.CreateActorIterator(Args[i], "PathNode");
			PathNode node;
			do
			{
				if (node && node != self) 
					neighbors.Push(node);
			} while (node = PathNode(it.Next()))
			
		}
	}

	// For ACS access with ScriptCall.
	// 'con' values are:
	// 0: No connections
	// 1: Connect tid1 to tid2 one-way
	// 2: ^ but two-way.
	static void SetConnectionGlobal(int tid1, int tid2, int con)
	{
		if (tid1 == 0 || tid2 == 0)
			return;

		PathNode node;
		Array<PathNode> nodes2; // Cache for actors with tid2
		{
			let it = Level.CreateActorIterator(tid2, "PathNode");
			
			do
			{
				if (node) 
					nodes2.Push(node);
			} while (node = PathNode(it.Next()))
		}
		// Nothing to set to.
		if (nodes2.Size() < 1)
			return;

		let it = Level.CreateActorIterator(tid1, "PathNode");
		node = null;
		do
		{	
			if (node)
			{
				foreach (n2 : nodes2)
				{
					if (n2)
					{
						node.SetConnection(n2, con);
						n2.SetConnection(node, (con > 1));
					}
				}
			}
		} while (node = PathNode(it.Next()))
	}

	void SetConnection(PathNode other, bool on)
	{
		if (!other) return;

		if (on)
		{
			if (neighbors.Find(other) >= neighbors.Size())
				neighbors.Push(other);
		}
		else neighbors.Delete(neighbors.Find(other));
	}
}