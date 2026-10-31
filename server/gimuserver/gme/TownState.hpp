#pragma once

#include "App.hpp"
#include <unordered_set>

/*
* The player's town as the client wants it (UserInfo and the TownFacilityUpdate reply):
* - YRgx49WG UserTownFacilityInfo: one per facility whose need_mission_id is 0 or cleared, with the
*   stored level (1 when never upgraded) and the karma injected toward the next level;
* - yj46Q2xw UserTownLocationInfo: the same per town location;
* - 51yQrDBR PermitReceipe: the recipes released by every facility level up to the current one
*   (TownFacilityLvMst.release_receipe), which is exactly the village's Synthesis List.
* Facility upgrades are paid in karma on the Upgrades screen (MyTownFacilityExtScene2), never in
* gems (see CLAUDE.md "Free economy").
*/
namespace TownState
{
	struct Lists
	{
		std::vector<UserTownFacilityInfo> facilities;
		std::vector<UserTownLocationInfo> locations;
		std::vector<PermitReceipe> recipes;
	};

	inline drogon::Task<Lists> Build(const Db::Player& p, const std::vector<UserClearMissionInfo>& clears)
	{
		Lists out;
		std::unordered_set<int32_t> cleared;
		for (const auto& c : clears) cleared.insert(c.mission_id);
		const auto& init = theServer()->cache().initializeResp();
		const auto facilities = co_await Db::LoadTown(p.user_id, 1);
		const auto locations = co_await Db::LoadTown(p.user_id, 2);

		std::unordered_set<int32_t> seen;
		for (const auto& f : init.town_facility)
		{
			if (f.need_mission_id != 0 && !cleared.contains(f.need_mission_id)) continue;
			const auto it = facilities.find(f.id);
			UserTownFacilityInfo fi = {};
			fi.user_id = p.user_id;
			fi.facility_id = f.id;
			fi.lv = it != facilities.end() ? it->second.lv : 1;
			fi.karma = it != facilities.end() ? it->second.karma : 0;
			out.facilities.emplace_back(fi);
			for (const auto& lv : init.town_facility_lv)
			{
				if (lv.id != f.id || lv.lv > fi.lv) continue;
				for (const auto rid : lv.release_receipe)
				{
					if (!seen.insert(rid).second) continue;
					PermitReceipe pr = {};
					pr.recipe_id = rid;
					pr.cnt = 0;     // creations done (hides the recipe once >= its limit, unless -1)
					pr.limit = -1;  // no time limit (0 reads as expired)
					out.recipes.emplace_back(pr);
				}
			}
		}
		for (const auto& l : init.town_location)
		{
			if (l.need_mission_id != 0 && !cleared.contains(l.need_mission_id)) continue;
			const auto it = locations.find(l.id);
			UserTownLocationInfo li = {};
			li.user_id = p.user_id;
			li.location_id = l.id;
			li.lv = it != locations.end() ? it->second.lv : 1;
			li.karma = it != locations.end() ? it->second.karma : 0;
			out.locations.emplace_back(li);
		}
		co_return out;
	}
}
