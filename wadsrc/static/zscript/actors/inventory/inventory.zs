struct VisStyle
{
	bool			Invert;
	float			Alpha;
	int				RenderStyle;
}

class Inventory : Actor
{
	const BLINKTHRESHOLD = (4*32);
	const BONUSADD = 6;

	private bool bSharingItem; // Currently being shared (avoid infinite recursions).
	private bool pickedUp[MAXPLAYERS]; // If items are set to local, track who already picked it up.
	private bool bCreatingCopy; // Tells GoAway that it needs to return true so a new copy of the item is spawned.

	deprecated("3.7") private int ItemFlags;
	Actor Owner;						// Who owns this item? NULL if it's still a pickup.
	int Amount;						// Amount of item this instance has
	int MaxAmount;					// Max amount of item this instance can have
	int InterHubAmount;				// Amount of item that can be kept between hubs or levels
	int RespawnTics;					// Tics from pickup time to respawn time
	TextureID Icon;					// Icon to show on status bar or HUD
	TextureID AltHUDIcon;
	int DropTime;					// Countdown after dropping
	Class<Actor> SpawnPointClass;	// For respawning like Heretic's mace
	Class<Actor> PickupFlash;		// actor to spawn as pickup flash
	Sound PickupSound;
	bool bPickupGood;
	bool bCreateCopyMoved;
	bool bInitEffectFailed;
	meta String PickupMsg;
	meta int GiveQuest;
	meta array<class<Actor> > ForbiddenToPlayerClass;
	meta array<class<Actor> > RestrictedToPlayerClass;
	
	property PickupMessage: PickupMsg;
	property GiveQuest: GiveQuest;
	property Amount: Amount;
	property InterHubAmount: InterHubAmount;
	property MaxAmount: MaxAmount;
	property PickupFlash: PickupFlash;
	property PickupSound: PickupSound;
	property UseSound: UseSound;
	property RespawnTics: RespawnTics;

	flagdef Quiet: ItemFlags, 0;
	flagdef Autoactivate: ItemFlags, 1;
	flagdef Undroppable: ItemFlags, 2;
	flagdef Invbar: ItemFlags, 3;
	flagdef HubPower: ItemFlags, 4;
	flagdef Untossable: ItemFlags, 5;
	flagdef AdditiveTime: ItemFlags, 6;
	flagdef FancyPickupSound: ItemFlags, 7;
	flagdef BigPowerup: ItemFlags, 8;
	flagdef KeepDepleted: ItemFlags, 9;
	flagdef IgnoreSkill: ItemFlags, 10;
	flagdef NoAttenPickupSound: ItemFlags, 11;
	flagdef PersistentPower : ItemFlags, 12;
	flagdef RestrictAbsolutely: ItemFlags, 13;
	flagdef NeverRespawn: ItemFlags, 14;
	flagdef NoScreenFlash: ItemFlags, 15;
	flagdef Tossed: ItemFlags, 16;
	flagdef AlwaysRespawn: ItemFlags, 17;
	flagdef Transfer: ItemFlags, 18;
	flagdef NoTeleportFreeze: ItemFlags, 19;
	flagdef NoScreenBlink: ItemFlags, 20;
	flagdef IsArmor: ItemFlags, 21;
	flagdef IsHealth: ItemFlags, 22;
	flagdef AlwaysPickup: ItemFlags, 23;
	flagdef Unclearable: ItemFlags, 24;
	flagdef NeverLocal: ItemFlags, 25;
	flagdef IsKeyItem: ItemFlags, 26;

	flagdef ForceRespawnInSurvival: none, 0;
	flagdef PickupFlash: none, 6;
	flagdef InterHubStrip: none, 12;

	Default
	{
		Inventory.Amount 1;
		Inventory.MaxAmount 1;
		Inventory.InterHubAmount 1;
		Inventory.UseSound "misc/invuse";
		Inventory.PickupSound "misc/i_pkup";
		Inventory.PickupMessage "$TXT_DEFAULTPICKUPMSG";
	}
	
	//native override void Tick();
	
	override void Tick()
	{
		if (Owner == null)
		{
			// AActor::Tick is only handling interaction with the world
			// and we don't want that for owned inventory items.
			Super.Tick();
		}
		else if (tics != -1)	// ... but at least we have to advance the states
		{
			tics--;
					
			// you can cycle through multiple states in a tic
			// [RH] Use <= 0 instead of == 0 so that spawnstates
			// of 0 tics work as expected.
			if (tics <= 0)
			{
				if (curstate == null)
				{
					Destroy();
					return;
				}
				if (!SetState (curstate.NextState))
					return; 		// freed itself
			}
		}
		if (DropTime)
		{
			if (--DropTime == 0)
			{
				bSpecial = default.bSpecial;
				bSolid = default.bSolid;
			}
		}
	}
	
	
	native static void PrintPickupMessage (bool localview, String str);

