/**
 * @file cl_produce.c
 * @brief Single player production stuff
 */

/*
Copyright (C) 2002-2006 UFO: Alien Invasion team.

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

#include "client.h"
#include "cl_global.h"
int RS_ItemInBase(int item_idx, base_t *base);	/* Only used for PR_RequirementsMet.
						 * I hope this works correctly.
						 * This function needs to be fixed BTW. See the TODO there.*/


/* holds the current active production category */
static int produceCategory = 0;

/* holds the current active selected queue index/objID */
static qboolean selectedQueueItem 	= qfalse;
static int selectedIndex 			= -1;

/**
 * @TODO: Now that we have the Workshop in basemanagement.ufo
 * we need to check whether the player has already set this building up
 * otherwise he won't be allowed to produce more equipment stuff
 */

/* 20060921 LordHavoc: added PRODUCE_DIVISOR to allow reducing prices below 1x */
static const int PRODUCE_FACTOR = 1;
static const int PRODUCE_DIVISOR = 2;

static cvar_t* mn_production_limit;
static cvar_t* mn_production_workers;

/** @brief number of blank lines between queued items and tech list */
#define QUEUE_SPACERS 2

/**
 * @brief add a new item to the bottom of the production queue
 */
static production_t *PR_QueueNew(production_queue_t *queue, signed int objID, signed int amount)
{
	technology_t *t;
	objDef_t *od;
	int numWorkshops = 0;
	production_t *prod;

	assert(baseCurrent);

	numWorkshops = B_GetNumberOfBuildingsInBaseByType(baseCurrent->idx, B_WORKSHOP);

	if (queue->numItems >= numWorkshops * MAX_PRODUCTIONS_PER_WORKSHOP) {
		Com_Printf("PR_QueueNew: Max number of productions reached in %i workshops - build more workshops\n", numWorkshops);
		return NULL;
	}

	/* initialize */
	prod = &queue->items[queue->numItems];
	od = &csi.ods[objID];
	t = (technology_t*)(od->tech);

	prod->objID = objID;
	prod->amount = amount;
	prod->timeLeft = t->produceTime;

	queue->numItems++;
	return prod;
}

/**
 * @brief Delete the selected entry from the queue.
 */
static void PR_QueueDelete(production_queue_t *queue, int index)
{
	int i;

	/* If prod->items_cached is qtrue we need to ...
	 *  -) add all items listed in the prod.-requirements /multiplied by amount) to the storage again.
	 *  -) set prod->items_cached to qfalse
	*/

	queue->numItems--;
	if (queue->numItems < 0)
		queue->numItems = 0;
	
	/* Copy up other items. */
	for (i = index; i < queue->numItems; i++) {
		queue->items[i] = queue->items[i+1];
	}
}

/**
 * @brief move the given queue item in the given direction
 */
static void PR_QueueMove(production_queue_t *queue, int index, int dir)
{
	int newIndex = index + dir;
	int i;
	production_t saved;

	if (newIndex == index)
		return;

	saved = queue->items[index];

	/* copy up */
	for (i = index; i < newIndex; i++) {
		queue->items[i] = queue->items[i+1];
	}

	/* copy down */
	for (i = index; i > newIndex; i--) {
		queue->items[i] = queue->items[i-1];
	}

	/* insert item */
	queue->items[newIndex] = saved;
}

/**
 * @brief queue the next production in the queue
 * @param base the index of the base
 */
static void PR_QueueNext(int base)
{
	char messageBuffer[256];
	production_queue_t *queue = &gd.productions[base];

	PR_QueueDelete(queue, 0);
	if (queue->numItems == 0) {
		selectedQueueItem = qfalse;
		selectedIndex = -1;
		Com_sprintf(messageBuffer, sizeof(messageBuffer), _("Production queue for base %s is empty"), gd.bases[base].name);
		MN_AddNewMessage(_("Production queue empty"), messageBuffer, qfalse, MSG_PRODUCTION, NULL);
		CL_GameTimeStop();
		return;
	} else if (selectedIndex >= queue->numItems) {
		selectedIndex = queue->numItems - 1;
	}
}

/**
 * @brief Checks whether an item is finished
 * @sa CL_CampaignRun
 */
