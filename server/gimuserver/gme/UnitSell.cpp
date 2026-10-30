#include "App.hpp"
#include "Handlers.hpp"

/*
* UnitSell ("Ri3uTq9b", encode key 92VqcGFWuPkmT60U): the client sells units locally ("Units sold
* for N Zel") and sends the list when leaving the Sell Units screen (UnitSellRequest::addResult).
* The server removes the units (and their squad slots), credits the sell prices from the unit
* table (deploy/units.json) and answers the wallet and the whole box.
*/

HANDLEF(UnitSell)
{
	UnitSellReq req = {};
	const auto& ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(req, json);
	if (ec)
	{
		const auto& fmte = glz::format_error(ec, json);
		LOG_DEBUG << "Gme UnitSell Error during JSON read: " << fmte;
		co_return HandleResult::error("Deserialization error", fmte);
	}

	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player)
		co_return HandleResult::error("Unknown user", req.login_info.user_id);
	auto& p = *player;
	const auto& cache = theServer()->cache();

	const auto units = co_await Db::LoadUnits(p.user_id);
	int64_t zel = 0;
	std::vector<int32_t> gone;
	for (const auto& e : req.units)
	{
		const UserUnitInfo* u = nullptr;
		for (const auto& x : units) if (x.user_unit_id == e.user_unit_id) { u = &x; break; }
		if (!u)
		{
			LOG_WARN << "UnitSell: user " << p.user_id << " does not own unit " << e.user_unit_id;
			continue;
		}
		const auto* def = cache.unit(u->unit_id);
		zel += def ? def->sell_price : 0;
		gone.emplace_back(u->user_unit_id);
	}
	if (gone.empty())
		co_return HandleResult::error("Nothing to sell");
	if (zel != req.cost.zel)
		LOG_WARN << "UnitSell: client expected " << req.cost.zel << " zel, table says " << zel;

	co_await Db::RemoveUnits(p.user_id, gone);
	p.team.zel += zel;
	co_await Db::SavePlayer(p);
	LOG_DEBUG << "UnitSell: user " << p.user_id << " sold " << gone.size() << " units for " << zel << " zel";

	UnitSellResp resp = {};
	resp.team_info = p.team;
	resp.unit_info = co_await Db::LoadUnits(p.user_id);

	std::string buffer{};
	const auto& ec2 = glz::write_json(resp, buffer);
	if (ec2)
	{
		const auto& glze = glz::format_error(ec2, buffer);
		LOG_DEBUG << "Gme UnitSell Error during JSON writing: " << glze;
		co_return HandleResult::error("Serialization error", glze);
	}

	co_return HandleResult::success(buffer);
}
