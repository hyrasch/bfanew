#include "App.hpp"
#include "Handlers.hpp"
#include "StubPlayer.hpp"

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
* (tools/build_battles.py: missions.json xp/zel/karma and the requires chain).
*
* STUB: nothing is persisted, so the next UserInfo forgets the clear; the level-up is computed
* from the initial exp (0) + this mission's exp against the level table.
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

	// The request carries the serial id, not the mission id: MissionStart remembered the pair.
	const int32_t lastMission = theServer()->missionForRun(req.mission.serial_id);
	if (lastMission == 0)
	{
		LOG_WARN << "MissionEnd: unknown run " << req.mission.serial_id;
		co_return HandleResult::error("Unknown mission run", req.mission.serial_id);
	}
	const bool cleared = req.mission.state == 2;
	LOG_DEBUG << "MissionEnd: serial " << req.mission.serial_id << " state " << req.mission.state
		<< " mission " << lastMission << " zel " << req.result.zel << " karma " << req.result.karma
		<< " items " << req.result.items;

	MissionBattleData data = {};
	const auto path = std::filesystem::path("battle") / (std::to_string(lastMission) + ".json");
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

	const auto& scfg = theServer()->cache().serverConfig();
	MissionEndResp resp = {};

	StubPlayer::FillTeam(resp.team_info, scfg);
	const int32_t beforeLv = resp.team_info.level;
	const int64_t beforeExp = resp.team_info.exp;

	auto& r = resp.reward;
	r.clear_bonus = "";
	r.lvup_flg = 0;
	r.before_lv = beforeLv;
	r.inc_exp = cleared ? data.exp : 0;
	r.rein_req_state = 0;
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

	resp.team_info.zel += r.zel;
	resp.team_info.karma += r.karma;
	resp.team_info.exp = beforeExp + r.inc_exp;

	// level from the level table (user_level.json): the highest level whose exp is reached
	int32_t newLv = beforeLv;
	for (const auto& lv : theServer()->cache().initializeResp().progression)
		if (lv.exp <= resp.team_info.exp && lv.level > newLv)
			newLv = lv.level;
	if (newLv != beforeLv)
	{
		r.lvup_flg = 1;
		resp.team_info.level = newLv;
		for (const auto& lv : theServer()->cache().initializeResp().progression)
			if (lv.level == newLv)
			{
				resp.team_info.deck_cost = lv.deck_cost;
				resp.team_info.max_action_point = resp.team_info.action_point = lv.action_points; // level up refills energy
				resp.team_info.max_fight_point = resp.team_info.fight_point = lv.unk;
				resp.team_info.max_friend_count = lv.friend_count;
				break;
			}
	}
	else
		resp.team_info.action_point = std::max<int32_t>(0, resp.team_info.action_point - data.energy_use);

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
		resp.clear_mission_info.emplace_back(StubPlayer::ClearedMission(lastMission, date));

		for (const auto& u : data.unlocks)
		{
			if (r.clear_mission_id.empty()) r.clear_mission_id = u.mission_id;
			StubPlayer::AddPermit(resp.permit_place, StubPlayer::Place::Mission, u.mission_id);
			if (!u.dungeon_id.empty() && u.dungeon_id != "0")
			{
				StubPlayer::AddPermit(resp.permit_place, StubPlayer::Place::Dungeon, u.dungeon_id);
				StubPlayer::AddPermit(resp.permit_place, StubPlayer::Place::Area, u.area_id);
				StubPlayer::AddPermit(resp.permit_place, StubPlayer::Place::Land, u.land_id);
			}
		}
	}

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
