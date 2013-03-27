/**
 * @file
 */

/*
Copyright (C) 2002-2013 UFO: Alien Invasion.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "inventory.h"

void InventoryInterface::removeInvList (invList_t *invList)
{
	Com_DPrintf(DEBUG_SHARED, "removeInvList: remove one slot (%s)\n", invName);

	/* first entry */
	if (this->_invList == invList) {
		invList_t *ic = this->_invList;
		this->_invList = ic->getNext();
		free(ic);
	} else {
		invList_t *ic = this->_invList;
		invList_t *prev = NULL;
		while (ic) {
			if (ic == invList) {
				if (prev)
					prev->setNext(ic->getNext());
				free(ic);
				break;
			}
			prev = ic;
			ic = ic->getNext();
		}
	}
}

invList_t *InventoryInterface::addInvList (inventory_t *const inv, const invDef_t *container)
{
	invList_t *newEntry = (invList_t*)alloc(sizeof(invList_t));
	newEntry->setNext(NULL);	/* not really needed - but for better readability */

	Com_DPrintf(DEBUG_SHARED, "AddInvList: add one slot (%s)\n", invName);

	invList_t *firstEntry = inv->getContainer(container->id);
	if (!firstEntry) {
		/* create the list */
		inv->setContainer(container->id, newEntry);
	} else {
		/* read up to the end of the list */
		invList_t *list = firstEntry;
		while (list->getNext())
			list = list->getNext();
		/* append our new item as the last in the list */
		list->setNext(newEntry);
	}

	return newEntry;
}

/**
 * @brief Add an item to a specified container in a given inventory.
 * @note Set x and y to NONE if the item should get added to an automatically chosen free spot in the container.
 * @param[in,out] inv Pointer to inventory definition, to which we will add item.
 * @param[in] item Item to add to given container (needs to have "rotated" tag already set/checked, this is NOT checked here!)
 * @param[in] container Container in given inventory definition, where the new item will be stored.
 * @param[in] x The x location in the container.
 * @param[in] y The x location in the container.
 * @param[in] amount How many items of this type should be added. (this will overwrite the amount as defined in "item.amount")
 * @sa removeFromInventory
 * @return the @c invList_t pointer the item was added to, or @c NULL in case of an error (item wasn't added)
 */
invList_t *InventoryInterface::addToInventory (inventory_t *const inv, const Item* const item, const invDef_t *container, int x, int y, int amount)
{
	invList_t *ic;
	int checkedTo;

	if (!item->def())
		return NULL;

	if (amount <= 0)
		return NULL;

	assert(inv);
	assert(container);

	if (container->single && inv->getContainer(container->id))
		return NULL;

	/* CID_EQUIP and CID_FLOOR */
	if (container->temp) {
		for (ic = inv->getContainer(container->id); ic; ic = ic->getNext())
			if (ic->isSameAs(item)) {
				ic->addAmount(amount);
				Com_DPrintf(DEBUG_SHARED, "addToInventory: Amount of '%s': %i (%s)\n",
					ic->def()->name, ic->amount, invName);
				return ic;
			}
	}

	if (x < 0 || y < 0 || x >= SHAPE_BIG_MAX_WIDTH || y >= SHAPE_BIG_MAX_HEIGHT) {
		/* No (sane) position in container given as parameter - find free space on our own. */
		inv->findSpace(container, item, &x, &y, NULL);
		if (x == NONE)
			return NULL;
	}

	checkedTo = inv->canHoldItem(container, item->def(), x, y, NULL);
	assert(checkedTo);

	/* not found - add a new one */
	ic = addInvList(inv, container);

	/* Set the data in the new entry to the data we got via function-parameters.*/
	*ic = *item;
	ic->setNext(NULL);
	ic->setAmount(amount);

	/* don't reset an already applied rotation */
	if (checkedTo == INV_FITS_ONLY_ROTATED)
		ic->rotated = true;
	ic->setX(x);
	ic->setY(y);

	return ic;
}

/**
 * @param[in] inv The inventory the container is in.
 * @param[in] container The container where the item should be removed.
 * @param[in] fItem The item to be removed.
 * @return true If removal was successful.
 * @return false If nothing was removed or an error occurred.
 * @sa addToInventory
 */
