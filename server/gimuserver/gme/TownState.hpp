#pragma once

#include "App.hpp"
#include <chrono>
#include <random>
#include <unordered_set>

/*
* The player's town as the client wants it (UserInfo and the TownFacilityUpdate / TownUpdate replies):
* - YRgx49WG UserTownFacilityInfo: one per facility whose need_mission_id is 0 or cleared, with the
*   stored level (1 when never upgraded) and the karma injected toward the next level;
* - yj46Q2xw UserTownLocationInfo: the same per town location;
* - 51yQrDBR PermitReceipe: the recipes released by every facility level up to the current one
*   (TownFacilityLvMst.release_receipe), which is exactly the village's Synthesis List;
* - s8TCo2MS UserTownLocationDetail: one per TownLocationMst row (MyTownTopScene::downloadFiles
*   dereferences the record of every open location) with the taps left and the drops they give
*   ("itemId:zel:karma,...", one entry per tap, MyTownTopScene::collectItemEff takes entry
*   [n - tap_cnt]). An open location with no taps left refills once its refill time has passed:
*   the drops are rolled here from the TownLocationLvMst row of its stored level (tap count in
*   [tap_cnt_start, tap_cnt_end], the item from the weighted item_info pool, zel_rate % chance of
*   [zel_start, zel_end] zel, karma_rate % of [karma_start, karma_end] karma). The original
*   interval is unknown (the client never reads qA7M9EjP startdate): kRefillSeconds.
* Facility upgrades are paid in karma on the Upgrades screen (MyTownFacilityExtScene2), never in
* gems (see CLAUDE.md "Free economy").
*/
namespace TownState
{
	constexpr int64_t kRefillSeconds = 60 * 60;

	struct Lists
	{
		std::vector<UserTownFacilityInfo> facilities;
		std::vector<UserTownLocationInfo> locations;
		std::vector<PermitReceipe> recipes;
		std::vector<UserTownLocationDetail> details;
	};

	inline int64_t Now()
	{
		return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}

	// "a:b,c:d" -> [(a, b), (c, d)] (malformed entries skipped)
	inline std::vector<std::pair<int64_t, int64_t>> ParsePairs(const std::string& s)
	{
		std::vector<std::pair<int64_t, int64_t>> out;
		size_t pos = 0;
		while (pos < s.size())
		{
			const auto comma = s.find(',', pos);
			const auto entry = s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
			pos = comma == std::string::npos ? s.size() : comma + 1;
			const auto colon = entry.find(':');
			if (colon == std::string::npos) continue;
			try { out.emplace_back(std::stoll(entry.substr(0, colon)), std::stoll(entry.substr(colon + 1))); }
			catch (...) {}
		}
		return out;
	}

	// rolls a fresh collection for a location at the given level; empty drops if the level table
	// has no row for it
	inline Db::TownCollect Roll(const TownLocationLvMst& row)
	{
		static thread_local std::mt19937 rng{ std::random_device{}() };
		auto between = [&](int32_t lo, int32_t hi) { if (hi < lo) std::swap(lo, hi); return std::uniform_int_distribution<int32_t>(lo, hi)(rng); };
		auto chance = [&](int32_t pct) { return pct >= 100 || (pct > 0 && std::uniform_int_distribution<int32_t>(1, 100)(rng) <= pct); };

		const auto pool = ParsePairs(row.item_info);
		int64_t total = 0;
		for (const auto& [id, w] : pool) total += std::max<int64_t>(0, w);

		Db::TownCollect c;
		c.tap_cnt = between(row.tap_cnt_start, row.tap_cnt_end);
		for (int32_t i = 0; i < c.tap_cnt; ++i)
		{
			int64_t item = 0;
			if (total > 0)
			{
				auto pick = std::uniform_int_distribution<int64_t>(1, total)(rng);
				for (const auto& [id, w] : pool)
				{
					if (w <= 0) continue;
					pick -= w;
					if (pick <= 0) { item = id; break; }
				}
			}
			const int32_t zel = chance(row.zel_rate) ? between(row.zel_start, row.zel_end) : 0;
			const int32_t karma = chance(row.karma_rate) ? between(row.karma_start, row.karma_end) : 0;
			if (i) c.drops += ',';
			c.drops += std::to_string(item) + ':' + std::to_string(zel) + ':' + std::to_string(karma);
		}
		c.refill_at = 0;
		return c;
	}

