/*
 * CCampaignScreen.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

/// Campaign selection screen
class CCampaignScreen : public CIntObject
{
public:
	enum CampaignStatus {DEFAULT = 0, ENABLED, DISABLED, COMPLETED}; // the status of the campaign

private:
	/// A button which plays a video when you move the mouse cursor over it
	class CCampaignButton : public CIntObject
	{
	private:
		CLabel * hoverLabel;
		CampaignStatus status;

		std::string campFile; // the filename/resourcename of the campaign
		std::string video; // the resource name of the video
		std::string hoverText;

		void clickLeft(tribool down, bool previousState) override;
		void hover(bool on) override;

	public:
		CCampaignButton(const JsonNode & config);
		void show(SDL_Surface * to) override;
	};

	std::vector<CCampaignButton *> campButtons;
	std::vector<CPicture *> images;

	CButton * createExitButton(const JsonNode & button);

public:
	enum CampaignSet {ROE, AB, SOD, WOG};

	CCampaignScreen(const JsonNode & config);
	void showAll(SDL_Surface * to) override;
};

/// Campaign screen where you can choose one out of three starting bonuses
class CBonusSelection : public CIntObject
{
public:
	CBonusSelection(const std::string & campaignFName);
	CBonusSelection(std::shared_ptr<CCampaignState> _ourCampaign);
	~CBonusSelection();

	void showAll(SDL_Surface * to) override;
	void show(SDL_Surface * to) override;

private:
	struct SCampPositions
	{
		std::string campPrefix;
		int colorSuffixLength;

		struct SRegionDesc
		{
			std::string infix;
			int xpos, ypos;
		};

		std::vector<SRegionDesc> regions;

	};

	class CRegion : public CIntObject
	{
		CBonusSelection * owner;
		SDL_Surface * graphics[3]; //[0] - not selected, [1] - selected, [2] - striped
		bool accessible; //false if region should be striped
		bool selectable; //true if region should be selectable
		int myNumber; //number of region

	public:
		std::string rclickText;

		CRegion(CBonusSelection * _owner, bool _accessible, bool _selectable, int _myNumber);
		~CRegion();
		void show(SDL_Surface * to) override;
		void clickLeft(tribool down, bool previousState) override;
		void clickRight(tribool down, bool previousState) override;
	};

	void init();
	void loadPositionsOfGraphics();
	void updateStartButtonState(int selected = -1); //-1 -- no bonus is selected
	void updateBonusSelection();
	void updateCampaignState();

	// Event handlers
	void goBack();
	void startMap();
	void restartMap();
	void selectMap(int mapNr, bool initialSelect);
	void selectBonus(int id);
	void increaseDifficulty();
	void decreaseDifficulty();

	// GUI components
	SDL_Surface * background;
	CButton * startB, * restartB, * backB;
	CTextBox * campaignDescription, * mapDescription;
	std::vector<SCampPositions> campDescriptions;
	std::vector<CRegion *> regions;
	CRegion * highlightedRegion;
	CToggleGroup * bonuses;
	std::array<CAnimImage *, 5> diffPics; //pictures of difficulties, user-selectable (or not if campaign locks this)
	CButton * diffLb, * diffRb; //buttons for changing difficulty
	CAnimImage * sizes; //icons of map sizes
	std::shared_ptr<CAnimation> sFlags;

	// Data
	std::shared_ptr<CCampaignState> ourCampaign;
	int selectedMap;
	boost::optional<int> selectedBonus;
	StartInfo startInfo;
	std::unique_ptr<CMapHeader> ourHeader;
};

class CPrologEpilogVideo : public CWindowObject
{
	CCampaignScenario::SScenarioPrologEpilog spe;
	int positionCounter;
	int voiceSoundHandle;
	std::function<void()> exitCb;

	CMultiLineLabel * text;

public:
	CPrologEpilogVideo(CCampaignScenario::SScenarioPrologEpilog _spe, std::function<void()> callback);

	void clickLeft(tribool down, bool previousState) override;
	void show(SDL_Surface * to) override;
};
