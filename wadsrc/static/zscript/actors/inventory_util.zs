extend class Actor
{

	//============================================================================
	//
	// AActor :: FirstInv
	//
	// Returns the first item in this actor's inventory that has IF_INVBAR set.
	//
	//============================================================================

	clearscope Inventory FirstInv ()
	{
		if (Inv == NULL)
		{
			return NULL;
		}
		if (Inv.bInvBar)
		{
			return Inv;
		}
		return Inv.NextInv ();
	}

	//============================================================================
	//
	// AActor :: AddInventory
	//
	//============================================================================

	virtual void AddInventory (Inventory item)
	{
		// Check if it's already attached to an actor
		if (item.Owner != NULL)
		{
			// Is it attached to us?
			if (item.Owner == self)
				return;

			// No, then remove it from the other actor first
			item.Owner.RemoveInventory (item);
		}

		item.Owner = self;
		item.Inv = Inv;
		Inv = item;

		// Each item receives an unique ID when added to an actor's inventory.
		// This is used by the DEM_INVUSE command to identify the item. Simply
		// using the item's position in the list won't work, because ticcmds get
		// run sometime in the future, so by the time it runs, the inventory
		// might not be in the same state as it was when DEM_INVUSE was sent.
		Inv.InventoryID = InventoryID++;
	}

	//============================================================================
	//
	// AActor :: GiveInventory
	//
	//============================================================================

	bool GiveInventory(Class<Inventory> type, int amount, bool givecheat = false)
	{
		bool result = true;
		let player = self.player;

		// This can be called from places which do not check the given item's type.
		if (type == null || !(type is 'Inventory')) return false;

		Weapon savedPendingWeap = player != NULL ? player.PendingWeapon : NULL;
		bool hadweap = player != NULL ? player.ReadyWeapon != NULL : true;

		Inventory item;
		if (!givecheat)
		{
			item = Inventory(Spawn (type));
		}
		else
		{
			item = Inventory(Spawn (type, Pos, NO_REPLACE));
			if (item == NULL) return false;
		}

		// This shouldn't count for the item statistics.
		item.ClearCounters();
		if (!givecheat || amount > 0)
		{
			item.SetGiveAmount(self, amount, givecheat);
		}
		if (!item.CallTryPickup (self))
		{
			item.Destroy ();
			result = false;
		}
		// If the item was a weapon, don't bring it up automatically
		// unless the player was not already using a weapon.
		// Don't bring it up automatically if this is called by the give cheat.
		if (!givecheat && player != NULL && savedPendingWeap != NULL && hadweap)
		{
			player.PendingWeapon = savedPendingWeap;
		}
		return result;
	}

	//============================================================================
	//
	// AActor :: RemoveInventory
	//
	//============================================================================

	virtual void RemoveInventory(Inventory item)
	{
		if (item != NULL && item.Owner != NULL)	// can happen if the owner was destroyed by some action from an item's use state.
		{
			if (Inv == item) Inv = item.Inv;
			else
			{
				for (Actor invp = self; invp != null; invp = invp.Inv)
				{
					if (invp.Inv == item)
					{
						invp.Inv = item.Inv;
						break;
					}
				}
			}
			item.DetachFromOwner();
			item.Owner = NULL;
			item.Inv = NULL;
		}
	}

	//============================================================================
	//
	// AActor :: TakeInventory
	//
	//============================================================================

	bool TakeInventory(class<Inventory> itemclass, int amount, bool fromdecorate = false, bool notakeinfinite = false)
	{
		amount = abs(amount);
		let item = FindInventory(itemclass);

		if (item == NULL)
			return false;

		if (!fromdecorate)
		{
			item.DepleteBy(amount);
			// It won't be used in non-decorate context, so return false here
			return false;
		}

		bool result = false;
		if (item.Amount > 0)
		{
			result = true;
		}

		// Do not take ammo if the "no take infinite/take as ammo depletion" flag is set
		// and infinite ammo is on
		if (notakeinfinite &&
		(sv_infiniteammo || (player && FindInventory('PowerInfiniteAmmo', true))) && (item is 'Ammo'))
		{
			// Nothing to do here, except maybe res = false;? Would it make sense?
			result = false;
		}
		else
		{
			item.DepleteBy(amount);
		}

		return result;
	}


	//============================================================================
	//
	// AActor :: SetInventory
	//
	//============================================================================

	bool SetInventory(class<Inventory> itemclass, int amount, bool beyondMax = false)
	{
		let item = FindInventory(itemclass);

		if (item != null)
		{
			// A_SetInventory sets the absolute amount.
			// Subtract or set the appropriate amount as necessary.

			if (amount == item.Amount)
			{
				// Nothing was changed.
				return false;
			}
			else if (amount <= 0)
			{
				//Remove it all.
				return TakeInventory(itemclass, item.Amount, true, false);
			}
			else if (amount < item.Amount)
			{
				int amt = abs(item.Amount - amount);
				return TakeInventory(itemclass, amt, true, false);
			}
			else
			{
				item.Amount = (beyondMax ? amount : clamp(amount, 0, item.MaxAmount));
				return true;
			}
		}
		else
		{
			if (amount <= 0)
			{
				return true;
			}
			item = Inventory(Spawn(itemclass));
			if (item == null)
			{
				return false;
			}
			else
			{
				item.Amount = amount;
				item.bDropped = true;
				item.bIgnoreSkill = true;
				item.ClearCounters();
				if (!item.CallTryPickup(self))
				{
					item.Destroy();
					return false;
				}
				return true;
			}
		}
		return false;
	}


	//============================================================================
	//
	// AActor :: UseInventory
	//
	// Attempts to use an item. If the use succeeds, one copy of the item is
	// removed from the inventory. If all copies are removed, then the item is
	// destroyed.
	//
	//============================================================================

	virtual bool UseInventory (Inventory item)
	{
		// No using items if you're dead or you don't have them.
		if (health <= 0 || item.Amount <= 0 || item.bDestroyed)
		{
			return false;
		}

		if (!item.Use(false))
		{
			return false;
		}

		if (sv_infiniteinventory)
		{
			return true;
		}
		else
		{
			item.DepleteBy(1); //useinventory can only really use one item at a time
		}
		return true;
	}

	//===========================================================================
	//
	// AActor :: DropInventory
	//
	// Removes a single copy of an item and throws it out in front of the actor.
	//
	//===========================================================================

	Inventory DropInventory (Inventory item, int amt = 1)
	{
		Inventory drop = item.CreateTossable(amt);
		if (drop == null) return NULL;
		drop.ClearLocalPickUps();
		drop.bNeverLocal = true;
		drop.SetOrigin(Pos + (0, 0, 10.), false);
		drop.Angle = Angle;
		drop.VelFromAngle(5.);
		drop.Vel.Z = 1.;
		drop.Vel += Vel;
		drop.bNoGravity = false;	// Don't float
		drop.ClearCounters();	// do not count for statistics again
		drop.OnDrop(self);
		return drop;
	}


	//============================================================================
	//
	// AActor :: ClearInventory
	//
	// Clears the inventory of a single actor.
	//
	//============================================================================

	virtual void ClearInventory()
	{
		// In case destroying an inventory item causes another to be destroyed
		// (e.g. Weapons destroy their sisters), keep track of the pointer to
		// the next inventory item rather than the next inventory item itself.
		// For example, if a weapon is immediately followed by its sister, the
		// next weapon we had tracked would be to the sister, so it is now
		// invalid and we won't be able to find the complete inventory by
		// following it.
		//
		// When we destroy an item, we leave last alone, since the destruction
		// process will leave it pointing to the next item we want to check. If
		// we don't destroy an item, then we move last to point to its Inventory
		// pointer.
		//
		// It should be safe to assume that an item being destroyed will only
		// destroy items further down in the chain, because if it was going to
		// destroy something we already processed, we've already destroyed it,
		// so it won't have anything to destroy.

		let last = self;

		while (last.inv != NULL)
		{
			let inv = last.inv;
			if (!inv.bUndroppable && !inv.bUnclearable)
			{
				inv.DepleteOrDestroy();
				if (!inv.bDestroyed) last = inv;	// was only depleted so advance the pointer manually.
			}
			else
			{
				last = inv;
			}
		}
		if (player != null)
		{
			player.ReadyWeapon = null;
			player.PendingWeapon = WP_NOCHANGE;
		}
	}


	//============================================================================
	//
	// AActor :: GiveAmmo
	//
	// Returns true if the ammo was added, false if not.
	//
	//============================================================================

	bool GiveAmmo (Class<Ammo> type, int amount)
	{
		if (type != NULL && type is 'Ammo')
		{
			let item = Inventory(Spawn (type));
			if (item)
			{
				item.Amount = amount;
				item.bDropped = true;
				if (!item.CallTryPickup (self))
				{
					item.Destroy ();
					return false;
				}
				return true;
			}
		}
		return false;
	}


	//===========================================================================
	//
	// DoGiveInventory
	//
	//===========================================================================

	static bool DoGiveInventory(Actor receiver, bool orresult, class<Inventory> mi, int amount, int setreceiver)
	{
		int paramnum = 0;

		if (receiver == NULL)
		{ // If there's nothing to receive it, it's obviously a fail, right?
			return false;
		}
		if (!orresult)
		{
			receiver = receiver.GetPointer(setreceiver);
			if (receiver == NULL)
			{ 
				return false;
			}
		}
		// Owned inventory items cannot own anything because their Inventory pointer is repurposed for the owner's linked list.
		if (receiver is 'Inventory' && Inventory(receiver).Owner != null)
		{
			return false;
		}

		if (amount <= 0)
		{
			amount = 1;
		}
		if (mi)
		{
			let item = Inventory(Spawn(mi));
			if (item == NULL)
			{
				return false;
			}
			if (item is 'Health')
			{
				item.Amount *= amount;
			}
			else
			{
				item.Amount = amount;
			}
			item.bDropped = true;
			item.ClearCounters();
			if (!item.CallTryPickup(receiver))
			{
				item.Destroy();
				return false;
			}
			else
			{
				return true;
			}
		}
		return false;
	}

	bool A_GiveInventory(class<Inventory> itemtype, int amount = 0, int giveto = AAPTR_DEFAULT)
	{
		return DoGiveInventory(self, false, itemtype, amount, giveto);
	}

	bool A_GiveToTarget(class<Inventory> itemtype, int amount = 0, int giveto = AAPTR_DEFAULT)
	{
		return DoGiveInventory(target, false, itemtype, amount, giveto);
	}

	int A_GiveToChildren(class<Inventory> itemtype, int amount = 0)
	{
		let it = ThinkerIterator.Create('Actor');
		Actor mo;
		int count = 0;

		while ((mo = Actor(it.Next())))
		{
			if (mo.master == self)
			{
				count += DoGiveInventory(mo, true, itemtype, amount, AAPTR_DEFAULT);
			}
		}
		return count;
	}

	int A_GiveToSiblings(class<Inventory> itemtype, int amount = 0)
	{
		let it = ThinkerIterator.Create('Actor');
		Actor mo;
		int count = 0;

		if (self.master != NULL)
		{
			while ((mo = Actor(it.Next())))
			{
				if (mo.master == self.master && mo != self)
				{
					count += DoGiveInventory(mo, true, itemtype, amount, AAPTR_DEFAULT);
				}
			}
		}
		return count;
	}

	//===========================================================================
	//
	// A_TakeInventory
	//
	//===========================================================================

	bool DoTakeInventory(Actor receiver, bool orresult, class<Inventory> itemtype, int amount, int flags, int setreceiver = AAPTR_DEFAULT)
	{
		int paramnum = 0;

		if (itemtype == NULL)
		{
			return false;
		}
		if (receiver == NULL)
		{
			return false;
		}
		if (!orresult)
		{
			receiver = receiver.GetPointer(setreceiver);
		}
		if (receiver == NULL)
		{
			return false;
		}

		return receiver.TakeInventory(itemtype, amount, true, (flags & TIF_NOTAKEINFINITE) != 0);
	}

	bool A_TakeInventory(class<Inventory> itemtype, int amount = 0, int flags = 0, int giveto = AAPTR_DEFAULT)
	{
		return DoTakeInventory(self, false, itemtype, amount, flags, giveto);
	}

	bool A_TakeFromTarget(class<Inventory> itemtype, int amount = 0, int flags = 0, int giveto = AAPTR_DEFAULT)
	{
		return DoTakeInventory(target, false, itemtype, amount, flags, giveto);
	}

	int A_TakeFromChildren(class<Inventory> itemtype, int amount = 0)
	{
		let it = ThinkerIterator.Create('Actor');
		Actor mo;
		int count = 0;

		while ((mo = Actor(it.Next())))
		{
			if (mo.master == self)
			{
				count += DoTakeInventory(mo, true, itemtype, amount, 0, AAPTR_DEFAULT);
			}
		}
		return count;
	}

	int A_TakeFromSiblings(class<Inventory> itemtype, int amount = 0)
	{
		let it = ThinkerIterator.Create('Actor');
		Actor mo;
		int count = 0;

		if (self.master != NULL)
		{
			while ((mo = Actor(it.Next())))
			{
				if (mo.master == self.master && mo != self)
				{
					count += DoTakeInventory(mo, true, itemtype, amount, 0, AAPTR_DEFAULT);
				}
			}
		}
		return count;
	}

	//===========================================================================
	//
	// A_SetInventory
	//
	//===========================================================================

	bool A_SetInventory(class<Inventory> itemtype, int amount, int ptr = AAPTR_DEFAULT, bool beyondMax = false)
	{
		bool res = false;

		if (itemtype == null)
		{
			return false;
		}

		Actor mobj = GetPointer(ptr);

		if (mobj == null)
		{
			return false;
		}

		// Do not run this function on voodoo dolls because the way they transfer the inventory to the player will not work with the code below.
		if (mobj.player != null)
		{
			mobj = mobj.player.mo;
		}
		return mobj.SetInventory(itemtype, amount, beyondMax);
	}


	//============================================================================
	//
	// P_TossItem
	//
	//============================================================================

	void TossItem ()
	{
		int style = sv_dropstyle;
		if (style==0) style = gameinfo.defaultdropstyle;

		if (style==2)
		{
			Vel.X += random2[DropItem](7);
			Vel.Y += random2[DropItem](7);
		}
		else
		{
			Vel.X += random2[DropItem]() / 256.;
			Vel.Y += random2[DropItem]() / 256.;
			Vel.Z = 5. + random[DropItem]() / 64.;
		}
	}


	//---------------------------------------------------------------------------
	//
	// PROC A_DropItem
	//
	//---------------------------------------------------------------------------

	Actor A_DropItem(class<Actor> item, int dropamount = -1, int chance = 256)
	{
		if (item != NULL && random[DropItem]() <= chance)
		{
			Actor mo;
			double spawnz = 0;

			if (!(Level.compatflags & COMPATF_NOTOSSDROPS))
			{
				int style = sv_dropstyle;
				if (style == 0)
				{
					style = gameinfo.defaultdropstyle;
				}
				if (style == 2)
				{
					spawnz = 24;
				}
				else
				{
					spawnz = Height / 2;
				}
			}
			mo = Spawn(item, pos + (0, 0, spawnz), ALLOW_REPLACE);
			if (mo != NULL)
			{
				mo.bDropped = true;
				mo.bNoGravity = false;	// [RH] Make sure it is affected by gravity
				if (!(Level.compatflags & COMPATF_NOTOSSDROPS))
				{
					mo.TossItem ();
				}
				let inv = Inventory(mo);
				if (inv)
				{
					inv.ModifyDropAmount(dropamount);
					inv.bTossed = true;
					if (inv.SpecialDropAction(self))
					{
						// The special action indicates that the item should not spawn
						inv.Destroy();
						return null;
					}
				}
				return mo;
			}
		}
		return NULL;
	}

	//==========================================================================
	//
	// CountInv
	//
	// NON-ACTION function to return the inventory count of an item.
	//
	//==========================================================================

	clearscope int CountInv(class<Inventory> itemtype, int ptr_select = AAPTR_DEFAULT) const
	{
		let realself = GetPointer(ptr_select);
		if (realself == NULL || itemtype == NULL)
		{
			return 0;
		}
		else
		{
			let item = realself.FindInventory(itemtype);
			return item ? item.Amount : 0;
		}
	}

	//==========================================================================
	//
	// State jump function
	//
	//==========================================================================

	bool CheckInventory(class<Inventory> itemtype, int itemamount, int owner = AAPTR_DEFAULT)
	{
		if (itemtype == null)
		{
			return false;
		}
		let owner = GetPointer(owner);
		if (owner == null)
		{
			return false;
		}

		let item = owner.FindInventory(itemtype);

		if (item)
		{
			if (itemamount > 0)
			{
				if (item.Amount >= itemamount)
				{
					return true;
				}
			}
			else if (item.Amount >= item.MaxAmount)
			{
				return true;
			}
		}
		return false;
	}

	//============================================================================
	//
	// AActor :: ObtainInventory
	//
	// Removes the items from the other actor and puts them in this actor's
	// inventory. The actor receiving the inventory must not have any items.
	//
	//============================================================================

	void ObtainInventory (Actor other)
	{
		Inv = other.Inv;
		InventoryID = other.InventoryID;
		other.Inv = NULL;
		other.InventoryID = 0;

		let you = PlayerPawn(other);
		let me = PlayerPawn(self);
		
		if (you)
		{
			if (me)
			{
				me.InvFirst = you.InvFirst;
				me.InvSel = you.InvSel;
			}
			you.InvFirst = NULL;
			you.InvSel = NULL;
		}

		
		for (let item = Inv; item != null; item = item.Inv)
		{
			item.Owner = self;
		}
	}

	
	//===========================================================================
	//
	// A_SelectWeapon
	//
	//===========================================================================

	bool A_SelectWeapon(class<Weapon> whichweapon, int flags = 0)
	{
		bool selectPriority = !!(flags & SWF_SELECTPRIORITY);
		let player = self.player;

		if ((!selectPriority && whichweapon == NULL) || player == NULL)
		{
			return false;
		}

		let weaponitem = Weapon(FindInventory(whichweapon));

		if (weaponitem != NULL)
		{
			if (player.ReadyWeapon != weaponitem)
			{
				player.PendingWeapon = weaponitem;
			}
			return true;
		}
		else if (selectPriority)
		{
			// [XA] if the named weapon cannot be found (or is a dummy like 'None'),
			//      select the next highest priority weapon. This is basically
			//      the same as A_CheckReload minus the ammo check. Handy.
			player.mo.PickNewWeapon(NULL);
			return true;
		}
		else
		{
			return false;
		}
	}

	
	int GetAmmoCapacity(class<Ammo> type)
	{
		if (type != NULL)
		{
			let item = FindInventory(type);
			if (item != NULL)
			{
				return item.MaxAmount;
			}
			else
			{
				return GetDefaultByType(type).MaxAmount;
			}
		}
		return 0;
	}

	void SetAmmoCapacity(class<Ammo> type, int amount)
	{
		if (type != NULL)
		{
			let item = FindInventory(type);
			if (item != NULL)
			{
				item.MaxAmount = amount;
			}
			else
			{
				item = GiveInventoryType(type);
				if (item != NULL)
				{
					item.MaxAmount = amount;
					item.Amount = 0;
				}
			}
		}
	}

	clearscope static class<BasicArmor> GetBasicArmorClass()
	{
		class<BasicArmor> cls = (class<BasicArmor>)(GameInfo.BasicArmorClass);
		if (cls)
			return cls;

		return "BasicArmor";
	}

	clearscope static class<HexenArmor> GetHexenArmorClass()
	{
		class<HexenArmor> cls = (class<HexenArmor>)(GameInfo.HexenArmorClass);
		if (cls)
			return cls;

		return "HexenArmor";
	}

}
