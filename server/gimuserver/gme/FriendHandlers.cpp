#include "App.hpp"
#include "Handlers.hpp"
#include "Friends.hpp"
#include <sstream>

/*
* bfanew: the friend requests (net/friends.kdl has the ids, keys, URLs and fields; the rules are
* in Friends.hpp and CLAUDE.md "Friends"). Every reply is a FriendListResp: the client keeps one
* FriendInfoList that each reply replaces and that every scene filters by friend_type (1 friend,
* 2 request received, 3 request sent, 4 stranger), so a handler answers with the whole relation
* list (or the search results), plus the helper candidates, the friend point values and the wallet.
*   FriendGet          2o4axPIC  login, mission results, the Social tab -> friends + helper candidates
*   FriendGetAgree     aVqJrR08  the Agree list (raid / guild entry)     -> the same list
*   FriendApply        WUNi08YL  send a request                          -> the same list
*   FriendAgree        NYKBH4V6  accept one                              -> the same list
*   FriendRefuse       N5SQd7oA  reject one or all, or cancel one sent    -> the same list
*   FriendDelete       SfMN9w4p  remove a friend                         -> the same list
*   FriendSearch       umNt8M7i  search by the 8-digit ID                -> the one found
*   FriendRandomSearch mE5gUQOp  random summoners                        -> candidates
*   FriendFavorite     ps6Vgx4R  star / unstar                           -> the same list
*/

namespace
{
	template <typename T>
	HandleResult Reply(const T& resp)
	{
		std::string buffer{};
		const auto& ec = glz::write_json(resp, buffer);
		if (ec)
			return HandleResult::error("Serialization error", glz::format_error(ec, buffer));
		return HandleResult::success(buffer);
	}

	template <typename Req>
	std::optional<HandleResult> Read(Req& req, const std::string& json, const char* name)
	{
		const auto& ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(req, json);
		if (!ec) return std::nullopt;
		const auto& fmte = glz::format_error(ec, json);
		LOG_DEBUG << "Gme " << name << " Error during JSON read: " << fmte;
		return HandleResult::error("Deserialization error", fmte);
	}

	drogon::Task<HandleResult> ListReply(const Db::Player& me, bool withReinforcements, const char* name)
	{
		const auto rows = co_await Db::LoadFriends(me.user_id);
		const int64_t now = Vortex::Now();
		FriendListResp resp = {};
		resp.friends = co_await Friends::Relations(me, rows, now);
		if (withReinforcements)
			resp.reinforcements = co_await Friends::Reinforcements(me, rows, now);
		Friends::FillTail(resp, me);
		LOG_DEBUG << name << ": user " << me.user_id << " -> " << resp.friends.size() << " relations (" << Friends::CountFriends(rows) << " friends), " << resp.reinforcements.size() << " candidates, " << me.team.friend_point << " friend points";
		co_return Reply(resp);
	}

	template <typename Req>
	drogon::Task<std::optional<Db::Player>> Me(const Req& req)
	{
		co_return co_await Db::LoadPlayer(req.login_info);
	}

	std::vector<std::string> Split(const std::string& csv)
	{
		std::vector<std::string> out;
		std::stringstream ss(csv);
		std::string item;
		while (std::getline(ss, item, ','))
			if (!item.empty()) out.push_back(item);
		return out;
	}
}

HANDLEF(FriendGet)
{
	FriendGetReq req = {};
	if (auto err = Read(req, json, "FriendGet")) co_return *err;
	auto me = co_await Me(req);
	if (!me) co_return HandleResult::error("Unknown user", req.login_info.user_id);
	co_return co_await ListReply(*me, true, "FriendGet");
}

HANDLEF(FriendGetAgree)
{
	FriendGetAgreeReq req = {};
	if (auto err = Read(req, json, "FriendGetAgree")) co_return *err;
	auto me = co_await Me(req);
	if (!me) co_return HandleResult::error("Unknown user", req.login_info.user_id);
	co_return co_await ListReply(*me, false, "FriendGetAgree");
}

