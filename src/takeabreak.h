/*
	This file is part of Warzone 2100.
	Copyright (C) 2026  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/
/** \file
 *  "Take a Break": a skirmish AI temporarily plays for the local player
 *  (building, research, unit design, combat) until they take control back.
 */

#pragma once

/// Whether Take a Break can be used in the current game (skirmish / multiplayer skirmish, not a replay or spectating)
bool takeABreakAvailable();
/// Whether an AI is currently playing for the local player
bool takeABreakActive();
/// Hands control to the user's default skirmish AI
bool takeABreakStart();
/// Takes control back from the AI
void takeABreakStop();
/// Adds / removes the "Take back control" banner to match the current state
void takeABreakRefreshBanner();