void PR_ProductionRun(void)
{
	int i;
	objDef_t *od;
	technology_t *t;
	production_t *prod;

	for (i = 0; i < MAX_BASES; i++) {
		if (!gd.bases[i].founded)
			continue;

		/* not actually any active productions */
		if (gd.productions[i].numItems <= 0)
			continue;

		prod = &gd.productions[i].items[0];
		assert(prod->objID >= 0);
		od = &csi.ods[prod->objID];

		/* not enough money to produce more items in this base */
		if (od->price*PRODUCE_FACTOR/PRODUCE_DIVISOR > ccs.credits)
			continue;

		t = (technology_t*)(od->tech);
#ifdef DEBUG
		if (!t)
			Sys_Error("PR_ProductionRun: No tech pointer for object id %i ('%s')\n", prod->objID, od->kurz);
#endif
		prod->timeLeft--;
		if (prod->timeLeft <= 0) {
			CL_UpdateCredits(ccs.credits - (od->price*PRODUCE_FACTOR/PRODUCE_DIVISOR));
			prod->timeLeft = t->produceTime;
			prod->amount--;
			/* now add it to equipment */
			/* FIXME: overflow possible */
			gd.bases[i].storage.num[prod->objID]++;

			/* queue the next production */
			if (prod->amount<=0) {
				PR_QueueNext(i);
			}
		}
	}
}

/**
 * @brief Prints information about the selected item in production
 * @note -1 for objID means that there ic no production in this base
 */
static void PR_ProductionInfo (void)
{
	static char productionInfo[512];
	technology_t *t;
	objDef_t *od;
	int objID;

	assert(baseCurrent);

	if (selectedQueueItem) {
		objID = gd.productions[baseCurrent->idx].items[selectedIndex].objID;
	} else {
		objID = selectedIndex;
	}

	if (objID >= 0) {
		od = &csi.ods[objID];
		t = (technology_t*)(od->tech);
		Com_sprintf(productionInfo, sizeof(productionInfo), "%s\n", od->name);
		Q_strcat(productionInfo, va(_("Costs per item\t%i c\n"), (od->price*PRODUCE_FACTOR/PRODUCE_DIVISOR)),
				sizeof(productionInfo) );
		Q_strcat(productionInfo, va(_("Productiontime\t%ih\n"), (t->produceTime)),
				sizeof(productionInfo) );
		CL_ItemDescription(objID);
	} else {
		Com_sprintf(productionInfo, sizeof(productionInfo), _("No item selected"));
		Cvar_Set("mn_item", "");
	}
	menuText[TEXT_PRODUCTION_INFO] = productionInfo;
}

/**
 * @brief Click function for production list
 * @note Opens the ufopedia - by right clicking an item
 */
static void PR_ProductionListRightClick_f (void)
{
	int i, j, num, idx;
	technology_t *t = NULL;
	objDef_t *od = NULL;
	production_queue_t *queue = NULL;

	/* can be called from everywhere without a started game */
	if (!baseCurrent ||!curCampaign)
		return;
	queue = &gd.productions[baseCurrent->idx];

	/* not enough parameters */
	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <arg>\n", Cmd_Argv(0));
		return;
	}

	/* clicked which item? */
	num = atoi(Cmd_Argv(1));

	/* Clicked the production queue or the item list? */
	if (num < queue->numItems) {
		od = &csi.ods[queue->items[num].objID];
		t = (technology_t*)(od->tech);
		UP_OpenWith(t->id);
	}else if (num >= queue->numItems + QUEUE_SPACERS) {
		/* Clicked in the item list. */
		idx = num - queue->numItems - QUEUE_SPACERS;
		for (j=0, i = 0, od = csi.ods; i < csi.numODs; i++, od++) {
			t = (technology_t*)(od->tech);
#ifdef DEBUG
			if (!t)
				Sys_Error("PR_ProductionListRightClick_f: No tech pointer for object id %i ('%s')\n", i, od->kurz);
#endif
			/* Open up ufopedia for this entry. */
			if (od->buytype == produceCategory && RS_IsResearched_ptr(t)) {
				if (j==idx) {
					UP_OpenWith(t->id);
					return;
				}
				j++;
			}
		}
	}
#ifdef DEBUG
	else
		Com_DPrintf("PR_ProductionListRightClick_f: Click on spacer %i\n", num);
#endif
}

/**
 * @brief Click function for production list.
 */
