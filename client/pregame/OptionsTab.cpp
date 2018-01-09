/*
 * OptionsTab.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "OptionsTab.h"

#include "../CBitmapHandler.h"
#include "../CGameInfo.h"
#include "../gui/CAnimation.h"
#include "../gui/CGuiHandler.h"
#include "../widgets/CComponent.h"
#include "../widgets/Buttons.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/ObjectLists.h"
#include "../widgets/TextControls.h"
#include "../windows/GUIClasses.h"
#include "../windows/InfoWindows.h"

#include "../../lib/NetPacks.h"
#include "../../lib/CGeneralTextHandler.h"
#include "../../lib/CArtHandler.h"
#include "../../lib/CTownHandler.h"
#include "../../lib/CHeroHandler.h"

static void swapPlayers(PlayerSettings & a, PlayerSettings & b)
{
	std::swap(a.playerID, b.playerID);
	std::swap(a.name, b.name);

	if(a.playerID == 1)
		CGPreGame::playerColor = a.color;
	else if(b.playerID == 1)
		CGPreGame::playerColor = b.color;
}

OptionsTab::OptionsTab()
	: turnDuration(nullptr)
{
	OBJ_CONSTRUCTION;
	bg = new CPicture("ADVOPTBK", 0, 6);
	pos = bg->pos;

	if(SEL->screenType == CMenuScreen::newGame)
		turnDuration = new CSlider(Point(55, 551), 194, std::bind(&OptionsTab::setTurnLength, this, _1), 1, 11, 11, true, CSlider::BLUE);
}

OptionsTab::~OptionsTab()
{

}

void OptionsTab::showAll(SDL_Surface * to)
{
	CIntObject::showAll(to);
	printAtMiddleLoc(CGI->generaltexth->allTexts[515], 222, 30, FONT_BIG, Colors::YELLOW, to);
	printAtMiddleWBLoc(CGI->generaltexth->allTexts[516], 222, 68, FONT_SMALL, 300, Colors::WHITE, to); //Select starting options, handicap, and name for each player in the game.
	printAtMiddleWBLoc(CGI->generaltexth->allTexts[517], 107, 110, FONT_SMALL, 100, Colors::YELLOW, to); //Player Name Handicap Type
	printAtMiddleWBLoc(CGI->generaltexth->allTexts[518], 197, 110, FONT_SMALL, 70, Colors::YELLOW, to); //Starting Town
	printAtMiddleWBLoc(CGI->generaltexth->allTexts[519], 273, 110, FONT_SMALL, 70, Colors::YELLOW, to); //Starting Hero
	printAtMiddleWBLoc(CGI->generaltexth->allTexts[520], 349, 110, FONT_SMALL, 70, Colors::YELLOW, to); //Starting Bonus
	printAtMiddleLoc(CGI->generaltexth->allTexts[521], 222, 538, FONT_SMALL, Colors::YELLOW, to); // Player Turn Duration
	if(turnDuration)
		printAtMiddleLoc(CGI->generaltexth->turnDurations[turnDuration->getValue()], 319, 559, FONT_SMALL, Colors::WHITE, to); //Turn duration value
}

void OptionsTab::recreate()
{
	for(auto & elem : entries)
	{
		delete elem.second;
	}
	entries.clear();
	usedHeroes.clear();

	OBJ_CONSTRUCTION_CAPTURING_ALL;
	for(auto it = SEL->sInfo.playerInfos.begin(); it != SEL->sInfo.playerInfos.end(); ++it)
	{
		entries.insert(std::make_pair(it->first, new PlayerOptionsEntry(this, it->second)));
		const std::vector<SHeroName> & heroes = SEL->current->mapHeader->players[it->first.getNum()].heroesNames;
		for(auto & heroe : heroes)
			if(heroe.heroId >= 0) //in VCMI map format heroId = -1 means random hero
				usedHeroes.insert(heroe.heroId);
	}
}

void OptionsTab::nextCastle(PlayerColor player, int dir)
{
	if(SEL->isGuest())
	{
		SEL->postRequest(RequestOptionsChange::TOWN, dir, player);
		return;
	}

	PlayerSettings & s = SEL->sInfo.playerInfos[player];
	si16 & cur = s.castle;
	auto & allowed = SEL->current->mapHeader->players[s.color.getNum()].allowedFactions;
	const bool allowRandomTown = SEL->current->mapHeader->players[s.color.getNum()].isFactionRandom;

	if(cur == PlayerSettings::NONE) //no change
		return;

	if(cur == PlayerSettings::RANDOM) //first/last available
	{
		if(dir > 0)
			cur = *allowed.begin(); //id of first town
		else
			cur = *allowed.rbegin(); //id of last town

	}
	else // next/previous available
	{
		if((cur == *allowed.begin() && dir < 0) || (cur == *allowed.rbegin() && dir > 0))
		{
			if(allowRandomTown)
			{
				cur = PlayerSettings::RANDOM;
			}
			else
			{
				if(dir > 0)
					cur = *allowed.begin();
				else
					cur = *allowed.rbegin();
			}
		}
		else
		{
			assert(dir >= -1 && dir <= 1); //othervice std::advance may go out of range
			auto iter = allowed.find(cur);
			std::advance(iter, dir);
			cur = *iter;
		}
	}

	if(s.hero >= 0 && !SEL->current->mapHeader->players[s.color.getNum()].hasCustomMainHero()) // remove hero unless it set to fixed one in map editor
	{
		usedHeroes.erase(s.hero); // restore previously selected hero back to available pool
		s.hero = PlayerSettings::RANDOM;
	}
	if(cur < 0 && s.bonus == PlayerSettings::RESOURCE)
		s.bonus = PlayerSettings::RANDOM;

	entries[player]->selectButtons();

	SEL->propagateOptions();
	entries[player]->update();
	redraw();
}

void OptionsTab::nextHero(PlayerColor player, int dir)
{
	if(SEL->isGuest())
	{
		SEL->postRequest(RequestOptionsChange::HERO, dir, player);
		return;
	}

	PlayerSettings & s = SEL->sInfo.playerInfos[player];
	int old = s.hero;
	if(s.castle < 0 || s.playerID == PlayerSettings::PLAYER_AI || s.hero == PlayerSettings::NONE)
		return;

	if(s.hero == PlayerSettings::RANDOM) // first/last available
	{
		int max = CGI->heroh->heroes.size(),
			min = 0;
		s.hero = nextAllowedHero(player, min, max, 0, dir);
	}
	else
	{
		if(dir > 0)
			s.hero = nextAllowedHero(player, s.hero, CGI->heroh->heroes.size(), 1, dir);
		else
			s.hero = nextAllowedHero(player, -1, s.hero, 1, dir); // min needs to be -1 -- hero at index 0 would be skipped otherwise
	}

	if(old != s.hero)
	{
		usedHeroes.erase(old);
		usedHeroes.insert(s.hero);
		entries[player]->update();
		redraw();
	}
	SEL->propagateOptions();
}

int OptionsTab::nextAllowedHero(PlayerColor player, int min, int max, int incl, int dir)
{
	if(dir > 0)
	{
		for(int i = min + incl; i <= max - incl; i++)
			if(canUseThisHero(player, i))
				return i;
	}
	else
	{
		for(int i = max - incl; i >= min + incl; i--)
			if(canUseThisHero(player, i))
				return i;
	}
	return -1;
}

void OptionsTab::nextBonus(PlayerColor player, int dir)
{
	if(SEL->isGuest())
	{
		SEL->postRequest(RequestOptionsChange::BONUS, dir, player);
		return;
	}

	PlayerSettings & s = SEL->sInfo.playerInfos[player];
	PlayerSettings::Ebonus & ret = s.bonus = static_cast<PlayerSettings::Ebonus>(static_cast<int>(s.bonus) + dir);

	if(s.hero == PlayerSettings::NONE &&
		!SEL->current->mapHeader->players[s.color.getNum()].heroesNames.size() &&
		ret == PlayerSettings::ARTIFACT) //no hero - can't be artifact
	{
		if(dir < 0)
			ret = PlayerSettings::RANDOM;
		else
			ret = PlayerSettings::GOLD;
	}

	if(ret > PlayerSettings::RESOURCE)
		ret = PlayerSettings::RANDOM;
	if(ret < PlayerSettings::RANDOM)
		ret = PlayerSettings::RESOURCE;

	if(s.castle == PlayerSettings::RANDOM && ret == PlayerSettings::RESOURCE) //random castle - can't be resource
	{
		if(dir < 0)
			ret = PlayerSettings::GOLD;
		else
			ret = PlayerSettings::RANDOM;
	}

	SEL->propagateOptions();
	entries[player]->update();
	redraw();
}

void OptionsTab::setTurnLength(int npos)
{
	static const int times[] = {1, 2, 4, 6, 8, 10, 15, 20, 25, 30, 0};
	vstd::amin(npos, ARRAY_COUNT(times) - 1);

	SEL->sInfo.turnTime = times[npos];
	redraw();
}

void OptionsTab::flagPressed(PlayerColor color)
{
	PlayerSettings & clicked = SEL->sInfo.playerInfos[color];
	PlayerSettings * old = nullptr;

/*	if(SEL->playerNames.size() == 1) //single player -> just swap
	{
		if(color == CGPreGame::playerColor) //that color is already selected, no action needed
			return;

		old = &SEL->sInfo.playerInfos[CGPreGame::playerColor];
		swapPlayers(*old, clicked);
	}
	else*/
	{
		//identify clicked player
		int clickedNameID = clicked.playerID; //human is a number of player, zero means AI

		if(clickedNameID > 0 && playerToRestore.id == clickedNameID) //player to restore is about to being replaced -> put him back to the old place
		{
			PlayerSettings & restPos = SEL->sInfo.playerInfos[playerToRestore.color];
			SEL->setPlayer(restPos, playerToRestore.id);
			playerToRestore.reset();
		}

		int newPlayer; //which player will take clicked position

		//who will be put here?
		if(!clickedNameID) //AI player clicked -> if possible replace computer with unallocated player
		{
			newPlayer = SEL->getIdOfFirstUnallocatedPlayer();
			if(!newPlayer) //no "free" player -> get just first one
				newPlayer = SEL->playerNames.begin()->first;
		}
		else //human clicked -> take next
		{
			auto i = SEL->playerNames.find(clickedNameID); //clicked one
			i++; //player AFTER clicked one

			if(i != SEL->playerNames.end())
				newPlayer = i->first;
			else
				newPlayer = 0; //AI if we scrolled through all players
		}

		SEL->setPlayer(clicked, newPlayer); //put player

		//if that player was somewhere else, we need to replace him with computer
		if(newPlayer) //not AI
		{
			for(auto i = SEL->sInfo.playerInfos.begin(); i != SEL->sInfo.playerInfos.end(); i++)
			{
				int curNameID = i->second.playerID;
				if(i->first != color && curNameID == newPlayer)
				{
					assert(i->second.playerID);
					playerToRestore.color = i->first;
					playerToRestore.id = newPlayer;
					SEL->setPlayer(i->second, 0); //set computer
					old = &i->second;
					break;
				}
			}
		}
	}

	entries[clicked.color]->selectButtons();
	if(old)
	{
		entries[old->color]->selectButtons();
		if(old->hero >= 0)
			usedHeroes.erase(old->hero);

		old->hero = entries[old->color]->pi.defaultHero();
		entries[old->color]->update(); // update previous frame images in case entries were auto-updated
	}

	SEL->propagateOptions();
	GH.totalRedraw();
}

