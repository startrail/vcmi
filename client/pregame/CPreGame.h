/*
 * CPreGame.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../lib/StartInfo.h"
#include "../../lib/FunctionList.h"
#include "../../lib/mapping/CMapInfo.h"
#include "../../lib/rmg/CMapGenerator.h"
#include "../windows/CWindowObject.h"


class CMapInfo;
class CMusicHandler;
class CMapHeader;
class CCampaignHeader;
class CTextInput;
class CCampaign;
class CGStatusBar;
class CTextBox;
class CConnection;
class JsonNode;
class CMultiLineLabel;
class CToggleButton;
class CToggleGroup;
class CTabbedInt;
class CAnimation;
class CAnimImage;
class CButton;
class CLabel;
class CSlider;

namespace boost{ class thread; class recursive_mutex;}

/// The main menu screens listed in the EState enum
class CMenuScreen : public CIntObject
{
	const JsonNode & config;

	CTabbedInt * tabs;

	CPicture * background;
	std::vector<CPicture *> images;

	CIntObject * createTab(size_t index);

public:
	std::vector<std::string> menuNameToEntry;

	enum EState { //where are we?
		mainMenu, newGame, loadGame, campaignMain, saveGame, scenarioInfo, campaignList
	};

	enum EGameMode //MPTODO
	{
////		SINGLE_PLAYER = 0, MULTI_HOT_SEAT,
		MULTI_NETWORK_HOST = 0, MULTI_NETWORK_GUEST, SINGLE_CAMPAIGN
	};
	CMenuScreen(const JsonNode & configNode);

	void showAll(SDL_Surface * to) override;
	void show(SDL_Surface * to) override;
	void activate() override;
	void deactivate() override;

	void switchToTab(size_t index);
};

class CMenuEntry : public CIntObject
{
	std::vector<CPicture *> images;
	std::vector<CButton *> buttons;

	CButton * createButton(CMenuScreen * parent, const JsonNode & button);

public:
	CMenuEntry(CMenuScreen * parent, const JsonNode & config);
};

/// Multiplayer mode
class CMultiMode : public CIntObject
{
public:
	CMenuScreen::EState state;
	CPicture * bg;
	CTextInput * txt;
	CButton * btns[7]; //0 - hotseat, 6 - cancel
	CGStatusBar * bar;

	CMultiMode(CMenuScreen::EState State);
	void hostTCP();
	void joinTCP();

	void onNameChange(std::string newText);
};

/// Hot seat player window
class CMultiPlayers : public CIntObject
{
	CMenuScreen::EGameMode mode;
	CMenuScreen::EState state;
	CPicture * bg;
	CTextBox * title;
	CTextInput * txt[8];
	CButton * ok, * cancel;
	CGStatusBar * bar;

	void onChange(std::string newText);
	void enterSelectionScreen();

public:
	CMultiPlayers(const std::string & firstPlayer, CMenuScreen::EGameMode Mode, CMenuScreen::EState State);
};

/// Manages the configuration of pregame GUI elements like campaign screen, main menu, loading screen,...
class CGPreGameConfig
{
public:
	static CGPreGameConfig & get();
	const JsonNode & getConfig() const;
	const JsonNode & getCampaigns() const;

private:
	CGPreGameConfig();

	const JsonNode campaignSets;
	const JsonNode config;
};

/// Handles background screen, loads graphics for victory/loss condition and random town or hero selection
class CGPreGame : public CIntObject, public IUpdateable
{
	void loadGraphics();
	void disposeGraphics();

	CGPreGame(); //Use createIfNotPresent

public:
	CMenuScreen * menu;

	std::shared_ptr<CAnimation> victoryIcons, lossIcons;

	~CGPreGame();
	void update() override;
	static void openSel(CMenuScreen::EState type, CMenuScreen::EGameMode gameMode = CMenuScreen::MULTI_NETWORK_HOST, const std::map<ui8, std::string> * Names = nullptr);

	void openCampaignScreen(std::string name);

	static CGPreGame * create();
	void removeFromGui();
	static void showLoadingScreen(std::function<void()> loader);

	//MPTODO mess
	static CPicture * createPicture(const JsonNode & config);
	static void setPlayer(PlayerSettings & pset, ui8 player, const std::map<ui8, std::string> & playerNames);
	static void updateStartInfo(std::string filename, StartInfo & sInfo, const std::unique_ptr<CMapHeader> & mapHeader, const std::map<ui8, std::string> & playerNames);
	static PlayerColor playerColor;
	static std::string saveGameName;
};

/// Simple window to enter the server's address.
class CSimpleJoinScreen : public CIntObject
{
	CPicture * bg;
	CTextBox * title;
	CButton * ok, * cancel;
	CGStatusBar * bar;
	CTextInput * address;
	CTextInput * port;

	void connectToServer();
	void onChange(const std::string & newText);

public:
	CSimpleJoinScreen();
};

class CLoadingScreen : public CWindowObject
{
	boost::thread loadingThread;

	std::string getBackground();

public:
	CLoadingScreen(std::function<void()> loader);
	~CLoadingScreen();

	void showAll(SDL_Surface * to) override;
};

class CreditsScreen : public CIntObject
{
	int positionCounter;
	CMultiLineLabel * credits;

public:
	CreditsScreen();
	void show(SDL_Surface * to) override;
	void clickLeft(tribool down, bool previousState) override;
	void clickRight(tribool down, bool previousState) override;
};

extern CGPreGame * CGP;