static void PR_ProductionListClick_f (void)
{
	int i, j, num, idx;
	objDef_t *od = NULL;
	technology_t *t = NULL;
	production_queue_t *queue = NULL;
	production_t *prod = NULL;

	/* can be called from everywhere without a started game */
	if (!baseCurrent ||!curCampaign)
		return;
	queue = &gd.productions[baseCurrent->idx];

#if 0 /* FIXME: not a concern any more ... */
	/* there is already a running production - stop it first */
	if (gd.productions[baseCurrent->idx].amount > 0 ) {
		PR_ProductionInfo();
		return;
	}
#endif

	/* not enough parameters */
	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <arg>\n", Cmd_Argv(0));
		return;
	}

	/* clicked which item? */
	num = atoi(Cmd_Argv(1));

	/* Clicked the production queue or the item list? */
	if (num < queue->numItems) {
		prod = &queue->items[num];
		selectedQueueItem = qtrue;
		selectedIndex = num;
		PR_ProductionInfo();
	} else if (num >= queue->numItems + QUEUE_SPACERS) {
		/* Clicked in the item list. */
		idx = num - queue->numItems - QUEUE_SPACERS;
		for (j = 0, i = 0, od = csi.ods; i < csi.numODs; i++, od++) {
			t = (technology_t*)(od->tech);
#ifdef DEBUG
			if (!t)
				Sys_Error("PR_ProductionListClick_f: No tech pointer for object id %i ('%s')\n", i, od->kurz);
#endif
			/* We can only produce items that fulfill the following conditions... */
			if (	od->buytype == produceCategory	/* Item is int he current inventory-category */
				&& RS_IsResearched_ptr(t)		/* Tech is researched */
				&& *od->name				/* Item has a name defined. TODO: produce error if this is not true? */
				&& t->produceTime >= 0		/* Item is produceable */
			) {
				if (j==idx) {
#if 0
					/* FIXME: don't start production yet .. */
					gd.productions[baseCurrent->idx].objID = i;
					gd.productions[baseCurrent->idx].amount = 0;
					gd.productions[baseCurrent->idx].timeLeft = t->produceTime;
#endif
					selectedQueueItem = qfalse;
					selectedIndex = i;
					PR_ProductionInfo();
					return;
				}
				j++;
			}
		}
	}
#ifdef DEBUG
	else
		Com_DPrintf("PR_ProductionListClick_f: Click on spacer %i\n", num);
#endif
}

/** @brief update the list of queued and available items */
static void PR_UpdateProductionList(void)
{
	int i;
	static char productionList[1024];
	static char productionQueued[256];
	static char productionAmount[256];
	objDef_t *od;
	production_queue_t *queue;
	production_t *prod;

	productionAmount[0] = productionList[0] = productionQueued[0] = '\0';
	queue = &gd.productions[baseCurrent->idx];

	/* first add all the queue items */
	for (i = 0; i < queue->numItems; i++) {
		prod = &queue->items[i];
		od = &csi.ods[prod->objID];

		Q_strcat(productionList, va("%s\n", od->name), sizeof(productionList));
		Q_strcat(productionAmount, va("%i\n", baseCurrent->storage.num[prod->objID]), sizeof(productionAmount));
		Q_strcat(productionQueued, va("%i\n", prod->amount), sizeof(productionQueued));
	}

	/* then spacers */
	for (i = 0; i < QUEUE_SPACERS; i++) {
		Q_strcat(productionList, "\n", sizeof(productionList));
		Q_strcat(productionAmount, "\n", sizeof(productionAmount));
		Q_strcat(productionQueued, "\n", sizeof(productionQueued));
	}

	/* then go through all object definitions */
	for (i = 0, od = csi.ods; i < csi.numODs; i++, od++) {
		/* we can produce what was researched before */
		if (od->buytype == produceCategory && RS_IsResearched_ptr(od->tech) && *od->name) {
			Q_strcat(productionList, va("%s\n", od->name), sizeof(productionList));
			Q_strcat(productionAmount, va("%i\n", baseCurrent->storage.num[i]), sizeof(productionAmount));
			Q_strcat(productionQueued, "\n", sizeof(productionQueued));
		}
	}
	/* bind the menu text to our static char array */
	menuText[TEXT_PRODUCTION_LIST] = productionList;
	/* bind the amount of available items */
	menuText[TEXT_PRODUCTION_AMOUNT] = productionAmount;
	/* bind the amount of queued items */
	menuText[TEXT_PRODUCTION_QUEUED] = productionQueued;

#if 0 /* FIXME: needed now? */
	/* now print the information about the current item in production */
	PR_ProductionInfo();
#endif
}

/**
 * @brief will select a new tab on the produciton list
 */
static void PR_ProductionSelect(void)
{
	/* can be called from everywhere without a started game */
	if (!baseCurrent ||!curCampaign)
		return;

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: prod_init <category>\n");
		return;
	}
	produceCategory = atoi(Cmd_Argv(1));
	PR_UpdateProductionList();
}

/**
 * @brief Will fill the list of produceable items
 */
