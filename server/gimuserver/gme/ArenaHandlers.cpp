#include "App.hpp"
#include "Handlers.hpp"
#include "Arena.hpp"
#include "Friends.hpp"

/*
* bfanew: the Arena's requests (net/arena.kdl has the ids, keys, URLs and every field; the design
* is in Arena.hpp and CLAUDE.md "Arena"):
*   ArenaStart   zu6ifpy0  entering the Arena (ArenaTopConnectScene)   -> record + wallet
*   ArenaInfo    oim9TU1D  the same from the arena place select          -> record + wallet
*   ArenaMatching 82ztj6Iy the Battle button / Next: one opponent        -> ArenaSuggestEnemy
*   ArenaBattleStart 7f1rg92L Fight: spends an orb, opens the battle     -> both squads, record, wallet
*   ArenaBattleEnd oQNtKU51 the battle log: scores it                    -> result, rewards, record, wallet
*   ArenaRetire  eyg8sA32  giving up                                     -> record + wallet
*   ArenaBattleRestart NPyXDgV9 resuming a battle in progress            -> both squads again
*   ArenaFriendList xkd9H50w friends to fight (ArenaFriendListScene)     -> the friends with an arena squad
*   GetPlayerInfo vUQrAV65  the Record button (ArenaArchiveScene)        -> record + wallet + statistics
*/

namespace
{
	template <typename Req>
	drogon::Task<std::optional<Db::Player>> Load(const Req& req)
	{
		co_return co_await Db::LoadPlayer(req.login_info);
	}

	template <typename T>
	HandleResult Reply(const T& resp)
	{
		std::string buffer{};
		const auto& ec = glz::write_json(resp, buffer);
		if (ec)
			return HandleResult::error("Serialization error", glz::format_error(ec, buffer));
		return HandleResult::success(buffer);
	}

	// nullopt when the request parsed, else the error to return
	template <typename Req>
	std::optional<HandleResult> Read(Req& req, const std::string& json, const char* name)
	{
		const auto& ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(req, json);
		if (!ec) return std::nullopt;
		const auto& fmte = glz::format_error(ec, json);
		LOG_DEBUG << "Gme " << name << " Error during JSON read: " << fmte;
		return HandleResult::error("Deserialization error", fmte);
	}

	drogon::Task<HandleResult> StartLike(const std::string& json, const char* name)
	{
		ArenaStartReq req = {};
		if (auto err = Read(req, json, name)) co_return *err;
		auto player = co_await Db::LoadPlayer(req.login_info);
		if (!player) co_return HandleResult::error("Unknown user", req.login_info.user_id);
		auto& p = *player;
		auto st = co_await Db::LoadArena(p.user_id);
		const int64_t now = Vortex::Now();
		Arena::Regen(p, st, now);
		co_await Db::SaveArena(p.user_id, st);
		LOG_DEBUG << name << ": user " << p.user_id << " " << st.rank_point << " ABP, " << st.win_cnt << "/" << st.battle_cnt << ", orbs " << p.team.fight_point << "/" << p.team.max_fight_point << (st.battle_id.empty() ? "" : ", battle " + st.battle_id + " in progress");
		ArenaStartResp resp = {};
		resp.arena_info = { Arena::Info(p, st) };
		resp.team_info = p.team;
		resp.arena_archive = { Arena::Archive(p, st) };
		co_return Reply(resp);
	}

	drogon::Task<HandleResult> BattleData(Db::Player& p, Db::ArenaState& st, const Arena::Opponent& enemy, int32_t deckNum, const char* name)
	{
		const auto decks = co_await Db::LoadDecks(p.user_id);
		const auto units = co_await Db::LoadUnits(p.user_id);
		const auto mine = Arena::SquadOf(decks, deckNum, p.team.active_deck, units);
		if (!mine.count)
			co_return HandleResult::error("Your arena squad is empty", std::to_string(deckNum));
		const int32_t battleId = std::atoi(st.battle_id.c_str());
		ArenaBattleStartResp resp = {};
		resp.enemy_info = { Arena::EnemyInfo(enemy, battleId) };
		ArenaBattlePartyInfo party = {};
		party.party_info = mine.party;
		party.unit_info = mine.units;
		resp.party_info = { party };
		resp.arena_info = { Arena::Info(p, st) };
		resp.team_info = p.team;
		resp.arena_archive = { Arena::Archive(p, st) };
		LOG_DEBUG << name << ": user " << p.user_id << " battle " << st.battle_id << " vs " << enemy.handle_name << " (" << enemy.user_id << ", " << enemy.rank_point << " ABP), squads " << mine.count << " / " << (enemy.npc ? "npc" : "player");
		co_return Reply(resp);
	}
}

