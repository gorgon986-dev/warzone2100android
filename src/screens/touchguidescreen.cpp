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
 *  Touch controls guide (see touchguidescreen.h)
 */

#include "touchguidescreen.h"

#include "lib/framework/frame.h"
#include "lib/framework/wzapp.h"
#include "lib/ivis_opengl/screen.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/textdraw.h"
#include "lib/widget/widget.h"
#include "lib/widget/form.h"
#include "lib/widget/label.h"
#include "lib/widget/button.h"

#include "../intdisplay.h"
#include "../intimage.h"
#include "../main.h"
#include "../loop.h"
#include "../warzoneconfig.h"

#include <limits>
#include <vector>

namespace
{
struct GuidePage
{
	const char *title;
	const char *body;
};

// N_() marks strings for translation; they are translated when shown
const std::vector<GuidePage> &guidePages()
{
	static const std::vector<GuidePage> pages = {
		{ N_("Touch Controls"),
		  N_("Welcome to Warzone 2100 on touch!\n\n"
		     "This short guide explains how to play with your fingers instead of a mouse and keyboard.\n\n"
		     "You can open it again at any time from Options > Controls > Touch Controls Guide.") },
		{ N_("Moving the Camera"),
		  N_("Drag one finger across the battlefield to move the camera.\n\n"
		     "Pinch with two fingers to zoom in and out, or drag with two fingers to move the camera.\n\n"
		     "Tap or drag on the minimap to jump the camera to that spot.") },
		{ N_("Selecting Units"),
		  N_("Tap a unit or structure to select it.\n\n"
		     "Double-tap a unit to select all units of that type on screen.\n\n"
		     "To draw a selection box: press and hold for a moment, then drag. "
		     "Or tap the Box button first - your next drag draws a box instead of moving the camera.") },
		{ N_("Giving Orders"),
		  N_("With units selected, tap the ground to move there, or tap an enemy to attack it.\n\n"
		     "Press and hold, then lift (without dragging) to right-click: deselect, cancel, or open unit orders. "
		     "A quick tap with two fingers also right-clicks.\n\n"
		     "To build, select a truck, pick a structure from the build menu, then tap where it should go.") },
		{ N_("Touch Buttons"),
		  N_("The buttons on the right edge of the battlefield:\n\n"
		     "Box - your next drag draws a selection box.\n"
		     "Queue - while lit, orders and production are queued (like holding Shift or Ctrl on a PC).\n"
		     "Army - selects all your combat units.\n"
		     "Screen - selects every unit on screen.\n\n"
		     "Change their size in Options > Interface > Touch Button Size.") },
		{ N_("Menus"),
		  N_("The button in the top-right corner (or your phone's Back gesture) opens the game menu and pauses the game.\n\n"
		     "In menus, drag with one finger to scroll lists and move sliders.") },
		{ N_("Take a Break"),
		  N_("Need a break? Open the game menu and choose Take a Break: an AI plays for you - building, researching, designing units and fighting.\n\n"
		     "Tap the banner at the top of the screen to take back control.\n\n"
		     "Choose which AI plays for you, and how well it plays, in Options > Interface > Take a Break.") },
		{ N_("Commanding the AI"),
		  N_("While the AI plays for you, use the buttons under the banner to tell it what to focus on:\n\n"
		     "Auto - (lights up) the AI decides on its own what to focus on.\n"
		     "Fortify - build the strongest defenses toward the enemy.\n"
		     "Research - start upgrades in idle research facilities.\n"
		     "Units - produce tanks with your best weapons.\n"
		     "Oil - grab free oil resources.\n"
		     "Attack / Retreat - send your army in, or bring it home.\n\n"
		     "The AI keeps playing in between, so it may re-task units later.") },
	};
	return pages;
}

class GuideButton : public W_BUTTON
{
public:
	void display(int xOffset, int yOffset) override
	{
		const int x0 = x() + xOffset;
		const int y0 = y() + yOffset;
		const bool hover = isMouseOverWidget() || (state & WBUT_DOWN) != 0;
		RenderWindowFrame(FRAME_NORMAL, x0, y0, width(), height());
		if (hover)
		{
			PIELIGHT fill = WZCOL_FORM_HILITE;
			fill.byte.a = 160;
			pie_UniTransBoxFill(static_cast<float>(x0 + 3), static_cast<float>(y0 + 3), static_cast<float>(x0 + width() - 3), static_cast<float>(y0 + height() - 3), fill);
		}
		if (lastText != pText)
		{
			wzText.setText(pText, font_regular_bold);
			lastText = pText;
		}
		const int fx = x0 + (width() - wzText.width()) / 2;
		const int fy = y0 + (height() - wzText.lineSize()) / 2 - wzText.aboveBase();
		wzText.render(fx, fy, hover ? WZCOL_TEXT_BRIGHT : WZCOL_TEXT_MEDIUM);
	}
private:
	WzText wzText;
	WzString lastText;
};

class TouchGuidePanel : public IntFormAnimated
{
public:
	TouchGuidePanel() : IntFormAnimated(false) {}

