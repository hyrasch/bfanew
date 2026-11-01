#pragma once

#include <chrono>
#include <utility>
#include <vector>

/*
* bfanew: what a brand-new player gets (Db::PlayerForAccount creates the account from this).
* Phase 1: a player who just finished the (skipped) tutorial in November 2013.
*/
namespace NewPlayer
{
	constexpr const char* kHandleName = "Summoner";

	/*! The placeholder unit upstream gives every player (referenced by the decks). */
	inline UserUnitInfo StarterUnit()
	{
		UserUnitInfo d = {};
		d.user_unit_id = 0; // assigned by the database
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

	/*! A fresh level-1 unit of the unit table (deploy/units.json), user_unit_id unset. */
	inline UserUnitInfo UnitFromTable(int32_t unitId, const UnitDef& def, int32_t unitType = 1, double roll = 0.5)
	{
		static constexpr const char* kElements[] = { "", "fire", "water", "earth", "thunder", "light", "dark" };
		UserUnitInfo d = {};
		d.unit_id = unitId;
		d.unit_type_id = unitType; // 1 Lord, 2 Anima, 3 Breaker, 4 Guardian, 5 Oracle (not the rarity)
		d.unit_lv = 1;
		d.exp = 0;
		d.total_exp = 0;
		d.base_hp = def.hp.empty() ? 1 : def.hp[0];
		d.base_atk = def.atk.empty() ? 1 : def.atk[0];
		d.base_def = def.def_.empty() ? 1 : def.def_[0];
		d.base_heal = def.rec.empty() ? 1 : def.rec[0];
		d.element = (def.element >= 1 && def.element <= 6) ? kElements[def.element] : "fire";
		d.leader_skill_id = def.ls_id;
		d.skill_id = def.bb_id;
		d.skill_lv = 1;
		d.extra_skill_id = def.es_id;
		d.new_flag = true;
		d.receive_date = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
		Units::ApplyType(d, def, roll);
		return d;
	}

	/*! Puts a unit at a level: exp from the unit's exp pattern, stats interpolated (Units::StatAt). */
	inline void SetUnitLevel(UserUnitInfo& u, const UnitDef& def, int32_t lv, double roll = 0.5)
	{
		const auto& cache = theServer()->cache();
		lv = std::max<int32_t>(1, std::min<int32_t>(lv, def.max_level));
		u.unit_lv = lv;
		u.total_exp = static_cast<int32_t>(cache.expForLevel(def.exp_pattern, lv));
		u.exp = 0;
		u.base_hp = Units::StatAt(def.hp, lv, def.max_level);
		u.base_atk = Units::StatAt(def.atk, lv, def.max_level);
		u.base_def = Units::StatAt(def.def_, lv, def.max_level);
		u.base_heal = Units::StatAt(def.rec, lv, def.max_level);
		Units::ApplyType(u, def, roll);
	}

	/*! Fills the level-dependent fields of a team from the level table (user_level.json). */
	inline void ApplyLevel(UserTeamInfo& t, int32_t level, bool refill)
	{
		for (const auto& lv : theServer()->cache().initializeResp().progression)
			if (lv.level == level)
			{
				t.level = level;
				// cost limit shown in Manage Squad ("COST x/limit")
				t.deck_cost = lv.deck_cost;
				// energy (15 at level 1, "Energy x/max" on the home screen), arena orbs, friend slots
				t.max_action_point = lv.action_points;
				t.max_fight_point = lv.unk; // "YS2JG9no": arena orbs (3)
				t.max_friend_count = lv.friend_count;
				if (refill)
				{
					t.action_point = t.max_action_point;
					t.fight_point = t.max_fight_point;
				}
				break;
			}
	}

	/*! Wallet and counters of a new player, from config.json "server" (initial_* keys). */
	inline void FillTeam(UserTeamInfo& t, const ServerConfig& scfg)
	{
		// "TwqMChon" is serialised as "a,b,c": the squad numbers used as helper squads 1/2/3.
		// The client splits it and StrToInt()s every piece (an empty string is a null read),
		// and equal numbers give the "same unit placed in all Helper Squads" caution.
		t.reinforcement_deck = { 0, 1, 2 };
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
		ApplyLevel(t, static_cast<int32_t>(scfg.initialLevel), true);
	}

	/*! PermitPlace kinds as the client types them (PermitPlaceInfo::setType). */
	enum Place : int { Area = 1, Dungeon = 2, Mission = 3, Land = 4, Gate = 5 };

	/*! Appends one PermitPlace record (exactly one id key, see net/permit_place.kdl). */
	inline void AddPermit(std::vector<PermitPlace>& out, int kind, const std::string& id)
	{
		PermitPlace p = {};
		p.time_limit = 0;
		p.quest_bonus_type = 0;
		p.quest_bonus_rate = "0";
		p.quest_bonus_time_limit = 0;
		p.period_text = "";
		p.enterable_flg = true;
		switch (kind)
		{
		case Gate: p.gate_id = id; break;
		case Land: p.land_id = id; break;
		case Area: p.area_id = id; break;
		case Dungeon: p.dungeon_id = id; break;
		case Mission: p.mission_id = id; break;
		default: return;
		}
		out.emplace_back(std::move(p));
	}

	/*!
	* Where a new player may go: gate 1 "Grand Gaia", land 1 "Mistral", area 100, dungeon 1 (the
	* tutorial) and dungeon 10 with its first mission. Ids from the 2022 tables. The client shows
	* ONLY what is permitted (AreaMstList::getActiveList), and with nothing permitted the Quest
	* button falls straight back to the home screen.
	*/
	inline std::vector<std::pair<int, std::string>> Permits()
	{
		return {
			{ Gate, "1" }, { Land, "1" }, { Area, "100" },
			{ Dungeon, "1" }, { Dungeon, "10" },
			{ Mission, "1" }, { Mission, "2" }, { Mission, "10" },
		};
	}

	/*! The tutorial missions count as cleared so that requiredMissionIds checks pass. */
	inline std::vector<int32_t> ClearedMissions() { return { 1, 2 }; }
}
