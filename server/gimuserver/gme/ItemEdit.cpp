#include "App.hpp"
#include "Handlers.hpp"

/*
* ItemEdit ("ruoB7bD8", encode key DHEfRexCu0q5TAQm): the items equipped for quests. Sent when
* leaving Manage Items (ItemEditTopScene), by the slot editor (ItemEditSelectCntScene::setEqpItem,
* ItemEditListScene::removeEqp), by the "equip all" shortcuts (GameUtils::eqpItemFull / eqpItemEmpty)
* and by the bonus-item screens. The request carries the WHOLE equipped list ("71U5wzhI", one entry
* per slot: item id, slot, count) and, separately, the bonus items ("nAligJSQ", ignored for now).
*
* The server keeps the list (player_equip), capped at what the warehouse holds, and answers the
* stored list and the warehouse (both replace the client's lists).
*/

HANDLEF(ItemEdit)
{
	ItemEditReq req = {};
	const auto& ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(req, json);
	if (ec)
	{
		const auto& fmte = glz::format_error(ec, json);
		LOG_DEBUG << "Gme ItemEdit Error during JSON read: " << fmte;
		co_return HandleResult::error("Deserialization error", fmte);
	}

	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player)
		co_return HandleResult::error("Unknown user", req.login_info.user_id);
	auto& p = *player;

	const auto warehouse = co_await Db::LoadItems(p.user_id);
	auto owned = [&](const std::string& itemId) -> int32_t {
		for (const auto& w : warehouse) if (w.item_id == itemId) return w.item_num;
		return 0;
	};

	std::vector<UserEquipItemInfo> equip;
	for (const auto& e : req.equip)
	{
		const auto have = owned(e.item_id);
		if (have <= 0 || e.item_num <= 0)
		{
			LOG_WARN << "ItemEdit: user " << p.user_id << " equips item " << e.item_id << " x" << e.item_num << " but owns " << have;
			continue;
		}
		UserEquipItemInfo u = {};
		u.item_id = e.item_id;
		u.disp_order = e.disp_order;
		u.item_num = std::min(e.item_num, have);
		equip.emplace_back(u);
	}
	co_await Db::SaveEquip(p.user_id, equip);
	LOG_DEBUG << "ItemEdit: user " << p.user_id << " equips " << equip.size() << " item slots";

	ItemEditResp resp = {};
	resp.equip_info = equip;
	resp.warehouse_info = warehouse;

	std::string buffer{};
	const auto& ec2 = glz::write_json(resp, buffer);
	if (ec2)
	{
		const auto& glze = glz::format_error(ec2, buffer);
		LOG_DEBUG << "Gme ItemEdit Error during JSON writing: " << glze;
		co_return HandleResult::error("Serialization error", glze);
	}

	co_return HandleResult::success(buffer);
}
