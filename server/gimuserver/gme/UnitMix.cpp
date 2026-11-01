#include "App.hpp"
#include "Handlers.hpp"
#include "Tutorial.hpp"

#include <cmath>
#include <random>
#include <set>

/*
* UnitMix ("Mw08CIg2", encode key JnegC7RrN3FoW8dQ): fusion (UnitMixConnectScene::initConnect).
*
* Request: "Km35HAXv" the base (order 1) then the materials, "mCE3rUu5" the zel the client
* computed, "60subGk3" mix type / unit count.
*
* Rules, read off the client so that the server matches what the Fuse screen predicts:
*   - exp per material (GameUtils::getMixExp): material total exp / 2.5 + cost * 2 + adjust exp
*     + 100/200/500/1000/1500/3000/5000/10000 by rarity 1..8, times 1.5 when the material has
*     the base's element; Great / Super success multiply the total by the define table rates
*     (1.5 / 2.0; the chances are ours: 10% / 2%);
*   - zel (GameUtils::getMixGold): sum over materials of (base level + material level) * 100;
*   - level from the unit's exp pattern (unit_exp_pattern.json), capped at the unit's max level,
*     stats interpolated between the level-1 and max-level values (Units::StatAt);
*   - brave burst level: each material with the base's brave burst (same bb id or same unit
*     family) has a 20% chance to raise it, up to 10.
*
* Reply: "1ZbHB6Im" UnitOpeResult (what UnitMixPlayScene animates: before/after level, exp,
* skill level, one "lv:hp:atk:def:rec" record per level reached, records joined with commas), the wallet and the whole box.
*/

namespace
{
	constexpr int kGreatChancePct = 10;
	constexpr int kSuperChancePct = 2;
	constexpr int kBbUpChancePct = 20;
	constexpr int kMaxBbLevel = 10;

	std::mt19937& rng()
	{
		static std::mt19937 g{ std::random_device{}() };
		return g;
	}

	int roll100()
	{
		std::uniform_int_distribution<int> d(0, 99);
		return d(rng());
	}

	float rarityExp(int32_t rarity)
	{
		switch (rarity)
		{
		case 1: return 100.f;
		case 2: return 200.f;
		case 3: return 500.f;
		case 4: return 1000.f;
		case 5: return 1500.f;
		case 6: return 3000.f;
		case 7: return 5000.f;
		case 8: return 10000.f;
		default: return 0.f;
		}
	}
}

