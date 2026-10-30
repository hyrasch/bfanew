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
	void HandleReload(const drogon::HttpRequestPtr& rq, std::function<void(const drogon::HttpResponsePtr&)>&& callback);

	METHOD_LIST_BEGIN
		ADD_METHOD_TO(DevController::HandleReload, "/bfanew/reload", drogon::Get);
	METHOD_LIST_END
};
