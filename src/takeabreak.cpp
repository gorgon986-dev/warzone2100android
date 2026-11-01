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
 *  "Take a Break": a skirmish AI temporarily plays for the local player.
 *
 *  Works like Left 4 Dead 2's "take a break": the player's own AI script (their
 *  default skirmish AI, ex. Nexus / Cobra / BoneCrusher) is attached to their
 *  player and does everything a player would - building, research, upgrades,
 *  unit design and combat - until they take control back.
 *
 *  In network games everyone is told (via a system chat message) when a player's
 *  AI takes over and when they return.
 */

#include "takeabreak.h"

#include "lib/framework/frame.h"
#include "lib/framework/stdio_ext.h"
#include "lib/ivis_opengl/screen.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/textdraw.h"
#include "lib/framework/wzstring.h"
#include "lib/netplay/netplay.h"
#include "lib/widget/widget.h"
#include "lib/widget/button.h"

#include "qtscript.h"
#include "wzapi.h"
#include "multiplay.h"
#include "multiint.h"
#include "main.h"
#include "hci.h"
#include "intdisplay.h"
#include "intimage.h"
#include "console.h"

static wzapi::scripting_instance *breakAI = nullptr;
static std::string breakAIName;

static const UDWORD IDTAKEABREAK_FORM = 0x7A2000;

bool takeABreakAvailable()
{
	if (!bMultiPlayer || game.type != LEVEL_TYPE::SKIRMISH)
	{
		return false; // skirmish AIs don't know how to play the campaign
	}
	if (NETisReplay() || GetGameMode() != GS_NORMAL)
	{
		return false;
	}
	if (selectedPlayer >= MAX_PLAYERS || NetPlay.players[selectedPlayer].isSpectator)
	{
		return false;
	}
	return true;
}

bool takeABreakActive()
{
	if (breakAI != nullptr && !isPlayerScriptLoaded(breakAI))
	{
		// the game ended / scripts were shut down
		breakAI = nullptr;
	}
	return breakAI != nullptr;
}

static void announce(const std::string &text)
{
	if (NetPlay.bComms)
	{
		sendInGameSystemMessage(text.c_str());
	}
	else
	{
		addConsoleMessage(text.c_str(), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
	}
}

bool takeABreakStart()
{
	if (!takeABreakAvailable() || takeABreakActive())
	{
		return false;
	}

	const std::string aiScript = getDefaultSkirmishAI();
	const std::string aiDisplayName = getDefaultSkirmishAI(true);
	if (aiScript.empty())
	{
		addConsoleMessage(_("No AI available to take over."), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		return false;
	}

	wzapi::scripting_instance *instance = loadPlayerScript(WzString("multiplay/skirmish/") + WzString::fromUtf8(aiScript), selectedPlayer, AIDifficulty::HARD, wzapi::ScriptBinding::PlayerAI);
	if (!instance)
	{
		addConsoleMessage(_("Failed to start the AI. Check the logs for details."), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		return false;
	}
	instance->handle_eventGameInit();
	instance->handle_eventStartLevel();

	breakAI = instance;
	breakAIName = aiDisplayName;
	announce(astringf(_("%s is taking a break - the %s AI is playing for them."), getPlayerName(selectedPlayer), aiDisplayName.c_str()));
	takeABreakRefreshBanner();
	return true;
}

void takeABreakStop()
{
	if (!takeABreakActive())
	{
		takeABreakRefreshBanner();
		return;
	}
	unloadPlayerScript(breakAI);
	breakAI = nullptr;
	announce(astringf(_("%s is back in control."), getPlayerName(selectedPlayer)));
	takeABreakRefreshBanner();
}

namespace
{
// A "Take back control" bar at the top of the screen, in the game's panel style
class TakeBackControlButton : public W_BUTTON
{
public:
	void display(int xOffset, int yOffset) override
	{
		const int x0 = x() + xOffset;
		const int y0 = y() + yOffset;
		const bool hover = isMouseOverWidget() || (state & WBUT_DOWN) != 0;
		RenderWindowFrame(FRAME_NORMAL, x0, y0, width(), height());
		PIELIGHT fill = WZCOL_FORM_HILITE;
		fill.byte.a = hover ? 200 : 120;
		pie_UniTransBoxFill(static_cast<float>(x0 + 3), static_cast<float>(y0 + 3), static_cast<float>(x0 + width() - 3), static_cast<float>(y0 + height() - 3), fill);
		if (lastText != pText)
		{
			wzText.setText(pText, font_regular_bold);
			lastText = pText;
		}
		const int fx = x0 + (width() - wzText.width()) / 2;
		const int fy = y0 + (height() - wzText.lineSize()) / 2 - wzText.aboveBase();
		wzText.render(fx, fy, hover ? WZCOL_TEXT_BRIGHT : WZCOL_YELLOW);
	}
private:
	WzText wzText;
	WzString lastText;
};
}

void takeABreakRefreshBanner()
{
	if (!psWScreen)
	{
		return;
	}
	auto existing = widgGetFromID(psWScreen, IDTAKEABREAK_FORM);
	const bool active = takeABreakActive();
	if (!active)
	{
		if (existing)
		{
			widgDelete(existing);
		}
		return;
	}
	if (existing)
	{
		return;
	}

	auto button = std::make_shared<TakeBackControlButton>();
	button->id = IDTAKEABREAK_FORM;
	psWScreen->psForm->attach(button);
	button->setString(WzString::fromUtf8(astringf(_("%s AI is playing for you - tap here to take back control"), breakAIName.c_str())));
	button->setTip(_("Take back control from the AI"));
	button->addOnClickHandler([](W_BUTTON&) {
		widgScheduleTask([]() { takeABreakStop(); });
	});
	button->setCalcLayout(LAMBDA_CALCLAYOUT_SIMPLE({
		const int w = std::min<int>(static_cast<int>(screenWidth) - 40, 520);
		const int h = 34;
		psWidget->setGeometry((static_cast<int>(screenWidth) - w) / 2, 64, w, h);
	}));
}