HANDLEF(FriendApply)
{
	FriendApplyReq req = {};
	if (auto err = Read(req, json, "FriendApply")) co_return *err;
	auto me = co_await Me(req);
	if (!me) co_return HandleResult::error("Unknown user", req.login_info.user_id);
	const auto& other = req.target.user_id;
	if (other.empty() || other == me->user_id)
		co_return HandleResult::error("You cannot send a friend request to yourself", other);
	const auto target = co_await Db::LoadPlayerByUserId(other);
	if (!target)
		co_return HandleResult::error("This summoner does not exist", other);
	const auto rows = co_await Db::LoadFriends(me->user_id);
	if (Friends::CountFriends(rows) >= me->team.max_friend_count)
		co_return HandleResult::error("Your friend list is full", std::to_string(me->team.max_friend_count));
	const int64_t now = Vortex::Now();
	if (const auto r = Friends::Row(rows, other))
	{
		if (r->state == Friends::Friend) co_return HandleResult::error("You are already friends", other);
		if (r->state == Friends::Sent) co_return HandleResult::error("A friend request was already sent", other);
		// they asked first: that is an acceptance
		co_await Db::SetFriend(me->user_id, other, Friends::Friend, now);
		co_await Db::SetFriend(other, me->user_id, Friends::Friend, now);
		LOG_DEBUG << "FriendApply: user " << me->user_id << " and " << other << " are friends (mutual requests)";
		co_return co_await ListReply(*me, false, "FriendApply");
	}
	co_await Db::SetFriend(me->user_id, other, Friends::Sent, now);
	co_await Db::SetFriend(other, me->user_id, Friends::Received, now);
	LOG_DEBUG << "FriendApply: user " << me->user_id << " -> " << other << " (" << target->handle_name << ")";
	co_return co_await ListReply(*me, false, "FriendApply");
}

HANDLEF(FriendAgree)
{
	FriendAgreeReq req = {};
	if (auto err = Read(req, json, "FriendAgree")) co_return *err;
	auto me = co_await Me(req);
	if (!me) co_return HandleResult::error("Unknown user", req.login_info.user_id);
	const auto& other = req.target.user_id;
	const auto rows = co_await Db::LoadFriends(me->user_id);
	const auto r = Friends::Row(rows, other);
	if (!r || r->state != Friends::Received)
		co_return HandleResult::error("No friend request from this summoner", other);
	if (Friends::CountFriends(rows) >= me->team.max_friend_count)
		co_return HandleResult::error("Your friend list is full", std::to_string(me->team.max_friend_count));
	const int64_t now = Vortex::Now();
	co_await Db::SetFriend(me->user_id, other, Friends::Friend, now);
	co_await Db::SetFriend(other, me->user_id, Friends::Friend, now);
	LOG_DEBUG << "FriendAgree: user " << me->user_id << " accepted " << other;
	co_return co_await ListReply(*me, false, "FriendAgree");
}

HANDLEF(FriendRefuse)
{
	FriendRefuseReq req = {};
	if (auto err = Read(req, json, "FriendRefuse")) co_return *err;
	auto me = co_await Me(req);
	if (!me) co_return HandleResult::error("Unknown user", req.login_info.user_id);
	const auto rows = co_await Db::LoadFriends(me->user_id);
	std::vector<std::string> ids = Split(req.param.user_ids);
	if (req.param.all == "1")
		for (const auto& r : rows)
			if (r.state == Friends::Received) ids.push_back(r.friend_id);
	int refused = 0;
	for (const auto& other : ids)
	{
		// Remove on a "Waiting" row rejects the request received, on a "Pending" row cancels the one sent
		const auto r = Friends::Row(rows, other);
		if (!r || (r->state != Friends::Received && r->state != Friends::Sent)) continue;
		co_await Db::RemoveFriend(me->user_id, other);
		co_await Db::RemoveFriend(other, me->user_id);
		refused++;
	}
	LOG_DEBUG << "FriendRefuse: user " << me->user_id << " refused " << refused << " request(s)" << (req.param.all == "1" ? " (all)" : "");
	co_return co_await ListReply(*me, false, "FriendRefuse");
}

