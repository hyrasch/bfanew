#include "App.hpp"
#include "Handlers.hpp"
#include "Vortex.hpp"

/*
* UpdatePermitPlaceInfo ("1MJT6L3W", encode key 3zip5Htw, URL /actionSymbol/AsTF24Gn.php): the
* client asks for a fresh permit list when a timed permit ran out
* (PermitPlaceInfoList::exsitsTimeLimitOrverQuest). The body is the login info only. The reply is
* the whole list (yXNM8kL3: the player's stored permits plus the Vortex entries open now).
*/
HANDLEF(UpdatePermitPlaceInfo)
{
	UpdatePermitPlaceInfoReq req = {};
	const auto& ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(req, json);
	if (ec)
	{
		const auto& fmte = glz::format_error(ec, json);
		LOG_DEBUG << "Gme UpdatePermitPlaceInfo Error during JSON read: " << fmte;
		co_return HandleResult::error("Deserialization error", fmte);
	}

	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player)
		co_return HandleResult::error("Unknown user", req.login_info.user_id);
	auto& p = *player;

	UpdatePermitPlaceInfoResp resp = {};
	resp.permit_place = co_await Db::LoadPermits(p.user_id);
	auto parades = co_await Db::LoadParades(p.user_id);
	auto vortex = Vortex::Permits(Vortex::Now(), &parades);
	LOG_DEBUG << "UpdatePermitPlaceInfo: user " << p.user_id << ": " << resp.permit_place.size() << " stored + " << vortex.size() << " Vortex permits";
	resp.permit_place.insert(resp.permit_place.end(), vortex.begin(), vortex.end());

	std::string buffer{};
	const auto& ec2 = glz::write_json(resp, buffer);
	if (ec2)
	{
		const auto& glze = glz::format_error(ec2, buffer);
		LOG_DEBUG << "Gme UpdatePermitPlaceInfo Error during JSON writing: " << glze;
		co_return HandleResult::error("Serialization error", glze);
	}

	co_return HandleResult::success(buffer);
}
