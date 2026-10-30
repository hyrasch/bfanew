#include "App.hpp"
#include "Handlers.hpp"

/*
* UserInfo ("cTZ3W2JG"): the player's state after Initialize. Everything player-specific comes
* from the database (gimuserver/db/Player.hpp); the login-time tables come from the cache.
*/
HANDLEF(UserInfo)
{
	UserInfoReq req = {};
	const auto& ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(req, json);
	if (ec)
	{
		const auto& fmte = glz::format_error(ec, json);
		LOG_DEBUG << "Gme UserInfo Error during JSON read: " << fmte;
		co_return HandleResult::error("Deserialization error", fmte);
	}

	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player)
	{
		LOG_WARN << "UserInfo: unknown user " << req.login_info.user_id << " / gumi " << req.login_info.gumi_live_userid;
		co_return HandleResult::error("Unknown user", req.login_info.user_id);
	}
	auto& p = *player;

	UserInfoResp resp = theServer()->cache().userInfoResp(); // copy !!

	resp.login_info.user_id = p.user_id;
	resp.login_info.account_id = p.account_id;
	resp.login_info.handle_name = p.handle_name;
	resp.login_info.tutorial_end_flag = p.tutorial_end;
	resp.login_info.tutorial_status = 0;
	resp.login_info.feature_gate = 0;

	p.team.user_id = p.user_id;
	resp.team_info = p.team;
	resp.unit_info = co_await Db::LoadUnits(p.user_id);
	resp.party_deck_info = co_await Db::LoadDecks(p.user_id);
	resp.clear_mission_info = co_await Db::LoadClears(p.user_id);
	resp.permit_place = co_await Db::LoadPermits(p.user_id);

	// phase 1: no login campaign (id 0 keeps the "Login Campaign" popup from showing)
	resp.campaign_info.current_day = 0;
	resp.campaign_info.total_days = 0;
	resp.campaign_info.first_for_the_day = false;
	resp.campaign_info.id = 0;

	resp.summoner_journal.user_id = p.user_id;
	resp.signal_key.key = "5EdKHavF";

	co_await Db::SavePlayer(p); // the energy regenerated since the last visit

	std::string buffer{};
	const auto& ec2 = glz::write_json(resp, buffer);
	if (ec2)
	{
		const auto& glze = glz::format_error(ec2, buffer);
		LOG_DEBUG << "Gme UserInfo Error during JSON writing: " << glze;
		co_return HandleResult::error("Serialization error", glze);
	}

	co_return HandleResult::success(buffer);
}