bool OptionsTab::canUseThisHero(PlayerColor player, int ID)
{
	return CGI->heroh->heroes.size() > ID
		&& SEL->sInfo.playerInfos[player].castle == CGI->heroh->heroes[ID]->heroClass->faction
		&& !vstd::contains(usedHeroes, ID)
		&& SEL->current->mapHeader->allowedHeroes[ID];
}

size_t OptionsTab::CPlayerSettingsHelper::getImageIndex()
{
	enum EBonusSelection //frames of bonuses file
	{
		WOOD_ORE = 0,   CRYSTAL = 1,    GEM  = 2,
		MERCURY  = 3,   SULFUR  = 5,    GOLD = 8,
		ARTIFACT = 9,   RANDOM  = 10,
		WOOD = 0,       ORE     = 0,    MITHRIL = 10, // resources unavailable in bonuses file

		TOWN_RANDOM = 38,  TOWN_NONE = 39, // Special frames in ITPA
		HERO_RANDOM = 163, HERO_NONE = 164 // Special frames in PortraitsSmall
	};

	switch(type)
	{
	case TOWN:
		switch(settings.castle)
		{
		case PlayerSettings::NONE:
			return TOWN_NONE;
		case PlayerSettings::RANDOM:
			return TOWN_RANDOM;
		default:
			return CGI->townh->factions[settings.castle]->town->clientInfo.icons[true][false] + 2;
		}

	case HERO:
		switch(settings.hero)
		{
		case PlayerSettings::NONE:
			return HERO_NONE;
		case PlayerSettings::RANDOM:
			return HERO_RANDOM;
		default:
		{
			if(settings.heroPortrait >= 0)
				return settings.heroPortrait;
			return CGI->heroh->heroes[settings.hero]->imageIndex;
		}
		}

	case BONUS:
	{
		switch(settings.bonus)
		{
		case PlayerSettings::RANDOM:
			return RANDOM;
		case PlayerSettings::ARTIFACT:
			return ARTIFACT;
		case PlayerSettings::GOLD:
			return GOLD;
		case PlayerSettings::RESOURCE:
		{
			switch(CGI->townh->factions[settings.castle]->town->primaryRes)
			{
			case Res::WOOD_AND_ORE:
				return WOOD_ORE;
			case Res::WOOD:
				return WOOD;
			case Res::MERCURY:
				return MERCURY;
			case Res::ORE:
				return ORE;
			case Res::SULFUR:
				return SULFUR;
			case Res::CRYSTAL:
				return CRYSTAL;
			case Res::GEMS:
				return GEM;
			case Res::GOLD:
				return GOLD;
			case Res::MITHRIL:
				return MITHRIL;
			}
		}
		}
	}
	}
	return 0;
}

