#pragma once

#include "App.hpp"
#include "NewPlayer.hpp"
#include "Vortex.hpp"
#include <algorithm>
#include <cstdlib>
#include <optional>

/*
* bfanew: the classic Arena (CLAUDE.md "Arena"). Battles run on the client between two squads the
* server hands over as the NPC JSON strings of ArenaBattleEnemyInfo / ArenaBattlePartyInfo
* (net/arena.kdl documents every key); the server keeps the record (player_arena: points,
* counts, the battle in progress and its opponent), hands out opponents (other players' arena
* squads, NPC summoners built from the Rare Summon pool when there are not enough), spends the
* arena orbs (UserTeamInfo fight_point, one per battle, one back every DefineMst recover_time_fight
* seconds), scores the result and pays the rank-up rewards of arena_rank.json.
*/
namespace Arena
{
	constexpr int kWinBase = 20;   //!< ABP for a win, plus up to +20 against a stronger opponent, down to +10 against a weaker one
	constexpr int kLoss = 10;      //!< ABP lost on a defeat (never below 0)
	constexpr int kKarmaWin = 300; //!< karma per rank for a win
	constexpr int kKarmaLose = 100;

	inline int32_t RecoverSeconds()
	{
		const auto v = theServer()->cache().initializeResp().defines.recover_time_fight;
		return v > 0 ? v : 1800;
	}

	inline const ArenaRankMst* RankFor(int32_t points)
	{
		const ArenaRankMst* best = nullptr;
		for (const auto& r : theServer()->cache().initializeResp().arena_ranks)
			if (points >= r.rank_point_start && (!best || r.rank_point_start >= best->rank_point_start))
				best = &r;
		return best;
	}

	inline int32_t RankId(int32_t points) { const auto* r = RankFor(points); return r ? r->id : 1; }

	inline const ArenaRankMst* RankById(int32_t id)
	{
		for (const auto& r : theServer()->cache().initializeResp().arena_ranks)
			if (r.id == id) return &r;
		return nullptr;
	}

	// the arena orbs: one back every RecoverSeconds while below the maximum
	inline void Regen(Db::Player& p, Db::ArenaState& st, int64_t now)
	{
		const int32_t max = p.team.max_fight_point;
		if (max <= 0) { p.team.fight_rest_timer = 0; return; }
		if (st.fight_at <= 0 || p.team.fight_point >= max)
		{
			st.fight_at = now;
			if (p.team.fight_point > max) p.team.fight_point = max;
			p.team.fight_rest_timer = 0;
			return;
		}
		const int64_t per = RecoverSeconds();
		const int64_t gained = (now - st.fight_at) / per;
		if (gained > 0)
		{
			p.team.fight_point = static_cast<int32_t>(std::min<int64_t>(max, p.team.fight_point + gained));
			st.fight_at += gained * per;
		}
		if (p.team.fight_point >= max) { st.fight_at = now; p.team.fight_rest_timer = 0; }
		else p.team.fight_rest_timer = static_cast<int32_t>(per - (now - st.fight_at));
	}

	inline UserArenaInfo Info(const Db::Player& p, const Db::ArenaState& st)
	{
		UserArenaInfo i = {};
		i.user_id = p.user_id;
		i.arena_rank_id = RankId(st.rank_point);
		i.rank_point = st.rank_point;
		i.battle_cnt = st.battle_cnt;
		i.win_cnt = st.win_cnt;
		i.battle_cnt_accept = st.accept_cnt;
		i.win_cnt_accept = st.accept_win;
		i.max_arena_rank_id = std::to_string(std::max(st.max_rank, i.arena_rank_id));
		i.current_position = 1;
		i.battle_id = st.battle_id;
		return i;
	}

	// ------------------------------------------------------------------ the NPC JSON strings
	inline std::string Esc(const std::string& s)
	{
		std::string o;
		o.reserve(s.size() + 8);
		for (const char c : s)
		{
			switch (c)
			{
			case '"': o += "\\\""; break;
			case '\\': o += "\\\\"; break;
			case '\n': o += "\\n"; break;
			case '\r': break;
			default: if (static_cast<unsigned char>(c) >= 0x20) o += c;
			}
		}
		return o;
	}

	inline std::string Kv(const char* k, const std::string& v, bool last = false)
	{
		return std::string("\"") + k + "\":\"" + Esc(v) + (last ? "\"" : "\",");
	}
	inline std::string Kv(const char* k, int64_t v, bool last = false) { return Kv(k, std::to_string(v), last); }

	inline std::string TeamJson(const std::string& userId, int32_t lv, const std::string& message)
	{
		return "{" + Kv("P_USER_ID", userId) + Kv("P_LV", lv) + Kv("P_FRIEND_MESSAGE", message) + Kv("P_GUILD_ID", "0", true) + "}";
	}

