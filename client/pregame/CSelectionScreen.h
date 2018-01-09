/*
 * CSelectionScreen.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CPreGame.h"

class CTextBox;
class CTextInput;
class CAnimImage;
class CToggleGroup;
struct CPackForSelectionScreen;
class RandomMapTab;
class OptionsTab;
class SelectionTab;
class InfoCard;
class CChatBox;

/// Interface for selecting a map.
class ISelectionScreenInfo
{
public:
	CMenuScreen::EGameMode gameMode;
	CMenuScreen::EState screenType; //new/save/load#Game
	const CMapInfo * current;
	StartInfo sInfo;
	std::map<ui8, std::string> playerNames; // id of player <-> player name; 0 is reserved as ID of AI "players"

	ISelectionScreenInfo();
	virtual ~ISelectionScreenInfo();
	virtual void update(){};
	virtual void propagateOptions() {};
	virtual void postRequest(ui8 what, ui8 dir) {};
	virtual void postChatMessage(const std::string & txt){};

	void setPlayer(PlayerSettings & pset, ui8 player);
	void updateStartInfo(std::string filename, StartInfo & sInfo, const std::unique_ptr<CMapHeader> & mapHeader);

	ui8 getIdOfFirstUnallocatedPlayer(); //returns 0 if none
	bool isGuest() const;
	bool isHost() const;

};

/// The actual map selection screen which consists of the options and selection tab
class CSelectionScreen : public CIntObject, public ISelectionScreenInfo
{
	bool bordered;

public:
	CPicture * bg; //general bg image
	InfoCard * card;
	OptionsTab * opt;
	RandomMapTab * randMapTab;
	CButton * start, * back;

	SelectionTab * sel;
	CIntObject * curTab;

	boost::thread * serverHandlingThread;
	boost::recursive_mutex * mx;
	std::list<CPackForSelectionScreen *> upcomingPacks; //protected by mx

	bool ongoingClosing;
	ui8 myNameID; //used when networking - otherwise all player are "mine"

	CSelectionScreen(CMenuScreen::EState Type, CMenuScreen::EGameMode GameMode = CMenuScreen::MULTI_NETWORK_HOST);
	~CSelectionScreen();
	void showAll(SDL_Surface * to) override;
	void toggleTab(CIntObject * tab);
	void changeSelection(const CMapInfo * to);
	void startCampaign();
	void startScenario();
	void difficultyChange(int to);

	void handleConnection();
	void update() override;
	void processPacks();

	void setSInfo(const StartInfo & si);
	void propagateNames();
	void propagateOptions() override;
	void postRequest(ui8 what, ui8 dir) override;
	void postChatMessage(const std::string & txt) override;
};

/// Save game screen
class CSavingScreen : public CSelectionScreen
{
public:
	const CMapInfo * ourGame;


	CSavingScreen();
	~CSavingScreen();
};

class InfoCard : public CIntObject
{
	CAnimImage * victory, * loss, * sizes;
	std::shared_ptr<CAnimation> sFlags;

public:
	CPicture * bg;

	bool chatOn; //if chat is shown, then description is hidden
	CTextBox * mapDescription;
	CChatBox * chat;
	CPicture * playerListBg;

	CToggleGroup * difficulty;

	InfoCard();
	~InfoCard();
	void showAll(SDL_Surface * to) override;
	void clickRight(tribool down, bool previousState) override;
	void changeSelection(const CMapInfo * to);
	void showTeamsPopup();
	void toggleChat();
	void setChat(bool activateChat);
};

/// Implementation of the chat box
class CChatBox : public CIntObject
{
public:
	CTextBox * chatHistory;
	CTextInput * inputBox;

	CChatBox(const Rect & rect);

	void keyPressed(const SDL_KeyboardEvent & key) override;

	void addNewMessage(const std::string & text);
};

/// Scenario information screen shown during the game (thus not really a "pre-game" but fits here anyway)
class CScenarioInfo : public CIntObject, public ISelectionScreenInfo
{
public:
	CButton * back;
	InfoCard * card;
	OptionsTab * opt;

	CScenarioInfo(const CMapHeader * mapHeader, const StartInfo * startInfo);
	~CScenarioInfo();
};

extern ISelectionScreenInfo * SEL;
