#include "App.hpp"
#include "DevController.hpp"

#include <gimuserver/gme/NewPlayer.hpp>

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
	const auto* def = theServer()->cache().unit(unitId);
	const auto* itemDef = theServer()->cache().item(itemId);
	if (user.empty() || (!def && !itemDef))
	{
		resp->setStatusCode(k400BadRequest);
		resp->setBody("usage: /bfanew/give?user=<gumi user id or user id>&unit=<unit id>|item=<item id>&count=1 (unknown unit / item?)");
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
