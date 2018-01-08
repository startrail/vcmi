/*
 * OptionsTab.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once
#include "CSelectionScreen.h"

/// The options tab which is shown at the map selection phase.
class OptionsTab : public CIntObject
{
	CPicture * bg;

public:
	enum SelType
	{
		TOWN,
		HERO,
		BONUS
	};

	struct CPlayerSettingsHelper
	{
		const PlayerSettings & settings;
		const SelType type;

		CPlayerSettingsHelper(const PlayerSettings & settings, SelType type)
			: settings(settings), type(type)
		{}

		/// visible image settings
		size_t getImageIndex();
		std::string getImageName();

		std::string getName(); /// name visible in options dialog
		std::string getTitle(); /// title in popup box
		std::string getSubtitle(); /// popup box subtitle
		std::string getDescription(); /// popup box description, not always present
	};

	class CPregameTooltipBox : public CWindowObject, public CPlayerSettingsHelper
	{
		void genHeader();
		void genTownWindow();
		void genHeroWindow();
		void genBonusWindow();

	public:
		CPregameTooltipBox(CPlayerSettingsHelper & helper);
	};

	struct SelectedBox : public CIntObject, public CPlayerSettingsHelper //img with current town/hero/bonus
	{
		CAnimImage * image;
		CLabel * subtitle;

		SelectedBox(Point position, PlayerSettings & settings, SelType type);
		void clickRight(tribool down, bool previousState) override;

		void update();
	};

	struct PlayerOptionsEntry : public CIntObject
	{
		PlayerInfo & pi;
		PlayerSettings & s;
		CPicture * bg;
		CButton * btns[6]; //left and right for town, hero, bonus
		CButton * flag;
		SelectedBox * town;
		SelectedBox * hero;
		SelectedBox * bonus;
		enum {HUMAN_OR_CPU, HUMAN, CPU} whoCanPlay;

		PlayerOptionsEntry(OptionsTab * owner, PlayerSettings & S);
		void showAll(SDL_Surface * to) override;
		void update();
		void selectButtons(); //hides unavailable buttons
	};

	CSlider * turnDuration;

	std::set<int> usedHeroes;

	struct PlayerToRestore
	{
		PlayerColor color;
		int id;
		void reset() { id = -1; color = PlayerColor::CANNOT_DETERMINE; }
		PlayerToRestore(){ reset(); }
	} playerToRestore;


	std::map<PlayerColor, PlayerOptionsEntry *> entries; //indexed by color

	OptionsTab();
	~OptionsTab();

	void showAll(SDL_Surface * to) override;
	void recreate();
	void nextCastle(PlayerColor player, int dir); //dir == -1 or +1
	void nextHero(PlayerColor player, int dir); //dir == -1 or +1
	int nextAllowedHero(PlayerColor player, int min, int max, int incl, int dir);
	void nextBonus(PlayerColor player, int dir); //dir == -1 or +1
	void setTurnLength(int npos);
	void flagPressed(PlayerColor player);



	bool canUseThisHero(PlayerColor player, int ID);
};