HANDLEF(UnitMix)
{
	UnitMixReq req = {};
	const auto& ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(req, json);
	if (ec)
	{
		const auto& fmte = glz::format_error(ec, json);
		LOG_DEBUG << "Gme UnitMix Error during JSON read: " << fmte;
		co_return HandleResult::error("Deserialization error", fmte);
	}

	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player)
		co_return HandleResult::error("Unknown user", req.login_info.user_id);
	auto& p = *player;
	const auto& cache = theServer()->cache();

	// the base is the entry with order 1, the rest are materials
	auto units = co_await Db::LoadUnits(p.user_id);
	auto find = [&](int32_t id) -> UserUnitInfo* {
		for (auto& u : units) if (u.user_unit_id == id) return &u;
		return nullptr;
	};
	UserUnitInfo* base = nullptr;
	std::vector<UserUnitInfo*> materials;
	for (const auto& e : req.units)
	{
		auto* u = find(e.user_unit_id);
		if (!u)
			co_return HandleResult::error("Unknown unit", std::to_string(e.user_unit_id));
		if (e.order == 1) base = u;
		else materials.emplace_back(u);
	}
	if (!base || materials.empty())
		co_return HandleResult::error("No base or no material");
	for (auto* m : materials)
		if (m == base)
			co_return HandleResult::error("The base cannot be a material");

	const auto* baseDef = cache.unit(base->unit_id);
	if (!baseDef)
		co_return HandleResult::error("Unknown base unit", std::to_string(base->unit_id));

	// zel
	int64_t zel = 0;
	for (const auto* m : materials)
		zel += static_cast<int64_t>(base->unit_lv + m->unit_lv) * 100;
	if (p.team.zel < zel)
		co_return HandleResult::error("Not enough zel");

	// exp
	float expGain = 0.f;
	int bbUps = 0;
	for (const auto* m : materials)
	{
		const auto* md = cache.unit(m->unit_id);
		const int32_t rarity = md ? md->rarity : m->unit_type_id;
		const int32_t cost = md ? md->cost : 1;
		const int32_t adjust = md ? md->adjust_exp : 0;
		float e = static_cast<float>(m->total_exp) / 2.5f + static_cast<float>(cost * 2) + static_cast<float>(adjust) + rarityExp(rarity);
		if (md && md->element == baseDef->element)
			e *= 1.5f;
		expGain += e;
		const bool sameBurst = (m->skill_id != 0 && m->skill_id == base->skill_id) || (md && md->group != 0 && md->group == baseDef->group);
		if (sameBurst && roll100() < kBbUpChancePct)
			bbUps++;
	}
	int32_t successType = 0;
	const auto& defs = cache.initializeResp().defines;
	const int r = roll100();
	if (r < kSuperChancePct)
	{
		successType = 2;
		expGain *= defs.unit_mix_super_exp_rate > 0 ? defs.unit_mix_super_exp_rate : 2.0f;
	}
	else if (r < kSuperChancePct + kGreatChancePct)
	{
		successType = 1;
		expGain *= defs.unit_mix_great_exp_rate > 0 ? defs.unit_mix_great_exp_rate : 1.5f;
	}
	const int32_t gained = static_cast<int32_t>(std::lround(expGain));

	// apply to the base
	UnitOpeResult res = {};
	res.unit_id = std::to_string(base->unit_id);
	res.user_unit_id = std::to_string(base->user_unit_id);
	res.zel = static_cast<int32_t>(zel);
	res.exp = gained;
	res.success_type = successType;
	res.before_lv = base->unit_lv;
	res.before_exp = base->total_exp;
	res.before_skill_lv = base->skill_lv;
	res.before_extra_skill_lv = base->extra_skill_lv;
	res.before_eqp_frame_id = base->equipitem_frame_id;
	res.before_eqp_frame_id2 = base->equipitem_frame_id2;
	res.before_fe_bp = base->fe_bp;
	res.before_fe_info = base->fe_skill_info;
	res.before_max_fe_bp = base->fe_max_usable_bp;

	const int64_t maxExp = cache.expForLevel(baseDef->exp_pattern, baseDef->max_level);
	int64_t total = static_cast<int64_t>(base->total_exp) + gained;
	if (maxExp > 0 && total > maxExp) total = maxExp;
	const int32_t newLv = cache.levelForExp(baseDef->exp_pattern, total, baseDef->max_level);

	std::string lvup;
	for (int32_t lv = base->unit_lv + 1; lv <= newLv; lv++)
	{
		if (!lvup.empty()) lvup += ",";
		// UnitMixPlayScene::parseMixResult: records split on ",", fields on ":"
		lvup += std::to_string(lv) + ":" + std::to_string(Units::StatAt(baseDef->hp, lv, baseDef->max_level))
			+ ":" + std::to_string(Units::StatAt(baseDef->atk, lv, baseDef->max_level))
			+ ":" + std::to_string(Units::StatAt(baseDef->def_, lv, baseDef->max_level))
			+ ":" + std::to_string(Units::StatAt(baseDef->rec, lv, baseDef->max_level));
	}

	base->unit_lv = newLv;
	base->total_exp = static_cast<int32_t>(total);
	base->exp = static_cast<int32_t>(total - cache.expForLevel(baseDef->exp_pattern, newLv));
	base->base_hp = Units::StatAt(baseDef->hp, newLv, baseDef->max_level);
	base->base_atk = Units::StatAt(baseDef->atk, newLv, baseDef->max_level);
	base->base_def = Units::StatAt(baseDef->def_, newLv, baseDef->max_level);
	base->base_heal = Units::StatAt(baseDef->rec, newLv, baseDef->max_level);
	{
		const auto rolls = co_await Db::LoadUnitRolls(p.user_id);
		const auto it = rolls.find(base->user_unit_id);
		Units::ApplyType(*base, *baseDef, it == rolls.end() ? 0.5 : it->second); // the type bonus follows the level
	}
	base->skill_lv = std::min<int32_t>(kMaxBbLevel, std::max<int32_t>(1, base->skill_lv) + bbUps);
	base->new_flag = false;

	res.after_lv = base->unit_lv;
	res.after_exp = base->total_exp;
	res.after_skill_lv = base->skill_lv;
	res.after_extra_skill_lv = base->extra_skill_lv;
	res.lvup_status = lvup;
	res.param_up_state = "";
	res.after_eqp_frame_id = base->equipitem_frame_id;
	res.after_eqp_frame_id2 = base->equipitem_frame_id2;
	res.after_fe_bp = base->fe_bp;
	res.after_fe_info = base->fe_skill_info;
	res.after_max_fe_bp = base->fe_max_usable_bp;
	res.before_dbb_skill_level = 0;
	res.after_dbb_skill_level = 0;

	LOG_DEBUG << "UnitMix: user " << p.user_id << " base " << base->user_unit_id << " (unit " << base->unit_id << ") +" << gained
		<< " exp (success " << successType << "), lv " << res.before_lv << " -> " << res.after_lv << ", bb " << res.before_skill_lv
		<< " -> " << res.after_skill_lv << ", " << materials.size() << " materials, " << zel << " zel";

	std::vector<int32_t> gone;
	for (const auto* m : materials) gone.emplace_back(m->user_unit_id);
	co_await Db::UpdateUnit(p.user_id, *base);
	co_await Db::RemoveUnits(p.user_id, gone);
	p.team.zel -= zel;
	p.unit_mix_cnt++; // the tutorial counts fusions (gme/Tutorial.hpp)
	co_await Db::SavePlayer(p);

	UnitMixResp resp = {};
	resp.result = res;
	resp.team_info = p.team;
	resp.unit_info = co_await Db::LoadUnits(p.user_id);
	resp.archive = { Tutorial::Archive(p) };

	std::string buffer{};
	const auto& ec2 = glz::write_json(resp, buffer);
	if (ec2)
	{
		const auto& glze = glz::format_error(ec2, buffer);
		LOG_DEBUG << "Gme UnitMix Error during JSON writing: " << glze;
		co_return HandleResult::error("Serialization error", glze);
	}

	co_return HandleResult::success(buffer);
}