HANDLEF(ArenaStart) { co_return co_await StartLike(json, "ArenaStart"); }
HANDLEF(ArenaInfo) { co_return co_await StartLike(json, "ArenaInfo"); }
HANDLEF(GetPlayerInfo) { co_return co_await StartLike(json, "GetPlayerInfo"); }
HANDLEF(ArenaRetire)
{
	ArenaBattleEndReq req = {};
	if (auto err = Read(req, json, "ArenaRetire")) co_return *err;
	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player) co_return HandleResult::error("Unknown user", req.login_info.user_id);
	auto& p = *player;
	auto st = co_await Db::LoadArena(p.user_id);
	const int64_t now = Vortex::Now();
	Arena::Regen(p, st, now);
	LOG_DEBUG << "ArenaRetire: user " << p.user_id << " gives up battle " << st.battle_id;
	if (!st.battle_id.empty())
	{
		// a retreat counts as a defeat
		st.battle_cnt++;
		st.rank_point = std::max(0, st.rank_point - Arena::kLoss);
	}
	st.battle_id.clear();
	st.enemy.clear();
	co_await Db::SaveArena(p.user_id, st);
	co_await Db::SavePlayer(p);
	ArenaStartResp resp = {};
	resp.arena_info = { Arena::Info(p, st) };
	resp.team_info = p.team;
	resp.arena_archive = { Arena::Archive(p, st) };
	co_return Reply(resp);
}

HANDLEF(ArenaFriendList)
{
	// the friends with an arena squad (gme/Friends.hpp): the client fights one through ArenaBattleStart with its user id
	ArenaStartReq req = {};
	if (auto err = Read(req, json, "ArenaFriendList")) co_return *err;
	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player) co_return HandleResult::error("Unknown user", req.login_info.user_id);
	ArenaFriendListResp resp = {};
	for (const auto& row : co_await Db::LoadFriends(player->user_id))
	{
		if (row.state != Friends::Friend) continue;
		const auto other = co_await Db::LoadPlayerByUserId(row.friend_id);
		if (!other) continue;
		const auto o = co_await Arena::Real(*other);
		if (!o) continue;
		const auto st = co_await Db::LoadArena(other->user_id);
		FriendUserArenaInfo f = {};
		f.user_id = o->user_id;
		f.handle_name = o->handle_name;
		f.team_lv = o->lv;
		f.unit_id = o->leader_unit;
		f.unit_lv = 1;
		for (const auto& u : co_await Db::LoadUnits(other->user_id))
			if (u.unit_id == o->leader_unit) { f.unit_lv = u.unit_lv; break; }
		f.arena_rank_id = std::to_string(Arena::RankId(st.rank_point));
		f.ranking_point = st.rank_point;
		f.battle_cnt = st.battle_cnt;
		f.win_cnt = st.win_cnt;
		f.battle_cnt_accept = st.accept_cnt;
		f.win_cnt_accept = st.accept_win;
		f.current_position = 1;
		resp.friends.push_back(f);
	}
	LOG_DEBUG << "ArenaFriendList: user " << player->user_id << " -> " << resp.friends.size() << " friends";
	co_return Reply(resp);
}