	States(Actor)
	{
	HideDoomish:
		TNT1 A 1050;
		TNT1 A 0 A_RestoreSpecialPosition;
		TNT1 A 1 A_RestoreSpecialDoomThing;
		Stop;
	HideSpecial:
		ACLO E 1400;
		ACLO A 0 A_RestoreSpecialPosition;
		ACLO A 4 A_RestoreSpecialThing1;
		ACLO BABCBCDC 4;
		ACLO D 4 A_RestoreSpecialThing2;
		Stop;
	Held:
		TNT1 A -1;
		Stop;
	HoldAndDestroy:
		TNT1 A 1;
		Stop;
	}
	
	//===========================================================================
	//
	// Inventory :: MarkPrecacheSounds
	//
	//===========================================================================

	override void MarkPrecacheSounds()
	{
		Super.MarkPrecacheSounds();
		MarkSound(PickupSound);
	}

	//===========================================================================
	//
	// Inventory :: BeginPlay
	//
	//===========================================================================

	override void BeginPlay ()
	{
		Super.BeginPlay ();
		bDropped = true;	// [RH] Items are dropped by default
	}

	//===========================================================================
	//
	// Inventory :: Destroy
	//
	//===========================================================================

	override void OnDestroy ()
	{
		if (Owner != NULL)
		{
			Owner.RemoveInventory (self);
		}
		Inv = NULL;
		Super.OnDestroy();
	}
	
	//===========================================================================
	//
	// Inventory :: ShouldSpawn
	//
	//===========================================================================

	override bool ShouldSpawn()
	{
		// [RH] Other things that shouldn't be spawned depending on dmflags
		if (deathmatch || alwaysapplydmflags)
		{
			if (sv_nohealth && bIsHealth) return false;
			if (sv_noarmor && bIsArmor) return false;
		}
		return true;
	}
	
	//---------------------------------------------------------------------------
	//
	// PROC A_RestoreSpecialThing1
	//
	// Make a special thing visible again.
	//
	//---------------------------------------------------------------------------

	void A_RestoreSpecialThing1()
	{
		bInvisible = false;
		if (DoRespawn ())
		{
			A_StartSound ("misc/spawn", CHAN_VOICE);
		}
	}

	//---------------------------------------------------------------------------
	//
	// PROC A_RestoreSpecialThing2
	//
	//---------------------------------------------------------------------------

	void A_RestoreSpecialThing2()
	{
		bSpecial = true;
		if (!Default.bNoGravity)
		{
			bNoGravity = false;
		}
		SetState (SpawnState);
	}

	//---------------------------------------------------------------------------
	//
	// PROC A_RestoreSpecialDoomThing
	//
	//---------------------------------------------------------------------------

	void A_RestoreSpecialDoomThing()
	{
		bInvisible = false;
		bSpecial = true;
		if (!Default.bNoGravity)
		{
			bNoGravity = false;
		}
		if (DoRespawn ())
		{
			SetState (SpawnState);
			A_StartSound ("misc/spawn", CHAN_VOICE);
			Spawn ("ItemFog", Pos, ALLOW_REPLACE);
		}
	}

	virtual bool ShouldShareItem(Actor giver)
	{
		return false;
	}

	protected void ShareItemWithPlayers(Actor giver)
	{
		if (bSharingItem)
			return;

		int skip = giver && giver.player ? giver.PlayerNumber() : -1;
		for (int i; i < MAXPLAYERS; ++i)
		{
			if (!playerInGame[i] || i == skip)
				continue;

			let item = CreateLocalCopy(players[i].mo);
			if (!item || item == self)
				continue;

			item.ClearCounters();
			item.bSharingItem = true;
			item.bDropped = item.bNeverLocal = true;
			if (!item.CallTryPickup(players[i].mo))
			{
				item.Destroy();
				continue;
			}
			item.bSharingItem = false;

			if (!bQuiet)
			{
				PrintPickupMessage(i == consoleplayer, item.PickupMessage());

				item.PlayPickupSound(players[i].mo);
				if (!bNoScreenFlash && players[i].PlayerState != PST_DEAD)
					players[i].BonusCount = BONUSADD;
			}
		}
	}

	//===========================================================================
	//
	// Inventory :: DoRespawn
	//
	//===========================================================================

	bool DoRespawn ()
	{
		if (SpawnPointClass != NULL)
		{
			Actor spot = NULL;
			let state = Level.GetSpotState();

			if (state != NULL) spot = state.GetRandomSpot(SpawnPointClass, false);
			if (spot != NULL) 
			{
				SetOrigin (spot.Pos, false);
				SetZ(floorz);
			}
		}
		return true;
	}
	
