#pragma once
#include "App.hpp"
#include "Arena.hpp"
#include "Vortex.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

/*
* bfanew: friends (CLAUDE.md "Friends"; net/friends.kdl has every id, key and field).
*
* A friend record on the wire is the leader unit of one of the player's helper squads plus the
* player's name, level, message, arena rank and last login (FriendInfo / ReinforcementInfo):
* the client builds the helper unit of a quest from that record (MissionStart 1VagK32J is its
* csv), so nothing else is needed for a helper to fight. Rows (db/Player.hpp FriendRow) hold a
* state per direction: 1 friends, 2 a request to answer, 3 a request sent.
*
* Friend points: a helper earns the player kFriendPoints when the helper is a friend, else
* kNormalPoints, paid at MissionEnd (2014 values: 10 / 5). They buy Honor Summons.
*/
namespace Friends
{
	constexpr int32_t kFriendPoints = 10;
	constexpr int32_t kNormalPoints = 5;
	constexpr int32_t kReinforcements = 30; //!< non-friend helpers offered in the picker
	constexpr int32_t kRandomResults = 20;  //!< FriendRandomSearch results

	// FriendInfo friend_type as the scenes test it: FriendListUtils::getFriendList keeps 1; the Agree
	// list draws 2 ("Pending", a request the player sent, Remove) and 3 ("Waiting", a request
	// received, Accept / Remove); the ID / random search scenes list 4 (and say "already friends" on 1)
	enum State : int32_t { Friend = 1, Sent = 2, Received = 3, Stranger = 4 };

	inline std::optional<Db::FriendRow> Row(const std::vector<Db::FriendRow>& rows, const std::string& other)
	{
		for (const auto& r : rows)
			if (r.friend_id == other) return r;
		return std::nullopt;
	}

	inline drogon::Task<bool> AreFriends(std::string me, std::string other)
	{
		const auto rows = co_await Db::LoadFriends(me);
		const auto r = Row(rows, other);
		co_return r && r->state == Friend;
	}

	inline int32_t CountFriends(const std::vector<Db::FriendRow>& rows)
	{
		return static_cast<int32_t>(std::count_if(rows.begin(), rows.end(), [](const auto& r) { return r.state == Friend; }));
	}

	// ------------------------------------------------------------------ records

	/*! The leader of the first helper squad (UserTeamInfo reinforcement_deck numbers) that has one,
	 *  else the active squad's, else any unit: {unit, deck number}. */
	inline std::optional<std::pair<UserUnitInfo, int32_t>> Leader(const Db::Player& p, const std::vector<UserPartyDeckInfo>& decks, const std::vector<UserUnitInfo>& units)
	{
		auto find = [&](int32_t deckNum) -> std::optional<UserUnitInfo> {
			const UserPartyDeckInfo* leader = nullptr;
			for (const auto& d : decks)
				if (d.deck_type == 1 && d.deck_num == deckNum && (d.member_type == 0 || !leader)) leader = &d;
			if (!leader) return std::nullopt;
			for (const auto& u : units)
				if (u.user_unit_id == leader->user_unit_id) return u;
			return std::nullopt;
		};
		std::vector<int32_t> order(p.team.reinforcement_deck.begin(), p.team.reinforcement_deck.end());
		order.push_back(p.team.active_deck);
		for (const auto deckNum : order)
			if (auto u = find(deckNum)) return std::make_pair(*u, deckNum);
		if (!units.empty()) return std::make_pair(units.front(), p.team.active_deck);
		return std::nullopt;
	}

	template <typename T>
	void FillCommon(T& r, const Db::Player& other, const std::optional<std::pair<UserUnitInfo, int32_t>>& leader, int32_t arenaPoints, int64_t now)
	{
		r.user_id = other.user_id;
		r.handle_name = other.handle_name;
		r.team_lv = other.team.level;
		// seconds since the last login ("Online N days ago"; a unix time reads as 999 days)
		r.last_login_date = other.last_login ? static_cast<int32_t>(std::max<int64_t>(0, now - other.last_login)) : 30 * 86400;
		r.friend_id = other.account_id;
		r.friend_message = other.team.friend_message;
		r.arena_rank_id = std::to_string(Arena::RankId(arenaPoints));
		r.priority = 1;
		r.guild_id = 0;
		if (!leader) return;
		const auto& u = leader->first;
		r.deck_no = leader->second;
		r.unit_id = u.unit_id;
		r.unit_lv = u.unit_lv;
		r.unit_type_id = u.unit_type_id;
		r.base_hp = u.base_hp; r.add_hp = u.add_hp; r.ext_hp = u.ext_hp;
		r.base_atk = u.base_atk; r.add_atk = u.add_atk; r.ext_atk = u.ext_atk;
		r.base_def = u.base_def; r.add_def = u.add_def; r.ext_def = u.ext_def;
		r.base_heal = u.base_heal; r.add_heal = u.add_heal; r.ext_heal = u.ext_heal;
		r.skill_id = u.skill_id;
		r.skill_lv = std::max(1, u.skill_lv);
		r.extra_skill_id = u.extra_skill_id;
		r.extra_skill_lv = u.extra_skill_lv;
		r.extra_passive_skill_id = u.extra_passive_skill_id;
		r.extra_passive_skill_id2 = u.extra_passive_skill_id2;
		r.equipitem_id = u.equipitem_id;
		r.equipitem_id2 = u.equipitem_id2;
		r.fe_skill_info = u.fe_skill_info;
		r.element = std::atoi(u.element.c_str());
		r.omni_level = u.omni_level;
		r.unit_img_type = 0;
	}

