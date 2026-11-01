#include "App.hpp"
#include "Handlers.hpp"
#include "Tutorial.hpp"

#include <gimuserver/utils/Random.hpp>

HANDLEF(Initialize)
{
	InitializeReq req = {};
	const auto& ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(req, json);
	if (ec)
	{
		const auto& fmte = glz::format_error(ec, json);
		LOG_DEBUG << "Gme Initialize Error during JSON read: " << fmte;
		co_return HandleResult::error("Deserialization error", fmte);
	}

	// NOTE: A real server would verify the gumi token first...
	// TODO: Handle MSTs to answer

	InitializeResp resp = theServer()->cache().initializeResp(); // copy !!

#if 0
	const auto& res = co_await theDb()->execSqlCoro("SELECT id, username, debug_mode FROM userinfo WHERE gumi_user_id=$1", req.login_info.gumi_live_userid);
	if (res.empty())
	{
		// Gumi user does not exist! Create a new user and add it to the database

		const auto& cache = theServer()->cache();
		const auto& scfg = cache.serverConfig();
		const auto& def = cache.initializeResp().defines;

		// No handle! We are a new user after all!
		resp.login_info.account_id = "1111";

		co_await theDb()->execSqlCoro("INSERT INTO userinfo (id, gumi_user_id, device_id, debug_mode, "
			"level, "
			"max_unit_count, max_friend_count, "
			"zel, karma, brave_coin, "
			"max_warehouse_count, free_gems, energy) "
			"VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13);",
			// id, gumi_user_id, device_id, debug_mode
			resp.login_info.account_id, req.login_info.gumi_live_userid, req.login_info.device_id, false,
			// level
			scfg.initialLevel,
			// max_unit_count, max_friend_count
			50, 200,
			// zel, karma, brave_coin
			scfg.initialZel, scfg.initialKarma, scfg.initialBraveCoins,
			// max_warehouse, free_gems, energy
			10, 5, 20);
	}
	else
	{
		// only one query pls
		const auto& sql = res[0];
		size_t col = 0;
		resp.login_info.account_id = sql[col++].as<std::string>();
		resp.login_info.handle_name = sql[col++].as<std::string>();
		resp.login_info.debug_mode = sql[col++].as<bool>();
	}
#endif


	// bfanew: the player behind the gumi account from /accounts/guest/login/ (created on first login)
	const auto player = co_await Db::PlayerForAccount(req.login_info.gumi_live_userid);
	resp.login_info.account_id = player.account_id;
	resp.login_info.handle_name = player.handle_name;
	resp.login_info.user_id = player.user_id; // the client keeps it and sends it in every request
	// a player the tutorial has not named yet does not exist for the client (UserInfo::existUser is
	// the user id): with an id the LoginScene asks for UserInfo and stops on EMPTY_PARTY_INFO,
	// without one it goes to TutorialScene1 (name, element) and CreateUser answers the id
	// (gme/Tutorial.hpp)
	if (!player.tutorial_end && player.handle_name.empty())
		resp.login_info.user_id = "";
	resp.login_info.tutorial_end_flag = player.tutorial_end;
	resp.login_info.tutorial_status = Tutorial::Status(player); // the chapter (gme/Tutorial.hpp)

	//resp.user_info.gumi_live_token = req.user_info.gumi_live_token;
	//resp.user_info.gumi_live_userid = req.user_info.gumi_live_userid;

	resp.signal_key.key = "C7vnXA5T";

	resp.challenge_arena_user_info.user_id = "n9ZMPC0t"; // rank name?
	resp.challenge_arena_user_info.unkstr2 = "F"; // ranking?
	resp.challenge_arena_user_info.league_id = 1;

	resp.summoner_journal.user_id = resp.login_info.user_id;

	// bfanew: the Daily Login Spin (2016) has no feature flag of its own in the client (the home
	// shortcut icon is always built, HomeScene2::initOverlayMenus) and an id of 0 here crashes the
	// home scene (null reward lookup), so phase 1 keeps the campaign with 0 spins (see CLAUDE.md
	// "Feature gating")
	resp.daily_login_rewards.id = 1;
	resp.daily_login_rewards.current_day = 1;
	resp.daily_login_rewards.message = " day(s) more to guaranteed Gem!";

	std::string buffer{};
	const auto& ec2 = glz::write_json(resp, buffer);
	if (ec2)
	{
		const auto& glze = glz::format_error(ec2, buffer);
		LOG_DEBUG << "Gme Initialize Error during JSON writing: " << glze;
		co_return HandleResult::error("Serialization error", glze);
	}

	co_return HandleResult::success(buffer);
}