	void initialize()
	{
		title = std::make_shared<W_LABEL>();
		attach(title);
		title->setFont(font_large, WZCOL_YELLOW);

		body = std::make_shared<W_LABEL>();
		attach(body);
		body->setFontColour(WZCOL_TEXT_BRIGHT);

		pageLabel = std::make_shared<W_LABEL>();
		attach(pageLabel);
		pageLabel->setFont(font_regular, WZCOL_TEXT_MEDIUM);

		backButton = std::make_shared<GuideButton>();
		attach(backButton);
		backButton->setString(_("Back"));
		backButton->addOnClickHandler([](W_BUTTON& but) {
			auto panel = std::dynamic_pointer_cast<TouchGuidePanel>(but.parent());
			widgScheduleTask([panel]() { if (panel) { panel->showPage(panel->page - 1); } });
		});

		nextButton = std::make_shared<GuideButton>();
		attach(nextButton);
		nextButton->addOnClickHandler([](W_BUTTON& but) {
			auto panel = std::dynamic_pointer_cast<TouchGuidePanel>(but.parent());
			widgScheduleTask([panel]() {
				if (!panel) { return; }
				if (panel->page + 1 >= static_cast<int>(guidePages().size()))
				{
					closeTouchGuideScreen();
				}
				else
				{
					panel->showPage(panel->page + 1);
				}
			});
		});

		showPage(0);
	}

	void showPage(int newPage)
	{
		const int count = static_cast<int>(guidePages().size());
		page = std::max(0, std::min(newPage, count - 1));
		const GuidePage &p = guidePages()[page];
		title->setString(WzString::fromUtf8(gettext(p.title)));
		bodyText = WzString::fromUtf8(gettext(p.body));
		pageLabel->setString(WzString::fromUtf8(astringf(_("%d / %d"), page + 1, count)));
		backButton->show(page > 0);
		nextButton->setString(page + 1 >= count ? _("Done") : _("Next"));
		geometryChanged();
	}

	void geometryChanged() override
	{
		if (!title)
		{
			return;
		}
		const int pad = 18;
		const int w = width();
		const int h = height();
		const int buttonH = 34;
		const int buttonW = 110;
		title->setGeometry(pad, pad, w - 2 * pad, 30);
		const int bodyTop = pad + 40;
		body->setGeometry(pad, bodyTop, w - 2 * pad, h - bodyTop - buttonH - 2 * pad);
		body->setFormattedString(bodyText, static_cast<uint32_t>(std::max(50, w - 2 * pad)), font_regular);
		const int footerY = h - pad - buttonH;
		backButton->setGeometry(pad, footerY, buttonW, buttonH);
		nextButton->setGeometry(w - pad - buttonW, footerY, buttonW, buttonH);
		pageLabel->setGeometry((w - 60) / 2, footerY + 8, 60, 20);
	}

	int page = 0;

private:
	std::shared_ptr<W_LABEL> title;
	std::shared_ptr<W_LABEL> body;
	std::shared_ptr<W_LABEL> pageLabel;
	std::shared_ptr<GuideButton> backButton;
	std::shared_ptr<GuideButton> nextButton;
	WzString bodyText;
};

std::shared_ptr<W_SCREEN> touchGuideScreen;
}

void showTouchGuideScreen()
{
	if (touchGuideScreen)
	{
		return;
	}
	touchGuideScreen = W_SCREEN::make();
	auto rootFrm = W_FULLSCREENOVERLAY_CLICKFORM::make();
	rootFrm->onClickedFunc = []() { /* only the buttons (or Back) close the guide */ };
	rootFrm->onCancelPressed = []() { widgScheduleTask([]() { closeTouchGuideScreen(); }); };
	touchGuideScreen->psForm->attach(rootFrm);

	auto panel = std::make_shared<TouchGuidePanel>();
	rootFrm->attach(panel);
	panel->setCalcLayout([](WIDGET *psWidget) {
		const int w = std::min<int>(560, static_cast<int>(screenWidth) - 40);
		const int h = std::min<int>(380, static_cast<int>(screenHeight) - 40);
		psWidget->setGeometry((static_cast<int>(screenWidth) - w) / 2, (static_cast<int>(screenHeight) - h) / 2, w, h);
	});
	panel->initialize();

	widgRegisterOverlayScreen(touchGuideScreen, std::numeric_limits<uint16_t>::max() - 2);
}

void closeTouchGuideScreen()
{
	if (!touchGuideScreen)
	{
		return;
	}
	widgRemoveOverlayScreen(touchGuideScreen);
	touchGuideScreen = nullptr;
}

bool isTouchGuideScreenUp()
{
	return touchGuideScreen != nullptr;
}

void touchGuideMaybeAutoShow()
{
#if defined(__ANDROID__)
	static int titleFrames = 0;
	if (war_getTouchGuideSeen() || isTouchGuideScreenUp())
	{
		return;
	}
	if (GetGameMode() != GS_TITLE_SCREEN || loop_GetVideoStatus())
	{
		titleFrames = 0;
		return;
	}
	// let the title screen settle first
	if (++titleFrames < 30)
	{
		return;
	}
	war_setTouchGuideSeen(true);
	showTouchGuideScreen();
#endif
}