bool InventoryInterface::removeFromInventory (inventory_t* const inv, const invDef_t *container, invList_t *fItem)
{
	invList_t *ic, *previous;

	assert(inv);
	assert(container);
	assert(fItem);

	ic = inv->getContainer(container->id);
	if (!ic)
		return false;

	/** @todo the problem here is, that in case of a move inside the same container
	 * the item don't just get updated x and y values but it is tried to remove
	 * one of the items => crap - maybe we have to change the inventory move function
	 * to check for this case of move and only update the x and y coordinates instead
	 * of calling the add and remove functions */
	if (container->single || ic == fItem) {
		this->cacheItem = *ic;
		/* temp container like CID_EQUIP and CID_FLOOR */
		if (container->temp && ic->amount > 1) {
			ic->addAmount(-1);
			Com_DPrintf(DEBUG_SHARED, "removeFromInventory: Amount of '%s': %i (%s)\n",
				ic->def()->name, ic->amount, invName);
			return true;
		}

		if (container->single && ic->getNext())
			Com_Printf("removeFromInventory: Error: single container %s has many items. (%s)\n", container->name, invName);

		/* An item in other containers than CID_FLOOR or CID_EQUIP should
		 * always have an amount value of 1.
		 * The other container types do not support stacking.*/
		assert(ic->amount == 1);

		inv->setContainer(container->id, ic->getNext());

		/* updated invUnused to be able to reuse this space later again */
		removeInvList(ic);

		return true;
	}

	for (previous = inv->getContainer(container->id); ic; ic = ic->getNext()) {
		if (ic == fItem) {
			this->cacheItem = *ic;
			/* temp container like CID_EQUIP and CID_FLOOR */
			if (ic->amount > 1 && container->temp) {
				ic->addAmount(-1);
				Com_DPrintf(DEBUG_SHARED, "removeFromInventory: Amount of '%s': %i (%s)\n",
					ic->def()->name, ic->amount, invName);
				return true;
			}

			if (ic == inv->getContainer(container->id))
				inv->setContainer(container->id, inv->getContainer(container->id)->getNext());
			else
				previous->setNext(ic->getNext());

			removeInvList(ic);

			return true;
		}
		previous = ic;
	}
	return false;
}

/**
 * @brief Conditions for moving items between containers.
 * @param[in] inv The inventory to move in.
 * @param[in] from Source container.
 * @param[in] fItem The item to be moved.
 * @param[in] to Destination container.
 * @param[in] tx X coordinate in destination container.
 * @param[in] ty Y coordinate in destination container.
 * @param[in,out] TU pointer to entity available TU at this moment
 * or @c NULL if TU doesn't matter (outside battlescape)
 * @param[out] icp
 * @return IA_NOTIME when not enough TU.
 * @return IA_NONE if no action possible.
 * @return IA_NORELOAD if you cannot reload a weapon.
 * @return IA_RELOAD_SWAP in case of exchange of ammo in a weapon.
 * @return IA_RELOAD when reloading.
 * @return IA_ARMOUR when placing an armour on the actor.
 * @return IA_MOVE when just moving an item.
 */