	//===========================================================================
	//
	// Inventory :: Grind
	//
	//===========================================================================

	override bool Grind(bool items)
	{
		// Does this grind request even care about items?
		if (!items)
		{
			return false;
		}
		// Dropped items are normally destroyed by crushers. Set the DONTGIB flag,
		// and they'll act like corpses with it set and be immune to crushers.
		if (bDropped)
		{
			if (!bDontGib)
			{
				Destroy();
			}
			return false;
		}
		// Non-dropped items call the super method for compatibility.
		return Super.Grind(items);
	}		

	//===========================================================================
	//
	// Inventory :: BecomeItem
	//
	// Lets this actor know that it's about to be placed in an inventory.
	//
	//===========================================================================

	void BecomeItem ()
	{
		if (!bNoBlockmap || !bNoSector)
		{
			A_ChangeLinkFlags(1, 1);
		}
		ChangeTid(0);
		bSpecial = false;
		// if the item was turned into a monster through Dehacked, undo that here.
		bCountkill = false;
		bIsMonster = false;
		ChangeStatNum(STAT_INVENTORY);
		// stop all sounds this item is playing.
		A_StopAllSounds();
		SetState (FindState("Held"));
	}

	//===========================================================================
	//
	// Inventory :: BecomePickup
	//
	// Lets this actor know it should wait to be picked up.
	//
	//===========================================================================

	void BecomePickup ()
	{
		if (Owner != NULL)
		{
			Owner.RemoveInventory (self);
		}
		if (bNoBlockmap || bNoSector)
		{
			A_ChangeLinkFlags(0, 0);
			FindFloorCeiling();
		}
		bSpecial = true;
		bDropped = true;
		bCountItem = false;
		bInvisible = false;
		ChangeStatNum(STAT_DEFAULT);
		SetState (SpawnState);
	}

	//===========================================================================
	//
	// Inventory :: CreateCopy
	//
	// Returns an actor suitable for placing in an inventory, either itself or
	// a copy based on whether it needs to respawn or not. Returning NULL
	// indicates the item should not be picked up.
	//
	//===========================================================================

	virtual Inventory CreateCopy (Actor other)
	{
		Inventory copy;

		// Clamping this on local copy creation presents too many possible
		// pitfalls (e.g. Health items).
		if (!IsCreatingLocalCopy())
			Amount = MIN(Amount, MaxAmount);
		if (GoAway ())
		{
			copy = Inventory(Spawn (GetClass()));
			copy.Amount = Amount;
			copy.MaxAmount = MaxAmount;
		}
		else
		{
			copy = self;
		}
		return copy;
	}

	//===========================================================================
	//
	// Inventory :: HandlePickup
	//
	// Returns true if the pickup was handled (or should not happen at all),
	// false if not.
	//
	//===========================================================================

	virtual bool HandlePickup (Inventory item)
	{
		if (item.GetClass() == GetClass())
		{
			if (Amount < MaxAmount || (sv_unlimited_pickup && !item.ShouldStay()))
			{
				if (Amount > 0 && Amount + item.Amount < 0)
				{
					Amount = 0x7fffffff;
				}
				else
				{
					Amount += item.Amount;
				}
			
				if (Amount > MaxAmount && !sv_unlimited_pickup)
				{
					Amount = MaxAmount;
				}
				item.bPickupGood = true;
			}
			return true;
		}
		return false;
	}

	//===========================================================================
	//
	// Inventory :: CallHandlePickup
	//
	// Runs all HandlePickup methods in the chain
	//
	//===========================================================================

	private bool CallHandlePickup(Inventory item)
	{
		let me = self;
		while (me != null)
		{
			if (me.HandlePickup(item)) return true;
			me = me.Inv;
		}
		return false;
	}
		
	//===========================================================================
	//
	// Inventory :: TryPickup
	//
	//===========================================================================

