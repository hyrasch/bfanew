#include "App.hpp"
#include "DevController.hpp"

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
