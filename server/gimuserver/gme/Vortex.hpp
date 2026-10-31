#pragma once

#include "App.hpp"
#include "NewPlayer.hpp"
#include <chrono>
#include <ctime>

/*
* bfanew: the Vortex (land 99). The client lists, under the quest map's Vortex button
* (AreaSPSelectScene), the special areas and dungeons that have a PermitPlace record (yXNM8kL3 in
* UserInfo / UpdatePermitPlaceInfo; AreaMstList::getActiveList, isLockDungeon ->
* PermitPlaceInfo::isEnterable, strPeriod -> period_text with OPEN_PERIOD). So a schedule is a list
* of permits with the seconds left: deploy/system/vortex_schedule.json (weekday / hour windows in
* the game's clock, UTC + utc_offset_hours) -> the permits for the entries open now, plus the
* missions of those dungeons (deploy/phase.json dungeon_missions, from the filtered F_MISSION_MST)
* so that the mission list inside is unlocked too. MissionStart refuses a Vortex mission whose
* dungeon is not open (Vortex::IsOpen).
*/
namespace Vortex
{
	constexpr int32_t kLand = 99;

	struct Window
	{
		bool open = false;
		int64_t ends_in = 0; // seconds until the current window closes (0 = no limit)
	};

	// seconds since epoch -> the game clock's weekday (0 = Sunday) and seconds into the day
	inline void GameClock(int64_t now, int32_t utcOffsetHours, int& weekday, int64_t& secOfDay)
	{
		const int64_t shifted = now + static_cast<int64_t>(utcOffsetHours) * 3600;
		const int64_t day = shifted >= 0 ? shifted / 86400 : -((-shifted + 86399) / 86400);
		secOfDay = shifted - day * 86400;
		weekday = static_cast<int>(((day % 7) + 7 + 4) % 7); // 1970-01-01 was a Thursday (4)
	}

	inline Window WindowOf(const VortexEntry& e, int64_t now, int32_t utcOffsetHours)
	{
		int weekday; int64_t sec;
		GameClock(now, utcOffsetHours, weekday, sec);
		Window w;
		if (!e.weekdays.empty() && std::find(e.weekdays.begin(), e.weekdays.end(), weekday) == e.weekdays.end())
			return w;
		if (e.hours.empty())
		{
			w.open = true;
			if (!e.weekdays.empty())
			{
				// until the end of the last consecutive open day
				int64_t end = 86400 - sec;
				for (int d = (weekday + 1) % 7; std::find(e.weekdays.begin(), e.weekdays.end(), d) != e.weekdays.end() && end < 7 * 86400; d = (d + 1) % 7)
					end += 86400;
				w.ends_in = end;
			}
			return w;
		}
		for (const auto& h : e.hours)
		{
			if (h.size() < 2) continue;
			const int64_t from = h[0] * 3600, to = h[1] * 3600;
			if (sec >= from && sec < to) { w.open = true; w.ends_in = to - sec; return w; }
		}
		return w;
	}

	inline int64_t Now()
	{
		return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}

	// the permits for everything open now
	inline std::vector<PermitPlace> Permits(int64_t now = Now())
	{
		std::vector<PermitPlace> out;
		const auto& sched = theServer()->cache().vortex();
		const auto& phase = theServer()->cache().phase();
		std::unordered_set<int32_t> areas, dungeons;
		for (const auto& e : sched.entries)
		{
			const auto w = WindowOf(e, now, sched.utc_offset_hours);
			if (!w.open) continue;
			if (areas.insert(e.area).second)
				NewPlayer::AddPermit(out, NewPlayer::Area, std::to_string(e.area));
			// an area listed under a folder area (F_AREA_MST sHMai2ZG) shows only with the folder permitted
			if (const auto parent = phase.areaParent(e.area); parent && areas.insert(parent).second)
				NewPlayer::AddPermit(out, NewPlayer::Area, std::to_string(parent));
			for (const auto d : e.dungeons)
			{
				if (!dungeons.insert(d).second) continue;
				NewPlayer::AddPermit(out, NewPlayer::Dungeon, std::to_string(d));
				auto& p = out.back();
				p.time_limit = static_cast<int32_t>(std::min<int64_t>(w.ends_in, INT32_MAX));
				p.period_text = e.period_text;
				for (const auto m : phase.dungeonMissions(d))
					NewPlayer::AddPermit(out, NewPlayer::Mission, std::to_string(m));
			}
		}
		if (!out.empty())
		{
			NewPlayer::AddPermit(out, NewPlayer::Gate, std::to_string(kLand));
			NewPlayer::AddPermit(out, NewPlayer::Land, std::to_string(kLand));
		}
		return out;
	}

	// whether the Vortex dungeon holding this mission is open now (true for non-Vortex missions)
	inline bool IsOpen(int32_t missionId, int64_t now = Now())
	{
		const auto& phase = theServer()->cache().phase();
		const auto dungeon = phase.vortexDungeonOf(missionId);
		if (dungeon == 0) return true;
		const auto& sched = theServer()->cache().vortex();
		for (const auto& e : sched.entries)
			if (std::find(e.dungeons.begin(), e.dungeons.end(), dungeon) != e.dungeons.end() && WindowOf(e, now, sched.utc_offset_hours).open)
				return true;
		return false;
	}
}