	virtual protected bool TryPickup (in out Actor toucher)
	{
		Actor newtoucher = toucher; // in case changed by the powerup

		// If HandlePickup() returns true, it will set the IF_PICKUPGOOD flag
		// to indicate that self item has been picked up. If the item cannot be
		// picked up, then it leaves the flag cleared.

		bPickupGood = false;
		if (toucher.Inv != NULL && toucher.Inv.CallHandlePickup (self))
		{
			// Let something else the player is holding intercept the pickup.
			if (!bPickupGood)
			{
				return false;
			}
			bPickupGood = false;
			GoAwayAndDie ();
		}
		else if (MaxAmount > 0)
		{
			// Add the item to the inventory. It is not already there, or HandlePickup
			// would have already taken care of it.
			let copy = CreateCopy (toucher);
			if (copy == NULL)
			{
				return false;
			}
			// Some powerups cannot activate absolutely, for
			// example, PowerMorph; fail the pickup if so.
			if (copy.bInitEffectFailed)
			{
				if (copy != self) copy.Destroy();
				else bInitEffectFailed = false;
				return false;
			}
			// Handle owner-changing powerups
			if (copy.bCreateCopyMoved)
			{
				newtoucher = copy.Owner;
				copy.Owner = NULL;
				bCreateCopyMoved = false;
			}
			// Continue onwards with the rest
			copy.AttachToOwner (newtoucher);
			if (bAutoActivate)
			{
				if (copy.Use (true))
				{
					if (--copy.Amount <= 0)
					{
						copy.bSpecial = false;
						copy.SetStateLabel ("HoldAndDestroy");
					}
				}
			}
		}
		else if (bAutoActivate)
		{
			// Special case: If an item's MaxAmount is 0, you can still pick it
			// up if it is autoactivate-able.

			// The item is placed in the inventory just long enough to be used.
			toucher.AddInventory(self);
			bool usegood = Use(true);

			// Handle potential change of toucher/owner because of morph
			if (usegood && self.owner)
			{
				toucher = self.owner;
			}

			toucher.RemoveInventory(self);

			if (usegood)
			{
				GoAwayAndDie();
			}
			else
			{
				return false;
			}
		}
		return true;
	}

	//===========================================================================
	//
	// Inventory :: GiveQuest
	//
	//===========================================================================

	void GiveQuestItem (Actor toucher)
	{
		if (GiveQuest > 0)
		{
			String qname = "QuestItem" .. GiveQuest;
			class<Inventory> type = qname;
			if (type != null)
			{
				toucher.GiveInventoryType (type);
			}
		}
	}

	//===========================================================================
	//
	// Inventory :: CanPickup
	//
	//===========================================================================
	
	virtual bool CanPickup(Actor toucher)
	{
		if (toucher == null) return false;

		int rsize = RestrictedToPlayerClass.Size();
		if (rsize > 0)
		{
			for (int i=0; i < rsize; i++)
			{
				if (toucher is RestrictedToPlayerClass[i]) return true;
			}
			return false;
		}
		rsize = ForbiddenToPlayerClass.Size();
		if (rsize > 0)
		{
			for (int i=0; i < rsize; i++)
			{
				if (toucher is ForbiddenToPlayerClass[i]) return false;
			}
		}
		return true;
	}

	//===========================================================================
	//
	// Inventory :: CallTryPickup
	//
	// In this case the caller function is more than a simple wrapper around the virtual method and
	// is what must be actually called to pick up an item.
	//
	//===========================================================================

	bool, Actor CallTryPickup(Actor toucher)
	{
		let saved_toucher = toucher;
		let Invstack = Inv; // A pointer of the inventories item stack.

		// unmorphed versions of a currently morphed actor cannot pick up anything. 
		if (bUnmorphed) return false, null;

		// [AA] starting with true, so that CanReceive can unset it,
		// if necessary:
		bool res = true;
		class<Inventory> cls = self.GetClass();
		// [AA] CanReceive lets the actor receiving the item process it first.
		if (!toucher.CanReceive(self))
		{
			res = false;
		}
		// CanPickup processes restrictions by player class.
		else if (CanPickup(toucher))
		{
			res = TryPickup(toucher);
		}
		else if (!bRestrictAbsolutely)
		{
			// let an item decide for itself how it will handle this
			res = TryPickupRestricted(toucher);
		}
		else
			return false, null;


		if (!res && (bAlwaysPickup) && !ShouldStay())
		{
			res = true;
			GoAwayAndDie();
		}

		if (res)
		{
			GiveQuestItem(toucher);

			// Transfer all inventory across that the old object had, if requested.
			if (bTransfer)
			{
				while (Invstack)
				{
					let titem = Invstack;
					Invstack = titem.Inv;
					if (titem.Owner == self)
					{
						if (!titem.CallTryPickup(toucher)) // The object no longer can exist
						{
							titem.Destroy();
						}
					}
				}
			}
			// [AA] Let the toucher do something with the item they've just received:
			toucher.HasReceived(self, cls);

			// If the item can be shared, make sure every player gets a copy.
			if (multiplayer && !deathmatch && ShouldShareItem(toucher))
				ShareItemWithPlayers(toucher);
		}
		return res, toucher;
	}
	
	//===========================================================================
	//
	// Inventory :: ShouldStay
	//
	// Returns true if the item should not disappear, even temporarily.
	//
	//===========================================================================

	virtual bool ShouldStay ()
	{
		return false;
	}

	//===========================================================================
	//
	// Inventory :: TryPickupRestricted
	//
	//===========================================================================

	virtual bool TryPickupRestricted (in out Actor toucher)
	{
		return false;
	}

	//===========================================================================
	//
	// Inventory :: AttachToOwner
	//
	//===========================================================================