HANDLEF(ArenaMatching)
{
	ArenaMatchingReq req = {};
	if (auto err = Read(req, json, "ArenaMatching")) co_return *err;
	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player) co_return HandleResult::error("Unknown user", req.login_info.user_id);
	auto& p = *player;
	auto st = co_await Db::LoadArena(p.user_id);
	const int64_t now = Vortex::Now();
	Arena::Regen(p, st, now);
	std::string avoid;
	if (const auto last = Arena::FromJson(st.suggested)) avoid = last->user_id;
	const auto enemy = co_await Arena::Pick(p, st.rank_point, avoid, now);
	st.suggested = Arena::ToJson(enemy);
	co_await Db::SaveArena(p.user_id, st);
	LOG_DEBUG << "ArenaMatching: user " << p.user_id << " deck " << req.param.deck_num << " -> " << enemy.handle_name << " (" << enemy.user_id << ", lv " << enemy.lv << ", " << enemy.rank_point << " ABP, leader " << enemy.leader_unit << ")";
	ArenaMatchingResp resp = {};
	resp.suggest_enemy = { Arena::Suggest(enemy) };
	resp.arena_info = { Arena::Info(p, st) };
	co_return Reply(resp);
}

HANDLEF(ArenaBattleStart)
{
	ArenaBattleStartReq req = {};
	if (auto err = Read(req, json, "ArenaBattleStart")) co_return *err;
	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player) co_return HandleResult::error("Unknown user", req.login_info.user_id);
	auto& p = *player;
	auto st = co_await Db::LoadArena(p.user_id);
	const int64_t now = Vortex::Now();
	Arena::Regen(p, st, now);
	if (p.team.fight_point <= 0)
	{
		LOG_WARN << "ArenaBattleStart: user " << p.user_id << " has no arena orb";
		co_return HandleResult::error("Out of Arena Orbs", "");
	}
	// the opponent the player saw (ArenaMatching), rebuilt when the client names another
	auto enemy = Arena::FromJson(st.suggested);
	if ((!enemy || enemy->user_id != req.enemy.user_id) && !req.enemy.user_id.empty() && req.enemy.user_id != "0")
	{
		// a named player who is not the suggestion: a friend picked on ArenaFriendListScene
		if (const auto other = co_await Db::LoadPlayerByUserId(req.enemy.user_id))
			if (auto o = co_await Arena::Real(*other)) enemy = o;
	}
	if (!enemy || enemy->user_id != req.enemy.user_id)
	{
		LOG_WARN << "ArenaBattleStart: user " << p.user_id << " names enemy " << req.enemy.user_id << ", the suggestion was " << (enemy ? enemy->user_id : "none") << ": picking afresh";
		enemy = co_await Arena::Pick(p, st.rank_point, "", now);
	}
	if (p.team.fight_point >= p.team.max_fight_point) st.fight_at = now; // the regen clock starts when a full bar is spent from
	p.team.fight_point--;
	st.battle_id = std::to_string(now % 2000000000);
	st.enemy = Arena::ToJson(*enemy);
	st.suggested.clear();
	co_await Db::SaveArena(p.user_id, st);
	co_await Db::SavePlayer(p);
	co_return co_await BattleData(p, st, *enemy, req.param.deck_num, "ArenaBattleStart");
}

HANDLEF(ArenaBattleRestart)
{
	ArenaStartReq req = {};
	if (auto err = Read(req, json, "ArenaBattleRestart")) co_return *err;
	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player) co_return HandleResult::error("Unknown user", req.login_info.user_id);
	auto& p = *player;
	auto st = co_await Db::LoadArena(p.user_id);
	const auto enemy = Arena::FromJson(st.enemy);
	if (st.battle_id.empty() || !enemy)
		co_return HandleResult::error("No battle in progress", "");
	co_return co_await BattleData(p, st, *enemy, p.team.arena_deck_num, "ArenaBattleRestart");
}

