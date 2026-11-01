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
#include "lib/widget/editbox.h"
#include "lib/framework/input.h"

#include "qtscript.h"
#include "wzapi.h"
#include "multiplay.h"
#include "multiint.h"
#include "main.h"
#include "hci.h"
#include "intdisplay.h"
#include "intimage.h"
#include "console.h"
#include "warzoneconfig.h"

static wzapi::scripting_instance *breakAI = nullptr;
static wzapi::scripting_instance *breakCommander = nullptr; // carries out typed commands
static std::string breakAIName;

static const char *COMMANDER_SCRIPT = "multiplay/script/takeabreak_commander.js";

static const UDWORD IDTAKEABREAK_FORM = 0x7A2000;

static void rebuildBanner();

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
		breakCommander = nullptr;
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

	// the AI chosen in Options > Interface > Take a Break, or the default skirmish AI
	std::string aiScript = getDefaultSkirmishAI();
	std::string aiDisplayName = getDefaultSkirmishAI(true);
	const std::string chosen = war_getTakeABreakAI();
	for (const auto &ai : getAIDataList())
	{
		if (!chosen.empty() && chosen == ai.js)
		{
			aiScript = ai.js;
			aiDisplayName = ai.name;
		}
	}
	const AIDifficulty difficulty = static_cast<AIDifficulty>(war_getTakeABreakDifficulty());
	if (aiScript.empty())
	{
		addConsoleMessage(_("No AI available to take over."), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		return false;
	}

	wzapi::scripting_instance *instance = loadPlayerScript(WzString("multiplay/skirmish/") + WzString::fromUtf8(aiScript), selectedPlayer, difficulty, wzapi::ScriptBinding::PlayerAI);
	if (!instance)
	{
		addConsoleMessage(_("Failed to start the AI. Check the logs for details."), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		return false;
	}
	instance->handle_eventGameInit();
	instance->handle_eventStartLevel();

	breakAI = instance;
	breakAIName = aiDisplayName;

	breakCommander = loadPlayerScript(WzString(COMMANDER_SCRIPT), selectedPlayer, difficulty, wzapi::ScriptBinding::PlayerAI);
	if (breakCommander)
	{
		breakCommander->handle_eventGameInit();
		breakCommander->handle_eventStartLevel();
	}
	else
	{
		debug(LOG_ERROR, "Failed to load %s", COMMANDER_SCRIPT);
	}
	announce(astringf(_("%s is taking a break - the %s AI is playing for them."), getPlayerName(selectedPlayer), aiDisplayName.c_str()));
	rebuildBanner();
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
	if (breakCommander && isPlayerScriptLoaded(breakCommander))
	{
		unloadPlayerScript(breakCommander);
	}
	breakCommander = nullptr;
	announce(astringf(_("%s is back in control."), getPlayerName(selectedPlayer)));
	rebuildBanner();
}

void takeABreakSendCommand(const std::string &command)
{
	if (!takeABreakActive() || command.empty())
	{
		return;
	}
	if (breakCommander && isPlayerScriptLoaded(breakCommander))
	{
		breakCommander->handle_eventChat(selectedPlayer, selectedPlayer, command.c_str());
	}
	// the AI itself may understand some commands too (ex. Cobra's strategy names)
	breakAI->handle_eventChat(selectedPlayer, selectedPlayer, command.c_str());
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

static void addBannerButton(const std::shared_ptr<WIDGET> &parent, const WzString &text, const std::function<void()> &onClick, int x, int y, int w, int h)
{
	auto button = std::make_shared<TakeBackControlButton>();
	parent->attach(button);
	button->setString(text);
	button->setGeometry(x, y, w, h);
	button->addOnClickHandler([onClick](W_BUTTON&) {
		widgScheduleTask([onClick]() { onClick(); });
	});
}

static void rebuildBanner();

void takeABreakRefreshBanner()
{
	if (!psWScreen)
	{
		return;
	}
	auto existing = widgGetFromID(psWScreen, IDTAKEABREAK_FORM);
	const bool active = takeABreakActive();
	if (existing && active)
	{
		return; // keep it (and anything typed in the command box)
	}
	rebuildBanner();
}

static void rebuildBanner()
{
	if (!psWScreen)
	{
		return;
	}
	if (auto existing = widgGetFromID(psWScreen, IDTAKEABREAK_FORM))
	{
		widgDelete(existing);
	}
	if (!takeABreakActive())
	{
		return;
	}

	const int bannerH = 34;
	const int gap = 6;
	auto form = std::make_shared<IntFormTransparent>();
	form->id = IDTAKEABREAK_FORM;
	psWScreen->psForm->attach(form);
	form->setCalcLayout([](WIDGET *psWidget) {
		const int w = std::min<int>(static_cast<int>(screenWidth) - 40, 620);
		const int h = 34 * 2 + 6;
		psWidget->setGeometry((static_cast<int>(screenWidth) - w) / 2, 64, w, h);
	});
	const int formW = std::min<int>(static_cast<int>(screenWidth) - 40, 620);

	addBannerButton(form, WzString::fromUtf8(astringf(_("%s AI is playing for you - tap to take back control"), breakAIName.c_str())),
		[]() { takeABreakStop(); }, 0, 0, formW, bannerH);

	// command buttons
	struct CommandButton { const char *label; const char *command; };
	static const CommandButton commands[] = {
		{ N_("Fortify"), "build defenses" },
		{ N_("Research"), "research" },
		{ N_("Build Units"), "build units" },
		{ N_("Oil"), "build oil" },
		{ N_("Attack"), "attack" },
		{ N_("Retreat"), "retreat" },
	};
	const int count = static_cast<int>(sizeof(commands) / sizeof(commands[0]));
	const int buttonW = (formW - gap * (count - 1)) / count;
	for (int i = 0; i < count; ++i)
	{
		const std::string command = commands[i].command;
		addBannerButton(form, WzString::fromUtf8(gettext(commands[i].label)),
			[command]() { takeABreakSendCommand(command); }, i * (buttonW + gap), bannerH + gap, buttonW, bannerH);
	}
}