	virtual void AttachToOwner (Actor other)
	{
		BecomeItem ();
		other.AddInventory (self);
	}

	//===========================================================================
	//
	// Inventory :: DetachFromOwner
	//
	// Performs any special work needed when the item leaves an inventory,
	// either through destruction or becoming a pickup.
	//
	//===========================================================================

	virtual void DetachFromOwner ()
	{
	}

	//===========================================================================
	//
	// Inventory::CreateTossable
	//
	// Creates a copy of the item suitable for dropping. If this actor embodies
	// only one item, then it is tossed out itself. Otherwise, the count drops
	// by one and a new item with an amount of 1 is spawned.
	//
	//===========================================================================

	virtual Inventory CreateTossable (int amt = -1)
	{
		// If self actor lacks a SpawnState, don't drop it. (e.g. A base weapon
		// like the fist can't be dropped because you'll never see it.)
		if (SpawnState == GetDefaultByType("Actor").SpawnState || SpawnState == NULL)
		{
			return NULL;
		}
		if (bUndroppable || bUntossable || Owner == NULL || Amount <= 0 || amt == 0)
		{
			return NULL;
		}
		if (Amount == 1 && !bKeepDepleted)
		{
			BecomePickup ();
			DropTime = 30;
			bSpecial = bSolid = false;
			return self;
		}
		let copy = Inventory(Spawn (GetClass(), Owner.Pos, NO_REPLACE));
		if (copy != NULL)
		{
			amt = clamp(amt, 1, Amount);
			
			copy.MaxAmount = MaxAmount;
			copy.Amount = amt;
			copy.DropTime = 30;
			copy.bSpecial = copy.bSolid = false;
			Amount -= amt;
		}
		return copy;
	}

	
	//===========================================================================
	//
	// Inventory :: PickupMessage
	//
	// Returns the message to print when this actor is picked up.
	//
	//===========================================================================

	virtual String PickupMessage ()
	{
		return PickupMsg;
	}
	
	//===========================================================================
	//
	// Inventory :: Touch
	//
	// Handles collisions from another actor, possible adding itself to the
	// collider's inventory.
	//
	//===========================================================================

	override void Touch (Actor toucher)
	{
		bool localPickUp;
		let player = toucher.player;
		if (player)
		{
			// If a voodoo doll touches something, pretend the real player touched it instead.
			toucher = player.mo;
			// Client already picked this up, so ignore them.
			if (HasPickedUpLocally(toucher))
				return;

			localPickUp = CanPickUpLocally(toucher) && !ShouldStay() && !ShouldRespawn();
		}

		bool localview = toucher.CheckLocalView();

		if (!toucher.CanTouchItem(self))
			return;

		Inventory give = self;
		if (localPickUp)
		{
			give = CreateLocalCopy(toucher);
			if (!give)
				return;

			localPickUp = give != self;
			if (localPickUp)
				give.ClearCounters();
		}

		bool res;
		[res, toucher] = give.CallTryPickup(toucher);
		if (!res)
		{
			if (give != self)
				give.Destroy();

			return;
		}

		// This is the only situation when a pickup flash should ever play.
		if (PickupFlash != NULL && !ShouldStay())
		{
			Spawn(PickupFlash, Pos, ALLOW_REPLACE);
		}

		if (!bQuiet)
		{
			PrintPickupMessage(localview, give.PickupMessage ());

			// Special check so voodoo dolls picking up items cause the
			// real player to make noise.
			if (player != NULL)
			{
				give.PlayPickupSound (player.mo);
				if (!bNoScreenFlash && player.playerstate != PST_DEAD)
				{
					player.bonuscount = BONUSADD;
				}
			}
			else
			{
				give.PlayPickupSound (toucher);
			}
		}							

		// [RH] Execute an attached special (if any)
		DoPickupSpecial (toucher);

		if (bCountItem)
		{
			if (player != NULL)
			{
				player.itemcount++;
			}
			level.found_items++;
		}

		if (bCountSecret)
		{
			Actor ac = player != NULL? Actor(player.mo) : toucher;
			ac.GiveSecret(true, true);
		}

		if (localPickUp)
			PickUpLocally(toucher);

		//Added by MC: Check if item taken was the roam destination of any bot
		for (int i = 0; i < MAXPLAYERS; i++)
		{
			if (players[i].Bot != NULL && self == players[i].Bot.dest)
				players[i].Bot.dest = NULL;
		}
	}

	//===========================================================================
	//
	// Inventory :: DepleteBy
	//
	// Handles item depletion when using or taking items
	//
	//===========================================================================
	virtual void DepleteBy(int by)
	{
		if (amount < 1 || by >= amount)
		{
			DepleteOrDestroy();
		}
		else
		{
			amount -= by;
		}
	}

