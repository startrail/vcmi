/*
 * NetPacksPregame.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "CPreGame.h"
#include "CSelectionScreen.h"
#include "OptionsTab.h"
#include "../CServerHandler.h"
#include "../CGameInfo.h"
#include "../gui/CGuiHandler.h"
#include "../../lib/NetPacks.h"
#include "../../lib/serializer/Connection.h"

// MPTODO: Can this be avoided?
#include "OptionsTab.h"
#include "RandomMapTab.h"
#include "SelectionTab.h"

void startGame(StartInfo * options);

void ChatMessage::apply(CSelectionScreen * selScreen)
{
	selScreen->card->chat->addNewMessage(playerName + ": " + message);
	GH.totalRedraw();
}

void QuitMenuWithoutStarting::apply(CSelectionScreen * selScreen)
{
	if(!selScreen->ongoingClosing)
	{
		*CSH->c << this; //resend to confirm
		GH.popIntTotally(selScreen); //will wait with deleting us before this thread ends
	}
	CSH->stopConnection();
}

void PlayerJoined::apply(CSelectionScreen * selScreen)
{
	for(auto & player : players)
	{
		SEL->playerNames.insert(player);

		//put new player in first slot with AI
		for(auto & elem : SEL->sInfo.playerInfos)
		{
			if(!elem.second.playerID && !elem.second.compOnly)
			{
				selScreen->setPlayer(elem.second, player.first);
				selScreen->opt->entries[elem.second.color]->selectButtons();
				break;
			}
		}
	}

	selScreen->propagateNames();
	selScreen->propagateOptions();
	selScreen->toggleTab(selScreen->curTab);

	GH.totalRedraw();
}

void SelectMap::apply(CSelectionScreen * selScreen)
{
	if(selScreen->isGuest())
	{
		free = false;
		selScreen->changeSelection(mapInfo);
	}
}

void UpdateStartOptions::apply(CSelectionScreen * selScreen)
{
	if(!selScreen->isGuest())
		return;

	selScreen->setSInfo(*options);
}

void PregameGuiAction::apply(CSelectionScreen * selScreen)
{
	if(!selScreen->isGuest())
		return;

	switch(action)
	{
	case NO_TAB:
		selScreen->toggleTab(selScreen->curTab);
		break;
	case OPEN_OPTIONS:
		selScreen->toggleTab(selScreen->opt);
		break;
	case OPEN_SCENARIO_LIST:
		selScreen->toggleTab(selScreen->sel);
		break;
	case OPEN_RANDOM_MAP_OPTIONS:
		selScreen->toggleTab(selScreen->randMapTab);
		break;
	}
}

void RequestOptionsChange::apply(CSelectionScreen * selScreen)
{
	if(!selScreen->isHost())
		return;

	PlayerColor color = selScreen->sInfo.getPlayersSettings(playerID)->color;

	switch(what)
	{
	case TOWN:
		selScreen->opt->nextCastle(color, direction);
		break;
	case HERO:
		selScreen->opt->nextHero(color, direction);
		break;
	case BONUS:
		selScreen->opt->nextBonus(color, direction);
		break;
	}
}

void PlayerLeft::apply(CSelectionScreen * selScreen)
{
	if(selScreen->isGuest())
		return;

	for(auto & pair : selScreen->playerNames)
	{
		if(pair.second.connection != connectionID)
			continue;

		selScreen->playerNames.erase(pair.first);

		if(PlayerSettings * s = selScreen->sInfo.getPlayersSettings(pair.first)) //it's possible that player was unallocated
		{
			selScreen->setPlayer(*s, 0);
			selScreen->opt->entries[s->color]->selectButtons();
		}
	}

	selScreen->propagateNames();
	selScreen->propagateOptions();
	GH.totalRedraw();
}

void PlayersNames::apply(CSelectionScreen * selScreen)
{
	if(selScreen->isGuest())
		selScreen->playerNames = playerNames;
}

void StartWithCurrentSettings::apply(CSelectionScreen * selScreen)
{
	if(!selScreen->ongoingClosing)
	{
		*CSH->c << this; //resend to confirm
	}
	vstd::clear_pointer(selScreen->serverHandlingThread); //detach us
	CGPreGame::saveGameName.clear();

	CGP->showLoadingScreen(std::bind(&startGame, new StartInfo(selScreen->sInfo)));
	throw 666; //EVIL, EVIL, EVIL workaround to kill thread (does "goto catch" outside listening loop)
}



void WelcomeClient::apply(CSelectionScreen * sel)
{
	CSH->c->connectionID = connectionId;
//	sel->changeSelection(curmap);
}