	/*! unix seconds -> "YYYY/MM/DD HH:MM" in the game's clock (the Vortex schedule's UTC offset). */
	inline std::string GameClock(int64_t t)
	{
		const int64_t local = t + static_cast<int64_t>(theServer()->cache().vortex().utc_offset_hours) * 3600;
		const int64_t day = local >= 0 ? local / 86400 : (local - 86399) / 86400;
		const int64_t sec = local - day * 86400;
		std::string d = Vortex::DateOf(day);
		for (auto& c : d) if (c == '-') c = '/';
		char buf[8];
		std::snprintf(buf, sizeof buf, " %02d:%02d", static_cast<int>(sec / 3600), static_cast<int>(sec % 3600 / 60));
		return d + buf;
	}

	struct Loaded
	{
		Db::Player player;
		std::optional<std::pair<UserUnitInfo, int32_t>> leader;
		int32_t arena_points = 0;
	};

	inline drogon::Task<std::optional<Loaded>> LoadOther(std::string userId)
	{
		auto p = co_await Db::LoadPlayerByUserId(userId);
		if (!p) co_return std::nullopt;
		Loaded l;
		l.player = *p;
		const auto units = co_await Db::LoadUnits(userId);
		const auto decks = co_await Db::LoadDecks(userId);
		l.leader = Leader(*p, decks, units);
		l.arena_points = (co_await Db::LoadArena(userId)).rank_point;
		co_return l;
	}

	inline FriendInfo Record(const Loaded& l, int32_t state, int32_t favorite, int64_t since, int64_t now)
	{
		FriendInfo r = {};
		FillCommon(r, l.player, l.leader, l.arena_points, now);
		r.friend_type = state;
		r.favorite = favorite;
		r.ranking_point = l.arena_points;
		r.req_time = since ? GameClock(since) : "";
		r.elapsed_agree_time = since ? static_cast<int32_t>(std::max<int64_t>(0, now - since)) : 0;
		return r;
	}

	inline ReinforcementInfo Reinforcement(const Loaded& l, bool isFriend, int64_t now)
	{
		ReinforcementInfo r = {};
		FillCommon(r, l.player, l.leader, l.arena_points, now);
		// the picker prints getFriendPoint() as "Honor +N": the points this helper earns
		r.friend_type = isFriend ? Friend : 0;
		r.friend_point = isFriend ? kFriendPoints : kNormalPoints;
		r.normal_friend_point = kNormalPoints;
		if (l.leader) r.user_unit_id = l.leader->first.user_unit_id;
		return r;
	}

	// ------------------------------------------------------------------ replies

	/*! Every relation of the player as FriendInfo records (friends with their favorites first, then
	 *  the requests received and sent): the client keeps ONE FriendInfoList and each scene filters
	 *  it by friend_type (FriendList2Scene 1, FriendAgreeListScene 2 and 3), so every reply
	 *  carries the whole list. */
	inline drogon::Task<std::vector<FriendInfo>> Relations(const Db::Player& me, const std::vector<Db::FriendRow>& rows, int64_t now)
	{
		std::vector<FriendInfo> out;
		for (const auto& row : rows)
		{
			if (row.state != Friend && row.state != Received && row.state != Sent) continue;
			const auto l = co_await LoadOther(row.friend_id);
			if (!l) continue;
			out.push_back(Record(*l, row.state, row.state == Friend ? row.favorite : 0, row.since, now));
		}
		std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
			if (a.friend_type != b.friend_type) return a.friend_type < b.friend_type;
			return a.favorite > b.favorite;
		});
		co_return out;
	}

	/*! The helper candidates of the quest picker (ReinforcementSelectScene2 draws ReinforcementInfoList
	 *  only, FriendListUtils::getReinforcementList sorts it with FriendInfoList's favorites): the
	 *  friends first, then other players. */
	inline drogon::Task<std::vector<ReinforcementInfo>> Reinforcements(const Db::Player& me, const std::vector<Db::FriendRow>& rows, int64_t now)
	{
		std::vector<ReinforcementInfo> out;
		for (const auto& row : rows)
		{
			if (row.state != Friend) continue;
			const auto l = co_await LoadOther(row.friend_id);
			if (!l || !l->leader) continue;
			out.push_back(Reinforcement(*l, true, now));
		}
		for (const auto& other : co_await Db::ListOtherPlayers(me.user_id, kReinforcements))
		{
			const auto r = Row(rows, other.user_id);
			if (r && r->state == Friend) continue;
			const auto l = co_await LoadOther(other.user_id);
			if (!l || !l->leader) continue;
			out.push_back(Reinforcement(*l, false, now));
		}
		co_return out;
	}

	inline void FillTail(FriendListResp& resp, const Db::Player& me)
	{
		FriendPointInfo pts = {};
		pts.friend_point_num = kFriendPoints;
		pts.normal_point_num = kNormalPoints;
		resp.point_info = { pts };
		resp.team_info = { me.team };
	}
}