std::string OptionsTab::CPlayerSettingsHelper::getImageName()
{
	switch(type)
	{
	case OptionsTab::TOWN:
		return "ITPA";
	case OptionsTab::HERO:
		return "PortraitsSmall";
	case OptionsTab::BONUS:
		return "SCNRSTAR";
	}
	return "";
}

std::string OptionsTab::CPlayerSettingsHelper::getName()
{
	switch(type)
	{
	case TOWN:
	{
		switch(settings.castle)
		{
		case PlayerSettings::NONE:
			return CGI->generaltexth->allTexts[523];
		case PlayerSettings::RANDOM:
			return CGI->generaltexth->allTexts[522];
		default:
			return CGI->townh->factions[settings.castle]->name;
		}
	}
	case HERO:
	{
		switch(settings.hero)
		{
		case PlayerSettings::NONE:
			return CGI->generaltexth->allTexts[523];
		case PlayerSettings::RANDOM:
			return CGI->generaltexth->allTexts[522];
		default:
		{
			if(!settings.heroName.empty())
				return settings.heroName;
			return CGI->heroh->heroes[settings.hero]->name;
		}
		}
	}
	case BONUS:
	{
		switch(settings.bonus)
		{
		case PlayerSettings::RANDOM:
			return CGI->generaltexth->allTexts[522];
		default:
			return CGI->generaltexth->arraytxt[214 + settings.bonus];
		}
	}
	}
	return "";
}