inventory_action_t InventoryInterface::moveInInventory (inventory_t* const inv, const invDef_t *from, invList_t *fItem, const invDef_t *to, int tx, int ty, int *TU, invList_t ** icp)
{
	invList_t *ic;

	int time;
	int checkedTo = INV_DOES_NOT_FIT;
	bool alreadyRemovedSource = false;

	assert(to);
	assert(from);

	if (icp)
		*icp = NULL;

	if (from == to && fItem->getX() == tx && fItem->getY() == ty)
		return IA_NONE;

	time = from->out + to->in;
	if (from == to) {
		if (from->isFloorDef())
			time = 0;
		else
			time /= 2;
	}

	if (TU && *TU < time)
		return IA_NOTIME;

	assert(inv);

	/* Special case for moving an item within the same container. */
	if (from == to) {
		/* Do nothing if we move inside a scroll container. */
		if (from->scroll)
			return IA_NONE;

		ic = inv->getContainer(from->id);
		for (; ic; ic = ic->getNext()) {
			if (ic == fItem) {
				if (ic->amount > 1) {
					checkedTo = inv->canHoldItem(to, ic->def(), tx, ty, fItem);
					if (checkedTo & INV_FITS) {
						ic->setX(tx);
						ic->setY(ty);
						if (icp)
							*icp = ic;
						return IA_MOVE;
					}
					return IA_NONE;
				}
			}
		}
	}

	/* If weapon is twohanded and is moved from hand to hand do nothing. */
	/* Twohanded weapon are only in CID_RIGHT. */
	if (fItem->def()->fireTwoHanded && to->isLeftDef() && from->isRightDef()) {
		return IA_NONE;
	}

	/* If non-armour moved to an armour slot then abort.
	 * Same for non extension items when moved to an extension slot. */
	if ((to->armour && !fItem->isArmour())
	 || (to->extension && !fItem->def()->extension)
	 || (to->headgear && !fItem->def()->headgear)) {
		return IA_NONE;
	}

	/* Check if the target is a blocked inv-armour and source!=dest. */
	if (to->single)
		checkedTo = inv->canHoldItem(to, fItem->def(), 0, 0, fItem);
	else {
		if (tx == NONE || ty == NONE)
			inv->findSpace(to, fItem, &tx, &ty, fItem);
		/* still no valid location found */
		if (tx == NONE || ty == NONE)
			return IA_NONE;

		checkedTo = inv->canHoldItem(to, fItem->def(), tx, ty, fItem);
	}

	if (to->armour && from != to && !checkedTo) {
		item_t cacheItem2;
		invList_t *icTo;
		/* Store x/y origin coordinates of removed (source) item.
		 * When we re-add it we can use this. */
		const int cacheFromX = fItem->getX();
		const int cacheFromY = fItem->getY();

		/* Check if destination/blocking item is the same as source/from item.
		 * In that case the move is not needed -> abort. */
		icTo = inv->getItemAtPos(to, tx, ty);
		if (fItem->def() == icTo->def())
			return IA_NONE;

		/* Actually remove the ammo from the 'from' container. */
		if (!removeFromInventory(inv, from, fItem))
			return IA_NONE;
		else
			/* Removal successful - store this info. */
			alreadyRemovedSource = true;

		cacheItem2 = this->cacheItem; /* Save/cache (source) item. The cacheItem is modified in MoveInInventory. */

		/* Move the destination item to the source. */
		moveInInventory(inv, to, icTo, from, cacheFromX, cacheFromY, TU, icp);

		/* Reset the cached item (source) (It'll be move to container emptied by destination item later.) */
		this->cacheItem = cacheItem2;
		checkedTo = inv->canHoldItem(to, this->cacheItem.def(), 0, 0, fItem);
	} else if (!checkedTo) {
		/* Get the target-invlist (e.g. a weapon). We don't need to check for
		 * scroll because checkedTo is always true here. */
		ic = inv->getItemAtPos(to, tx, ty);

		if (ic && !to->isEquipDef() && fItem->def()->isLoadableInWeapon(ic->def())) {
			/* A target-item was found and the dragged item (implicitly ammo)
			 * can be loaded in it (implicitly weapon). */
			if (ic->getAmmoLeft() >= ic->def()->ammo && ic->ammo == fItem->def()) {
				/* Weapon already fully loaded with the same ammunition -> abort */
				return IA_NORELOAD;
			}
			time += ic->def()->reload;
			if (!TU || *TU >= time) {
				if (TU)
					*TU -= time;
				if (ic->getAmmoLeft() >= ic->def()->ammo) {
					/* exchange ammo */
					const Item item(ic->ammo);
					/* Put current ammo in place of the new ammo unless floor - there can be more than 1 item */
					const int cacheFromX = from->isFloorDef() ? NONE : fItem->getX();
					const int cacheFromY = from->isFloorDef() ? NONE : fItem->getY();

					/* Actually remove the ammo from the 'from' container. */
					if (!removeFromInventory(inv, from, fItem))
						return IA_NONE;

					/* Add the currently used ammo in place of the new ammo in the "from" container. */
					if (addToInventory(inv, &item, from, cacheFromX, cacheFromY, 1) == NULL)
						Sys_Error("Could not reload the weapon - add to inventory failed (%s)", invName);

					ic->ammo = this->cacheItem.def();
					if (icp)
						*icp = ic;
					return IA_RELOAD_SWAP;
				} else {
					/* Actually remove the ammo from the 'from' container. */
					if (!removeFromInventory(inv, from, fItem))
						return IA_NONE;

					ic->ammo = this->cacheItem.def();
					/* loose ammo of type ic->m saved on server side */
					ic->setAmmoLeft(ic->def()->ammo);
					if (icp)
						*icp = ic;
					return IA_RELOAD;
				}
			}
			/* Not enough time -> abort. */
			return IA_NOTIME;
		}

		/* temp container like CID_EQUIP and CID_FLOOR */
		if (ic && to->temp) {
			/* We are moving to a blocked location container but it's the base-equipment floor or a battlescape floor.
			 * We add the item anyway but it'll not be displayed (yet)
			 * This is then used in addToInventory below.*/
			/** @todo change the other code to browse through these things. */
			inv->findSpace(to, fItem, &tx, &ty, fItem);
			if (tx == NONE || ty == NONE) {
				Com_DPrintf(DEBUG_SHARED, "MoveInInventory - item will be added non-visible (%s)\n", invName);
			}
		} else {
			/* Impossible move -> abort. */
			return IA_NONE;
		}
	}

	/* twohanded exception - only CID_RIGHT is allowed for fireTwoHanded weapons */
	if (fItem->def()->fireTwoHanded && to->isLeftDef())
		to = &this->csi->ids[CID_RIGHT];

	switch (checkedTo) {
	case INV_DOES_NOT_FIT:
		/* Impossible move - should be handled above, but add an abort just in case */
		Com_Printf("MoveInInventory: Item doesn't fit into container.");
		return IA_NONE;
	case INV_FITS:
		/* Remove rotated tag */
		fItem->rotated = false;
		break;
	case INV_FITS_ONLY_ROTATED:
		/* Set rotated tag */
		fItem->rotated = true;
		break;
	case INV_FITS_BOTH:
		/* Leave rotated tag as-is */
		break;
	}

	/* Actually remove the item from the 'from' container (if it wasn't already removed). */
	if (!alreadyRemovedSource)
		if (!removeFromInventory(inv, from, fItem))
			return IA_NONE;

	/* successful */
	if (TU)
		*TU -= time;

	assert(this->cacheItem.def());
	ic = addToInventory(inv, &this->cacheItem, to, tx, ty, 1);

	/* return data */
	if (icp) {
		assert(ic);
		*icp = ic;
	}

	if (to->isArmourDef()) {
		assert(this->cacheItem.isArmour());
		return IA_ARMOUR;
	}

	return IA_MOVE;
}

