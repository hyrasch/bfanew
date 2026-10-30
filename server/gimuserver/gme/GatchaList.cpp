#include "App.hpp"
#include "Handlers.hpp"

/*
* GatchaList ("Uo86DcRh"): sent by SummonsListTopScene, SummonsCategorizationScene,
* SummonsFixUnitSelectScene and after a summon (GachaActionScene::noticeOK).
* The client parses the top-level "1IR86sAv" key with GachaInfoResponse, which is the
* same banner list that UserInfo carries, so answer with the cached gacha_info table.
* The stub error reply used to kill the client one second after the home screen loaded.
*/
HANDLEF(GatchaList)
{
	GatchaListResp resp = {};
	resp.gacha_info = theServer()->cache().userInfoResp().gacha_info; // copy

	std::string buffer{};
	const auto& ec = glz::write_json(resp, buffer);
	if (ec)
	{
		const auto& glze = glz::format_error(ec, buffer);
		LOG_DEBUG << "Gme GatchaList Error during JSON writing: " << glze;
		co_return HandleResult::error("Serialization error", glze);
	}

	co_return HandleResult::success(buffer);
}