std::string OptionsTab::CPlayerSettingsHelper::getTitle()
{
	switch(type)
	{
	case OptionsTab::TOWN:
		return (settings.castle < 0) ? CGI->generaltexth->allTexts[103] : CGI->generaltexth->allTexts[80];
	case OptionsTab::HERO:
		return (settings.hero < 0) ? CGI->generaltexth->allTexts[101] : CGI->generaltexth->allTexts[77];
	case OptionsTab::BONUS:
	{
		switch(settings.bonus)
		{
		case PlayerSettings::RANDOM:
			return CGI->generaltexth->allTexts[86]; //{Random Bonus}
		case PlayerSettings::ARTIFACT:
			return CGI->generaltexth->allTexts[83]; //{Artifact Bonus}
		case PlayerSettings::GOLD:
			return CGI->generaltexth->allTexts[84]; //{Gold Bonus}
		case PlayerSettings::RESOURCE:
			return CGI->generaltexth->allTexts[85]; //{Resource Bonus}
		}
	}
	}
	return "";
}
std::string OptionsTab::CPlayerSettingsHelper::getSubtitle()
{
	switch(type)
	{
	case TOWN:
		return getName();
	case HERO:
	{
		if(settings.hero >= 0)
			return getName() + " - " + CGI->heroh->heroes[settings.hero]->heroClass->name;
		return getName();
	}

	case BONUS:
	{
		switch(settings.bonus)
		{
		case PlayerSettings::GOLD:
			return CGI->generaltexth->allTexts[87]; //500-1000
		case PlayerSettings::RESOURCE:
		{
			switch(CGI->townh->factions[settings.castle]->town->primaryRes)
			{
			case Res::MERCURY:
				return CGI->generaltexth->allTexts[694];
			case Res::SULFUR:
				return CGI->generaltexth->allTexts[695];
			case Res::CRYSTAL:
				return CGI->generaltexth->allTexts[692];
			case Res::GEMS:
				return CGI->generaltexth->allTexts[693];
			case Res::WOOD_AND_ORE:
				return CGI->generaltexth->allTexts[89]; //At the start of the game, 5-10 wood and 5-10 ore are added to your Kingdom's resource pool
			}
		}
		}
	}
	}
	return "";
}