/**
 * @brief Tries to add an item to a container (in the inventory inv).
 * @param[in] inv The inventory to add the item to.
 * @param[in] item Item to add to inventory (actually a copy of it).
 * @param[in] container Container id.
 * @sa findSpace
 * @sa addToInventory
 */
bool InventoryInterface::tryAddToInventory (inventory_t* const inv, const Item *const item, const invDef_t *container)
{
	int x, y;

	inv->findSpace(container, item, &x, &y, NULL);

	if (x == NONE) {
		assert(y == NONE);
		return false;
	} else {
		const int checkedTo = inv->canHoldItem(container, item->def(), x, y, NULL);
		if (!checkedTo)
			return false;

		const bool rotated = checkedTo == INV_FITS_ONLY_ROTATED;
		Item itemRotation = *item;
		itemRotation.rotated = rotated;

		return addToInventory(inv, &itemRotation, container, x, y, 1) != NULL;
	}
}

/**
 * @brief Clears the linked list of a container - removes all items from this container.
 * @param[in] inv The inventory where the container is located.
 * @param[in] containerId Index of the container which will be cleared.
 * @sa destroyInventory
 * @note This should only be called for temp containers if the container is really a temp container
 * e.g. the container of a dropped weapon in tactical mission (ET_ITEM)
 * in every other case just set the pointer to NULL for a temp container like CID_EQUIP or CID_FLOOR
 */
void InventoryInterface::emptyContainer (inventory_t* const inv, const containerIndex_t containerId)
{
	invList_t *ic;

	ic = inv->getContainer(containerId);

	while (ic) {
		invList_t *old = ic;
		ic = ic->getNext();
		removeInvList(old);
	}

	inv->resetContainer(containerId);
}

/**
 * @brief Destroys inventory.
 * @param[in] inv Pointer to the inventory which should be erased.
 * @note Loops through all containers in inventory. @c NULL for temp containers are skipped,
 * for real containers @c emptyContainer is called.
 * @sa emptyContainer
 */
void InventoryInterface::destroyInventory (inventory_t* const inv)
{
	containerIndex_t container;

	if (!inv)
		return;

	for (container = 0; container < CID_MAX; container++) {
		const invDef_t *invDef = &this->csi->ids[container];
		if (!invDef->temp)
			emptyContainer(inv, invDef->id);
	}

	OBJZERO(*inv);
}

/**
 * @brief Get the state of the given inventory: the items weight and the min TU needed to make full use of all the items.
 * @param[in] inventory The pointer to the inventory to evaluate.
 * @param[out] slowestFd The TU needed to use the slowest fireDef in the inventory.
 * @note temp containers are excluded.
 */
float InventoryInterface::GetInventoryState (const inventory_t *inventory, int &slowestFd)
{
	float weight = 0;

	slowestFd = 0;
	const Container *cont = NULL;
	while ((cont = inventory->getNextCont(cont))) {
		for (invList_t *ic = cont->_invList, *next; ic; ic = next) {
			next = ic->getNext();
			weight += ic->getWeight();
			const fireDef_t *fireDef = (*ic).getSlowestFireDef();
			if (slowestFd == 0 || (fireDef && fireDef->time > slowestFd))
					slowestFd = fireDef->time;
		}
	}
	return weight;
}

#define WEAPONLESS_BONUS	0.4		/* if you got neither primary nor secondary weapon, this is the chance to retry to get one (before trying to get grenades or blades) */

