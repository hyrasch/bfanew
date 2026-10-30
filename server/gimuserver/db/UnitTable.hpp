#pragma once

#include <string>
#include <unordered_map>
#include <vector>

/*
* bfanew: the server-side unit table, deploy/units.json built by tools/build_units.py from the
* decoded F_UNIT_MST. The server never reads the downloaded (encrypted) MSTs, but fusion and
* level-ups need the stats.
*/
struct UnitDef
{
	std::string name;
	int32_t rarity = 0;
	int32_t cost = 0;
	int32_t element = 0;
	int32_t max_level = 1;
	int32_t exp_pattern = 0;
	std::vector<int32_t> hp;  //!< [base at level 1, lord at max level]
	std::vector<int32_t> atk;
	std::vector<int32_t> def_;
	std::vector<int32_t> rec;
	std::string imp_caps;
	int32_t adjust_exp = 0;
	int32_t ls_id = 0;
	int32_t bb_id = 0;
	int32_t es_id = 0;
	int32_t sell_price = 0;
	int32_t group = 0;
};

template <>
struct glz::meta<UnitDef>
{
	using T = UnitDef;
	static constexpr auto value = object(
		"name", &T::name, "rarity", &T::rarity, "cost", &T::cost, "element", &T::element,
		"max_level", &T::max_level, "exp_pattern", &T::exp_pattern,
		"hp", &T::hp, "atk", &T::atk, "def", &T::def_, "rec", &T::rec,
		"imp_caps", &T::imp_caps, "adjust_exp", &T::adjust_exp, "ls_id", &T::ls_id, "bb_id", &T::bb_id,
		"es_id", &T::es_id, "sell_price", &T::sell_price, "group", &T::group);
};

using UnitTable = std::unordered_map<std::string, UnitDef>;

/*! One material of an evolution: type 1 = unit (F_UNIT_MST id), 2 = item. */
struct EvoMaterial
{
	int32_t type = 1;
	int32_t id = 0;
};

/*! deploy/evos.json (F_UNIT_EVO_MST): what a unit evolves into and what it costs. */
struct EvoDef
{
	int32_t evo_unit_id = 0;
	int32_t zel = 0;
	int32_t karma = 0;
	std::vector<EvoMaterial> materials;
};

using EvoTable = std::unordered_map<std::string, EvoDef>;

namespace Units
{
	/*! A stat at a level: linear between the level-1 and the max-level value (good enough until the real curve is known). */
	inline int32_t StatAt(const std::vector<int32_t>& pair, int32_t lv, int32_t maxLv)
	{
		if (pair.size() < 2) return pair.empty() ? 0 : pair[0];
		if (maxLv <= 1 || lv <= 1) return pair[0];
		if (lv >= maxLv) return pair[1];
		return pair[0] + static_cast<int32_t>((static_cast<int64_t>(pair[1] - pair[0]) * (lv - 1)) / (maxLv - 1));
	}
}
