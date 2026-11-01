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
 *  Touch controls guide: a short paged overlay explaining the touch controls,
 *  Take a Break and AI commands. Shown once on first launch (on touch devices),
 *  and available from Options > Controls.
 */

#pragma once

void showTouchGuideScreen();
void closeTouchGuideScreen();
bool isTouchGuideScreenUp();
/// Call every frame: shows the guide once, the first time the title screen is up on a touch device
void touchGuideMaybeAutoShow();