HANDLEF(FriendDelete)
{
	FriendDeleteReq req = {};
	if (auto err = Read(req, json, "FriendDelete")) co_return *err;
	auto me = co_await Me(req);
	if (!me) co_return HandleResult::error("Unknown user", req.login_info.user_id);
	const auto& other = req.target.user_id;
	co_await Db::RemoveFriend(me->user_id, other);
	co_await Db::RemoveFriend(other, me->user_id);
	LOG_DEBUG << "FriendDelete: user " << me->user_id << " removed " << other;
	co_return co_await ListReply(*me, false, "FriendDelete");
}

HANDLEF(FriendSearch)
{
	FriendSearchReq req = {};
	if (auto err = Read(req, json, "FriendSearch")) co_return *err;
	auto me = co_await Me(req);
	if (!me) co_return HandleResult::error("Unknown user", req.login_info.user_id);
	const auto target = co_await Db::LoadPlayerByAccountId(req.param.friend_id);
	if (!target || target->user_id == me->user_id)
		co_return HandleResult::error("No summoner has this ID", req.param.friend_id);
	const auto l = co_await Friends::LoadOther(target->user_id);
	if (!l) co_return HandleResult::error("No summoner has this ID", req.param.friend_id);
	const auto rows = co_await Db::LoadFriends(me->user_id);
	const auto r = Friends::Row(rows, target->user_id);
	FriendListResp resp = {};
	resp.friends = { Friends::Record(*l, r ? r->state : Friends::Stranger, r ? r->favorite : 0, r ? r->since : 0, Vortex::Now()) };
	Friends::FillTail(resp, *me);
	LOG_DEBUG << "FriendSearch: user " << me->user_id << " looked up " << req.param.friend_id << " -> " << target->user_id << " (" << target->handle_name << ")";
	co_return Reply(resp);
}

HANDLEF(FriendRandomSearch)
{
	FriendRandomSearchReq req = {};
	if (auto err = Read(req, json, "FriendRandomSearch")) co_return *err;
	auto me = co_await Me(req);
	if (!me) co_return HandleResult::error("Unknown user", req.login_info.user_id);
	FriendListResp resp = {};
	const int64_t now = Vortex::Now();
	for (const auto& other : co_await Db::RandomPlayers(me->user_id, Friends::kRandomResults))
	{
		if (req.param.lv_max > 0 && (other.team.level < req.param.lv_min || other.team.level > req.param.lv_max)) continue;
		const auto l = co_await Friends::LoadOther(other.user_id);
		if (!l) continue;
		resp.friends.push_back(Friends::Record(*l, Friends::Stranger, 0, 0, now));
	}
	Friends::FillTail(resp, *me);
	LOG_DEBUG << "FriendRandomSearch: user " << me->user_id << " lv " << req.param.lv_min << "-" << req.param.lv_max << " -> " << resp.friends.size() << " summoners";
	co_return Reply(resp);
}

HANDLEF(FriendFavorite)
{
	FriendFavoriteReq req = {};
	if (auto err = Read(req, json, "FriendFavorite")) co_return *err;
	auto me = co_await Me(req);
	if (!me) co_return HandleResult::error("Unknown user", req.login_info.user_id);
	co_await Db::SetFavorite(me->user_id, req.param.user_id, req.param.favorite ? 1 : 0);
	LOG_DEBUG << "FriendFavorite: user " << me->user_id << " " << (req.param.favorite ? "starred " : "unstarred ") << req.param.user_id;
	co_return co_await ListReply(*me, false, "FriendFavorite");
}