	inline const TownLocationLvMst* LevelRow(int32_t locationId, int32_t lv)
	{
		const TownLocationLvMst* best = nullptr;
		for (const auto& r : theServer()->cache().initializeResp().town_location_lv)
			if (r.id == locationId && r.lv <= lv && (!best || r.lv > best->lv)) best = &r;
		return best;
	}

	inline drogon::Task<Lists> Build(const Db::Player& p, const std::vector<UserClearMissionInfo>& clears)
	{
		Lists out;
		std::unordered_set<int32_t> cleared;
		for (const auto& c : clears) cleared.insert(c.mission_id);
		const auto& init = theServer()->cache().initializeResp();
		const auto facilities = co_await Db::LoadTown(p.user_id, 1);
		const auto locations = co_await Db::LoadTown(p.user_id, 2);
		auto collects = co_await Db::LoadCollect(p.user_id);
		const auto now = Now();

		std::unordered_set<int32_t> seen;
		for (const auto& f : init.town_facility)
		{
			if (f.need_mission_id != 0 && !cleared.contains(f.need_mission_id)) continue;
			const auto it = facilities.find(f.id);
			UserTownFacilityInfo fi = {};
			fi.user_id = p.user_id;
			fi.facility_id = f.id;
			fi.lv = it != facilities.end() ? it->second.lv : 1;
			fi.karma = it != facilities.end() ? it->second.karma : 0;
			out.facilities.emplace_back(fi);
			for (const auto& lv : init.town_facility_lv)
			{
				if (lv.id != f.id || lv.lv > fi.lv) continue;
				for (const auto rid : lv.release_receipe)
				{
					if (!seen.insert(rid).second) continue;
					PermitReceipe pr = {};
					pr.recipe_id = rid;
					pr.cnt = 0;     // creations done (hides the recipe once >= its limit, unless -1)
					pr.limit = -1;  // no time limit (0 reads as expired)
					out.recipes.emplace_back(pr);
				}
			}
		}
		for (const auto& l : init.town_location)
		{
			const bool open = l.need_mission_id == 0 || cleared.contains(l.need_mission_id);
			const auto it = locations.find(l.id);
			const int32_t lv = it != locations.end() ? it->second.lv : 1;
			if (open)
			{
				UserTownLocationInfo li = {};
				li.user_id = p.user_id;
				li.location_id = l.id;
				li.lv = lv;
				li.karma = it != locations.end() ? it->second.karma : 0;
				out.locations.emplace_back(li);
			}

			// the collection record: every location gets one; an open, emptied one refills when due
			UserTownLocationDetail d = {};
			d.user_id = p.user_id;
			d.location_id = l.id;
			auto cit = collects.find(l.id);
			if (open && (cit == collects.end() || (cit->second.tap_cnt <= 0 && cit->second.refill_at <= now)))
			{
				if (const auto* row = LevelRow(l.id, lv))
				{
					auto rolled = Roll(*row);
					LOG_DEBUG << "TownState: user " << p.user_id << " location " << l.id << " lv " << row->lv << " refilled: " << rolled.tap_cnt << " taps " << rolled.drops;
					co_await Db::SaveCollect(p.user_id, l.id, rolled);
					cit = collects.insert_or_assign(l.id, std::move(rolled)).first;
				}
			}
			if (cit != collects.end())
			{
				d.tap_cnt = cit->second.tap_cnt;
				d.drop_item_info = cit->second.drops;
			}
			out.details.emplace_back(d);
		}
		co_return out;
	}
}
