#include "App.hpp"
#include "Handlers.hpp"

/*
* bfanew: MissionContinue ("p8B2i9rJ", encode key G3FwvQfy5hcxHMen, URL /actionSymbol/vcn2pb38.php):
* MissionGameOverScene::initContinue sends it when the player answers Yes to "Continue?" after
* the whole squad died ("You can use N Gem to revive all Units", N = DefineMst ContinuDiaCnt, 0
* here, see CLAUDE.md "Free economy"); loopContinue then calls MissionScene::requestContinue and
* the battle goes on with the squad revived. Body: group Kz7qfSs5 {k9cxD7Ba serial id, j3g5P4cq
* state} and group 5PR2VmH1 {k9cxD7Ba, K2gIYm0h the battle's suspend data (MissionScene::
* createSuspendData), not stored}. Unanswered, the client popped "Unsupported request: p8B2i9rJ"
* and stayed on the dialog. The reply is the wallet.
*/

HANDLEF(MissionContinue)
{
	MissionContinueReq req = {};
	const auto& ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(req, json);
	if (ec)
	{
		const auto& fmte = glz::format_error(ec, json);
		LOG_DEBUG << "Gme MissionContinue Error during JSON read: " << fmte;
		co_return HandleResult::error("Deserialization error", fmte);
	}

	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player)
		co_return HandleResult::error("Unknown user", req.login_info.user_id);
	auto& p = *player;

	const int32_t missionId = co_await Db::MissionForRun(req.mission.serial_id, p.user_id);
	if (missionId == 0)
		LOG_WARN << "MissionContinue: unknown run " << req.mission.serial_id << " for user " << p.user_id;
	else
		LOG_DEBUG << "MissionContinue: user " << p.user_id << " continues mission " << missionId << " (free)";

	Db::RegenEnergy(p);
	MissionContinueResp resp = {};
	resp.team_info = p.team;

	std::string buffer{};
	const auto& ec2 = glz::write_json(resp, buffer);
	if (ec2)
		co_return HandleResult::error("Serialization error", glz::format_error(ec2, buffer));
	co_return HandleResult::success(buffer);
}