HANDLEF(ArenaBattleEnd)
{
	ArenaBattleEndReq req = {};
	if (auto err = Read(req, json, "ArenaBattleEnd")) co_return *err;
	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player) co_return HandleResult::error("Unknown user", req.login_info.user_id);
	auto& p = *player;
	auto st = co_await Db::LoadArena(p.user_id);
	const int64_t now = Vortex::Now();
	Arena::Regen(p, st, now);
	const auto enemy = Arena::FromJson(st.enemy);
	if (st.battle_id.empty() || !enemy)
	{
		LOG_WARN << "ArenaBattleEnd: user " << p.user_id << " ends battle " << req.result.battle_id << " but none is in progress";
		co_return HandleResult::error("No battle in progress", std::to_string(req.result.battle_id));
	}
	// BattleState::getArenaResult: 1 = the player won (see CLAUDE.md "Arena" for how this was checked)
	const bool win = req.result.result == 1;
	const int32_t before = st.rank_point;
	const int32_t beforeRank = Arena::RankId(before);
	const int32_t karma = win ? Arena::kKarmaWin * beforeRank : Arena::kKarmaLose;
	st.battle_cnt++;
	if (win) { st.win_cnt++; st.rank_point += Arena::WinPoints(before, enemy->rank_point); st.series_win++; st.max_series = std::max(st.max_series, st.series_win); }
	else { st.rank_point = std::max(0, st.rank_point - Arena::kLoss); st.series_win = 0; }
	if (!enemy->npc)
	{
		// the defender's record: a defence, won when the attacker lost (the "accept" counts of the arena top)
		auto def = co_await Db::LoadArena(enemy->user_id);
		def.accept_cnt++;
		if (!win) def.accept_win++;
		co_await Db::SaveArena(enemy->user_id, def);
	}
	auto archive = Arena::Archive(p, st);
	Arena::Accumulate(archive, req.result, win, st.rank_point - before);
	st.archive = Arena::ArchiveJson(archive);
	const int32_t afterRank = Arena::RankId(st.rank_point);
	p.team.karma += karma;

	// rank-up rewards (arena_rank.json: type 1 = gems, 2 = an item, "id:count"), once per rank
	ArenaBattleEndResp resp = {};
	std::string items;
	for (int32_t rank = st.max_rank + 1; rank <= afterRank; rank++)
	{
		const auto* row = Arena::RankById(rank);
		if (!row) continue;
		ArenaBattleReward r = {};
		r.first_rank_up = 1;
		r.dia_num = 0;
		if (row->reward_type == "1" && !row->reward_param.empty())
		{
			r.dia_num = row->reward_param.back();
			p.team.brave_coin += r.dia_num;
			p.team.free_gems += r.dia_num;
		}
		else if (row->reward_type == "2" && row->reward_param.size() >= 2)
		{
			r.items = std::to_string(row->reward_param[0]) + ":" + std::to_string(row->reward_param[1]);
			items += (items.empty() ? "" : ",") + r.items;
		}
		resp.reward.push_back(r);
	}
	if (!items.empty()) co_await Db::AddItems(p.user_id, items);
	st.max_rank = std::max(st.max_rank, afterRank);
	LOG_DEBUG << "ArenaBattleEnd: user " << p.user_id << " battle " << st.battle_id << " vs " << enemy->handle_name << " result " << req.result.result << (win ? " (win)" : " (loss)") << ": " << before << " -> " << st.rank_point << " ABP, rank " << beforeRank << " -> " << afterRank << ", karma +" << karma << ", rewards " << resp.reward.size();

	ArenaBattleResult res = {};
	res.battle_id = st.battle_id;
	res.result = std::to_string(req.result.result);
	res.before_arena_rank_id = std::to_string(beforeRank);
	res.after_arena_rank_id = std::to_string(afterRank);
	res.before_rank_point = before;
	res.after_rank_point = st.rank_point;
	res.karma = karma;
	// 1 opens the "send a friend request?" prompt (ArenaBattleResultInfo::enableFriendRequest):
	// real players who are not friends yet, never NPCs (gme/Friends.hpp)
	res.friend_req_state = (!enemy->npc && !(co_await Friends::AreFriends(p.user_id, enemy->user_id))) ? 1 : 0;
	res.bonus_point = 0;
	res.bonus_type = 0;
	res.bonus_param = "";
	res.battle_cnt = st.battle_cnt;
	st.battle_id.clear();
	st.enemy.clear();
	co_await Db::SaveArena(p.user_id, st);
	co_await Db::SavePlayer(p);
	resp.result = { res };
	resp.arena_info = { Arena::Info(p, st) };
	resp.team_info = p.team;
	resp.arena_archive = { Arena::Archive(p, st) };
	co_return Reply(resp);
}