	//===========================================================================
	//
	// Inventory :: DepleteOrDestroy
	//
	// If the item is depleted, just change its amount to 0, otherwise it's destroyed.
	//
	//===========================================================================

	virtual void DepleteOrDestroy ()
	{
		// If it's not ammo or an internal armor, destroy it.
		// Ammo needs to stick around, even when it's zero for the benefit
		// of the weapons that use it and to maintain the maximum ammo
		// amounts a backpack might have given.
		// Armor shouldn't be removed because they only work properly when
		// they are the last items in the inventory.
		if (bKeepDepleted)
		{
			Amount = 0;
		}
		else
		{
			Destroy();
		}
	}

	//===========================================================================
	//
	// Inventory :: PreTravelled
	//
	// Called when an item in somebody's inventory is about to be carried
	// over to another map, in case it needs to do special clean-up.
	//
	//===========================================================================

	virtual void PreTravelled() {}

	//===========================================================================
	//
	// Inventory :: Travelled
	//
	// Called when an item in somebody's inventory is carried over to another
	// map, in case it needs to do special reinitialization.
	//
	//===========================================================================

	virtual void Travelled() {}

	//===========================================================================
	//
	// Inventory :: DoEffect
	//
	// Handles any effect an item might apply to its owner
	// Normally only used by subclasses of Powerup
	//
	//===========================================================================

	virtual void DoEffect() {}
	
	//===========================================================================
	//
	// Inventory :: Hide
	//
	// Hides this actor until it's time to respawn again. 
	//
	//===========================================================================

	virtual void Hide ()
	{
		State HideSpecialState = NULL, HideDoomishState = NULL;

		bSpecial = false;
		bNoGravity = true;
		bInvisible = true;

		if (gameinfo.gametype & GAME_Raven)
		{
			HideSpecialState = FindState("HideSpecial");
			if (HideSpecialState == NULL)
			{
				HideDoomishState = FindState("HideDoomish");
			}
		}
		else
		{
			HideDoomishState = FindState("HideDoomish");
			if (HideDoomishState == NULL)
			{
				HideSpecialState = FindState("HideSpecial");
			}
		}

		if (HideSpecialState != NULL)
		{
			SetState (HideSpecialState);
			tics = 1400;
			if (PickupFlash != NULL) tics += 30;
		}
		else if (HideDoomishState != NULL)
		{
			SetState (HideDoomishState);
			tics = 1050;
		}
		if (RespawnTics != 0)
		{
			tics = RespawnTics;
		}
	}


	
	//===========================================================================
	//
	// Inventory :: ShouldRespawn
	//
	// Returns true if the item should hide itself and reappear later when picked
	// up.
	//
	//===========================================================================

	virtual bool ShouldRespawn ()
	{
		if (bBigPowerup && !sv_respawnsuper) return false;
		if (bNeverRespawn) return false;
		return sv_itemrespawn || bAlwaysRespawn;
	}

	//===========================================================================
	//
	// Inventory :: GoAway
	//
	// Returns true if you must create a copy of this item to give to the player
	// or false if you can use this one instead.
	//
	//===========================================================================

	protected bool GoAway ()
	{
		if (IsCreatingLocalCopy())
			return true;

		// Dropped items never stick around
		if (bDropped)
		{
			return false;
		}

		if (!ShouldStay ())
		{
			Hide ();
			if (ShouldRespawn ())
			{
				return true;
			}
			return false;
		}
		return true;
	}
	
	//===========================================================================
	//
	// Inventory :: GoAwayAndDie
	//
	// Like GoAway but used by items that don't insert themselves into the
	// inventory. If they won't be respawning, then they can destroy themselves.
	//
	//===========================================================================

	protected void GoAwayAndDie ()
	{
		if (!GoAway ())
		{
			bSpecial = false;
			if (!bNoBlockmap || !bNoSector)	// make sure that the item no longer interacts with the world for the short rest of its life.
			{
				A_ChangeLinkFlags(1, 1);
			}
			SetStateLabel("HoldAndDestroy");
		}
	}

	// Check if the Actor can recieve a local copy of the item instead of outright taking it.
	clearscope bool CanPickUpLocally(Actor other) const
	{
		return other && other.player
				&& multiplayer && !deathmatch && sv_localitems
				&& !bNeverLocal && (!bDropped || !sv_nolocaldrops);
	}

	// Check if a client has already picked up this item locally.
	clearscope bool HasPickedUpLocally(Actor client) const
	{
		return pickedUp[client.PlayerNumber()];
	}

	// When items are dropped, clear their local pick ups.
	void ClearLocalPickUps()
	{
		DisableLocalRendering(consoleplayer, false);
		for (int i; i < MAXPLAYERS; ++i)
			pickedUp[i] = false;
	}

