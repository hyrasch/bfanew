#include "App.hpp"
#include "Handlers.hpp"
#include "TownState.hpp"

/*
* TownFacilityUpdate ("8v43tz7g", encode key rq7Yd1nG, URL /actionSymbol/kba85fVc.php): the
* village's Upgrades screen. MyTownFacilityExtScene2::injectionFacility is local: it adds karma to
* a facility's / location's UserTownFacilityInfo::karma, levels it up when the next
* TownFacilityLvMst / TownLocationLvMst karma is reached, takes the karma from UserTeamInfo and
* counts it in UserState::addInjectionKarma; the request goes out with the next connection
* (ConnectRequestList): group EuY6L7AX {HTVh8a65: karma injected}, YRgx49WG the whole facility
* list and yj46Q2xw the whole location list as the client now has them (id, lv, karma toward the
* next level). The server checks the karma, stores the levels (player_town) and answers the
* wallet, both lists and the permitted recipes the new facility levels release.
*/

namespace
{
	// is (lv, karma) reachable from the stored (lv0, karma0) with `budget` karma according to the
	// level table? Returns the karma it costs, or -1.
	template <typename Rows, typename IdOf, typename LvOf, typename KarmaOf>
	int64_t CostOf(const Rows& rows, IdOf idOf, LvOf lvOf, KarmaOf karmaOf, int32_t id, int32_t lv0, int32_t karma0, int32_t lv, int32_t karma)
	{
		if (lv < lv0 || (lv == lv0 && karma < karma0)) return -1;
		int64_t cost = -karma0;
		for (int32_t l = lv0 + 1; l <= lv; ++l)
		{
			bool found = false;
			for (const auto& r : rows)
				if (idOf(r) == id && lvOf(r) == l) { cost += karmaOf(r); found = true; break; }
			if (!found) return -1;
		}
		return cost + karma;
	}
}

HANDLEF(TownFacilityUpdate)
{
	TownFacilityUpdateReq req = {};
	const auto& ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(req, json);
	if (ec)
	{
		const auto& fmte = glz::format_error(ec, json);
		LOG_DEBUG << "Gme TownFacilityUpdate Error during JSON read: " << fmte;
		co_return HandleResult::error("Deserialization error", fmte);
	}

	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player)
		co_return HandleResult::error("Unknown user", req.login_info.user_id);
	auto& p = *player;
	const auto& init = theServer()->cache().initializeResp();
	const auto facilities = co_await Db::LoadTown(p.user_id, 1);
	const auto locations = co_await Db::LoadTown(p.user_id, 2);

	int64_t spent = 0;
	for (const auto& f : req.facilities)
	{
		const auto it = facilities.find(f.facility_id);
		const int32_t lv0 = it != facilities.end() ? it->second.lv : 1, karma0 = it != facilities.end() ? it->second.karma : 0;
		if (f.lv == lv0 && f.karma == karma0) continue;
		const auto cost = CostOf(init.town_facility_lv, [](const auto& r) { return r.id; }, [](const auto& r) { return r.lv; }, [](const auto& r) { return r.karma; },
			f.facility_id, lv0, karma0, f.lv, f.karma);
		if (cost < 0)
		{
			LOG_WARN << "TownFacilityUpdate: user " << p.user_id << " facility " << f.facility_id << " lv " << lv0 << "/" << karma0 << " -> " << f.lv << "/" << f.karma << " refused";
			continue;
		}
		spent += cost;
		co_await Db::SaveTown(p.user_id, 1, f.facility_id, f.lv, f.karma);
	}
	for (const auto& l : req.locations)
	{
		const auto it = locations.find(l.location_id);
		const int32_t lv0 = it != locations.end() ? it->second.lv : 1, karma0 = it != locations.end() ? it->second.karma : 0;
		if (l.lv == lv0 && l.karma == karma0) continue;
		const auto cost = CostOf(init.town_location_lv, [](const auto& r) { return r.id; }, [](const auto& r) { return r.lv; }, [](const auto& r) { return r.karma; },
			l.location_id, lv0, karma0, l.lv, l.karma);
		if (cost < 0)
		{
			LOG_WARN << "TownFacilityUpdate: user " << p.user_id << " location " << l.location_id << " lv " << lv0 << "/" << karma0 << " -> " << l.lv << "/" << l.karma << " refused";
			continue;
		}
		spent += cost;
		co_await Db::SaveTown(p.user_id, 2, l.location_id, l.lv, l.karma);
	}

	const int64_t claimed = req.injection.empty() ? 0 : req.injection.front().karma;
	LOG_DEBUG << "TownFacilityUpdate: user " << p.user_id << " injected " << claimed << " karma (recomputed " << spent << ")";
	p.team.karma = static_cast<int32_t>(std::max<int64_t>(0, p.team.karma - spent));
	co_await Db::SavePlayer(p);

	TownFacilityUpdateResp resp = {};
	resp.team_info = p.team;
	auto town = co_await TownState::Build(p, co_await Db::LoadClears(p.user_id));
	resp.town_facility_info = std::move(town.facilities);
	resp.town_location_info = std::move(town.locations);
	resp.permit_receipes = std::move(town.recipes);

	std::string buffer{};
	const auto& ec2 = glz::write_json(resp, buffer);
	if (ec2)
	{
		const auto& glze = glz::format_error(ec2, buffer);
		LOG_DEBUG << "Gme TownFacilityUpdate Error during JSON writing: " << glze;
		co_return HandleResult::error("Serialization error", glze);
	}

	co_return HandleResult::success(buffer);
}