/**
 * @brief Pack a weapon, possibly with some ammo
 * @param[in] chr The character that will get the weapon
 * @param[in] weapon The weapon type index in gi.csi->ods
 * @param[in] ed The equipment for debug messages
 * @param[in] missedPrimary if actor didn't get primary weapon, this is 0-100 number to increase ammo number.
 * @param[in] maxWeight The max weight this actor is allowed to carry.
 * @sa isLoadableInWeapon()
 */
int InventoryInterface::PackAmmoAndWeapon (character_t* const chr, const objDef_t *weapon, int missedPrimary, const equipDef_t *ed, int maxWeight)
{
	inventory_t* const inv = &chr->inv;
	const int speed = chr->score.skills[ABILITY_SPEED];
	const objDef_t *ammo = NULL;
	bool allowLeft;
	bool packed;
	int ammoMult = 1;
	int tuNeed = 0;
	int maxTU;
	float weight;

	assert(!weapon->isArmour());
	Item item(weapon);

	/* are we going to allow trying the left hand */
	allowLeft = !(inv->getContainer(CID_RIGHT) && inv->getContainer(CID_RIGHT)->def()->fireTwoHanded);

	if (weapon->oneshot) {
		/* The weapon provides its own ammo (i.e. it is charged or loaded in the base.) */
		item.setAmmoLeft(weapon->ammo);
		item.ammo = weapon;
		Com_DPrintf(DEBUG_SHARED, "PackAmmoAndWeapon: oneshot weapon '%s' in equipment '%s' (%s).\n",
				weapon->id, ed->id, invName);
	} else if (!weapon->reload) {
		item.ammo = item.def(); /* no ammo needed, so fire definitions are in t */
	} else {
		/* find some suitable ammo for the weapon (we will have at least one if there are ammos for this
		 * weapon in equipment definition) */
		int totalAvailableAmmo = 0;
		int i;
		for (i = 0; i < csi->numODs; i++) {
			const objDef_t *obj = INVSH_GetItemByIDX(i);
			if (ed->numItems[i] && obj->isLoadableInWeapon(weapon)) {
				totalAvailableAmmo++;
			}
		}
		if (totalAvailableAmmo) {
			int randNumber = rand() % totalAvailableAmmo;
			for (i = 0; i < csi->numODs; i++) {
				const objDef_t *obj = INVSH_GetItemByIDX(i);
				if (ed->numItems[i] && obj->isLoadableInWeapon(weapon)) {
					randNumber--;
					if (randNumber < 0) {
						ammo = obj;
						break;
					}
				}
			}
		}

		if (!ammo) {
			Com_DPrintf(DEBUG_SHARED, "PackAmmoAndWeapon: no ammo for sidearm or primary weapon '%s' in equipment '%s' (%s).\n",
					weapon->id, ed->id, invName);
			return 0;
		}
		/* load ammo */
		item.setAmmoLeft(weapon->ammo);
		item.ammo = ammo;
	}

	if (!item.ammo) {
		Com_Printf("PackAmmoAndWeapon: no ammo for sidearm or primary weapon '%s' in equipment '%s' (%s).\n",
				weapon->id, ed->id, invName);
		return 0;
	}

	weight = GetInventoryState(inv, tuNeed) + item.getWeight();
	maxTU = GET_TU(speed, GET_ENCUMBRANCE_PENALTY(weight, chr->score.skills[ABILITY_POWER]));
	if (weight > maxWeight || tuNeed > maxTU) {
		Com_DPrintf(DEBUG_SHARED, "PackAmmoAndWeapon: weapon too heavy: '%s' in equipment '%s' (%s).\n",
				weapon->id, ed->id, invName);
		return 0;
	}

	/* now try to pack the weapon */
	packed = tryAddToInventory(inv, &item, &csi->ids[CID_RIGHT]);
	if (packed)
		ammoMult = 3;
	if (!packed && allowLeft)
		packed = tryAddToInventory(inv, &item, &csi->ids[CID_LEFT]);
	if (!packed)
		packed = tryAddToInventory(inv, &item, &csi->ids[CID_BELT]);
	if (!packed)
		packed = tryAddToInventory(inv, &item, &csi->ids[CID_HOLSTER]);
	if (!packed)
		return 0;

	/* pack some more ammo in the backpack */
	if (ammo) {
		int num;
		int numpacked = 0;

		/* how many clips? */
		num = (1 + ed->numItems[ammo->idx])
			* (float) (1.0f + missedPrimary / 100.0);

		/* pack some ammo */
		while (num--) {
			weight = GetInventoryState(inv, tuNeed) + item.getWeight();
			maxTU = GET_TU(speed, GET_ENCUMBRANCE_PENALTY(weight, chr->score.skills[ABILITY_POWER]));

			Item mun(ammo);
			/* ammo to backpack; belt is for knives and grenades */
			if (weight <= maxWeight && tuNeed <= maxTU)
					numpacked += tryAddToInventory(inv, &mun, &csi->ids[CID_BACKPACK]);
			/* no problem if no space left; one ammo already loaded */
			if (numpacked > ammoMult || numpacked * weapon->ammo > 11)
				break;
		}
	}

	return true;
}


