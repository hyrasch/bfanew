#include "App.hpp"
#include "Handlers.hpp"
#include "TownState.hpp"

/*
* TownUpdate ("CuQ5oB8U", encode key w1eo2ZDJ, URL /actionSymbol/uVrph04Y.php): the village's item
* collection. Tapping a sparkling location (MyTownTopScene::touchBegan -> collectItemEff) is local:
* with UserTownLocationDetail tap_cnt >= 1 the client takes entry [n - tap_cnt] of the location's
* drop_item_info ("itemId:zel:karma", n entries), adds the item to the warehouse (1, refused with
* WAREHOUSE_OVER_ERROR when the box is full), the zel and the karma to the wallet, decTapCnt(1),
* counts the tap in MyTownCollectLogList and queues this request (ConnectRequestList) for the next
* connection: group EuY6L7AX, one node, 0mRaAo39 "locationId:tapCnt,..." and LzKDI2i7 the sound
* shop purchases (ignored). The server grants the same entries from its stored copy of the drops
* (TownState rolled them, player_town_collect), never more than the taps it had, arms the refill
* timer once a location is empty, and answers the wallet, the warehouse and every location's
* record (s8TCo2MS replaces the client's list).
*/
HANDLEF(TownUpdate)
{
	TownUpdateReq req = {};
	const auto& ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(req, json);
	if (ec)
	{
		const auto& fmte = glz::format_error(ec, json);
		LOG_DEBUG << "Gme TownUpdate Error during JSON read: " << fmte;
		co_return HandleResult::error("Deserialization error", fmte);
	}

	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player)
		co_return HandleResult::error("Unknown user", req.login_info.user_id);
	auto& p = *player;

	auto collects = co_await Db::LoadCollect(p.user_id);
	const auto now = TownState::Now();
	std::string gained;
	int64_t zel = 0, karma = 0;
	for (const auto& log : req.log)
	{
		for (const auto& [locationId, taps] : TownState::ParsePairs(log.collect))
		{
			auto it = collects.find(static_cast<int32_t>(locationId));
			if (it == collects.end() || taps <= 0)
			{
				LOG_WARN << "TownUpdate: user " << p.user_id << " location " << locationId << " tapped " << taps << " times but has no collection";
				continue;
			}
			auto& c = it->second;
			std::vector<std::string> entries; // the drops, one "itemId:zel:karma" per tap

			for (size_t pos = 0; pos <= c.drops.size();)
			{
				const auto comma = c.drops.find(',', pos);
				entries.emplace_back(c.drops.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos));
				if (comma == std::string::npos) break;
				pos = comma + 1;
			}
			const int32_t granted = static_cast<int32_t>(std::min<int64_t>(taps, c.tap_cnt));
			if (granted < taps)
				LOG_WARN << "TownUpdate: user " << p.user_id << " location " << locationId << " reported " << taps << " taps, had " << c.tap_cnt;
			for (int32_t i = 0; i < granted; ++i)
			{
				const auto idx = entries.size() - c.tap_cnt; // the next entry
				if (idx < entries.size())
				{
					const auto& e = entries[idx];
					int64_t item = 0, z = 0, k = 0;
					const auto c1 = e.find(':'), c2 = c1 == std::string::npos ? c1 : e.find(':', c1 + 1);
					try
					{
						item = std::stoll(e.substr(0, c1));
						if (c1 != std::string::npos) z = std::stoll(e.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1));
						if (c2 != std::string::npos) k = std::stoll(e.substr(c2 + 1));
					}
					catch (...) {}
					if (item > 0) gained += (gained.empty() ? "" : ",") + std::to_string(item) + ":1";
					zel += z;
					karma += k;
				}
				--c.tap_cnt;
			}
			if (c.tap_cnt <= 0)
			{
				c.tap_cnt = 0;
				c.refill_at = now + TownState::kRefillSeconds;
			}
			LOG_DEBUG << "TownUpdate: user " << p.user_id << " location " << locationId << " collected " << granted << " (left " << c.tap_cnt << ")";
			co_await Db::SaveCollect(p.user_id, static_cast<int32_t>(locationId), c);
		}
	}
	if (!gained.empty()) co_await Db::AddItems(p.user_id, gained);
	p.team.zel = static_cast<int32_t>(std::min<int64_t>(p.team.zel + zel, INT32_MAX));
	p.team.karma = static_cast<int32_t>(std::min<int64_t>(p.team.karma + karma, INT32_MAX));
	co_await Db::SavePlayer(p);
	LOG_DEBUG << "TownUpdate: user " << p.user_id << " gained items [" << gained << "] zel " << zel << " karma " << karma;

	TownUpdateResp resp = {};
	resp.team_info = p.team;
	resp.warehouse_info = co_await Db::LoadItems(p.user_id);
	auto town = co_await TownState::Build(p, co_await Db::LoadClears(p.user_id));
	resp.town_location_detail = std::move(town.details);

	std::string buffer{};
	const auto& ec2 = glz::write_json(resp, buffer);
	if (ec2)
	{
		const auto& glze = glz::format_error(ec2, buffer);
		LOG_DEBUG << "Gme TownUpdate Error during JSON writing: " << glze;
		co_return HandleResult::error("Serialization error", glze);
	}

	co_return HandleResult::success(buffer);
}
