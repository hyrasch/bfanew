#include "App.hpp"
#include "WebPages.hpp"

#include <gimuserver/gme/NewPlayer.hpp>
#include <unordered_set>

using namespace drogon;

namespace
{
	const char* kStyle =
		"<style>"
		"body{background:#1b1710;color:#e8dcc0;font-family:sans-serif;margin:0;padding:8px 10px;font-size:15px}"
		"h1{font-size:20px;color:#ffd866;margin:6px 0 10px}h2{font-size:17px;color:#ffd866;margin:14px 0 4px;border-bottom:1px solid #6b5a2e}"
		"table{width:100%;border-collapse:collapse}td,th{padding:4px 6px;border-bottom:1px solid #3a3020;text-align:left}th{color:#c9b36a;font-weight:normal}"
		".tier{background:#2c2415;color:#ffd866;font-weight:bold}.r{text-align:right;white-space:nowrap}.own{color:#8ee07a}.no{color:#8a7f6a}"
		"p{line-height:1.4}a{color:#9ad0ff}small{color:#a89c82}"
		"</style>";

	std::string Escape(const std::string& s)
	{
		std::string o;
		for (const char c : s)
			switch (c)
			{
			case '&': o += "&amp;"; break;
			case '<': o += "&lt;"; break;
			case '>': o += "&gt;"; break;
			default: o += c;
			}
		return o;
	}

	std::string Pct(double v)
	{
		char buf[32];
		snprintf(buf, sizeof(buf), v == static_cast<int>(v) ? "%.0f%%" : "%.2f%%", v);
		return buf;
	}
}

Task<> WebPages::Rates(HttpRequestPtr rq, std::function<void(const HttpResponsePtr&)> callback)
{
	const auto& cache = theServer()->cache();
	const auto friendId = rq->getParameter("friend_id");

	// whose box: the Links page appends the player's friend id (= account id)
	std::unordered_set<int32_t> ownedGroups, ownedIds;
	std::string who;
	if (!friendId.empty())
	{
		if (const auto player = co_await Db::LoadPlayerByAccountId(friendId))
		{
			who = player->handle_name.empty() ? "Summoner" : player->handle_name;
			for (const auto& u : co_await Db::LoadUnits(player->user_id))
			{
				ownedIds.insert(u.unit_id);
				if (const auto* def = cache.unit(u.unit_id))
					ownedGroups.insert(def->group ? def->group : u.unit_id);
			}
		}
	}

	std::string html = "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Summon rates</title>";
	html += kStyle;
	html += "</head><body><h1>Summon rates</h1>";
	if (!who.empty())
		html += "<p><small>Box of " + Escape(who) + ": <span class=\"own\">&#10004;</span> = you own this unit or one of its evolutions.</small></p>";
	else
		html += "<p><small>Open this page from the game's Links page to see which units you own.</small></p>";

	const auto& pools = cache.poolsAll();
	for (const auto& [gachaId, pool] : pools)
	{
		html += "<h2>" + Escape(pool.name.empty() ? "Summon " + gachaId : pool.name) + "</h2>";
		html += "<p><small>Each pull lands in a rarity at its rate, then on one of its units with equal odds.</small></p><table>";
		for (const auto& [rarity, weight] : pool.rates)
		{
			const auto it = pool.units.find(rarity);
			const size_t n = it == pool.units.end() ? 0 : it->second.size();
			int32_t total = 0;
			for (const auto& [r, w] : pool.rates) total += std::max(0, w);
			const double tierPct = total > 0 ? 100.0 * weight / total : 0.0;
			html += "<tr class=\"tier\"><td colspan=\"2\">" + rarity + "&#9733; &nbsp;" + std::to_string(n) + " units</td><td class=\"r\">" + Pct(tierPct) + "</td></tr>";
			if (!n) continue;
			const double each = tierPct / static_cast<double>(n);
			for (const auto id : it->second)
			{
				const auto* def = cache.unit(id);
				const bool owned = ownedIds.contains(id) || (def && def->group && ownedGroups.contains(def->group));
				const auto nameIt = pool.names.find(std::to_string(id));
				const std::string name = nameIt != pool.names.end() ? nameIt->second : (def ? def->name : std::to_string(id));
				html += "<tr><td class=\"" + std::string(owned ? "own" : "no") + "\">" + (owned ? "&#10004;" : "&nbsp;") + "</td><td>" + Escape(name) + "</td><td class=\"r\">" + Pct(each) + "</td></tr>";
			}
		}
		html += "</table>";
	}
	if (pools.empty())
		html += "<p>No summon pool is served (deploy/system/phases/NN/gacha_pools.json).</p>";
	html += "</body></html>";

	auto resp = HttpResponse::newHttpResponse();
	resp->setContentTypeCode(CT_TEXT_HTML);
	resp->setBody(html);
	callback(resp);
}

void WebPages::Notice(const HttpRequestPtr& rq, const std::function<void(const HttpResponsePtr&)>& callback)
{
	const auto& cache = theServer()->cache();
	std::string html = "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>News</title>";
	html += kStyle;
	html += "</head><body><h1>bfanew</h1>";
	html += "<p>A phased Brave Frontier server. Phase " + std::to_string(cache.phase().id()) + ": everything Global had up to " + Escape(cache.phase().cutoff()) + ".</p>";
	html += "<p>" + std::to_string(cache.phase().unitCount()) + " units, " + std::to_string(cache.phase().missionCount()) + " missions, " + std::to_string(cache.vortex().entries.size()) + " Vortex schedule entries.</p>";
	html += "<p>Summon odds: Menu &gt; Links &gt; Summon rates (marks the units you own), or <a href=\"/bf/web/rates/index.php\">the plain list</a>.</p>";
	html += "<p><small>Energy refills, unit and item slots are free here; summons cost 1 Gem.</small></p>";
	html += "</body></html>";
	auto resp = HttpResponse::newHttpResponse();
	resp->setContentTypeCode(CT_TEXT_HTML);
	resp->setBody(html);
	callback(resp);
}
