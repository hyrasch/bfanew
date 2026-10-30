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
		// cost limit shown in Manage Squad ("COST x/limit"): from the level table
		t.deck_cost = 0;
		for (const auto& lv : theServer()->cache().initializeResp().progression)
			if (lv.level == t.level) { t.deck_cost = lv.deck_cost; break; }
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
}