std::string OptionsTab::CPlayerSettingsHelper::getDescription()
{
	switch(type)
	{
	case TOWN:
		return CGI->generaltexth->allTexts[104];
	case HERO:
		return CGI->generaltexth->allTexts[102];
	case BONUS:
	{
		switch(settings.bonus)
		{
		case PlayerSettings::RANDOM:
			return CGI->generaltexth->allTexts[94]; //Gold, wood and ore, or an artifact is randomly chosen as your starting bonus
		case PlayerSettings::ARTIFACT:
			return CGI->generaltexth->allTexts[90]; //An artifact is randomly chosen and equipped to your starting hero
		case PlayerSettings::GOLD:
			return CGI->generaltexth->allTexts[92]; //At the start of the game, 500-1000 gold is added to your Kingdom's resource pool
		case PlayerSettings::RESOURCE:
		{
			switch(CGI->townh->factions[settings.castle]->town->primaryRes)
			{
			case Res::MERCURY:
				return CGI->generaltexth->allTexts[690];
			case Res::SULFUR:
				return CGI->generaltexth->allTexts[691];
			case Res::CRYSTAL:
				return CGI->generaltexth->allTexts[688];
			case Res::GEMS:
				return CGI->generaltexth->allTexts[689];
			case Res::WOOD_AND_ORE:
				return CGI->generaltexth->allTexts[93]; //At the start of the game, 5-10 wood and 5-10 ore are added to your Kingdom's resource pool
			}
		}
		}
	}
	}
	return "";
}

OptionsTab::CPregameTooltipBox::CPregameTooltipBox(CPlayerSettingsHelper & helper)
	: CWindowObject(BORDERED | RCLICK_POPUP), CPlayerSettingsHelper(helper)
{
	OBJ_CONSTRUCTION_CAPTURING_ALL;

	int value = PlayerSettings::NONE;

	switch(CPlayerSettingsHelper::type)
	{
		break;
	case TOWN:
		value = settings.castle;
		break;
	case HERO:
		value = settings.hero;
		break;
	case BONUS:
		value = settings.bonus;
	}

	if(value == PlayerSettings::RANDOM)
		genBonusWindow();
	else if(CPlayerSettingsHelper::type == BONUS)
		genBonusWindow();
	else if(CPlayerSettingsHelper::type == HERO)
		genHeroWindow();
	else if(CPlayerSettingsHelper::type == TOWN)
		genTownWindow();

	center();
}

void OptionsTab::CPregameTooltipBox::genHeader()
{
	new CFilledTexture("DIBOXBCK", pos);
	updateShadow();

	new CLabel(pos.w / 2 + 8, 21, FONT_MEDIUM, CENTER, Colors::YELLOW, getTitle());

	new CLabel(pos.w / 2, 88, FONT_SMALL, CENTER, Colors::WHITE, getSubtitle());

	new CAnimImage(getImageName(), getImageIndex(), 0, pos.w / 2 - 24, 45);
}