	inline std::string UnitJson(const UserUnitInfo& u)
	{
		return "{" + Kv("P_USER_UNIT_ID", u.user_unit_id) + Kv("P_UNIT_ID", u.unit_id) + Kv("P_UNIT_TYPE_ID", u.unit_type_id)
			+ Kv("P_LV", u.unit_lv) + Kv("P_HP", u.base_hp) + Kv("P_ATK", u.base_atk) + Kv("P_DEF", u.base_def) + Kv("P_HEL", u.base_heal)
			+ Kv("P_SKILL_ID", u.skill_id) + Kv("P_SKILL_LV", std::max(1, u.skill_lv)) + Kv("P_EXTRA_SKILL_ID", u.extra_skill_id) + Kv("P_EXTRA_SKILL_LV", u.extra_skill_lv)
			+ Kv("P_EXTRA_PASSIVE_SKILL_ID", 0) + Kv("P_EXTRA_PASSIVE_SKILL_ID2", 0)
			+ Kv("P_ADD_HP", u.add_hp) + Kv("P_EXT_HP", u.ext_hp) + Kv("P_ADD_ATK", u.add_atk) + Kv("P_EXT_ATK", u.ext_atk)
			+ Kv("P_ADD_DEF", u.add_def) + Kv("P_EXT_DEF", u.ext_def) + Kv("P_ADD_HEL", u.add_heal) + Kv("P_EXT_HEL", u.ext_heal)
			+ Kv("P_EQP_ITEM_ID", u.equipitem_id) + Kv("P_EQP_ITEM_ID2", u.equipitem_id2) + Kv("P_IMG_TYPE", 0) + Kv("P_FE_INFO", "") + Kv("P_OMNI_LV", 0, true) + "}";
	}

	struct Squad
	{
		std::string party;      //!< NPC party JSON
		std::string units;      //!< NPC units JSON
		int32_t leader_unit = 0;
		int count = 0;
	};

	// a player's squad from the stored decks and box: deck type 1, number deckNum (the active squad when it is empty)
	inline Squad SquadOf(const std::vector<UserPartyDeckInfo>& decks, int32_t deckNum, int32_t fallbackDeck, const std::vector<UserUnitInfo>& units)
	{
		std::unordered_map<int32_t, const UserUnitInfo*> byId;
		for (const auto& u : units) byId[u.user_unit_id] = &u;
		auto build = [&](int32_t num) {
			Squad s;
			std::vector<const UserPartyDeckInfo*> slots;
			for (const auto& d : decks)
				if (d.deck_type == 1 && d.deck_num == num && byId.count(d.user_unit_id)) slots.push_back(&d);
			std::sort(slots.begin(), slots.end(), [](auto* a, auto* b) { return a->disp_order < b->disp_order; });
			std::string party = "[", unitsJson = "[";
			for (const auto* d : slots)
			{
				const auto* u = byId[d->user_unit_id];
				if (s.count) { party += ","; unitsJson += ","; }
				party += "{" + Kv("P_USER_UNIT_ID", d->user_unit_id) + Kv("P_MEMBER_TYPE", d->member_type) + Kv("P_DISPORDER", d->disp_order, true) + "}";
				unitsJson += UnitJson(*u);
				if (d->member_type == 0 || !s.leader_unit) s.leader_unit = u->unit_id;
				s.count++;
			}
			s.party = party + "]";
			s.units = unitsJson + "]";
			return s;
		};
		auto s = build(deckNum);
		if (!s.count && fallbackDeck != deckNum) s = build(fallbackDeck);
		return s;
	}

	// ------------------------------------------------------------------ opponents
	struct Opponent
	{
		std::string user_id;
		std::string handle_name;
		int32_t lv = 1;
		int32_t rank_point = 0;
		int32_t battle_cnt = 0;
		int32_t win_cnt = 0;
		int32_t leader_unit = 0;
		std::string team;   //!< NPC team JSON
		std::string party;  //!< NPC party JSON
		std::string units;  //!< NPC units JSON
		bool npc = false;
	};

	inline std::string ToJson(const Opponent& o) { std::string s; (void)glz::write_json(o, s); return s; }
	inline std::optional<Opponent> FromJson(const std::string& s)
	{
		Opponent o;
		if (s.empty() || glz::read<glz::opts{.error_on_unknown_keys = false}>(o, s)) return std::nullopt;
		return o;
	}

	inline uint64_t Mix(uint64_t x) { x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL; x ^= x >> 33; return x; }

