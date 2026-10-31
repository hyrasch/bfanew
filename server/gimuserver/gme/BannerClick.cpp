#include "App.hpp"
#include "Handlers.hpp"

/*
* bfanew: BannerClick ("a5k36D28", encode key a63Ghbi2): BannerLinkTopScene::sendBannerClickRequest
* sends it (async) when a Links banner (banner_info.json, MENU > Links) is tapped, right before
* the system browser opens the banner's url. Body: login info, signal key, version and group
* Pk5F1vhx with one node {oL71Fz3a: banner id}. A click counter for gumi; unanswered, the client
* shows "Unsupported request: a5k36D28" once it comes back from the browser. An empty object
* is enough.
*/

HANDLEF(BannerClick)
{
	const auto& body = json;
	const auto p = body.find("\"oL71Fz3a\":\"");
	LOG_DEBUG << "BannerClick banner " << (p == std::string::npos ? std::string("?") : body.substr(p + 12, body.find('"', p + 12) - p - 12));
	co_return HandleResult::success("{}");
}
