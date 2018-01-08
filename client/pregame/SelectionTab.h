/*
 * SelectionTab.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CSelectionScreen.h"

enum ESortBy
{
	_playerAm, _size, _format, _name, _viccon, _loscon, _numOfMaps, _fileName
}; //_numOfMaps is for campaigns

/// Class which handles map sorting by different criteria
class mapSorter
{
public:
	ESortBy sortBy;
	bool operator()(const CMapInfo * aaa, const CMapInfo * bbb);
	mapSorter(ESortBy es) : sortBy(es){};
};

/// The selection tab which is shown at the map selection screen
class SelectionTab : public CIntObject
{
public:
	int positions; //how many entries (games/maps) can be shown
	CPicture * bg; //general bg image
	CSlider * slider;
	std::vector<CMapInfo> allItems;
	std::vector<CMapInfo *> curItems;
	size_t selectionPos;
	std::function<void(CMapInfo *)> onSelect;

	ESortBy sortingBy;
	ESortBy generalSortingBy;
	bool ascending;

	CTextInput * txt;

	SelectionTab(CMenuScreen::EState Type, const std::function<void(CMapInfo *)> & OnSelect, CMenuScreen::EGameMode GameMode = CMenuScreen::MULTI_NETWORK_HOST);
	~SelectionTab();

	void showAll(SDL_Surface * to) override;
	void clickLeft(tribool down, bool previousState) override;
	void keyPressed(const SDL_KeyboardEvent & key) override;
	void onDoubleClick() override;

	void filter(int size, bool selectFirst = false); //0 - all
	void sortBy(int criteria);
	void sort();
	void select(int position); //position: <0 - positions>  position on the screen
	void selectAbs(int position); //position: absolute position in curItems vector
	void sliderMove(int slidPos);
	void printMaps(SDL_Surface * to);
	int getLine();
	void selectFName(std::string fname);
	const CMapInfo * getSelectedMapInfo() const;

private:
	std::shared_ptr<CAnimation> formatIcons;
	CMenuScreen::EState tabType;

	void parseMaps(const std::unordered_set<ResourceID> & files);
	void parseGames(const std::unordered_set<ResourceID> & files, CMenuScreen::EGameMode gameMode);
	void parseCampaigns(const std::unordered_set<ResourceID> & files);
	std::unordered_set<ResourceID> getFiles(std::string dirURI, int resType);
};