void OptionsTab::CPregameTooltipBox::genTownWindow()
{
	pos = Rect(0, 0, 228, 290);
	genHeader();

	new CLabel(pos.w / 2 + 8, 122, FONT_MEDIUM, CENTER, Colors::YELLOW, CGI->generaltexth->allTexts[79]);

	std::vector<CComponent *> components;
	const CTown * town = CGI->townh->factions[settings.castle]->town;

	for(auto & elem : town->creatures)
	{
		if(!elem.empty())
			components.push_back(new CComponent(CComponent::creature, elem.front(), 0, CComponent::tiny));
	}

	new CComponentBox(components, Rect(10, 140, pos.w - 20, 140));
}

void OptionsTab::CPregameTooltipBox::genHeroWindow()
{
	pos = Rect(0, 0, 292, 226);
	genHeader();

	// specialty
	new CAnimImage("UN44", CGI->heroh->heroes[settings.hero]->imageIndex, 0, pos.w / 2 - 22, 134);

	new CLabel(pos.w / 2 + 4, 117, FONT_MEDIUM, CENTER, Colors::YELLOW, CGI->generaltexth->allTexts[78]);
	new CLabel(pos.w / 2, 188, FONT_SMALL, CENTER, Colors::WHITE, CGI->heroh->heroes[settings.hero]->specName);
}

void OptionsTab::CPregameTooltipBox::genBonusWindow()
{
	pos = Rect(0, 0, 228, 162);
	genHeader();

	new CTextBox(getDescription(), Rect(10, 100, pos.w - 20, 70), 0, FONT_SMALL, CENTER, Colors::WHITE);
}

OptionsTab::SelectedBox::SelectedBox(Point position, PlayerSettings & settings, SelType type)
	: CIntObject(RCLICK, position), CPlayerSettingsHelper(settings, type)
{
	OBJ_CONSTRUCTION_CAPTURING_ALL;

	image = new CAnimImage(getImageName(), getImageIndex());
	subtitle = new CLabel(23, 39, FONT_TINY, CENTER, Colors::WHITE, getName());

	pos = image->pos;
}

void OptionsTab::SelectedBox::update()
{
	image->setFrame(getImageIndex());
	subtitle->setText(getName());
}

void OptionsTab::SelectedBox::clickRight(tribool down, bool previousState)
{
	if(down)
	{
		// cases when we do not need to display a message
		if(settings.castle == -2 && CPlayerSettingsHelper::type == TOWN)
			return;
		if(settings.hero == -2 && !SEL->current->mapHeader->players[settings.color.getNum()].hasCustomMainHero() && CPlayerSettingsHelper::type == HERO)
			return;

		GH.pushInt(new CPregameTooltipBox(*this));
	}
}

