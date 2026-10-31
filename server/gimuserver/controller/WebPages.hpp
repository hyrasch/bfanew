#pragma once

/**
* bfanew: pages for the client's in-app web view (BraveFrontierJNI.showWebView), served by
* BfWebController::HandleWebPage. The client builds their URLs as "http://" + game host + "/" +
* path, so they arrive with a doubled slash ("//bf/web/...") and LayerWebView appends "?os=2";
* the Links page (BannerTopScene, banner_info.json) appends "?friend_id=<the player's friend id>"
* when a banner's param says so, which is how the rates page knows whose box to mark.
*/
namespace WebPages
{
	/*! /bf/web/rates/index.php?friend_id=...: the summon pools with odds and owned marks. */
	drogon::Task<> Rates(drogon::HttpRequestPtr rq, std::function<void(const drogon::HttpResponsePtr&)> callback);

	/*! /bf/web/notice/indexCtrl.php: the INFO page (phase, links). */
	void Notice(const drogon::HttpRequestPtr& rq, const std::function<void(const drogon::HttpResponsePtr&)>& callback);
}
