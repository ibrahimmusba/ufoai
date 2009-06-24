/**
 * @file cp_rank.c
 */

/*
Copyright (C) 2002-2007 UFO: Alien Invasion team.

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

#include "../client.h"
#include "../../shared/parse.h"
#include "cp_rank.h"
#include "cp_campaign.h"

/**
 * @brief Get the index number of the given rankID
 */
int CL_GetRankIdx (const char* rankID)
{
	int i;

	for (i = 0; i < ccs.numRanks; i++) {
		if (!strcmp(ccs.ranks[i].id, rankID))
			return i;
	}
	Com_Printf("Could not find rank '%s'\n", rankID);
	return -1;
}

static const value_t rankValues[] = {
	{"name", V_TRANSLATION_STRING, offsetof(rank_t, name), 0},
	{"shortname", V_TRANSLATION_STRING,	offsetof(rank_t, shortname), 0},
	{"image", V_CLIENT_HUNK_STRING, offsetof(rank_t, image), 0},
	{"mind", V_INT, offsetof(rank_t, mind), MEMBER_SIZEOF(rank_t, mind)},
	{"killed_enemies", V_INT, offsetof(rank_t, killedEnemies), MEMBER_SIZEOF(rank_t, killedEnemies)},
	{"killed_others", V_INT, offsetof(rank_t, killedOthers), MEMBER_SIZEOF(rank_t, killedOthers)},
	{"factor", V_FLOAT, offsetof(rank_t, factor), MEMBER_SIZEOF(rank_t, factor)},
	{NULL, 0, 0, 0}
};

/**
 * @brief Parse medals and ranks defined in the medals.ufo file.
 * @sa CL_ParseScriptFirst
 */
void CL_ParseRanks (const char *name, const char **text)
{
	rank_t *rank;
	const char *errhead = "CL_ParseRanks: unexpected end of file (medal/rank ";
	const char *token;
	const value_t *v;
	int i;

	/* get name list body body */
	token = Com_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("CL_ParseRanks: rank/medal \"%s\" without body ignored\n", name);
		return;
	}

	for (i = 0; i < ccs.numRanks; i++) {
		if (!strcmp(name, ccs.ranks[i].name)) {
			Com_Printf("CL_ParseRanks: Rank with same name '%s' already loaded.\n", name);
			return;
		}
	}
	/* parse ranks */
	if (ccs.numRanks >= MAX_RANKS) {
		Com_Printf("CL_ParseRanks: Too many rank descriptions, '%s' ignored.\n", name);
		ccs.numRanks = MAX_RANKS;
		return;
	}

	rank = &ccs.ranks[ccs.numRanks++];
	memset(rank, 0, sizeof(*rank));
	rank->id = Mem_PoolStrDup(name, cp_campaignPool, 0);

	do {
		/* get the name type */
		token = Com_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;
		for (v = rankValues; v->string; v++)
			if (!strcmp(token, v->string)) {
				/* found a definition */
				token = Com_EParse(text, errhead, name);
				if (!*text)
					return;
				switch (v->type) {
				case V_CLIENT_HUNK_STRING:
					Mem_PoolStrDupTo(token, (char**) ((char*)rank + (int)v->ofs), cp_campaignPool, 0);
					break;
				default:
					Com_EParseValue(rank, token, v->type, v->ofs, v->size);
					break;
				}
				break;
			}

		if (!strcmp(token, "type")) {
			/* employeeType_t */
			token = Com_EParse(text, errhead, name);
			if (!*text)
				return;
			/* error check is performed in E_GetEmployeeType function */
			rank->type = E_GetEmployeeType(token);
		} else if (!v->string)
			Com_Printf("CL_ParseRanks: unknown token \"%s\" ignored (medal/rank %s)\n", token, name);
	} while (*text);
}

