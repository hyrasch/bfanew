#include "App.hpp"
#include "DevController.hpp"

#include <gimuserver/gme/NewPlayer.hpp>
#include <gimuserver/gme/Vortex.hpp>

using namespace drogon;

void DevController::HandleReload(const HttpRequestPtr& rq, std::function<void(const HttpResponsePtr&)>&& callback)
{
	auto resp = HttpResponse::newHttpResponse();
	resp->setContentTypeCode(ContentType::CT_TEXT_PLAIN);
	try
	{
		theServer()->reloadCache();
		LOG_INFO << "bfanew: cache reloaded from " << rq->getPeerAddr().toIpPort();
		resp->setStatusCode(k200OK);
		resp->setBody("reloaded");
	}
	catch (const std::exception& ex)
	{
		LOG_ERROR << "bfanew: cache reload failed: " << ex.what();
		resp->setStatusCode(k500InternalServerError);
		resp->setBody(std::string("reload failed: ") + ex.what());
	}
	callback(resp);
}

void DevController::HandleVortex(const HttpRequestPtr& rq, std::function<void(const HttpResponsePtr&)>&& callback)
{
	auto resp = HttpResponse::newHttpResponse();
	resp->setContentTypeCode(ContentType::CT_TEXT_PLAIN);
	int64_t now = Vortex::Now();
	const auto at = rq->getParameter("at");
	if (!at.empty())
	{
		int h = 0, mi = 0;
		const int64_t day = Vortex::DayOf(at);
		if (day == INT64_MIN)
		{
			resp->setStatusCode(k400BadRequest);
			resp->setBody("usage: /bfanew/vortex?at=YYYY-MM-DD[THH:MM] (game clock)");
			callback(resp);
			return;
		}
		if (const auto t = at.find('T'); t != std::string::npos)
			std::sscanf(at.c_str() + t + 1, "%d:%d", &h, &mi);
		now = day * 86400 + h * 3600 + mi * 60 - static_cast<int64_t>(theServer()->cache().vortex().utc_offset_hours) * 3600;
	}
	resp->setStatusCode(k200OK);
	resp->setBody(Vortex::Describe(now) + "permits: " + std::to_string(Vortex::Permits(now).size()) + "\n");
	callback(resp);
}

Task<> DevController::HandleGive(HttpRequestPtr rq, std::function<void(const HttpResponsePtr&)> callback)
{
	auto resp = HttpResponse::newHttpResponse();
	resp->setContentTypeCode(ContentType::CT_TEXT_PLAIN);
	const auto user = rq->getParameter("user");
	const auto unitId = std::atoi(rq->getParameter("unit").c_str());
	const auto itemId = rq->getParameter("item");
	int count = std::atoi(rq->getParameter("count").c_str());
	if (count <= 0) count = 1;
	const int lv = std::atoi(rq->getParameter("lv").c_str());
	const int level = std::atoi(rq->getParameter("level").c_str());
	const int keyId = std::atoi(rq->getParameter("key").c_str());
	const auto permitMission = rq->getParameter("mission"); // + dungeon, area, land, gate: permits (NewPlayer::Place kinds)
	const auto* def = theServer()->cache().unit(unitId);
	const auto* itemDef = theServer()->cache().item(itemId);
	if (user.empty() || (!def && !itemDef && level <= 0 && keyId <= 0 && permitMission.empty()))
	{
		resp->setStatusCode(k400BadRequest);
		resp->setBody("usage: /bfanew/give?user=<gumi user id or user id>&unit=<unit id>|item=<item id>&count=1&lv=1 | &level=<player level> | &key=<dungeon key id>&count=N | &mission=<id>&dungeon=<id>&area=<id>&land=<id> (permits)");
		callback(resp);
		co_return;
	}
	LoginInfoReq login = {};
	login.user_id = user;
	login.gumi_live_userid = user;
	auto player = co_await Db::LoadPlayer(login);
	if (!player)
	{
		resp->setStatusCode(k404NotFound);
		resp->setBody("no such player");
		callback(resp);
		co_return;
	}
	if (keyId > 0)
	{
		auto keys = co_await Db::LoadKeys(player->user_id);
		auto& st = keys[keyId];
		st.count += std::max(count, 1);
		co_await Db::SaveKey(player->user_id, keyId, st.count, st.last_day);
		resp->setStatusCode(k200OK);
		resp->setBody("player " + player->user_id + " key " + std::to_string(keyId) + ": " + std::to_string(st.count));
		callback(resp);
		co_return;
	}
	if (!permitMission.empty())
	{
		// bfanew: open a mission on the client (dev tests of battle data): the mission and the places that hold it
		std::string done;
		for (const auto& [kind, name] : { std::pair{ NewPlayer::Mission, "mission" }, std::pair{ NewPlayer::Dungeon, "dungeon" }, std::pair{ NewPlayer::Area, "area" }, std::pair{ NewPlayer::Land, "land" }, std::pair{ NewPlayer::Gate, "gate" } })
		{
			const auto id = rq->getParameter(name);
			if (id.empty()) continue;
			co_await Db::AddPermit(player->user_id, kind, id);
			done += std::string(name) + " " + id + " ";
		}
		resp->setStatusCode(k200OK);
		resp->setBody("player " + player->user_id + " permitted: " + done);
		callback(resp);
		co_return;
	}
	if (level > 0)
	{
		NewPlayer::ApplyLevel(player->team, level, true);
		co_await Db::SavePlayer(*player);
		LOG_INFO << "bfanew: put " << player->user_id << " at level " << level;
		resp->setStatusCode(k200OK);
		resp->setBody("player " + player->user_id + " at level " + std::to_string(player->team.level) + ", energy " + std::to_string(player->team.action_point) + "/" + std::to_string(player->team.max_action_point));
		callback(resp);
		co_return;
	}
	if (itemDef)
	{
		co_await Db::AddItems(player->user_id, itemId + ":" + std::to_string(count));
		LOG_INFO << "bfanew: gave " << count << " x item " << itemId << " (" << itemDef->name << ") to " << player->user_id;
		resp->setStatusCode(k200OK);
		resp->setBody("gave item " + itemId + " (" + itemDef->name + ") x" + std::to_string(count) + " to " + player->user_id);
		callback(resp);
		co_return;
	}
	std::string out;
	for (int i = 0; i < count; i++)
	{
		UserUnitInfo u = NewPlayer::UnitFromTable(unitId, *def);
		if (lv > 1) NewPlayer::SetUnitLevel(u, *def, lv);
		co_await Db::AddUnit(player->user_id, u);
		out += std::to_string(u.user_unit_id) + " ";
	}
	LOG_INFO << "bfanew: gave " << count << " x unit " << unitId << " to " << player->user_id;
	resp->setStatusCode(k200OK);
	resp->setBody("gave unit " + std::to_string(unitId) + " x" + std::to_string(count) + " to " + player->user_id + ": user_unit_id " + out);
	callback(resp);
}
