/**
 * @file m_node_textlist.c
 */

/*
Copyright (C) 1997-2008 UFO:AI Team

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

#include "../m_main.h"
#include "../m_internal.h"
#include "../m_font.h"
#include "../m_actions.h"
#include "../m_parse.h"
#include "../m_render.h"
#include "../m_data.h"
#include "m_node_text.h"
#include "m_node_textlist.h"
#include "m_node_abstractnode.h"

#include "../../client.h"
#include "../../../shared/parse.h"

#define EXTRADATA(node) (node->u.text)

/**
 * @brief Get the line number under an absolute position
 * @param[in] node a text node
 * @param[in] x position x on the screen
 * @param[in] y position y on the screen
 * @return The line number under the position (0 = first line)
 */
static int MN_TextListNodeGetLine (const menuNode_t *node, int x, int y)
{
	int lineHeight = EXTRADATA(node).lineHeight;
	if (lineHeight == 0) {
		const char *font = MN_GetFontFromNode(node);
		lineHeight = MN_FontGetHeight(font) / 2;
	}

	MN_NodeAbsoluteToRelativePos(node, &x, &y);
	return (int) (y / lineHeight) + EXTRADATA(node).super.scrollY.viewPos;
}

static void MN_TextListNodeMouseMove (menuNode_t *node, int x, int y)
{
	EXTRADATA(node).lineUnderMouse = MN_TextListNodeGetLine(node, x, y);
}

/**
 * @brief Handles line breaks and drawing for MN_TEXT menu nodes
 * @param[in] text Text to draw
 * @param[in] node The current menu node
 * @param[in] x The fixed x position every new line starts
 * @param[in] y The fixed y position the text node starts
 */
static void MN_TextLineNodeDrawText (menuNode_t* node, const linkedList_t* list)
{
	vec4_t colorHover;
	vec4_t colorSelectedHover;
	int fullSizeY;
	int count; /* variable x position */
	const char *font = MN_GetFontFromNode(node);
	vec2_t pos;
	int currentY;
	int viewSizeY;
	int lineHeight;

	lineHeight = EXTRADATA(node).lineHeight;
	if (lineHeight == 0) {
		const char *font = MN_GetFontFromNode(node);
		lineHeight = MN_FontGetHeight(font) / 2;
	}

	if (MN_AbstractScrollableNodeIsSizeChange(node))
		viewSizeY = node->size[1] / lineHeight;
	else
		viewSizeY = EXTRADATA(node).super.scrollY.viewSize;

	/* text box */
	MN_GetNodeAbsPos(node, pos);
	pos[0] += node->padding;
	pos[1] += node->padding;

	/* prepare colors */
	VectorScale(node->color, 0.8, colorHover);
	colorHover[3] = node->color[3];
	VectorScale(node->selectedColor, 0.8, colorSelectedHover);
	colorSelectedHover[3] = node->selectedColor[3];

	/* skip lines over the scroll */
	count = 0;
	while (list && count < EXTRADATA(node).super.scrollY.viewPos) {
		list = list->next;
		count++;
	}

	currentY = pos[1];
	while (list) {

		/* highlight selected line */
		if (count == EXTRADATA(node).textLineSelected && EXTRADATA(node).textLineSelected >= 0)
			R_Color(node->selectedColor);
		else
			R_Color(node->color);

		/* highlight line under mouse */
		if (node->state && count == EXTRADATA(node).lineUnderMouse) {
			if (fullSizeY == EXTRADATA(node).textLineSelected && EXTRADATA(node).textLineSelected >= 0)
				R_Color(colorSelectedHover);
			else
				R_Color(colorHover);
		}

		MN_DrawString(font, ALIGN_UL, pos[0], currentY,
			pos[0], currentY, node->size[0] - node->padding - node->padding, node->size[1],
			0, (const char*)list->data, 0, 0, NULL, qfalse, LONGLINES_PRETTYCHOP);

		/* next entries' position */
		currentY += lineHeight;


		list = list->next;
		count++;
	}

	/* update scroll status */
	MN_AbstractScrollableNodeSetY(node, -1, viewSizeY, fullSizeY);

	R_Color(NULL);
}

/**
 * @brief Draw a text node
 */
static void MN_TextListNodeDraw (menuNode_t *node)
{
	const menuSharedData_t *shared;
	shared = &mn.sharedData[EXTRADATA(node).dataID];
	if (shared->type != MN_SHARED_LINKEDLISTTEXT) {
		Com_Printf("MN_TextListNodeDraw: Only linkedlist text supported (dataid %d).\n", EXTRADATA(node).dataID);
		MN_ResetData(EXTRADATA(node).dataID);
		return;
	}

	MN_TextLineNodeDrawText(node, shared->data.linkedListText);
}

/**
 * @brief Calls the script command for a text node that is clickable
 * @sa MN_TextNodeRightClick
 */
static void MN_TextListNodeClick (menuNode_t * node, int x, int y)
{
	const int line = MN_TextListNodeGetLine(node, x, y);

	if (line < 0 || line >= EXTRADATA(node).super.scrollY.fullSize)
		return;

	if (line != EXTRADATA(node).textLineSelected) {
		EXTRADATA(node).textLineSelected = line;
		if (node->onChange)
			MN_ExecuteEventActions(node, node->onChange);
	}

	if (node->onClick)
		MN_ExecuteEventActions(node, node->onClick);
}

/**
 * @brief Calls the script command for a text node that is clickable via right mouse button
 * @todo we should delete that function
 */
static void MN_TextListNodeRightClick (menuNode_t * node, int x, int y)
{
	const int line = MN_TextListNodeGetLine(node, x, y);

	if (line < 0 || line >= EXTRADATA(node).super.scrollY.fullSize)
		return;

	if (line != EXTRADATA(node).textLineSelected) {
		EXTRADATA(node).textLineSelected = line;
		if (node->onChange)
			MN_ExecuteEventActions(node, node->onChange);
	}

	if (node->onRightClick)
		MN_ExecuteEventActions(node, node->onRightClick);
}

/**
 */
static void MN_TextListNodeMouseWheel (menuNode_t *node, qboolean down, int x, int y)
{
	MN_AbstractScrollableNodeScrollY(node, (down ? 1 : -1));
	if (node->onWheelUp && !down)
		MN_ExecuteEventActions(node, node->onWheelUp);
	if (node->onWheelDown && down)
		MN_ExecuteEventActions(node, node->onWheelDown);
	if (node->onWheel)
		MN_ExecuteEventActions(node, node->onWheel);
}

void MN_RegisterTextListNode (nodeBehaviour_t *behaviour)
{
	behaviour->name = "textlist";
	behaviour->extends = "text";
	behaviour->draw = MN_TextListNodeDraw;
	behaviour->leftClick = MN_TextListNodeClick;
	behaviour->rightClick = MN_TextListNodeRightClick;
	behaviour->mouseWheel = MN_TextListNodeMouseWheel;
	behaviour->mouseMove = MN_TextListNodeMouseMove;
}
