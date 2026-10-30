#include "App.hpp"
#include "Handlers.hpp"

#include <gimuserver/gme/NewPlayer.hpp>

/*
* UnitEvo ("0gUSE84e", encode key biHf01DxcrPou5Qt): evolution (UnitEvoScene). The client only
* enables Evolve when the base is at its max level and the materials of F_UNIT_EVO_MST are owned;
* the server checks the same against deploy/evos.json (tools/build_units.py --evos), consumes the
* materials and the zel, and turns the base into the evolved unit at level 1 (same user unit id,
* brave burst level and type kept, exp reset).
*
* Reply: "I82p0wCL" UnitOpeEvo (new unit id, user unit id, the ids before), the wallet and the
* whole box. Item materials (type 2) are not supported yet: no warehouse.
*/

HANDLEF(UnitEvo)
{
	UnitEvoReq req = {};
	const auto& ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(req, json);
	if (ec)
	{
		const auto& fmte = glz::format_error(ec, json);
		LOG_DEBUG << "Gme UnitEvo Error during JSON read: " << fmte;
		co_return HandleResult::error("Deserialization error", fmte);
	}

	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player)
		co_return HandleResult::error("Unknown user", req.login_info.user_id);
	auto& p = *player;
	const auto& cache = theServer()->cache();

	auto units = co_await Db::LoadUnits(p.user_id);
	auto find = [&](int32_t id) -> UserUnitInfo* {
		for (auto& u : units) if (u.user_unit_id == id) return &u;
		return nullptr;
	};
	// the client sends only the typed list: order 1 = base, 2.. = materials (type 1 unit, 2 item)
	UserUnitInfo* base = nullptr;
	std::vector<UserUnitInfo*> materials;
	for (const auto& m : req.materials)
	{
		if (m.material_type == 2)
			co_return HandleResult::error("Item materials are not supported yet");
		auto* u = find(m.id);
		if (!u)
			co_return HandleResult::error("Unknown unit", std::to_string(m.id));
		if (m.order == 1) base = u;
		else materials.emplace_back(u);
	}
	if (!base)
		co_return HandleResult::error("No base");

	const auto* baseDef = cache.unit(base->unit_id);
	const auto* evo = cache.evo(base->unit_id);
	if (!baseDef || !evo)
		co_return HandleResult::error("This unit cannot evolve", std::to_string(base->unit_id));
	if (base->unit_lv < baseDef->max_level)
		co_return HandleResult::error("The base is not at its max level");
	if (req.target.unit_id != 0 && req.target.unit_id != evo->evo_unit_id)
		LOG_WARN << "UnitEvo: client wants " << req.target.unit_id << ", table says " << evo->evo_unit_id;
	const auto* evoDef = cache.unit(evo->evo_unit_id);
	if (!evoDef)
		co_return HandleResult::error("Unknown evolved unit", std::to_string(evo->evo_unit_id));

	// every required unit material must be among the materials sent, each used once
	std::vector<UserUnitInfo*> pool = materials;
	for (const auto& need : evo->materials)
	{
		if (need.type != 1)
			co_return HandleResult::error("Item materials are not supported yet");
		bool found = false;
		for (auto it = pool.begin(); it != pool.end(); ++it)
			if ((*it)->unit_id == need.id && *it != base)
			{
				pool.erase(it);
				found = true;
				break;
			}
		if (!found)
			co_return HandleResult::error("Missing evolution material", std::to_string(need.id));
	}
	if (!pool.empty())
		LOG_WARN << "UnitEvo: " << pool.size() << " extra materials sent, consumed anyway";
	if (p.team.zel < evo->zel)
		co_return HandleResult::error("Not enough zel");
	if (p.team.karma < evo->karma)
		co_return HandleResult::error("Not enough karma");

	UnitOpeEvo res = {};
	res.unit_id_before = std::to_string(base->unit_id);
	res.user_unit_id_before = std::to_string(base->user_unit_id);

	base->unit_id = evo->evo_unit_id;
	base->leader_skill_id = evoDef->ls_id;
	base->skill_id = evoDef->bb_id;
	base->extra_skill_id = evoDef->es_id;
	base->new_flag = true;
	NewPlayer::SetUnitLevel(*base, *evoDef, 1);

	res.unit_id = std::to_string(base->unit_id);
	res.user_unit_id = std::to_string(base->user_unit_id);
	res.evolution_type = 0;

	std::vector<int32_t> gone;
	for (const auto* m : materials) gone.emplace_back(m->user_unit_id);
	co_await Db::UpdateUnit(p.user_id, *base);
	co_await Db::RemoveUnits(p.user_id, gone);
	p.team.zel -= evo->zel;
	p.team.karma -= evo->karma;
	co_await Db::SavePlayer(p);
	LOG_DEBUG << "UnitEvo: user " << p.user_id << " unit " << base->user_unit_id << " " << res.unit_id_before << " -> " << res.unit_id
		<< ", " << gone.size() << " materials, " << evo->zel << " zel";

	UnitEvoResp resp = {};
	resp.result = res;
	resp.team_info = p.team;
	resp.unit_info = co_await Db::LoadUnits(p.user_id);

	std::string buffer{};
	const auto& ec2 = glz::write_json(resp, buffer);
	if (ec2)
	{
		const auto& glze = glz::format_error(ec2, buffer);
		LOG_DEBUG << "Gme UnitEvo Error during JSON writing: " << glze;
		co_return HandleResult::error("Serialization error", glze);
	}

	co_return HandleResult::success(buffer);
}