/**
 * @brief Equip melee actor with item defined per teamDefs.
 * @param[in] inv The inventory that will get the weapon.
 * @param[in] td Pointer to a team definition.
 * @note Weapons assigned here cannot be collected in any case. These are dummy "actor weapons".
 */
void InventoryInterface::EquipActorMelee (inventory_t* const inv, const teamDef_t *td)
{
	assert(td->onlyWeapon);

	/* Get weapon def */
	const objDef_t *obj = td->onlyWeapon;

	/* Prepare item. This kind of item has no ammo, fire definitions are in item.t. */
	Item item(obj);
	item.ammo = item.def();
	item.setAmmoLeft(NONE_AMMO);
	/* Every melee actor weapon definition is firetwohanded, add to right hand. */
	if (!obj->fireTwoHanded)
		Sys_Error("INVSH_EquipActorMelee: melee weapon %s for team %s is not firetwohanded! (%s)",
				obj->id, td->id, invName);
	tryAddToInventory(inv, &item, &this->csi->ids[CID_RIGHT]);
}

/**
 * @brief Equip robot actor with default weapon. (defined in ugv_t->weapon)
 * @param[in] inv The inventory that will get the weapon.
 * @param[in] weapon Pointer to the item which being added to robot's inventory.
 */
void InventoryInterface::EquipActorRobot (inventory_t* const inv, const objDef_t *weapon)
{
	assert(weapon);

	/* Prepare weapon in item. */
	Item item(weapon);
	item.setAmmoLeft(weapon->ammo);

	/* Get ammo for item/weapon. */
	assert(weapon->numAmmos > 0);	/* There _has_ to be at least one ammo-type. */
	assert(weapon->ammos[0]);
	item.ammo = weapon->ammos[0];

	tryAddToInventory(inv, &item, &this->csi->ids[CID_RIGHT]);
}

/**
 * @brief Types of weapon that can be selected
 */
typedef enum {
	WEAPON_PARTICLE_OR_NORMAL = 0,	/**< primary weapon is a particle or normal weapon */
	WEAPON_OTHER = 1,				/**< primary weapon is not a particle or normal weapon */
	WEAPON_NO_PRIMARY = 2			/**< no primary weapon */
} equipPrimaryWeaponType_t;

/**
 * @brief Fully equip one actor. The equipment that is added to the inventory of the given actor
 * is taken from the equipment script definition.
 * @param[in] chr The character that will get the weapon.
 * @param[in] ed The equipment that is added from to the actors inventory
 * @param[in] maxWeight The max weight this actor is allowed to carry.
 * @note The code below is a complete implementation
 * of the scheme sketched at the beginning of equipment_missions.ufo.
 * Beware: If two weapons in the same category have the same price,
 * only one will be considered for inventory.
 */
