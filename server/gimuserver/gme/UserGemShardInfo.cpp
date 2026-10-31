#include "App.hpp"
#include "Handlers.hpp"

/*
* bfanew: UserGemShardInfo ("29slks49", encode key 930sDd3i): the gem shop (Shop tab > the gem
* button, ShopBuyCoinScene::initConnect) asks for the player's gem shards (a 2017 feature:
* fragments awarded with gem purchases, P_GEM_SHARD_TO_GEM) right when it opens, even with
* features.json buy_coin_free_shard off. Body: login info + signal key. Reply: s9d3lksS, a list
* of UserGemShardInfo {1ZF3zLrC id, J3uDa91b count} (UserGemShardInfoResponse::readParam clears
* the client's list first). Unanswered, "Unsupported request: 29slks49" popped up and the
* client restarted on OK. Nobody buys gems here (see CLAUDE.md "Free economy"): an empty list.
* The products of the scene come from PurchaseMstList (M_PURCHASE_MST), empty in phase 1.
*/

HANDLEF(UserGemShardInfo)
{
	co_return HandleResult::success("{\"s9d3lksS\":[]}");
}
