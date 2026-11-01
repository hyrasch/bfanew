#include "App.hpp"
#include "Handlers.hpp"

/*
* DungeonEventUpdate ("BjAt1D6b", encode key k5EiNe9x, URL /actionSymbol/4iqcLm1s.php): the client
* reports a story event (area / dungeon / mission event scene, the town's first-visit dialogue with
* Karl...) as watched. Queued through ConnectRequestList by MissionEventScene / AreaEventScene /
* DungeonEventScene::updateEvent and sent with the next connection; the body is
* BaseRequest::createDungeonEventUserInfoTag: the login info carrying the client's own state
* strings (N4XVE1uA = UserScenarioInfoList::getSaveStr, e.g. "80,0@0@1@0&|0,0" after the Tower of
* Mistral's dungeon event: per land "dungeon id,land flag@event id@event type@area event id&...|land
* id,flag"; 9yVsu21R = ReplayScenarioInfo's id list, "randall," after the town's first visit). The
* server stores both verbatim (players.scenario / special_scenario) and UserInfo echoes them, so the
* client's UserScenarioInfoList::parse restores what was watched and the dialogues play once.
*/

HANDLEF(DungeonEventUpdate)
{
	DungeonEventUpdateReq req = {};
	const auto& ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(req, json);
	if (ec)
	{
		const auto& fmte = glz::format_error(ec, json);
		LOG_DEBUG << "Gme DungeonEventUpdate Error during JSON read: " << fmte;
		co_return HandleResult::error("Deserialization error", fmte);
	}
	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player)
		co_return HandleResult::error("Unknown user", req.login_info.user_id);
	auto& p = *player;
	if (!req.login_info.user_scenario_info.empty())
		p.scenario = req.login_info.user_scenario_info;
	if (!req.login_info.user_special_scenario_info.empty())
		p.special_scenario = req.login_info.user_special_scenario_info;
	co_await Db::SavePlayer(p);
	LOG_DEBUG << "DungeonEventUpdate: user " << p.user_id << " scenario \"" << p.scenario << "\" special \"" << p.special_scenario << "\"";
	co_return HandleResult::success("{}");
}
