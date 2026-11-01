#include "App.hpp"
#include "Handlers.hpp"
#include "Gifts.hpp"
#include "Vortex.hpp"
#include <sstream>

/*
* bfanew: gifts between friends (net/present.kdl, Gifts.hpp, CLAUDE.md "Gifts").
*   GetGiftInfo ifYoPJ46  the Gift box (GiftRecieveConnectScene)          -> the gifts received, unclaimed
*   FixGiftInfo gLRIn74v  the want list edited, gifts taken, gifts sent   -> the gifts left + wallet + warehouse
*/

namespace
{
	template <typename T>
	HandleResult Reply(T& resp)
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

	std::vector<std::string> Split(const std::string& csv, char sep = ',')
	{
		std::vector<std::string> out;
		std::stringstream ss(csv);
		std::string item;
		while (std::getline(ss, item, sep))
			if (!item.empty()) out.push_back(item);
		return out;
	}

	drogon::Task<HandleResult> GiftReply(Db::Player& p, const char* name)
	{
		GiftInfoResp resp = {};
		resp.gift_info = co_await Gifts::Received(p.user_id);
		resp.team_info = { p.team };
		resp.warehouse = co_await Db::LoadItems(p.user_id);
		LOG_DEBUG << name << ": user " << p.user_id << " -> " << resp.gift_info.size() << " gifts waiting";
		co_return Reply(resp);
	}
}

HANDLEF(GetGiftInfo)
{
	GetGiftInfoReq req = {};
	if (auto err = Read(req, json, "GetGiftInfo")) co_return *err;
	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player) co_return HandleResult::error("Unknown user", req.login_info.user_id);
	co_return co_await GiftReply(*player, "GetGiftInfo");
}

HANDLEF(FixGiftInfo)
{
	FixGiftInfoReq req = {};
	if (auto err = Read(req, json, "FixGiftInfo")) co_return *err;
	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player) co_return HandleResult::error("Unknown user", req.login_info.user_id);
	auto& p = *player;
	const int64_t now = Vortex::Now();
	// the want list
	if (!req.param.want_gift.empty())
		p.team.want_gift = req.param.want_gift;
	// the gifts taken
	int taken = 0;
	const auto received = Split(req.param.receive_ids);
	if (!received.empty())
	{
		for (const auto& g : co_await Db::LoadGiftsTo(p.user_id))
		{
			if (g.received) continue;
			if (std::find(received.begin(), received.end(), std::to_string(g.id)) == received.end()) continue;
			co_await Gifts::Give(p, g);
			co_await Db::MarkGift(g.id, now);
			taken++;
		}
	}
	// the gifts sent: "friend user id:gift id,...", one per friend per day, among the friend's wants
	int sent = 0;
	const auto rows = co_await Db::LoadFriends(p.user_id);
	const auto sentToday = co_await Db::GiftsSentSince(p.user_id, Gifts::DayStart(now));
	for (const auto& entry : Split(req.param.send_list))
	{
		const auto parts = Split(entry, ':');
		if (parts.size() < 2) continue;
		const auto& to = parts[0];
		const int32_t giftId = std::atoi(parts[1].c_str());
		const auto r = Friends::Row(rows, to);
		if (!r || r->state != Friends::Friend || sentToday.count(to) || !Gifts::Def(giftId)) continue;
		co_await Db::AddGift(p.user_id, to, giftId, now);
		sent++;
	}
	co_await Db::SavePlayer(p);
	LOG_DEBUG << "FixGiftInfo: user " << p.user_id << " wants \"" << p.team.want_gift << "\", took " << taken << ", sent " << sent;
	co_return co_await GiftReply(p, "FixGiftInfo");
}