	// Client picked up this item. Mark it as invisible to that specific player and
	// prevent them from picking it up again.
	protected void PickUpLocally(Actor client)
	{
		int pNum = client.PlayerNumber();
		pickedUp[pNum] = true;
		DisableLocalRendering(pNum, true);
		bCountItem = bCountSecret = false;
	}

	// Force spawn a new version of the item. This needs to use CreateCopy so that
	// any transferrable properties on the item get correctly set.
	Inventory CreateLocalCopy(Actor client)
	{
		bCreatingCopy = true;
		let item = CreateCopy(client);
		bCreatingCopy = false;

		return item;
	}

	protected clearscope bool IsCreatingLocalCopy() const
	{
		return bCreatingCopy;
	}
	
	//===========================================================================
	//
	// Inventory :: ModifyDamage
	//
	// Allows inventory items to manipulate the amount of damage
	// inflicted. Damage is the amount of damage that would be done without manipulation,
	// and newdamage is the amount that should be done after the item has changed
	// it.
	// 'active' means it is called by the inflictor, 'passive' by the target.
	// It may seem that this is redundant and AbsorbDamage is the same. However,
	// AbsorbDamage is called only for players and also depends on other settings
	// which are undesirable for a protection artifact.
	//
	//===========================================================================

	virtual void ModifyDamage(int damage, Name damageType, out int newdamage, bool passive, Actor inflictor = null, Actor source = null, int flags = 0) {}

	virtual Vector2 ModifyBob(Vector2 Bob, double ticfrac) {return Bob;}

	virtual Vector3, Vector3 ModifyBob3D(Vector3 Translation, Vector3 Rotation, double ticfrac) {return Translation, Rotation;}
	

	virtual bool Use (bool pickup) { return false; }
	virtual double GetSpeedFactor() { return 1; }
	virtual bool GetNoTeleportFreeze() { return false; }
	virtual version("2.4") ui void AlterWeaponSprite(VisStyle vis, in out int changed) {}
	virtual void OwnerDied() {}
	virtual Color GetBlend () { return 0; }
	
	virtual void UseAll(Actor user)
	{
		if (bInvBar) user.UseInventory(self);
	}

	//===========================================================================
	//
	// Inventory :: DoPickupSpecial
	//
	// Executes this actor's special when it is picked up.
	//
	//===========================================================================

	virtual void DoPickupSpecial (Actor toucher)
	{
		if (special)
		{
			toucher.A_CallSpecial(special, args[0], args[1], args[2], args[3], args[4]);
			special = 0;
		}
	}
		//===========================================================================
	//
	// Inventory :: PlayPickupSound
	//
	//===========================================================================

	virtual void PlayPickupSound (Actor toucher)
	{
		double atten;
		int chan;
		int flags = 0;

		if (bNoAttenPickupSound)
		{
			atten = ATTN_NONE;
		}
		/*
		else if ((ItemFlags & IF_FANCYPICKUPSOUND) &&
			(toucher == NULL || toucher->CheckLocalView()))
		{
			atten = ATTN_NONE;
		}
		*/
		else
		{
			atten = ATTN_NORM;
		}

		if (toucher != NULL && toucher.CheckLocalView())
		{
			chan = CHAN_ITEM;
			flags = CHANF_NOPAUSE | CHANF_MAYBE_LOCAL;
		}
		else
		{
			chan = CHAN_ITEM;
			flags = CHANF_MAYBE_LOCAL;
		}
		toucher.A_StartSound(PickupSound, chan, flags, 1, atten);
	}

	//===========================================================================
	//
	// Inventory :: DrawPowerup
	//
	// This has been deprecated because it is not how this should be done
	// Use GetPowerupIcon instead!
	//
	//===========================================================================

	virtual ui version("2.4") bool DrawPowerup(int x, int y) { return false; }

	//===========================================================================
	//
	// Inventory :: AbsorbDamage
	//
	// Allows inventory items (primarily armor) to reduce the amount of damage
	// taken. Damage is the amount of damage that would be done without armor,
	// and newdamage is the amount that should be done after the armor absorbs
	// it.
	//
	//===========================================================================

	virtual void AbsorbDamage (int damage, Name damageType, out int newdamage, Actor inflictor = null, Actor source = null, int flags = 0) {}
	
	//===========================================================================
	//
	// Inventory :: SpecialDropAction
	//
	// Called by P_DropItem. Return true to prevent the standard drop tossing.
	// A few Strife items that are meant to trigger actions rather than be
	// picked up use this. Normal items shouldn't need it.
	//
	//===========================================================================

	virtual bool SpecialDropAction (Actor dropper)
	{
		return false;
	}

	//===========================================================================
	//
	// Inventory :: NextInv
	//
	// Returns the next item with IF_INVBAR set.
	//
	//===========================================================================

