#include "App.hpp"
#include "Handlers.hpp"
#include "Friends.hpp"
#include <cstdio>

#include <gimuserver/gme/NewPlayer.hpp>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

/*
* MissionEnd ("9TvyNR5H", encode key oINq0rfUFPx5MgmT): sent by MissionResultScene::initConnect
* when the last battle is over, with the run's serial id, the mission state (2 = cleared), the
* pickups (zel, karma, items, BC/HC) and battle statistics (MissionEndRequest::createBody).
*
* Reply: "F5Vs19mb" MissionReward (what the result screen shows: exp, zel, karma, level up,
* the places opened), "fEi17cnx" the updated wallet, "UT1SVg59" the cleared mission and
* "yXNM8kL3" the newly permitted places, so the world map opens the next mission.
*
* The mission's own reward and what it unlocks come from deploy/battle/<mission_id>.json
* (tools/build_battles.py: missions.json xp/zel/karma and the requires chain). Everything is
* written to the player (wallet, level, clears, permits, item pickups).
*/

HANDLEF(MissionEnd)
{
	MissionEndReq req = {};
	const auto& ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(req, json);
	if (ec)
	{
		const auto& fmte = glz::format_error(ec, json);
		LOG_DEBUG << "Gme MissionEnd Error during JSON read: " << fmte;
		co_return HandleResult::error("Deserialization error", fmte);
	}

	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player)
		co_return HandleResult::error("Unknown user", req.login_info.user_id);
	auto& p = *player;

	// The request carries the serial id, not the mission id: MissionStart remembered the pair.
	const auto run = co_await Db::RunFor(req.mission.serial_id, p.user_id);
	const int32_t missionId = run.mission_id;
	if (missionId == 0)
	{
		LOG_WARN << "MissionEnd: unknown run " << req.mission.serial_id << " for user " << p.user_id;
		co_return HandleResult::error("Unknown mission run", req.mission.serial_id);
	}
	const bool cleared = req.mission.state == 2;
	LOG_DEBUG << "MissionEnd: user " << p.user_id << " mission " << missionId << " state " << req.mission.state
		<< " zel " << req.result.zel << " karma " << req.result.karma << " items " << req.result.items;

	MissionBattleData data = {};
	const auto path = std::filesystem::path("battle") / (std::to_string(missionId) + ".json");
	if (std::ifstream in(path, std::ios::binary); in)
	{
		std::stringstream ss;
		ss << in.rdbuf();
		const std::string text = ss.str();
		if (const auto& ec1 = glz::read<glz::opts{.error_on_unknown_keys = false}>(data, text); ec1)
			LOG_WARN << "MissionEnd: bad battle data " << path.string() << ": " << glz::format_error(ec1, text);
	}
	else
		LOG_WARN << "MissionEnd: no battle data at " << path.string();

	MissionEndResp resp = {};

	const int32_t beforeLv = p.team.level;

	auto& r = resp.reward;
	r.clear_bonus = "";
	r.lvup_flg = 0;
	r.before_lv = beforeLv;
	r.inc_exp = cleared ? data.exp : 0;
	r.rein_req_state = 0;
	if (!run.helper.empty())
	{
		// bfanew: a helper earns friend points (Friends::kFriendPoints for a friend, kNormalPoints
		// otherwise) and a non-friend gets the "send a friend request?" prompt (MissionResultFriendRequestScene)
		const bool isFriend = co_await Friends::AreFriends(p.user_id, run.helper);
		p.team.friend_point += isFriend ? Friends::kFriendPoints : Friends::kNormalPoints;
		r.rein_req_state = isFriend ? 0 : 1;
		LOG_DEBUG << "MissionEnd: helper " << run.helper << (isFriend ? " (friend)" : "") << ", friend points " << p.team.friend_point;
	}
	r.clear_dungeon_id = "";
	r.clear_area_id = "";
	r.clear_mission_id = "";
	r.reward_units = "";
	r.zel = req.result.zel + (cleared ? data.zel : 0);
	r.karma = req.result.karma + (cleared ? data.karma : 0);
	r.exp_up_type = "0";
	r.zel_up_type = "0";
	r.karma_up_type = "0";
	r.exp_guild = 0;
	r.karma_guild = 0;
	r.zel_guild = 0;
	r.e_token = 0;
	r.repeat_clear_bonus = "";

	p.team.zel += r.zel;
	p.team.karma += r.karma;
	p.team.exp += r.inc_exp;

	// first clear: the mission's own rewards (F_MISSION_MST missionRewards, deploy/battle first_clear,
	// "type:id:count") plus one gem for every mission, because gems are free here (see CLAUDE.md
	// "Free economy"). The types as the client reads them: 8 gems (MissionResultBaseScene::
	// getRewardBraveCoin), 3 zel (getRewardBonusZel), 11 karma (getRewardBonusKaruma), 17 SP
	// (getRewardBonusSp, a 2016 currency, ignored); those go back in 92ms7J5M as "type:id:count,..."
	// (three fields per entry, the amount is the third). 6 is a unit (into the box, announced in
	// 3MAT6quo so the "Units Acquired" result page, MissionResultUnitScene, shows it), 7 a sphere
	// and 4 an item (into the warehouse, the result screens do not show them).
	if (cleared && !(co_await Db::IsCleared(p.user_id, missionId)))
	{
		int32_t gems = 1, zel = 0, karma = 0;
		std::string items;
		for (const auto& e : data.first_clear)
		{
			int type = 0, id = 0, count = 0;
			if (std::sscanf(e.c_str(), "%d:%d:%d", &type, &id, &count) < 3) continue;
			count = std::max(count, 1);
			switch (type)
			{
			case 8: gems += count; break;
			case 3: zel += count; break;
			case 11: karma += count; break;
			case 6:
			{
				const auto& cache = theServer()->cache();
				const auto* def = cache.unit(id);
				if (!def || !cache.phase().hasUnit(id))
				{
					LOG_WARN << "MissionEnd: first-clear unit " << id << " of mission " << missionId << (def ? " is out of phase" : " is not in units.json") << ", not granted";
					break;
				}
				for (int i = 0; i < count; i++)
				{
					UserUnitInfo u = NewPlayer::UnitFromTable(id, *def);
					co_await Db::AddUnit(p.user_id, u);
					if (!r.reward_units.empty()) r.reward_units += ",";
					// unitId:userUnitId:secret:1 (MissionResultUnitScene's constructor keeps entries of 4+
					// fields whose 4th is "1"; field 1 is the UnitMst / thumbnail id, field 3 != 0 shows the
					// "?" frame first and reveals the unit; an unknown unit id there is a null deref)
					r.reward_units += std::to_string(id) + ":" + std::to_string(u.user_unit_id) + ":1:1";
				}
				break;
			}
			case 4: case 7:
				if (!items.empty()) items += ",";
				items += std::to_string(id) + ":" + std::to_string(count);
				break;
			default:
				LOG_WARN << "MissionEnd: first-clear reward type " << type << " (id " << id << ") not granted for mission " << missionId;
			}
		}
		r.clear_bonus = "8:0:" + std::to_string(gems);
		if (zel > 0) r.clear_bonus += ",3:0:" + std::to_string(zel);
		if (karma > 0) r.clear_bonus += ",11:0:" + std::to_string(karma);
		p.team.brave_coin += gems; // the gem balance the client shows and spends (03UGMHxF)
		p.team.free_gems += gems;  // the free / paid split, kept in step
		p.team.zel += zel;
		p.team.karma += karma;
		if (!items.empty()) co_await Db::AddItems(p.user_id, items);
		LOG_DEBUG << "MissionEnd: first clear of " << missionId << " -> " << r.clear_bonus << (r.reward_units.empty() ? "" : " units " + r.reward_units) << (items.empty() ? "" : " items " + items);
	}

	// level from the level table (user_level.json): the highest level whose exp is reached
	int32_t newLv = beforeLv;
	for (const auto& lv : theServer()->cache().initializeResp().progression)
		if (lv.exp <= p.team.exp && lv.level > newLv)
			newLv = lv.level;
	if (newLv != beforeLv)
	{
		r.lvup_flg = 1;
		NewPlayer::ApplyLevel(p.team, newLv, true); // a level up refills the energy
	}
	Db::RegenEnergy(p);
	co_await Db::SavePlayer(p);

	if (cleared)
	{
		const auto now = std::chrono::system_clock::now();
		const std::time_t t = std::chrono::system_clock::to_time_t(now);
		char date[32] = {};
		std::tm tm = {};
#ifdef _WIN32
		localtime_s(&tm, &t);
#else
		localtime_r(&t, &tm);
#endif
		std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &tm);
		co_await Db::AddClear(p.user_id, missionId, date);
		co_await Db::AddItems(p.user_id, req.result.items);
	}
	// items used during the battles are gone whatever the outcome
	if (!req.result.items_used.empty())
	{
		LOG_DEBUG << "MissionEnd: user " << p.user_id << " used items " << req.result.items_used;
		co_await Db::TakeItems(p.user_id, req.result.items_used);
	}
	resp.warehouse_info = co_await Db::LoadItems(p.user_id);
	resp.equip_info = co_await Db::LoadEquip(p.user_id);
	// the whole box, always: an empty 4ceMWH6k would wipe the client's list (UserUnitInfoResponse::
	// readParam clears it first), and a first-clear unit has to show up in it
	resp.unit_info = co_await Db::LoadUnits(p.user_id);

	if (cleared)
	{

		for (const auto& u : data.unlocks)
		{
			if (r.clear_mission_id.empty()) r.clear_mission_id = u.mission_id;
			co_await Db::AddPermit(p.user_id, NewPlayer::Mission, u.mission_id);
			if (!u.dungeon_id.empty() && u.dungeon_id != "0")
			{
				co_await Db::AddPermit(p.user_id, NewPlayer::Dungeon, u.dungeon_id);
				co_await Db::AddPermit(p.user_id, NewPlayer::Area, u.area_id);
				co_await Db::AddPermit(p.user_id, NewPlayer::Land, u.land_id);
			}
		}
		resp.clear_mission_info = co_await Db::LoadClears(p.user_id);
		resp.permit_place = co_await Db::LoadPermits(p.user_id);
	}

	resp.team_info = p.team;

	std::string buffer{};
	const auto& ec2 = glz::write_json(resp, buffer);
	if (ec2)
	{
		const auto& glze = glz::format_error(ec2, buffer);
		LOG_DEBUG << "Gme MissionEnd Error during JSON writing: " << glze;
		co_return HandleResult::error("Serialization error", glze);
	}

	co_return HandleResult::success(buffer);
}
