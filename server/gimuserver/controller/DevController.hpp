#pragma once

/**
* bfanew development endpoints (no auth, local use only).
*/
class DevController : public drogon::HttpController<DevController>
{
public:
	/*!
	* GET /bfanew/reload: re-read deploy/system/*.json into the server cache.
	*/
	/*!
	* bfanew dev helper: gives a unit or an item to a player
	* (GET /bfanew/give?user=<gumi user id or user id>&unit=<unit id>|item=<item id>&count=1&lv=1),
	* so that fusion / evolution materials can be tested without a drop source.
	*/
	drogon::Task<> HandleGive(drogon::HttpRequestPtr rq, std::function<void(const drogon::HttpResponsePtr&)> callback);

	void HandleReload(const drogon::HttpRequestPtr& rq, std::function<void(const drogon::HttpResponsePtr&)>&& callback);

	METHOD_LIST_BEGIN
		ADD_METHOD_TO(DevController::HandleReload, "/bfanew/reload", drogon::Get);
		ADD_METHOD_TO(DevController::HandleGive, "/bfanew/give", drogon::Get);
	METHOD_LIST_END
};
