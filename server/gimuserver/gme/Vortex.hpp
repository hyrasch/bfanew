#pragma once

#include "App.hpp"
#include "NewPlayer.hpp"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <set>

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
*
* An entry is open on a day when the day's weekday is in `weekdays` (or the list is empty), the
* day's week falls on one of `weeks` of a `cycle_weeks`-week rotation (weeks start on Monday,
* numbered from 1970-01-05, so week N of the cycle is the same for every entry), and the day is
* inside `dates` when given; `hours` then cuts the day into [start, end) windows. `missions`
* restricts the permits to a subset of the dungeons' phase missions (a Super Metal Parade on
* weekends without the plain one, say). GET /bfanew/vortex?at=YYYY-MM-DD[THH:MM] prints what is
* open at a game-clock time (DevController).
*/
namespace Vortex
{
	constexpr int32_t kLand = 99;

	struct Window
	{
		bool open = false;
		int64_t ends_in = 0; // seconds until the current window closes (0 = no limit)
	};

	// seconds since epoch -> the game clock's day number (days since 1970-01-01) and seconds into it
	inline int64_t GameDay(int64_t now, int32_t utcOffsetHours, int64_t& secOfDay)
	{
		const int64_t shifted = now + static_cast<int64_t>(utcOffsetHours) * 3600;
		const int64_t day = shifted >= 0 ? shifted / 86400 : -((-shifted + 86399) / 86400);
		secOfDay = shifted - day * 86400;
		return day;
	}

	inline int Weekday(int64_t day) { return static_cast<int>(((day % 7) + 7 + 4) % 7); } // 1970-01-01 was a Thursday (4), 0 = Sunday
	inline int64_t WeekOf(int64_t day) { const int64_t d = day + 3; return d >= 0 ? d / 7 : -((-d + 6) / 7); } // weeks start on Monday (1970-01-05 = week 1)

	// "YYYY-MM-DD" -> day number (days_from_civil); INT64_MIN when malformed
	inline int64_t DayOf(const std::string& date)
	{
		int y = 0, m = 0, d = 0;
		if (std::sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 || d > 31) return INT64_MIN;
		y -= m <= 2;
		const int64_t era = (y >= 0 ? y : y - 399) / 400;
		const int64_t yoe = y - era * 400;
		const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
		const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
		return era * 146097 + doe - 719468;
	}

	// day number -> "YYYY-MM-DD" (civil_from_days)
	inline std::string DateOf(int64_t day)
	{
		const int64_t z = day + 719468;
		const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
		const int64_t doe = z - era * 146097;
		const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
		const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
		const int64_t mp = (5 * doy + 2) / 153;
		const int64_t d = doy - (153 * mp + 2) / 5 + 1;
		const int64_t m = mp + (mp < 10 ? 3 : -9);
		const int64_t y = yoe + era * 400 + (m <= 2);
		char buf[16];
		std::snprintf(buf, sizeof buf, "%04lld-%02lld-%02lld", static_cast<long long>(y), static_cast<long long>(m), static_cast<long long>(d));
		return buf;
	}

	// whether the entry is open at all on a game-clock day (weekday, cycle week, date range)
	inline bool DayOpen(const VortexEntry& e, int64_t day)
	{
		if (!e.weekdays.empty() && std::find(e.weekdays.begin(), e.weekdays.end(), Weekday(day)) == e.weekdays.end())
			return false;
		if (e.cycle_weeks > 0 && !e.weeks.empty())
		{
			const int32_t w = static_cast<int32_t>(((WeekOf(day) % e.cycle_weeks) + e.cycle_weeks) % e.cycle_weeks);
			if (std::find(e.weeks.begin(), e.weeks.end(), w) == e.weeks.end())
				return false;
		}
		if (!e.dates.empty())
		{
			const int64_t from = DayOf(e.dates[0]);
			const int64_t to = e.dates.size() > 1 ? DayOf(e.dates[1]) : from;
			if (from == INT64_MIN || to == INT64_MIN || day < from || day > to)
				return false;
		}
		return true;
	}

	inline Window WindowOf(const VortexEntry& e, int64_t now, int32_t utcOffsetHours)
	{
		int64_t sec;
		const int64_t day = GameDay(now, utcOffsetHours, sec);
		Window w;
		if (!DayOpen(e, day))
			return w;
		if (e.hours.empty())
		{
			w.open = true;
			if (!e.weekdays.empty() || e.cycle_weeks > 0 || !e.dates.empty())
			{
				// until the end of the last consecutive open day (at most a year ahead)
				int64_t end = 86400 - sec;
				for (int64_t d = day + 1; DayOpen(e, d) && end < 366 * 86400; d++)
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
					if (e.missions.empty() || std::find(e.missions.begin(), e.missions.end(), m) != e.missions.end())
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
			if (std::find(e.dungeons.begin(), e.dungeons.end(), dungeon) != e.dungeons.end()
				&& (e.missions.empty() || std::find(e.missions.begin(), e.missions.end(), missionId) != e.missions.end())
				&& WindowOf(e, now, sched.utc_offset_hours).open)
				return true;
		return false;
	}

	// a plain-text report of the schedule at a time (GET /bfanew/vortex)
	inline std::string Describe(int64_t now = Now())
	{
		const auto& sched = theServer()->cache().vortex();
		static const char* kDays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
		int64_t sec;
		const int64_t day = GameDay(now, sched.utc_offset_hours, sec);
		std::string s = "game clock " + DateOf(day) + " " + kDays[Weekday(day)];
		char buf[64];
		std::snprintf(buf, sizeof buf, " %02lld:%02lld (UTC%+d), week %lld\n", static_cast<long long>(sec / 3600), static_cast<long long>(sec % 3600 / 60), sched.utc_offset_hours, static_cast<long long>(WeekOf(day)));
		s += buf;
		std::set<int32_t> cycles;
		for (const auto& e : sched.entries) if (e.cycle_weeks > 0) cycles.insert(e.cycle_weeks);
		for (const auto c : cycles)
			s += "  cycle of " + std::to_string(c) + " weeks: week " + std::to_string(((WeekOf(day) % c) + c) % c) + "\n";
		for (const auto& e : sched.entries)
		{
			const auto w = WindowOf(e, now, sched.utc_offset_hours);
			s += (w.open ? "OPEN   " : "closed ") + e.name + " (area " + std::to_string(e.area) + ", dungeons";
			for (const auto d : e.dungeons) s += " " + std::to_string(d);
			s += ")";
			if (w.open && w.ends_in > 0)
			{
				std::snprintf(buf, sizeof buf, ", ends in %lldd %02lldh %02lldm", static_cast<long long>(w.ends_in / 86400), static_cast<long long>(w.ends_in % 86400 / 3600), static_cast<long long>(w.ends_in % 3600 / 60));
				s += buf;
			}
			s += "\n";
		}
		return s;
	}
}
