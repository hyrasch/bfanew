#include "App.hpp"
#include "Handlers.hpp"
#include "Keys.hpp"

/*
* bfanew: DungeonKeyUse ("aGT5S6qZ", encode key 3rPx6tTw, URL /actionSymbol/RmA2a5JM.php): the
* Yes of DungeonKeyConfirmScene (the Vortex's key dungeon tapped: AreaSPSelectScene::updateEvent
* / DungeonSPSelectScene2). Body: login info, signal key, version, group xki65BPq {phe9Ta54 keys
* to spend, 16KMNJLb key id}. The keys open the key's dungeon for DungeonKeyMst limit_sec with
* the missions of the tier that many keys buys (usage pattern: 1 Metal Parade!, 3 Super Metal
* Parade); the window is per player (player_parade) and UserInfo / UpdatePermitPlaceInfo /
* MissionStart honour it. Reply: the keys left (eFU7Qtb0) and the whole permit list (yXNM8kL3)
* with the dungeon enterable and the seconds left, which the client shows as "Remaining".
*/

HANDLEF(DungeonKeyUse)
{
	DungeonKeyUseReq req = {};
	const auto& ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(req, json);
	if (ec)
	{
		const auto& fmte = glz::format_error(ec, json);
		LOG_DEBUG << "Gme DungeonKeyUse Error during JSON read: " << fmte;
		co_return HandleResult::error("Deserialization error", fmte);
	}

	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player)
		co_return HandleResult::error("Unknown user", req.login_info.user_id);
	auto& p = *player;
	const auto& e = req.keys; // ::size(1): the group's one node
	const auto* k = Keys::Mst(e.key_id);
	if (!k)
		co_return HandleResult::error("Unknown key", std::to_string(e.key_id));
	const int use = std::max(e.use_cnt, 1);
	const auto missions = Keys::PatternMissions(*k, use);
	if (missions.empty())
		co_return HandleResult::error("No parade for that many keys", std::to_string(use));

	const int64_t now = Vortex::Now();
	auto keys = co_await Keys::Infos(p.user_id, now); // the daily grant first
	auto state = co_await Db::LoadKeys(p.user_id);
	auto& st = state[k->id];
	if (st.count < use)
	{
		LOG_WARN << "DungeonKeyUse: user " << p.user_id << " has " << st.count << " of key " << k->id << ", asked to use " << use;
		co_return HandleResult::error("Not enough keys", std::to_string(st.count));
	}
	st.count -= use;
	co_await Db::SaveKey(p.user_id, k->id, st.count, st.last_day);
	const int64_t until = now + std::max(k->limit_sec, 60);
	co_await Db::SaveParade(p.user_id, k->dungeon_id, until, missions);
	LOG_DEBUG << "DungeonKeyUse: user " << p.user_id << " spends " << use << " x " << k->name << " (" << st.count << " left): dungeon " << k->dungeon_id << " missions " << missions << " open for " << (until - now) << " s";

	DungeonKeyUseResp resp = {};
	for (auto& u : keys)
		if (u.key_id == k->id) { u.possession = st.count; u.cnt = st.count; }
	resp.dungeon_key_info = keys;
	resp.permit_place = co_await Db::LoadPermits(p.user_id);
	auto parades = co_await Db::LoadParades(p.user_id);
	auto vortex = Vortex::Permits(now, &parades);
	resp.permit_place.insert(resp.permit_place.end(), vortex.begin(), vortex.end());

	std::string buffer{};
	const auto& ec2 = glz::write_json(resp, buffer);
	if (ec2)
		co_return HandleResult::error("Serialization error", glz::format_error(ec2, buffer));
	co_return HandleResult::success(buffer);
}
