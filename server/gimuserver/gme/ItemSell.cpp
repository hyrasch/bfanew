#include "App.hpp"
#include "Handlers.hpp"

/*
* ItemSell ("qDQerU74", encode key 73aFNjPu): items sold on the Sell Items screen. Like UnitSell the
* client updates its warehouse locally (ItemSellListScene::sell) and sends the sold list
* ("M73i1c5U": warehouse item index + count) through ItemSellConnectScene. The server removes the
* items, credits the sell prices from deploy/items.json and answers the wallet and the whole
* warehouse.
*/

HANDLEF(ItemSell)
{
	ItemSellReq req = {};
	const auto& ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(req, json);
	if (ec)
	{
		const auto& fmte = glz::format_error(ec, json);
		LOG_DEBUG << "Gme ItemSell Error during JSON read: " << fmte;
		co_return HandleResult::error("Deserialization error", fmte);
	}

	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player)
		co_return HandleResult::error("Unknown user", req.login_info.user_id);
	auto& p = *player;
	const auto& cache = theServer()->cache();

	const auto warehouse = co_await Db::LoadItems(p.user_id);
	int64_t zel = 0;
	std::string sold;
	for (const auto& e : req.items)
	{
		// the item index we send is the item id
		const UserWarehouseInfo* w = nullptr;
		for (const auto& x : warehouse) if (x.item_index == e.item_index) { w = &x; break; }
		if (!w || e.item_num <= 0)
		{
			LOG_WARN << "ItemSell: user " << p.user_id << " does not own item index " << e.item_index;
			continue;
		}
		const auto n = std::min(e.item_num, w->item_num);
		const auto* def = cache.item(w->item_id);
		zel += static_cast<int64_t>(def ? def->sell_price : 0) * n;
		if (!sold.empty()) sold += ',';
		sold += w->item_id + ":" + std::to_string(n);
	}
	if (sold.empty())
		co_return HandleResult::error("Nothing to sell");

	co_await Db::TakeItems(p.user_id, sold);
	p.team.zel += zel;
	co_await Db::SavePlayer(p);
	LOG_DEBUG << "ItemSell: user " << p.user_id << " sold " << sold << " for " << zel << " zel";

	ItemSellResp resp = {};
	resp.team_info = p.team;
	resp.warehouse_info = co_await Db::LoadItems(p.user_id);

	std::string buffer{};
	const auto& ec2 = glz::write_json(resp, buffer);
	if (ec2)
	{
		const auto& glze = glz::format_error(ec2, buffer);
		LOG_DEBUG << "Gme ItemSell Error during JSON writing: " << glze;
		co_return HandleResult::error("Serialization error", glze);
	}

	co_return HandleResult::success(buffer);
}
