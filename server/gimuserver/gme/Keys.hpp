#pragma once

#include "App.hpp"
#include "Vortex.hpp"
#include <cstdlib>

/*
* bfanew: dungeon keys ("Parade keys"). The 2.19.6 client has the 2015 key system: DungeonKeyMst
* (Initialize 4NG79sX1, deploy/system/dungeon_keys.json: Metal Key -> dungeon 100600, Jewel Key ->
* 100650, each with the weekdays it is handed out, a window length and the usage tiers), the
* player's keys (UserInfo eFU7Qtb0, UserDungeonKeyInfo), and the Vortex shows a key dungeon with
* its key count: tapping it opens DungeonKeyConfirmScene, Yes sends DungeonKeyUse. The 2014 keys
* were daily-login presents on Mon/Tue/Thu/Fri (Metal) and Wednesday (Jewel); the key table
* carries exactly those weekdays (fK15a73g), so Infos() grants one key a day on them, at login,
* instead of the Randall key building (closed in phase 1). CLAUDE.md "Parade keys".
*/
namespace Keys
{
	inline const DungeonKeyMst* Mst(int32_t keyId)
	{
		for (const auto& k : theServer()->cache().initializeResp().dungeon_keys)
			if (k.id == keyId) return &k;
		return nullptr;
	}

	// "1,2,4,5" holds weekday wd (0 = Sunday)?
	inline bool OnWeekday(const std::string& days, int wd)
	{
		size_t i = 0;
		while (i < days.size())
		{
			const auto j = days.find(',', i);
			const auto f = days.substr(i, (j == std::string::npos ? days.size() : j) - i);
			if (!f.empty() && std::atoi(f.c_str()) == wd) return true;
			if (j == std::string::npos) break;
			i = j + 1;
		}
		return false;
	}

	// the missions the tier of `useCnt` keys opens, from the usage pattern
	// "keys:event:name:missions:x/keys:event:name:missions:x" ("" when no tier takes that many)
	inline std::string PatternMissions(const DungeonKeyMst& k, int useCnt)
	{
		size_t i = 0;
		const auto& s = k.usage_pattern;
		while (i < s.size())
		{
			const auto j = s.find('/', i);
			const auto seg = s.substr(i, (j == std::string::npos ? s.size() : j) - i);
			std::vector<std::string> f;
			size_t a = 0;
			while (true)
			{
				const auto c = seg.find(':', a);
				f.push_back(seg.substr(a, (c == std::string::npos ? seg.size() : c) - a));
				if (c == std::string::npos) break;
				a = c + 1;
			}
			if (f.size() >= 4 && std::atoi(f[0].c_str()) == useCnt) return f[3];
			if (j == std::string::npos) break;
			i = j + 1;
		}
		return "";
	}

	// the player's keys, after the daily grant (one key per key table row whose weekdays hold
	// today's game-clock weekday, once per day, capped at the possession limit)
	inline drogon::Task<std::vector<UserDungeonKeyInfo>> Infos(std::string userId, int64_t now = Vortex::Now())
	{
		const auto& sched = theServer()->cache().vortex();
		int64_t sec;
		const int64_t day = Vortex::GameDay(now, sched.utc_offset_hours, sec);
		const int wd = Vortex::Weekday(day);
		auto state = co_await Db::LoadKeys(userId);
		std::vector<UserDungeonKeyInfo> out;
		for (const auto& k : theServer()->cache().initializeResp().dungeon_keys)
		{
			auto& st = state[k.id];
			if (k.distribute_flag && st.last_day != day && OnWeekday(k.distribute_weekdays, wd))
			{
				const int limit = std::atoi(k.possession_limit.c_str());
				st.count = std::min(st.count + 1, limit > 0 ? limit : 50);
				st.last_day = day;
				co_await Db::SaveKey(userId, k.id, st.count, st.last_day);
				LOG_DEBUG << "Keys: user " << userId << " gets a " << k.name << " (" << st.count << " now)";
			}
			UserDungeonKeyInfo u = {};
			u.user_id = userId;
			u.key_id = k.id;
			u.possession = st.count;
			u.last_receipt_day = 0;
			u.active_type = 1;
			u.receipt_possible = false;
			u.next_receipt_date = "";
			u.cnt = st.count;
			out.push_back(u);
		}
		co_return out;
	}
}
