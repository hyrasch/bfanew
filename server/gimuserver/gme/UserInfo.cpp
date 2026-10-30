#include "App.hpp"
#include "Handlers.hpp"
#include "StubPlayer.hpp"

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

	UserInfoResp resp = theServer()->cache().userInfoResp(); // copy !!

	// TODO: A real server should check if user_id == gumi token...

    // TODO: GET THIS FROM A CACHE TOKEN ETC
    resp.login_info.user_id = StubPlayer::kUserId;

    // TEMP HACK!! Skip tutorial flag and put a real name
    resp.login_info.account_id = StubPlayer::kAccountId;
    resp.login_info.handle_name = StubPlayer::kHandleName;
    resp.login_info.tutorial_end_flag = true;
    resp.login_info.tutorial_status = 0;
    resp.login_info.feature_gate = 0;

    StubPlayer::FillTeam(resp.team_info, theServer()->cache().serverConfig());

    resp.unit_info.emplace_back(StubPlayer::StarterUnit());

    StubPlayer::FillQuestProgress(resp);

    for (int i = 0; i < 10; i++) {
        UserPartyDeckInfo deck = {};
        deck.deck_num = i;
        deck.deck_type = 1;
        deck.user_unit_id = 100; // Now maps to id from user_units
        resp.party_deck_info.emplace_back(deck);
    }

    // phase 1: no login campaign (id 0 keeps the "Login Campaign" popup from showing)
    resp.campaign_info.current_day = 0;
    resp.campaign_info.total_days = 0;
    resp.campaign_info.first_for_the_day = false;
    resp.campaign_info.id = 0;

    resp.summoner_journal.user_id = req.login_info.user_id; // we are really trusting the client here (bad)
    resp.signal_key.key = "5EdKHavF";


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
