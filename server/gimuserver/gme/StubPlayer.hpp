#pragma once

/*
* bfanew: the single hard-coded player every client currently gets (no persistence yet).
* Shared by UserInfo and GachaAction so that both answer with the same unit box, because
* the client replaces its whole unit list with any "4ceMWH6k" array it receives
* (UserUnitInfoResponse::readParam clears UserUnitInfoList first).
*
* Goes away with step 4 of CLAUDE.md (accounts and persistence).
*/

namespace StubPlayer
{
	constexpr const char* kUserId = "0839899613932562";
	constexpr const char* kAccountId = "12345678";
	constexpr const char* kHandleName = "OfflineMod!";

	/*! The placeholder unit upstream gives every player (user_unit_id 100, referenced by the decks). */
	inline UserUnitInfo StarterUnit()
	{
		UserUnitInfo d = {};
		d.user_id = kUserId;
		d.user_unit_id = 100;
		d.unit_type_id = 1;
		d.element = "fire";
		d.base_hp = 1000;  d.add_hp = 1001;  d.ext_hp = 1002;
		d.base_def = 1100; d.add_def = 1101; d.ext_def = 1102;
		d.base_heal = 1200; d.add_heal = 1201; d.ext_heal = 1202;
		d.base_atk = 1300; d.add_atk = 1301; d.ext_atk = 1302;
		d.limit_over_atk = 1400; d.limit_over_def = 1401; d.limit_over_heal = 1402; d.limit_over_hp = 1403;
		d.unit_lv = 1;
		d.new_flag = 1;
		d.ext_count = 1500;
		d.fe_bp = 100; d.fe_used_bp = 0; d.fe_max_usable_bp = 200;
		d.unit_img_type = 0;
		d.exp = 1;
		d.total_exp = 1;
		d.unit_id = 50253;
		return d;
	}

	/*! Wallet and counters from config.json "server" (initial_* keys). */
	inline void FillTeam(UserTeamInfo& t, const ServerConfig& scfg)
	{
		t.user_id = kUserId;
		// "TwqMChon" is serialised as "a,b,c": the squad numbers used as helper squads 1/2/3.
		// The client splits it and StrToInt()s every piece (an empty string is a null read,
		// crash seen in GachaAction), and equal numbers give the "same unit placed in all
		// Helper Squads" caution when leaving Manage Squad.
		t.reinforcement_deck = { 0, 1, 2 };
		t.level = 1;
		// from the level table (user_level.json): cost limit shown in Manage Squad ("COST
		// x/limit"), energy (15 at level 1, "Energy x/max" on the home screen; the mission
		// list refuses a quest with "You currently don't have enough energy" at 0/0), arena
		// orbs and friend slots. A new player starts full.
		t.deck_cost = 0;
		for (const auto& lv : theServer()->cache().initializeResp().progression)
			if (lv.level == t.level)
			{
				t.deck_cost = lv.deck_cost;
				t.max_action_point = t.action_point = lv.action_points;
				t.max_fight_point = t.fight_point = lv.unk; // "YS2JG9no": arena orbs (3)
				t.max_friend_count = lv.friend_count;
				break;
			}
		t.exp = 0;
		t.warehouse_count = 100;
		t.add_unit_count = 100;
		t.max_unit_count = 100;
		t.zel = scfg.initialZel;
		t.karma = scfg.initialKarma;
		t.brave_coin = scfg.initialBraveCoins;
		t.free_gems = scfg.initialFreeGems;
		t.paid_gems = 0;
		t.friend_point = scfg.initialFriendPoints;
	}

	/*!
	* Quest progress of a brand-new player who just finished the tutorial.
	*
	* "yXNM8kL3" PermitPlace: the places the player may enter. The client shows ONLY what is in
	* this list (AreaMstList::getActiveList filters on it, DungeonSelectScene2::isLockDungeon
	* checks the enterable flag), and with an empty list DungeonSelectScene2::initialize finds
	* no area and fades straight back to the home screen. Each record carries exactly one id.
	* Ids from the 2022 tables (mst-decoded.zip): gate 1 "Grand Gaia", land 1 "Mistral",
	* area 100 "Mistral", dungeon 1 = tutorial (missions 1, 2), dungeon 10 = first real dungeon
	* (missions 10, 11, 12; mission 10 requires mission 2).
	*
	* "UT1SVg59" UserClearMissionInfo: cleared missions. The tutorial missions count as cleared
	* so that requiredMissionIds checks pass.
	*
	* STUB: a real server derives both lists from the player's clear history (step 4).
	*/
	inline void FillQuestProgress(UserInfoResp& resp)
	{
		auto permit = [&](auto setter)
		{
			PermitPlace p = {};
			p.time_limit = 0;
			p.quest_bonus_type = 0;
			p.quest_bonus_rate = "0";
			p.quest_bonus_time_limit = 0;
			p.period_text = "";
			p.enterable_flg = true;
			setter(p);
			resp.permit_place.emplace_back(std::move(p));
		};
		permit([](PermitPlace& p) { p.gate_id = "1"; });
		permit([](PermitPlace& p) { p.land_id = "1"; });
		permit([](PermitPlace& p) { p.area_id = "100"; });
		permit([](PermitPlace& p) { p.dungeon_id = "1"; });
		permit([](PermitPlace& p) { p.dungeon_id = "10"; });
		for (const char* m : { "1", "2", "10" })
			permit([m](PermitPlace& p) { p.mission_id = m; });

		for (int32_t m : { 1, 2 })
		{
			UserClearMissionInfo c = {};
			c.user_id = kUserId;
			c.mission_id = m;
			c.clear_date = "2013-11-01 00:00:00";
			c.clear_cnt = 1;
			c.most_time = 0;
			c.get_bc = 0;
			c.get_hc = 0;
			resp.clear_mission_info.emplace_back(c);
		}
	}
}
