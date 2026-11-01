#include "App.hpp"
#include "Handlers.hpp"
#include "TownState.hpp"
#include "Vortex.hpp"
#include "Keys.hpp"
#include "Arena.hpp"
#include "Presents.hpp"
#include "Gifts.hpp"
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
	resp.login_info.friend_id = p.account_id; // what the Links page appends to web pages (?friend_id=)
	resp.login_info.handle_name = p.handle_name;
	resp.login_info.tutorial_end_flag = p.tutorial_end;
	resp.login_info.tutorial_status = 0;
	resp.login_info.feature_gate = 0;
	resp.login_info.user_scenario_info = p.scenario;                 // story events watched (gme/DungeonEventUpdate.cpp)
	resp.login_info.user_special_scenario_info = p.special_scenario;

	p.team.user_id = p.user_id;
	{
		// bfanew: friends (gme/Friends.hpp): the login time friends see, the requests waiting
		co_await Db::TouchLogin(p.user_id, Vortex::Now());
		p.team.friend_agree_count = 0;
		for (const auto& f : co_await Db::LoadFriends(p.user_id))
			if (f.state == 2) p.team.friend_agree_count++;
	}
	{
		// bfanew: the daily login bonus, a present in the Gift Box on the first login of a game day
		// (gme/Presents.hpp), and the badge count of the box
		const int64_t now = Vortex::Now();
		const int64_t today = Presents::GameDay(now);
		if (p.bonus_day != today)
		{
			p.bonus_streak = (p.bonus_day == today - 1) ? p.bonus_streak + 1 : 1;
			p.bonus_day = today;
			const auto& table = Presents::LoginBonus();
			const auto& b = table[(p.bonus_streak - 1) % table.size()];
			co_await Db::AddPresent(p.user_id, b.type, b.target, b.count, b.text, now, now + Presents::kExpireDays * 86400);
			LOG_DEBUG << "UserInfo: login bonus day " << p.bonus_streak << " for " << p.user_id << ": " << b.text;
		}
		// gifts between friends (gme/Gifts.hpp): the want list every player has, the gifts waiting
		if (p.team.want_gift.empty()) p.team.want_gift = Gifts::kDefaultWants;
		resp.gift_info = co_await Gifts::Received(p.user_id);
		p.team.present_count = 0;
		for (const auto& r : co_await Db::LoadPresents(p.user_id))
			if (!r.received && (r.expires == 0 || r.expires > now))
			{
				p.team.present_count++;
				resp.present_info.push_back(Presents::Record(r, p.user_id)); // the box's list is read at login too
			}
	}
	{
		// bfanew: the Arena record (orbs regenerated here too, so the home screen shows them)
		auto arena = co_await Db::LoadArena(p.user_id);
		Arena::Regen(p, arena, Vortex::Now());
		co_await Db::SaveArena(p.user_id, arena);
		resp.arena_info = { Arena::Info(p, arena) };
		resp.arena_archive = { Arena::Archive(p, arena) };
	}
	resp.team_info = p.team;
	resp.unit_info = co_await Db::LoadUnits(p.user_id);
	resp.party_deck_info = co_await Db::LoadDecks(p.user_id);
	resp.clear_mission_info = co_await Db::LoadClears(p.user_id);
	resp.permit_place = co_await Db::LoadPermits(p.user_id);
	{
		// the Vortex entries open now (gimuserver/gme/Vortex.hpp), the player's parade windows
		// (dungeon keys, gimuserver/gme/Keys.hpp) and the keys, with the daily grant
		resp.dungeon_key_info = co_await Keys::Infos(p.user_id);
		auto parades = co_await Db::LoadParades(p.user_id);
		auto vortex = Vortex::Permits(Vortex::Now(), &parades);
		resp.permit_place.insert(resp.permit_place.end(), vortex.begin(), vortex.end());
	}
	resp.warehouse_info = co_await Db::LoadItems(p.user_id);
	resp.equip_info = co_await Db::LoadEquip(p.user_id);

	// town facilities / locations (stored levels, level 1 by default), the recipes the facility
	// levels release = the village's Synthesis List, and one collection record per location
	// (MyTownTopScene::downloadFiles dereferences the record of every location whose
	// need_mission_id is cleared; mission 11 -> location 3 crashed the Town tab) with the taps
	// left and their drops, refilled here when due (gimuserver/gme/TownState.hpp)
	{
		auto town = co_await TownState::Build(p, resp.clear_mission_info);
		resp.town_facility_info = std::move(town.facilities);
		resp.town_location_info = std::move(town.locations);
		resp.permit_receipes = std::move(town.recipes);
		resp.town_location_detail = std::move(town.details);
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