	// an NPC summoner: five Rare Summon units at about the player's level with about the player's
	// points (the match-making of the original), the same squad for a day
	inline std::optional<Opponent> Npc(int index, const Db::Player& me, int32_t myPoints, int64_t now)
	{
		static const char* kNames[] = { "Gladiator Kevin", "Knight Lorand", "Summoner Lugina", "Priestess Elimo", "Mage Rina", "Guardian Darvan",
			"Swordsman Owen", "Ranger Lario", "Lancer Weiss", "Sorceress Lemia", "Pirate Verica", "Archer Nemia" };
		const auto& cache = theServer()->cache();
		const auto* pool = cache.pool(2000);
		if (!pool) return std::nullopt;
		std::vector<int32_t> ids;
		for (const auto& [rarity, list] : pool->units) ids.insert(ids.end(), list.begin(), list.end());
		if (ids.empty()) return std::nullopt;
		const int64_t day = now / 86400;
		const uint64_t seed = Mix(static_cast<uint64_t>(day) * 131 + index * 7919 + 17);
		Opponent o;
		o.npc = true;
		o.user_id = "npc" + std::to_string(index);
		o.handle_name = kNames[index % (sizeof kNames / sizeof *kNames)];
		const int32_t spread = static_cast<int32_t>(Mix(seed + 1) % 7) - 3;
		o.lv = std::max(1, me.team.level + spread);
		o.rank_point = std::max(0, myPoints + static_cast<int32_t>(Mix(seed + 2) % 200) - 60);
		o.battle_cnt = 20 + static_cast<int32_t>(Mix(seed + 3) % 200);
		o.win_cnt = o.battle_cnt / 2 + static_cast<int32_t>(Mix(seed + 4) % (o.battle_cnt / 2 + 1));
		std::string party = "[", units = "[";
		for (int slot = 0; slot < 5; slot++)
		{
			const int32_t unitId = ids[Mix(seed + 10 + slot) % ids.size()];
			const auto* def = cache.unit(unitId);
			if (!def) continue;
			UserUnitInfo u = NewPlayer::UnitFromTable(unitId, *def, 1 + static_cast<int32_t>(Mix(seed + 20 + slot) % 5));
			u.user_unit_id = 9000000 + index * 10 + slot;
			NewPlayer::SetUnitLevel(u, *def, std::min(def->max_level, std::max(1, o.lv)));
			if (slot) { party += ","; units += ","; }
			party += "{" + Kv("P_USER_UNIT_ID", u.user_unit_id) + Kv("P_MEMBER_TYPE", slot == 0 ? 0 : 1) + Kv("P_DISPORDER", slot, true) + "}";
			units += UnitJson(u);
			if (slot == 0) o.leader_unit = unitId;
		}
		o.party = party + "]";
		o.units = units + "]";
		o.team = TeamJson(o.user_id, o.lv, "Let's have a good fight!");
		return o;
	}

	// another registered player as an opponent (nullopt when their arena squad is empty)
	inline drogon::Task<std::optional<Opponent>> Real(const Db::Player& other)
	{
		const auto decks = co_await Db::LoadDecks(other.user_id);
		const auto units = co_await Db::LoadUnits(other.user_id);
		const auto squad = SquadOf(decks, other.team.arena_deck_num, other.team.active_deck, units);
		if (!squad.count) co_return std::nullopt;
		const auto st = co_await Db::LoadArena(other.user_id);
		Opponent o;
		o.user_id = other.user_id;
		o.handle_name = other.handle_name;
		o.lv = other.team.level;
		o.rank_point = st.rank_point;
		o.battle_cnt = st.battle_cnt;
		o.win_cnt = st.win_cnt;
		o.leader_unit = squad.leader_unit;
		o.party = squad.party;
		o.units = squad.units;
		o.team = TeamJson(other.user_id, other.team.level, other.team.friend_message);
		co_return o;
	}

	// an opponent for this player: a random other player with an arena squad, else an NPC;
	// never the one just shown (avoid) when there is a choice
	inline drogon::Task<Opponent> Pick(const Db::Player& me, int32_t myPoints, const std::string& avoid, int64_t now)
	{
		std::vector<Opponent> cands;
		for (const auto& other : co_await Db::ListOtherPlayers(me.user_id, 50))
			if (auto o = co_await Real(other); o && o->user_id != avoid)
				cands.push_back(*o);
		if (cands.size() < 3)
			for (int i = 0; i < 6 && cands.size() < 6; i++)
				if (auto o = Npc(i, me, myPoints, now); o && o->user_id != avoid)
					cands.push_back(*o);
		if (cands.empty())
			co_return *Npc(0, me, myPoints, now);
		const auto pick = Mix(static_cast<uint64_t>(now) * 2654435761ULL + me.user_id.size()) % cands.size();
		co_return cands[pick];
	}