static void PR_ProductionList (void)
{
	/* can be called from everywhere without a started game */
	if (!baseCurrent ||!curCampaign)
		return;

	PR_ProductionSelect();
	PR_ProductionInfo();
	Cvar_SetValue("mn_production_limit", MAX_PRODUCTIONS_PER_WORKSHOP * B_GetNumberOfBuildingsInBaseByType(baseCurrent->idx, B_WORKSHOP));
	Cvar_SetValue("mn_production_workers", E_CountHired(baseCurrent, EMPL_WORKER));
}

/**
 * @brief Returns true if the current base is able to produce items
 */
qboolean PR_ProductionAllowed(void)
{
	if (baseCurrent->baseStatus != BASE_UNDER_ATTACK &&
		B_GetNumberOfBuildingsInBaseByType(baseCurrent->idx, B_WORKSHOP) > 0 &&
		E_CountHired(baseCurrent, EMPL_WORKER) > 0 ) {
		Cbuf_AddText("set_prod_enabled;");
		return qtrue;
	} else {
		Cbuf_AddText("set_prod_disabled;");
		return qfalse;
	}
}

/**
 * @brief Function binding for prod_scroll that scrolls other text nodes
 * simultan with the
 */
static void PR_ProductionListScroll_f (void)
{
	menuNode_t *node1, *node2, *prodlist;

	prodlist = MN_GetNodeFromCurrentMenu("prodlist");
	node1 = MN_GetNodeFromCurrentMenu("prodlist_amount");
	node2 = MN_GetNodeFromCurrentMenu("prodlist_queued");

	if (!prodlist || !node1 || !node2)
		return;

	node1->textScroll = node2->textScroll = prodlist->textScroll;
}

/**
 * @brief Sets the production array to 0
 */
void PR_ProductionInit(void)
{
	Com_DPrintf("Reset all productions\n");
	memset(gd.productions, 0, sizeof(production_queue_t)*MAX_BASES);
	mn_production_limit = Cvar_Get("mn_production_limit", "0", 0, NULL);
	mn_production_workers = Cvar_Get("mn_production_workers", "0", 0, NULL);
}

/**
 * @brief Increase the production amount by given parameter
 */
void PR_ProductionIncrease(void)
{
	int amount = 1;
	char messageBuffer[256];
	production_queue_t *queue;
	production_t *prod;

	if (Cmd_Argc() == 2)
		amount = atoi(Cmd_Argv(1));

	if (!baseCurrent)
		return;
	queue = &gd.productions[baseCurrent->idx];

	if (selectedQueueItem) {
		assert(selectedIndex >= 0 && selectedIndex < queue->numItems);
		prod = &queue->items[selectedIndex];
		prod->amount += amount;
	} else {
		if (selectedIndex < 0)
			return;
		/* TODO:
		 * Check if production requirements have been met. "produceable_amount = PR_RequirementsMet(amount, req);"
		 * If that's the case (produceable_amount > 0) we
		 *  -) remove the additionally required items (multiplied by 'produceable_amount' .. if I understand the code corretly) from storage.
		 *  -) if produceable_amount < amount we tell the player that not all items could be added.
		 *  -) and set prod->items_cached to qtrue.
		 * If the requirements are not met (produceable_amount<=0) we
		 *  -) need to popup something like: "You need the following items in order to produce more of ITEM:   x of ITEM, x of ITEM, etc..."
		 *     This info should also be displayed in the item-info.
		 *  -) can can (if possible) thange the 'amount' to a vlalue that _can_ be produced (i.e. the maximum amount possible).
		*/
		prod = PR_QueueNew(queue, selectedIndex, amount);
		if (prod) {
			Com_sprintf(messageBuffer, sizeof(messageBuffer), _("Production of %s started"), csi.ods[selectedIndex].name);
			MN_AddNewMessage(_("Production started"), messageBuffer, qfalse, MSG_PRODUCTION, csi.ods[selectedIndex].tech);
			
			/* now we select the item we just created */
			selectedQueueItem = qtrue;
			selectedIndex = queue->numItems-1;
		} else {
			/* oops! too many items! */
			MN_Popup(_("Queue full!"), _("You cannot queue any more items!"));
		}
	}

	PR_ProductionInfo();
	PR_UpdateProductionList();
}

/**
 * @brief Stops the current running production
 */
void PR_ProductionStop(void)
{
	production_queue_t *queue;

	if (!baseCurrent || !selectedQueueItem)
		return;

	queue = &gd.productions[baseCurrent->idx];
	PR_QueueDelete(queue, selectedIndex);

	if (queue->numItems == 0) {
		selectedQueueItem = qfalse;
		selectedIndex = -1;
	} else if (selectedIndex >= queue->numItems) {
		selectedIndex = queue->numItems - 1;
	}

	PR_ProductionInfo();
	PR_UpdateProductionList();
}

