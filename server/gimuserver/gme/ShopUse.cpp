#include "App.hpp"
#include "Handlers.hpp"

/*
* ShopUse ("xe8tiSf4", encode key qthMXTQSkz3KfH9R, URL /actionSymbol/T10fSAQ6.php): everything the
* client pays gems for outside the summons. The scenes fill UserState (setShopUseType, setBravecoin
* = the price from the define table, setExtCnt) and ShopUseRequest::createBody sends them as one
* node of group 32ibWjFG: 60IsqxDt type, 03UGMHxF gems, 5gXxT7LZ extension count. Types: 1 energy
* refill (the home screen "+" / GameScene::confirmAnswerYesEnergy, the stamina shortage prompt),
* 10 the same from the result screen's repeat prompt, 2 unit box +ext (ShopUnitBoxExtScene),
* 3 warehouse +ext (ShopItemBoxExtScene), 7 friend slots +5, 5 continue after a defeat, 4 arena
* points, 6 stamp, 8 colosseum, 9 unit info reset, 11 / 12 summoner shop.
*
* bfanew is free: nobody pays gems here. The define table served to the client already prices
* these at 0 (deploy/system/defines.json: ActionPointHealCnt, UnitBoxExtCnt, ItemBoxExtCnt,
* ContinuDiaCnt, FriendExtCnt), and the server never deducts the gems the client reports. The
* reply is the wallet, which the client replaces (energy, box sizes).
*/

HANDLEF(ShopUse)
{
	ShopUseReq req = {};
	const auto& ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(req, json);
	if (ec)
	{
		const auto& fmte = glz::format_error(ec, json);
		LOG_DEBUG << "Gme ShopUse Error during JSON read: " << fmte;
		co_return HandleResult::error("Deserialization error", fmte);
	}

	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player)
		co_return HandleResult::error("Unknown user", req.login_info.user_id);
	auto& p = *player;
	const auto& defs = theServer()->cache().initializeResp();

	for (const auto& s : req.shop)
	{
		LOG_DEBUG << "ShopUse: user " << p.user_id << " type " << s.shop_use_type << " gems " << s.gems << " ext " << s.ext_cnt;
		switch (s.shop_use_type)
		{
		case 1:
		case 10:
			p.team.action_point = p.team.max_action_point;
			p.team.action_rest_timer = 0;
			Db::RegenEnergy(p); // full: resets the regeneration clock
			break;
		case 2:
			p.team.max_unit_count = std::min(p.team.max_unit_count + std::max(s.ext_cnt, 0), 4000);
			break;
		case 3:
			p.team.warehouse_count = std::min(p.team.warehouse_count + std::max(s.ext_cnt, 0), 3200);
			break;
		default:
			// friend slots, continue, arena points, stamps...: nothing to charge, nothing stored yet
			break;
		}
	}
	(void)defs;
	co_await Db::SavePlayer(p);

	ShopUseResp resp = {};
	resp.team_info = p.team;

	std::string buffer{};
	const auto& ec2 = glz::write_json(resp, buffer);
	if (ec2)
	{
		const auto& glze = glz::format_error(ec2, buffer);
		LOG_DEBUG << "Gme ShopUse Error during JSON writing: " << glze;
		co_return HandleResult::error("Serialization error", glze);
	}

	co_return HandleResult::success(buffer);
}
