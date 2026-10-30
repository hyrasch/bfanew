#include "App.hpp"
#include "Handlers.hpp"
#include <cstdlib>
#include <unordered_set>

/*
* UserInfo ("cTZ3W2JG"): the player's state after Initialize. Everything player-specific comes
* from the database (gimuserver/db/Player.hpp); the login-time tables come from the cache.
*/
HANDLEF(UserInfo)
{
	UserInfoReq req = {};
	const auto& ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(req, json);
	if (ec)
	{
		const auto& fmte = glz::format_error(ec, json);
		LOG_DEBUG << "Gme UserInfo Error during JSON read: " << fmte;
		co_return HandleResult::error("Deserialization error", fmte);
	}

	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player)
	{
		LOG_WARN << "UserInfo: unknown user " << req.login_info.user_id << " / gumi " << req.login_info.gumi_live_userid;
		co_return HandleResult::error("Unknown user", req.login_info.user_id);
	}
	auto& p = *player;

	UserInfoResp resp = theServer()->cache().userInfoResp(); // copy !!

	resp.login_info.user_id = p.user_id;
	resp.login_info.account_id = p.account_id;
	resp.login_info.handle_name = p.handle_name;
	resp.login_info.tutorial_end_flag = p.tutorial_end;
	resp.login_info.tutorial_status = 0;
	resp.login_info.feature_gate = 0;

	p.team.user_id = p.user_id;
	resp.team_info = p.team;
	resp.unit_info = co_await Db::LoadUnits(p.user_id);
	resp.party_deck_info = co_await Db::LoadDecks(p.user_id);
	resp.clear_mission_info = co_await Db::LoadClears(p.user_id);
	resp.permit_place = co_await Db::LoadPermits(p.user_id);
	resp.warehouse_info = co_await Db::LoadItems(p.user_id);
	resp.equip_info = co_await Db::LoadEquip(p.user_id);

	// one town location detail per location: MyTownTopScene::downloadFiles dereferences the record of
	// every location whose need_mission_id is cleared (mission 11 -> location 3 crashed the Town tab)
	resp.town_location_detail.clear();
	for (const auto& loc : theServer()->cache().initializeResp().town_location)
	{
		UserTownLocationDetail d = {};
		d.user_id = p.user_id;
		d.location_id = loc.id;
		d.tap_cnt = 0;
		resp.town_location_detail.emplace_back(d);
	}

	// town facilities: every facility whose need_mission_id is 0 or cleared is at level 1 (upgrades
	// are not implemented), and the recipes the village's Synthesis List offers (51yQrDBR
	// PermitReceipe, the only source of MyTownItemListScene::setRecipeList) are the union of
	// TownFacilityLvMst.release_receipe of those levels. Creations done are not tracked (always 0).
	{
		std::unordered_set<int32_t> cleared;
		for (const auto& c : resp.clear_mission_info) cleared.insert(c.mission_id);
		const auto& init = theServer()->cache().initializeResp();
		resp.town_facility_info.clear();
		resp.permit_receipes.clear();
		std::unordered_set<int32_t> seen;
		for (const auto& f : init.town_facility)
		{
			if (f.need_mission_id != 0 && !cleared.contains(f.need_mission_id)) continue;
			UserTownFacilityInfo fi = {};
			fi.user_id = p.user_id;
			fi.facility_id = f.id;
			fi.lv = 1;
			fi.karma = 0;
			resp.town_facility_info.emplace_back(fi);
			for (const auto& lv : init.town_facility_lv)
			{
				if (lv.id != f.id || lv.lv > 1) continue;
				for (const auto rid : lv.release_receipe)
				{
					if (!seen.insert(rid).second) continue;
					PermitReceipe pr = {};
					pr.recipe_id = rid;
					pr.cnt = 0;     // creations done (MyTownItemListScene skips a recipe once cnt >= its limit)
					pr.limit = -1;  // time limit: -1 none (0 would read as expired, getRemainingTime < 1)
					resp.permit_receipes.emplace_back(pr);
				}
			}
		}
	}

	// dev aid: BFANEW_BLANK=warehouse,equip,town,clears,permits,units,decks blanks lists to bisect client
	// crashes without rebuilding (used for the village crash of 2026-10-30)
	if (const char* blank = std::getenv("BFANEW_BLANK"))
	{
		const std::string b = blank;
		auto has = [&](const char* k) { return b.find(k) != std::string::npos; };
		if (has("warehouse")) resp.warehouse_info.clear();
		if (has("equip")) resp.equip_info.clear();
		if (has("town")) resp.town_location_detail.clear();
		if (has("clears")) resp.clear_mission_info.clear();
		if (has("permits")) resp.permit_place.clear();
		if (has("units")) resp.unit_info.clear();
		if (has("decks")) resp.party_deck_info.clear();
		LOG_WARN << "UserInfo: BFANEW_BLANK=" << b;
	}

	// phase 1: no login campaign (id 0 keeps the "Login Campaign" popup from showing)
	resp.campaign_info.current_day = 0;
	resp.campaign_info.total_days = 0;
	resp.campaign_info.first_for_the_day = false;
	resp.campaign_info.id = 0;

	resp.summoner_journal.user_id = p.user_id;
	resp.signal_key.key = "5EdKHavF";

	co_await Db::SavePlayer(p); // the energy regenerated since the last visit

	std::string buffer{};
	const auto& ec2 = glz::write_json(resp, buffer);
	if (ec2)
	{
		const auto& glze = glz::format_error(ec2, buffer);
		LOG_DEBUG << "Gme UserInfo Error during JSON writing: " << glze;
		co_return HandleResult::error("Serialization error", glze);
	}

	co_return HandleResult::success(buffer);
}