	clearscope Inventory NextInv () const
	{
		Inventory item = Inv;

		while (item != NULL && !item.bInvBar)
		{
			item = item.Inv;
		}
		return item;
	}


	//===========================================================================
	//
	// Inventory :: PrevInv
	//
	// Returns the previous item with IF_INVBAR set.
	//
	//===========================================================================

	clearscope Inventory PrevInv ()
	{
		Inventory lastgood = NULL;
		Inventory item = Owner.Inv;

		while (item != NULL && item != self)
		{
			if (item.bInvBar)
			{
				lastgood = item;
			}
			item = item.Inv;
		}
		return lastgood;
	}
	
	//===========================================================================
	//
	// Inventory :: OnDrop
	//
	// Called by AActor::DropInventory. Allows items to modify how they behave
	// after being dropped.
	//
	//===========================================================================

	virtual void OnDrop (Actor dropper) {}
	
	//---------------------------------------------------------------------------
	//
	// Modifies the drop amount of this item according to the current skill's
	// settings (also called by ADehackedPickup::TryPickup)
	//
	//---------------------------------------------------------------------------
	
	virtual void ModifyDropAmount(int dropamount)
	{
		if (dropamount > 0)
		{
			Amount = dropamount;
		}
	}
	
	//---------------------------------------------------------------------------
	//
	// Modifies the amount based on what an item should contain if given
	//
	//---------------------------------------------------------------------------
	
	virtual void SetGiveAmount(Actor receiver, int amount, bool givecheat)
	{
		if (givecheat)
		{
			let haveitem = receiver.FindInventory(GetClass());
			self.Amount = MIN(amount, haveitem == null? self.Default.MaxAmount : haveitem.MaxAmount);
		}
		else
		{
			self.Amount = amount;
		}
	}

	
	
}

//===========================================================================
//
// 
//
//===========================================================================

class DehackedPickup : Inventory
{
	Inventory RealPickup;
	bool droppedbymonster;
	
	private native class<Inventory> DetermineType();
	
	override bool TryPickup (in out Actor toucher)
	{
		let type = DetermineType ();
		if (type == NULL)
		{
			return false;
		}
		RealPickup = Inventory(Spawn (type, Pos, NO_REPLACE));
		if (RealPickup != NULL)
		{
			// The internally spawned item should never count towards statistics.
			RealPickup.ClearCounters();
			if (!bDropped)
			{
				RealPickup.bDropped = false;
			}
			// If this item has been dropped by a monster the
			// amount of ammo this gives must be adjusted.
			if (droppedbymonster)
			{
				RealPickup.ModifyDropAmount(0);
			}
			if (!RealPickup.CallTryPickup (toucher))
			{
				RealPickup.Destroy ();
				RealPickup = NULL;
				return false;
			}
			GoAwayAndDie ();
			return true;
		}
		return false;
	}

	override String PickupMessage ()
	{
		if (RealPickup != null)
			return RealPickup.PickupMessage ();
		else return "";
	}

	override bool ShouldStay ()
	{
		if (RealPickup != null)
			return RealPickup.ShouldStay ();
		else return true;
	}

	override bool ShouldRespawn ()
	{
		if (RealPickup != null)
			return RealPickup.ShouldRespawn ();
		else return false;
	}

	override void PlayPickupSound (Actor toucher)
	{
		if (RealPickup != null)
			RealPickup.PlayPickupSound (toucher);
	}

	override void DoPickupSpecial (Actor toucher)
	{
		Super.DoPickupSpecial (toucher);
		// If the real pickup hasn't joined the toucher's inventory, make sure it
		// doesn't stick around.
		if (RealPickup != null && RealPickup.Owner != toucher)
		{
			RealPickup.Destroy ();
		}
		RealPickup = null;
	}

	override void OnDestroy ()
	{
		if (RealPickup != null)
		{
			RealPickup.Destroy ();
			RealPickup = null;
		}
		Super.OnDestroy();
	}
	
	override void ModifyDropAmount(int dropamount)
	{
		// Must forward the adjustment to the real item.
		// dropamount is not relevant here because Dehacked cannot change it.
		droppedbymonster = true;
	}
	
}

//===========================================================================
//
// 
//
//===========================================================================

class FakeInventory : Inventory
{
	bool Respawnable;
	
	property respawns: Respawnable;

	override bool ShouldRespawn ()
	{
		return Respawnable && Super.ShouldRespawn();
	}

	override bool TryPickup (in out Actor toucher)
	{
		let success = toucher.A_CallSpecial(special, args[0], args[1], args[2], args[3], args[4]);

		if (success)
		{
			GoAwayAndDie ();
			return true;
		}
		return false;
	}

	override void DoPickupSpecial (Actor toucher)
	{
		// The special was already executed by TryPickup, so do nothing here
	}
	
}