/**
 * @brief Decrease the production amount by given parameter
 */
void PR_ProductionDecrease(void)
{
	int amount = 1;
	production_queue_t *queue;
	production_t *prod;

	if (Cmd_Argc() == 2)
		amount = atoi(Cmd_Argv(1));

	if (!baseCurrent || !selectedQueueItem)
		return;

	queue = &gd.productions[baseCurrent->idx];
	assert(selectedIndex >= 0 && selectedIndex < queue->numItems);
	prod = &queue->items[selectedIndex];
	prod->amount -= amount;

	if (prod->amount <= 0) {
		PR_ProductionStop();
	} else {
		PR_ProductionInfo();
		PR_UpdateProductionList();
 	}
}

/**
 * @brief shift the current production up the list
 */
static void PR_ProductionUp(void)
{
	production_queue_t *queue;

	if (!baseCurrent || !selectedQueueItem)
		return;

	if (selectedIndex == 0)
		return;

	queue = &gd.productions[baseCurrent->idx];
	PR_QueueMove(queue, selectedIndex, -1);

	selectedIndex--;
	PR_UpdateProductionList();
}

/**
 * @brief shift the current production down the list
 */
static void PR_ProductionDown(void)
{
	production_queue_t *queue;

	if (!baseCurrent || !selectedQueueItem)
		return;

	queue = &gd.productions[baseCurrent->idx];

	if (selectedIndex >= queue->numItems-1)
		return;

	PR_QueueMove(queue, selectedIndex, 1);

	selectedIndex++;
	PR_UpdateProductionList();
}

/**
 * @brief This is more or less the initial
 * Bind some of the functions in this file to console-commands that you can call ingame.
 * Called from MN_ResetMenus resp. CL_InitLocal
 */
void PR_ResetProduction(void)
{
	/* add commands */
	Cmd_AddCommand("prod_init", PR_ProductionList, NULL);
	Cmd_AddCommand("prod_scroll", PR_ProductionListScroll_f, "Scrolls the production lists");
	Cmd_AddCommand("prod_select", PR_ProductionSelect, NULL);
	Cmd_AddCommand("prodlist_rclick", PR_ProductionListRightClick_f, NULL);
	Cmd_AddCommand("prodlist_click", PR_ProductionListClick_f, NULL);
	Cmd_AddCommand("prod_inc", PR_ProductionIncrease, NULL);
	Cmd_AddCommand("prod_dec", PR_ProductionDecrease, NULL);
	Cmd_AddCommand("prod_stop", PR_ProductionStop, NULL);
	Cmd_AddCommand("prod_up", PR_ProductionUp, NULL);
	Cmd_AddCommand("prod_down", PR_ProductionDown, NULL);
}

/**
 * @brief Check if the production requirements are met for a defined amount.
 * @param[in] amount How many items are planned to be produced. 
 * @param[in] req The production requirements of the item that is to be produced.
 * @return 0: If nothing can be produced. 1+: If anything can be produced. 'amount': Maximum.
 */
static int PR_RequirementsMet(int amount, requirements_t *req)
{
	int a,i;
	int produceable_amount;
	qboolean produceable = qfalse;
	
	for (a = 0; a < amount; a++) {
		produceable = qtrue;
		for (i = 0; i < req->numLinks; i++) {
			if (req->type[i] == RS_LINK_ITEM) {
				/* The same code is used in "RS_RequirementsMet" */
				Com_DPrintf("PR_RequirementsMet: %s / %i\n", req->id[i], req->idx[i]);
				if (RS_ItemInBase(req->idx[i], baseCurrent) < req->amount[i]) {
					produceable = qfalse;
				}
			}
		}
		if (produceable)
			produceable_amount++;
		else
			break;
	}
	return produceable_amount;
}

/**
 * @brief Remove or add the requried items from/to the current base.
 * @param[in] amount How many items are planned to be added (positive number) or removed (negative number).
 * @param[in] req The production requirements of the item that is to be produced. Thes included numbers are multiplied with 'amount')
 */
static void PR_UpdateRequiredItemsInBasestorage(int amount, requirements_t *req)
{
	int i;
	
	if (amount == 0)
		return;

	for (i = 0; i < req->numLinks; i++) {
		if (req->type[i] == RS_LINK_ITEM) {
			if (amount > 0) {
				/* TODO: Add items (req->amount * amount) to the base. */
			} else { /* amount < 0 */
				/* TODO: Remove items (req->amount * -amount) from the base. */
			}
		}
	}
}
