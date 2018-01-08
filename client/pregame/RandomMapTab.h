/*
 * RandomMapTab.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CSelectionScreen.h"

/// The random map tab shows options for generating a random map.
class RandomMapTab : public CIntObject
{
public:
	RandomMapTab();

	void showAll(SDL_Surface * to) override;
	void updateMapInfo();
	CFunctionList<void(const CMapInfo *)> & getMapInfoChanged();
	const CMapInfo * getMapInfo() const;
	const CMapGenOptions & getMapGenOptions() const;
	void setMapGenOptions(std::shared_ptr<CMapGenOptions> opts);

private:
	void addButtonsWithRandToGroup(CToggleGroup * group, const std::vector<std::string> & defs, int startIndex, int endIndex, int btnWidth, int helpStartIndex, int helpRandIndex) const;
	void addButtonsToGroup(CToggleGroup * group, const std::vector<std::string> & defs, int startIndex, int endIndex, int btnWidth, int helpStartIndex) const;
	void deactivateButtonsFrom(CToggleGroup * group, int startId);
	void validatePlayersCnt(int playersCnt);
	void validateCompOnlyPlayersCnt(int compOnlyPlayersCnt);
	std::vector<int> getPossibleMapSizes();

	CPicture * bg;
	CToggleButton * twoLevelsBtn;
	CToggleGroup * mapSizeBtnGroup, * playersCntGroup, * teamsCntGroup, * compOnlyPlayersCntGroup,
		* compOnlyTeamsCntGroup, * waterContentGroup, * monsterStrengthGroup;
	CButton * showRandMaps;
	CMapGenOptions mapGenOptions;
	std::unique_ptr<CMapInfo> mapInfo;
	CFunctionList<void(const CMapInfo *)> mapInfoChanged;
};
