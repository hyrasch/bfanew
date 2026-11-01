#pragma once
#include "App.hpp"
#include "NewPlayer.hpp"
#include "Friends.hpp"
#include <string>
#include <vector>

/*
* bfanew: the Gift Box (CLAUDE.md "Gifts"; net/present.kdl has the ids, keys and fields).
*
* A present is a row of player_presents (db/Player.hpp PresentRow): a type, a target (unit or item
* id), a count, a message, the day it was sent and the day it expires. PresentList sends the
* unclaimed ones as UserPresentInfo records, PresentReceipt pays one or all of them into the
* wallet / box / warehouse and marks them claimed.
*
* The daily login bonus: the first UserInfo of a game day (the Vortex schedule's clock) drops the
* day's present of a 7-day cycle in the box (kLoginBonus), the way the 2014 login bonuses reached
* the player through the Gift Box; the streak counter picks the day and resets after a missed day.
*/
namespace Presents
{
	// PresentTopScene::getPresentName: 8 "N Gems", 3 "N Zel", 11 "N Karma", 2 "N Friend Points", 20 summoner
	// points, 5 / 4 / 7 / 19 an item (ItemMstList by target id: 7 is the sphere frame), 6 / 15 a unit
	enum Type : int32_t { Honor = 2, Zel = 3, Item = 5, Unit = 6, Sphere = 7, Gems = 8, Karma = 11 };

	constexpr int64_t kExpireDays = 30;

	struct Bonus { int32_t type; int32_t target; int32_t count; const char* text; };
	// the 7-day cycle (ours: the 2014 table is not documented), day 7 gives a gem
	inline const std::vector<Bonus>& LoginBonus()
	{
		static const std::vector<Bonus> table = {
			{ Zel, 0, 1000, "Login Bonus Day 1: 1,000 Zel" },
			{ Karma, 0, 500, "Login Bonus Day 2: 500 Karma" },
			{ Honor, 0, 50, "Login Bonus Day 3: 50 Honor Points" },
			{ Item, 20000, 3, "Login Bonus Day 4: 3 Cures" },
			{ Zel, 0, 3000, "Login Bonus Day 5: 3,000 Zel" },
			{ Karma, 0, 1500, "Login Bonus Day 6: 1,500 Karma" },
			{ Gems, 0, 1, "Login Bonus Day 7: 1 Gem" },
		};
		return table;
	}

	inline int64_t GameDay(int64_t now)
	{
		const int64_t local = now + static_cast<int64_t>(theServer()->cache().vortex().utc_offset_hours) * 3600;
		return local >= 0 ? local / 86400 : (local - 86399) / 86400;
	}

	inline UserPresentInfo Record(const Db::PresentRow& r, const std::string& userId)
	{
		UserPresentInfo p = {};
		p.present_id = std::to_string(r.id);
		p.user_id = userId;
		p.present_type = r.type;
		p.target_id = r.target_id;
		p.target_cnt = r.count;
		p.target_param = ""; // a unit's "id:type:lv" extras made the row title "Fencer Vargas: MST_EXTRAPA..." (an extra skill text key); the icon and name come from target_id
		p.receipt_type = 0;
		// fAi8Th5s is the EXPIRY: PresentTopScene::getPresentList keeps the presents whose
		// UserPresentInfo::getPresentTimeLeft (this minus the server time) is positive and sorts them by it
		const int64_t expires = r.expires ? r.expires : r.created + kExpireDays * 86400;
		p.present_date = static_cast<int32_t>(expires);
		p.present_date_str = Friends::GameClock(expires);
		p.receipt_date = 0;
		p.receipt_date_str = "";
		p.description = r.description;
		p.is_receipt = 0;
		p.operation = 1; // add / replace in the client's list (3 removes)
		return p;
	}

	/*! Pays a present into the player: wallet changes stay in p.team (caller saves), units and items
	 *  go to the tables. Returns an error text when it cannot be taken (box full). */
	inline drogon::Task<std::string> Give(Db::Player& p, const Db::PresentRow& r)
	{
		switch (r.type)
		{
		case Gems: p.team.brave_coin += r.count; p.team.free_gems += r.count; break;
		case Zel: p.team.zel += r.count; break;
		case Karma: p.team.karma += r.count; break;
		case Honor: p.team.friend_point += r.count; break;
		case Item:
		case Sphere: co_await Db::AddItems(p.user_id, std::to_string(r.target_id) + ":" + std::to_string(std::max(1, r.count))); break;
		case Unit:
		{
			const auto* def = theServer()->cache().unit(r.target_id);
			if (!def) co_return "This unit does not exist";
			const auto units = co_await Db::LoadUnits(p.user_id);
			if (static_cast<int32_t>(units.size()) >= p.team.max_unit_count) co_return "You are at max Unit capacity";
			auto u = NewPlayer::UnitFromTable(r.target_id, *def, 1);
			co_await Db::AddUnit(p.user_id, u);
			break;
		}
		default: co_return "Unknown present type " + std::to_string(r.type);
		}
		co_return "";
	}
}
