#include "App.hpp"
#include "Handlers.hpp"
#include "Tutorial.hpp"

/*
* ItemMix ("4P5GELTF", encode key AFqKIJ8Z4mHPB9xg, URL /actionSymbol/cmk86pt4.php): item
* synthesis on the village's Synthesis screen. Like the other item screens the client does the
* work locally first (MyTownItemMixScene::mixItem: karma -= recipe karma, materials taken from the
* warehouse, result added, PermitRecipeInfo counted) and queues one ItemMixRequest
* (GameUtils::createItemMixConnectList / MyTownItemMixConnectScene) whose group JTf2jY5o holds a
* single node {4HqhTf3a: "recipeId:count,..."} (ItemMixRequest::itemCsvData from
* ItemMixResultInfoList). The recipes are F_RECIPE_MST (deploy/system/receipes.json, Initialize
* 8f0bCciN): result item + count, karma, materials "itemId:count,...", key item ids (must be owned,
* not consumed), a mix limit (-1 unlimited; the limited ones are event recipes and are not tracked
* here, PermitReceipe 51yQrDBR stays empty) and a validity window (ignored).
* The server replays the mixes against the stored warehouse and answers the wallet and the whole
* warehouse (the client replaces its list), so a refused mix is undone on the client too.
*/

namespace
{
	std::vector<std::pair<std::string, int>> ParseCsv(const std::string& csv)
	{
		std::vector<std::pair<std::string, int>> out;
		size_t pos = 0;
		while (pos < csv.size())
		{
			const auto comma = csv.find(',', pos);
			const auto entry = csv.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
			pos = comma == std::string::npos ? csv.size() : comma + 1;
			const auto colon = entry.find(':');
			if (colon == std::string::npos) continue;
			try { out.emplace_back(entry.substr(0, colon), std::stoi(entry.substr(colon + 1))); }
			catch (...) {}
		}
		return out;
	}
}

HANDLEF(ItemMix)
{
	ItemMixReq req = {};
	const auto& ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(req, json);
	if (ec)
	{
		const auto& fmte = glz::format_error(ec, json);
		LOG_DEBUG << "Gme ItemMix Error during JSON read: " << fmte;
		co_return HandleResult::error("Deserialization error", fmte);
	}

	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player)
		co_return HandleResult::error("Unknown user", req.login_info.user_id);
	auto& p = *player;
	const auto& recipes = theServer()->cache().initializeResp().receipe;

	// the warehouse as a mutable map, replayed mix by mix
	std::unordered_map<std::string, int> have;
	for (const auto& w : co_await Db::LoadItems(p.user_id))
		have[w.item_id] = w.item_num;

	std::string taken, given;
	int64_t karma = 0;
	int mixes = 0;
	for (const auto& node : req.mix)
	{
		for (const auto& [recipeId, count] : ParseCsv(node.recipes))
		{
			const ReceipeMst* r = nullptr;
			for (const auto& x : recipes) if (std::to_string(x.id) == recipeId) { r = &x; break; }
			if (!r || count <= 0)
			{
				LOG_WARN << "ItemMix: user " << p.user_id << " unknown recipe " << recipeId;
				continue;
			}
			for (int n = 0; n < count; ++n)
			{
				// key items: owned, not consumed
				bool ok = true;
				for (const auto& [k, _] : ParseCsv(r->keyitem_ids + ":1"))
					if (!k.empty() && have[k] <= 0) ok = false;
				const auto mats = ParseCsv(r->materials);
				for (const auto& [id, num] : mats)
					if (have[id] < num) ok = false;
				if (!ok || p.team.karma < static_cast<int64_t>(karma + r->karma))
				{
					LOG_WARN << "ItemMix: user " << p.user_id << " cannot make recipe " << recipeId << " (materials or karma)";
					break;
				}
				for (const auto& [id, num] : mats)
				{
					have[id] -= num;
					if (!taken.empty()) taken += ',';
					taken += id + ":" + std::to_string(num);
				}
				const auto result = std::to_string(r->item_id);
				have[result] += r->item_count;
				if (!given.empty()) given += ',';
				given += result + ":" + std::to_string(r->item_count);
				karma += r->karma;
				++mixes;
			}
		}
	}

	if (mixes > 0)
	{
		co_await Db::TakeItems(p.user_id, taken);
		co_await Db::AddItems(p.user_id, given);
		p.team.karma -= karma;
		p.item_mix_cnt += mixes; // the tutorial counts syntheses (gme/Tutorial.hpp)
		co_await Db::SavePlayer(p);
	}
	LOG_DEBUG << "ItemMix: user " << p.user_id << " " << mixes << " mixes, took " << taken << ", made " << given << ", karma " << karma;

	ItemMixResp resp = {};
	resp.team_info = p.team;
	resp.warehouse_info = co_await Db::LoadItems(p.user_id);
	resp.archive = { Tutorial::Archive(p) };

	std::string buffer{};
	const auto& ec2 = glz::write_json(resp, buffer);
	if (ec2)
	{
		const auto& glze = glz::format_error(ec2, buffer);
		LOG_DEBUG << "Gme ItemMix Error during JSON writing: " << glze;
		co_return HandleResult::error("Serialization error", glze);
	}

	co_return HandleResult::success(buffer);
}
