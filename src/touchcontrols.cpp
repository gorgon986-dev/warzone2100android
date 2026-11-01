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
 *  On-screen touch buttons for touchscreen devices (Android).
 *
 *  A small column of buttons on the right edge of the battlefield:
 *   - Box:    the next one-finger drag draws a selection box (instead of moving the camera)
 *   - Queue:  toggle; acts like holding Shift (queue orders, production and structures)
 *   - Army:   select all combat units
 *   - Screen: select all units on screen
 */

#include "touchcontrols.h"

#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/textdraw.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/widget/widget.h"
#include "lib/widget/form.h"
#include "lib/widget/button.h"

#include "hci.h"
#include "intdisplay.h"
#include "main.h"
#include "ingameop.h"
#include "multiplay.h"
#include "keybind.h"
#include "selection.h"
#include "warzoneconfig.h"

#include <functional>

static const UDWORD IDTOUCH_FORM = 0x7A1000;

static bool touchCanPanCamera()
{
	if (GetGameMode() != GS_NORMAL)
	{
		return false;
	}
	if (InGameOpUp || bDisplayMultiJoiningStatus)
	{
		return false;
	}
	// over buttons, panels, the minimap, or an overlay (menus, dialogs): behave like a mouse drag
	auto mouseOverScreen = widgGetMouseOverScreen();
	if (mouseOverScreen && mouseOverScreen != psWScreen)
	{
		return false;
	}
	if (isMouseOverScreenOverlayChild(mouseX(), mouseY()))
	{
		return false;
	}
	if (psWScreen && isMouseOverSomeWidget(psWScreen))
	{
		return false;
	}
	return true;
}

bool touchControlsSupported()
{
#if defined(__ANDROID__)
	return true;
#else
	return false;
#endif
}

void touchControlsInit()
{
	if (!touchControlsSupported())
	{
		return;
	}
	inputTouchSetPanPredicate(touchCanPanCamera);
}

namespace
{
class TouchBarButton : public W_BUTTON
{
public:
	std::function<bool()> isOn;

	void display(int xOffset, int yOffset) override
	{
		const int x0 = x() + xOffset;
		const int y0 = y() + yOffset;
		const int x1 = x0 + width();
		const int y1 = y0 + height();
		const bool on = isOn && isOn();
		const bool pressed = (state & WBUT_DOWN) != 0;

		PIELIGHT fill = WZCOL_FORM_BACKGROUND;
		fill.byte.a = 200;
		if (on)
		{
			fill = WZCOL_FORM_HILITE;
		}
		if (pressed)
		{
			fill = WZCOL_FORM_DARK;
		}
		pie_UniTransBoxFill(static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x1), static_cast<float>(y1), fill);
		iV_Box(x0, y0, x1 - 1, y1 - 1, on ? WZCOL_WHITE : WZCOL_FORM_LIGHT);

		if (!pText.isEmpty())
		{
			const int fw = iV_GetTextWidth(pText, FontID);
			const int fx = x0 + (width() - fw) / 2;
			const int fy = y0 + (height() - iV_GetTextLineSize(FontID)) / 2 - iV_GetTextAboveBase(FontID);
			iV_SetTextColour(on ? WZCOL_WHITE : WZCOL_FORM_TEXT);
			iV_DrawText(pText.toUtf8().c_str(), fx, fy, FontID);
		}
	}
};

struct TouchButtonDef
{
	const char *label;
	std::function<void()> onClick;
	std::function<bool()> isOn;
};
}

void touchControlsAddToGameScreen()
{
	if (!touchControlsSupported() || !psWScreen)
	{
		return;
	}
	touchControlsInit();

	if (auto existing = widgGetFromID(psWScreen, IDTOUCH_FORM))
	{
		existing->callCalcLayout();
		return;
	}

	const std::vector<TouchButtonDef> buttons = {
		{ N_("Box"), []() { inputTouchSetBoxSelectArmed(!inputTouchBoxSelectArmed()); }, []() { return inputTouchBoxSelectArmed(); } },
		{ N_("Queue"), []() { inputSetVirtualQueueModifier(!inputVirtualQueueModifier()); }, []() { return inputVirtualQueueModifier(); } },
		{ N_("Army"), []() { kf_SelectUnits(DST_ALL_COMBAT)(); }, nullptr },
		{ N_("Screen"), []() { kf_SelectUnits(DST_UNUSED, DS_ALL_UNITS, true)(); }, nullptr },
	};

	auto form = std::make_shared<IntFormTransparent>();
	form->id = IDTOUCH_FORM;
	psWScreen->psForm->attach(form);

	std::vector<std::weak_ptr<TouchBarButton>> created;
	for (const auto &def : buttons)
	{
		auto button = std::make_shared<TouchBarButton>();
		form->attach(button);
		button->setString(WzString::fromUtf8(gettext(def.label)));
		button->FontID = font_regular_bold;
		button->isOn = def.isOn;
		auto onClick = def.onClick;
		button->addOnClickHandler([onClick](W_BUTTON&) {
			widgScheduleTask([onClick]() { onClick(); });
		});
		created.push_back(button);
	}

	form->setCalcLayout([created](WIDGET *psWidget) {
		const int scale = static_cast<int>(war_getTouchButtonScale());
		const int bw = 64 * scale / 100;
		const int bh = 40 * scale / 100;
		const int gap = 6 * scale / 100;
		const int count = static_cast<int>(created.size());
		const int totalH = count * bh + (count - 1) * gap;
		const int marginRight = 8;
		const int x0 = static_cast<int>(screenWidth) - bw - marginRight;
		const int y0 = std::max(60, (static_cast<int>(screenHeight) - totalH) / 2);
		psWidget->setGeometry(x0, y0, bw, totalH);
		int y = 0;
		for (auto &weakButton : created)
		{
			if (auto b = weakButton.lock())
			{
				b->setGeometry(0, y, bw, bh);
			}
			y += bh + gap;
		}
	});
}