void InventoryInterface::EquipActor (character_t* const chr, const equipDef_t *ed, int maxWeight)
{
	inventory_t* const inv = &chr->inv;
	const teamDef_t *td = chr->teamDef;
	const int speed = chr->score.skills[ABILITY_SPEED];
	int i;
	const int numEquip = lengthof(ed->numItems);
	int repeat = 0;

	if (td->weapons) {
		equipPrimaryWeaponType_t primary = WEAPON_NO_PRIMARY;
		int sum;
		int missedPrimary = 0; /**< If actor has a primary weapon, this is zero. Otherwise, this is the probability * 100
								* that the actor had to get a primary weapon (used to compensate the lack of primary weapon) */
		const objDef_t *primaryWeapon = NULL;
		int hasWeapon = 0;
		/* Primary weapons */
		const int maxWeaponIdx = std::min(this->csi->numODs - 1, numEquip - 1);
		int randNumber = rand() % 100;
		for (i = 0; i < maxWeaponIdx; i++) {
			const objDef_t *obj = INVSH_GetItemByIDX(i);
			if (ed->numItems[i] && obj->weapon && obj->fireTwoHanded && obj->isPrimary) {
				randNumber -= ed->numItems[i];
				missedPrimary += ed->numItems[i];
				if (!primaryWeapon && randNumber < 0)
					primaryWeapon = obj;
			}
		}
		/* See if a weapon has been selected. */
		if (primaryWeapon) {
			hasWeapon += PackAmmoAndWeapon(chr, primaryWeapon, 0, ed, maxWeight);
			if (hasWeapon) {
				int ammo;

				/* Find the first possible ammo to check damage type. */
				for (ammo = 0; ammo < this->csi->numODs; ammo++)
					if (ed->numItems[ammo] && this->csi->ods[ammo].isLoadableInWeapon(primaryWeapon))
						break;
				if (ammo < this->csi->numODs) {
					if (/* To avoid two particle weapons. */
						!(this->csi->ods[ammo].dmgtype == this->csi->damParticle)
						/* To avoid SMG + Assault Rifle */
						&& !(this->csi->ods[ammo].dmgtype == this->csi->damNormal)) {
						primary = WEAPON_OTHER;
					} else {
						primary = WEAPON_PARTICLE_OR_NORMAL;
					}
				}
				/* reset missedPrimary: we got a primary weapon */
				missedPrimary = 0;
			} else {
				Com_DPrintf(DEBUG_SHARED, "INVSH_EquipActor: primary weapon '%s' couldn't be equipped in equipment '%s' (%s).\n",
						primaryWeapon->id, ed->id, invName);
				repeat = WEAPONLESS_BONUS > frand();
			}
		}

		/* Sidearms (secondary weapons with reload). */
		do {
			int randNumber = rand() % 100;
			const objDef_t *secondaryWeapon = NULL;
			for (i = 0; i < this->csi->numODs; i++) {
				const objDef_t *obj = INVSH_GetItemByIDX(i);
				if (ed->numItems[i] && obj->weapon && obj->reload && !obj->deplete && obj->isSecondary) {
					randNumber -= ed->numItems[i] / (primary == WEAPON_PARTICLE_OR_NORMAL ? 2 : 1);
					if (randNumber < 0) {
						secondaryWeapon = obj;
						break;
					}
				}
			}

			if (secondaryWeapon) {
				hasWeapon += PackAmmoAndWeapon(chr, secondaryWeapon, missedPrimary, ed, maxWeight);
				if (hasWeapon) {
					const float AKIMBO_CHANCE = 0.3; 	/**< if you got a one-handed secondary weapon (and no primary weapon),
															 this is the chance to get another one (between 0 and 1) */
					/* Try to get the second akimbo pistol if no primary weapon. */
					if (primary == WEAPON_NO_PRIMARY && !secondaryWeapon->fireTwoHanded && frand() < AKIMBO_CHANCE) {
						PackAmmoAndWeapon(chr, secondaryWeapon, 0, ed, maxWeight);
					}
				}
			}
		} while (!hasWeapon && repeat--);

		/* Misc items and secondary weapons without reload. */
		if (!hasWeapon)
			repeat = WEAPONLESS_BONUS > frand();
		else
			repeat = 0;
		/* Misc object probability can be bigger than 100 -- you're sure to
		 * have one misc if it fits your backpack */
		sum = 0;
		for (i = 0; i < this->csi->numODs; i++) {
			const objDef_t *obj = INVSH_GetItemByIDX(i);
			if (ed->numItems[i] && ((obj->weapon && obj->isSecondary
			 && (!obj->reload || obj->deplete)) || obj->isMisc)) {
				/* if ed->num[i] is greater than 100, the first number is the number of items you'll get:
				 * don't take it into account for probability
				 * Make sure that the probability is at least one if an item can be selected */
				sum += ed->numItems[i] ? std::max(ed->numItems[i] % 100, 1) : 0;
			}
		}
		if (sum) {
			do {
				int randNumber = rand() % sum;
				const objDef_t *secondaryWeapon = NULL;
				for (i = 0; i < this->csi->numODs; i++) {
					const objDef_t *obj = INVSH_GetItemByIDX(i);
					if (ed->numItems[i] && ((obj->weapon && obj->isSecondary
					 && (!obj->reload || obj->deplete)) || obj->isMisc)) {
						randNumber -= ed->numItems[i] ? std::max(ed->numItems[i] % 100, 1) : 0;
						if (randNumber < 0) {
							secondaryWeapon = obj;
							break;
						}
					}
				}

				if (secondaryWeapon) {
					int num = ed->numItems[secondaryWeapon->idx] / 100 + (ed->numItems[secondaryWeapon->idx] % 100 >= 100 * frand());
					while (num--) {
						hasWeapon += PackAmmoAndWeapon(chr, secondaryWeapon, 0, ed, maxWeight);
					}
				}
			} while (repeat--); /* Gives more if no serious weapons. */
		}

		/* If no weapon at all, bad guys will always find a blade to wield. */
		if (!hasWeapon) {
			int maxPrice = 0;
			const objDef_t *blade = NULL;
			Com_DPrintf(DEBUG_SHARED, "INVSH_EquipActor: no weapon picked in equipment '%s', defaulting to the most expensive secondary weapon without reload. (%s)\n",
					ed->id, invName);
			for (i = 0; i < this->csi->numODs; i++) {
				const objDef_t *obj = INVSH_GetItemByIDX(i);
				if (ed->numItems[i] && obj->weapon && obj->isSecondary && !obj->reload) {
					if (obj->price > maxPrice) {
						maxPrice = obj->price;
						blade = obj;
					}
				}
			}
			if (maxPrice)
				hasWeapon += PackAmmoAndWeapon(chr, blade, 0, ed, maxWeight);
		}
		/* If still no weapon, something is broken, or no blades in equipment. */
		if (!hasWeapon)
			Com_DPrintf(DEBUG_SHARED, "INVSH_EquipActor: cannot add any weapon; no secondary weapon without reload detected for equipment '%s' (%s).\n",
					ed->id, invName);

		/* Armour; especially for those without primary weapons. */
		repeat = (float) missedPrimary > frand() * 100.0;
	} else {
		return;
	}

	if (td->armour) {
		do {
			int randNumber = rand() % 100;
			for (i = 0; i < this->csi->numODs; i++) {
				const objDef_t *armour = INVSH_GetItemByIDX(i);
				if (ed->numItems[i] && armour->isArmour()) {
					randNumber -= ed->numItems[i];
					if (randNumber < 0) {
						const Item item(armour);
						int tuNeed = 0;
						const float weight = GetInventoryState(inv, tuNeed) + item.getWeight();
						const int maxTU = GET_TU(speed, GET_ENCUMBRANCE_PENALTY(weight, chr->score.skills[ABILITY_POWER]));
						if (weight > maxWeight || tuNeed > maxTU)
							continue;
						if (tryAddToInventory(inv, &item, &this->csi->ids[CID_ARMOUR])) {
							repeat = 0;
							break;
						}
					}
				}
			}
		} while (repeat-- > 0);
	} else {
		Com_DPrintf(DEBUG_SHARED, "INVSH_EquipActor: teamdef '%s' may not carry armour (%s)\n",
				td->name, invName);
	}

	{
		int randNumber = rand() % 10;
		for (i = 0; i < this->csi->numODs; i++) {
			if (ed->numItems[i]) {
				const objDef_t *miscItem = INVSH_GetItemByIDX(i);
				if (miscItem->isMisc && !miscItem->weapon) {
					randNumber -= ed->numItems[i];
					if (randNumber < 0) {
						const bool oneShot = miscItem->oneshot;
						const Item item(miscItem, oneShot ? miscItem : NULL, oneShot ? miscItem->ammo : NONE_AMMO);
						containerIndex_t container;
						int tuNeed;
						const fireDef_t *itemFd = item.getSlowestFireDef();
						const float weight = GetInventoryState(inv, tuNeed) + item.getWeight();
						const int maxTU = GET_TU(speed, GET_ENCUMBRANCE_PENALTY(weight, chr->score.skills[ABILITY_POWER]));

						if (miscItem->headgear)
							container = CID_HEADGEAR;
						else if (miscItem->extension)
							container = CID_EXTENSION;
						else
							container = CID_BACKPACK;
						if (weight > maxWeight || tuNeed > maxTU || (itemFd && itemFd->time > maxTU))
							continue;
						tryAddToInventory(inv, &item, &this->csi->ids[container]);
					}
				}
			}
		}
	}
}

/**
 * @brief Calculate the number of used inventory slots
 * @return The number of free inventory slots
 */
int InventoryInterface::GetUsedSlots ()
{
	int i = 0;
	const invList_t *slot = _invList;
	while (slot) {
		slot = slot->getNext();
		i++;
	}
	Com_DPrintf(DEBUG_SHARED, "Used inventory slots %i (%s)\n", i, invName);
	return i;
}

/**
 * @brief Initializes the inventory definition by linking the ->next pointers properly.
 * @param[in] name The name that is shown in the output
 * @param[in] csi The client-server-information structure
 * @param[in] import Pointers to the lifecycle functions
 * @sa G_Init
 * @sa CL_InitLocal
 */
void InventoryInterface::initInventory (const char *name, const csi_t *csi, const inventoryImport_t *import)
{
	const item_t item;

	OBJZERO(*this);

	this->import = import;
	this->invName = name;
	this->cacheItem = item;
	this->csi = csi;
	this->_invList = NULL;
}

void InventoryInterface::destroyInventoryInterface (void)
{
	if (this->import == NULL)
		return;
	this->import->FreeAll();
	OBJZERO(*this);
}
