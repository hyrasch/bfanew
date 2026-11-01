#include "App.hpp"
#include "Handlers.hpp"
#include "Presents.hpp"
#include "Vortex.hpp"
#include <algorithm>

/*
* bfanew: the Gift Box requests (net/present.kdl; the rules in Presents.hpp, CLAUDE.md "Gifts").
*   PresentList    nhjvB52R  Social > Gifts (PresentTopScene)       -> the unclaimed presents + wallet, box, warehouse
*   PresentReceipt bV5xa0ZW  Claim one (receipt type 1) or all (2)  -> the presents left + what changed
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

	drogon::Task<HandleResult> ListReply(Db::Player& p, const char* name, const std::vector<int64_t>& removed = {})
	{
		PresentListResp resp = {};
		const int64_t now = Vortex::Now();
		for (const auto& r : co_await Db::LoadPresents(p.user_id))
		{
			if (!r.received && (r.expires == 0 || r.expires > now))
				resp.presents.push_back(Presents::Record(r, p.user_id));
			else if (std::find(removed.begin(), removed.end(), r.id) != removed.end())
			{
				auto gone = Presents::Record(r, p.user_id); // claimed just now: the client drops it from its list
				gone.is_receipt = 1;
				gone.operation = 3;
				resp.presents.push_back(gone);
			}
		}
		p.team.present_count = static_cast<int32_t>(resp.presents.size());
		resp.team_info = { p.team };
		resp.unit_info = co_await Db::LoadUnits(p.user_id);
		resp.warehouse = co_await Db::LoadItems(p.user_id);
		LOG_DEBUG << name << ": user " << p.user_id << " -> " << resp.presents.size() << " presents";
		co_return Reply(resp);
	}
}

HANDLEF(PresentList)
{
	PresentListReq req = {};
	if (auto err = Read(req, json, "PresentList")) co_return *err;
	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player) co_return HandleResult::error("Unknown user", req.login_info.user_id);
	co_return co_await ListReply(*player, "PresentList");
}

HANDLEF(PresentReceipt)
{
	PresentReceiptReq req = {};
	if (auto err = Read(req, json, "PresentReceipt")) co_return *err;
	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player) co_return HandleResult::error("Unknown user", req.login_info.user_id);
	auto& p = *player;
	const int64_t now = Vortex::Now();
	const bool all = req.param.receipt_type == 2;
	int given = 0;
	std::vector<int64_t> claimed;
	std::string firstError;
	for (const auto& r : co_await Db::LoadPresents(p.user_id))
	{
		if (r.received || (r.expires && r.expires <= now)) continue;
		if (!all && std::to_string(r.id) != req.param.present_id) continue;
		const auto err = co_await Presents::Give(p, r);
		if (!err.empty())
		{
			if (firstError.empty()) firstError = err;
			continue; // a full box stops the units, the rest is still paid
		}
		co_await Db::MarkPresent(r.id, now);
		claimed.push_back(r.id);
		given++;
	}
	co_await Db::SavePlayer(p);
	LOG_DEBUG << "PresentReceipt: user " << p.user_id << (all ? " claimed all: " : " claimed ") << given << (firstError.empty() ? "" : " (" + firstError + ")");
	if (!given && !firstError.empty())
		co_return HandleResult::error(firstError, req.param.present_id);
	co_return co_await ListReply(p, "PresentReceipt", claimed);
}