	inline ArenaSuggestEnemy Suggest(const Opponent& o)
	{
		ArenaSuggestEnemy s = {};
		s.user_id = o.user_id;
		s.handle_name = o.handle_name;
		s.lv = o.lv;
		s.arena_rank_id = RankId(o.rank_point);
		s.unit_id = std::to_string(o.leader_unit);
		s.battle_cnt = o.battle_cnt;
		s.win_cnt = o.win_cnt;
		s.battle_cnt_accept = 0;
		s.win_cnt_accept = 0;
		s.rank_point = o.rank_point;
		s.friend_id = "";
		s.unit_img_type = 0;
		return s;
	}

	inline ArenaBattleEnemyInfo EnemyInfo(const Opponent& o, int32_t battleId)
	{
		ArenaBattleEnemyInfo e = {};
		e.user_id = o.user_id;
		e.battle_id = battleId;
		e.team_info = o.team;
		e.party_info = o.party;
		e.unit_info = o.units;
		e.friend_message = o.npc ? "Let's have a good fight!" : "";
		e.rule = 0;
		return e;
	}

	// ------------------------------------------------------------------ the statistics (Record screen)
	inline UserTeamArenaArchive Archive(const Db::Player& p, const Db::ArenaState& st)
	{
		UserTeamArenaArchive a = {};
		if (!st.archive.empty() && glz::read<glz::opts{.error_on_unknown_keys = false}>(a, st.archive))
			a = {};
		a.user_id = p.user_id;
		a.atk_series_win_cnt = st.series_win;
		a.max_atk_series_win_cnt = st.max_series;
		if (a.sum_damage.empty()) a.sum_damage = "0";
		if (a.sum_heal.empty()) a.sum_heal = "0";
		return a;
	}

	inline std::string AddBig(const std::string& a, const std::string& b)
	{
		return std::to_string(std::atoll(a.c_str()) + std::atoll(b.c_str()));
	}

	// adds a battle's log to the statistics
	inline void Accumulate(UserTeamArenaArchive& a, const ArenaBattleEndParam& log, bool win, int32_t pointsWon)
	{
		a.arena_defeat_unit_cnt += log.defeat_unit_cnt;
		a.sum_all_knockdown_cnt += log.defeat_unit_cnt;
		a.sum_bcrystal_cnt += log.bc;
		a.sum_hcrystal_cnt += log.hc;
		if (win && log.timeout) a.sum_timeout_win_cnt++;
		if (win && log.perfect) a.sum_perfect_win_cnt++;
		a.sum_arena_point += std::max(0, pointsWon);
		a.sum_damage = AddBig(a.sum_damage, log.sum_damage);
		a.sum_heal = AddBig(a.sum_heal, log.sum_heal);
		a.sum_overkill_attack_cnt += log.overkill_cnt;
		const auto& cache = theServer()->cache();
		size_t pos = 0;
		while (pos < log.defeat_unit_ids.size())
		{
			const auto next = log.defeat_unit_ids.find(',', pos);
			const auto tok = log.defeat_unit_ids.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
			pos = next == std::string::npos ? log.defeat_unit_ids.size() : next + 1;
			if (tok.empty()) continue;
			const auto* def = cache.unit(std::atoi(tok.c_str()));
			if (!def) continue;
			switch (def->element)
			{
			case 1: a.sum_defeat_fire_unit_cnt++; break;
			case 2: a.sum_defeat_water_unit_cnt++; break;
			case 3: a.sum_defeat_tree_unit_cnt++; break;
			case 4: a.sum_defeat_thunder_unit_cnt++; break;
			case 5: a.sum_defeat_saint_unit_cnt++; break;
			case 6: a.sum_defeat_dark_unit_cnt++; break;
			}
			switch (def->rarity)
			{
			case 1: a.sum_defeat_rare1_unit_cnt++; break;
			case 2: a.sum_defeat_rare2_unit_cnt++; break;
			case 3: a.sum_defeat_rare3_unit_cnt++; break;
			case 4: a.sum_defeat_rare4_unit_cnt++; break;
			case 5: a.sum_defeat_rare5_unit_cnt++; break;
			case 6: a.sum_defeat_rare6_unit_cnt++; break;
			case 7: a.sum_defeat_rare7_unit_cnt++; break;
			case 8: a.sum_defeat_rare8_unit_cnt++; break;
			}
		}
	}

	inline std::string ArchiveJson(const UserTeamArenaArchive& a) { std::string s; (void)glz::write_json(a, s); return s; }

	inline int32_t WinPoints(int32_t myPts, int32_t enemyPts)
	{
		return kWinBase + std::clamp((enemyPts - myPts) / 50, -10, 20);
	}
}