OptionsTab::PlayerOptionsEntry::PlayerOptionsEntry(OptionsTab * owner, PlayerSettings & S)
	: pi(SEL->current->mapHeader->players[S.color.getNum()]), s(S)
{
	OBJ_CONSTRUCTION;
	defActions |= SHARE_POS;

	int serial = 0;
	for(int g = 0; g < s.color.getNum(); ++g)
	{
		PlayerInfo & itred = SEL->current->mapHeader->players[g];
		if(itred.canComputerPlay || itred.canHumanPlay)
			serial++;
	}

	pos.x += 54;
	pos.y += 122 + serial * 50;

	static const char * flags[] =
	{
		"AOFLGBR.DEF", "AOFLGBB.DEF", "AOFLGBY.DEF", "AOFLGBG.DEF",
		"AOFLGBO.DEF", "AOFLGBP.DEF", "AOFLGBT.DEF", "AOFLGBS.DEF"
	};
	static const char * bgs[] =
	{
		"ADOPRPNL.bmp", "ADOPBPNL.bmp", "ADOPYPNL.bmp", "ADOPGPNL.bmp",
		"ADOPOPNL.bmp", "ADOPPPNL.bmp", "ADOPTPNL.bmp", "ADOPSPNL.bmp"
	};

	bg = new CPicture(BitmapHandler::loadBitmap(bgs[s.color.getNum()]), 0, 0, true);
	if(SEL->screenType == CMenuScreen::newGame)
	{
		btns[0] = new CButton(Point(107, 5), "ADOPLFA.DEF", CGI->generaltexth->zelp[132], std::bind(&OptionsTab::nextCastle, owner, s.color, -1));
		btns[1] = new CButton(Point(168, 5), "ADOPRTA.DEF", CGI->generaltexth->zelp[133], std::bind(&OptionsTab::nextCastle, owner, s.color, +1));
		btns[2] = new CButton(Point(183, 5), "ADOPLFA.DEF", CGI->generaltexth->zelp[148], std::bind(&OptionsTab::nextHero, owner, s.color, -1));
		btns[3] = new CButton(Point(244, 5), "ADOPRTA.DEF", CGI->generaltexth->zelp[149], std::bind(&OptionsTab::nextHero, owner, s.color, +1));
		btns[4] = new CButton(Point(259, 5), "ADOPLFA.DEF", CGI->generaltexth->zelp[164], std::bind(&OptionsTab::nextBonus, owner, s.color, -1));
		btns[5] = new CButton(Point(320, 5), "ADOPRTA.DEF", CGI->generaltexth->zelp[165], std::bind(&OptionsTab::nextBonus, owner, s.color, +1));
	}
	else
		for(auto & elem : btns)
			elem = nullptr;

	selectButtons();

	assert(SEL->current && SEL->current->mapHeader);
	const PlayerInfo & p = SEL->current->mapHeader->players[s.color.getNum()];
	assert(p.canComputerPlay || p.canHumanPlay); //someone must be able to control this player
	if(p.canHumanPlay && p.canComputerPlay)
		whoCanPlay = HUMAN_OR_CPU;
	else if(p.canComputerPlay)
		whoCanPlay = CPU;
	else
		whoCanPlay = HUMAN;

	if(SEL->screenType != CMenuScreen::scenarioInfo && SEL->current->mapHeader->players[s.color.getNum()].canHumanPlay)
	{
		flag = new CButton(Point(-43, 2), flags[s.color.getNum()], CGI->generaltexth->zelp[180], std::bind(&OptionsTab::flagPressed, owner, s.color));
		flag->hoverable = true;
		flag->block(SEL->isGuest());
	}
	else
		flag = nullptr;

	town = new SelectedBox(Point(119, 2), s, TOWN);
	hero = new SelectedBox(Point(195, 2), s, HERO);
	bonus = new SelectedBox(Point(271, 2), s, BONUS);
}

void OptionsTab::PlayerOptionsEntry::showAll(SDL_Surface * to)
{
	CIntObject::showAll(to);
	printAtMiddleLoc(s.name, 55, 10, FONT_SMALL, Colors::WHITE, to);
	printAtMiddleWBLoc(CGI->generaltexth->arraytxt[206 + whoCanPlay], 28, 39, FONT_TINY, 50, Colors::WHITE, to);
}

void OptionsTab::PlayerOptionsEntry::update()
{
	town->update();
	hero->update();
	bonus->update();
}

void OptionsTab::PlayerOptionsEntry::selectButtons()
{
	if(!btns[0])
		return;

	const bool foreignPlayer = SEL->isGuest() && !SEL->isMyColor(s.color);

	if((pi.allowedFactions.size() < 2 && !pi.isFactionRandom) || foreignPlayer)
	{
		btns[0]->disable();
		btns[1]->disable();
	}
	else
	{
		btns[0]->enable();
		btns[1]->enable();
	}

	if((pi.defaultHero() != -1 || !s.playerID || s.castle < 0) //fixed hero
		|| foreignPlayer) //or not our player
	{
		btns[2]->disable();
		btns[3]->disable();
	}
	else
	{
		btns[2]->enable();
		btns[3]->enable();
	}

	if(foreignPlayer)
	{
		btns[4]->disable();
		btns[5]->disable();
	}
	else
	{
		btns[4]->enable();
		btns[5]->enable();
	}
}
