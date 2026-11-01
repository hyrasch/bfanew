#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cmath>
#include <map>

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
	//! type id ("2" Anima, "3" Breaker, "4" Guardian, "5" Oracle) -> stat ("hp"...) -> [min, max] at max level
	//! (the datamine's per-unit ranges; Lord = the hp/atk/def/rec pairs above)
	std::map<std::string, std::map<std::string, std::vector<int32_t>>> types;
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
		"es_id", &T::es_id, "sell_price", &T::sell_price, "group", &T::group, "types", &T::types);
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

/*! deploy/items.json (F_ITEM_MST): what the server needs to know about an item. */
struct ItemDef
{
	std::string name;
	int32_t type = 0;
	int32_t rarity = 0;
	int32_t sell_price = 0;
	int32_t max_stack = 999;
	int32_t max_equipped = 0;
	std::string thumbnail;
};

using ItemTable = std::unordered_map<std::string, ItemDef>;

namespace Units
{
	/*! A stat at a level: linear between the level-1 (base) and the max-level (Lord) value. The unit
	 *  table holds nothing else (F_UNIT_MST: base + lord stats and an exp pattern; the client's UnitMst
	 *  has getMinHp / getMaxHp only), and the community calculators grew the stats the same way. */
	inline int32_t StatAt(const std::vector<int32_t>& pair, int32_t lv, int32_t maxLv)
	{
		if (pair.size() < 2) return pair.empty() ? 0 : pair[0];
		if (maxLv <= 1 || lv <= 1) return pair[0];
		if (lv >= maxLv) return pair[1];
		return pair[0] + static_cast<int32_t>((static_cast<int64_t>(pair[1] - pair[0]) * (lv - 1)) / (maxLv - 1));
	}

	/*! The type bonus of one stat (UserUnitInfo add_*): a unit's type (2 Anima, 3 Breaker, 4 Guardian,
	 *  5 Oracle; 1 Lord has none) moves two stats by a per-unit percentage, the summon's roll (0..1)
	 *  picking a point of the [min, max] range the datamine lists at max level; the bonus is that
	 *  percentage of the stat at the current level (so it grows with the unit). */
	inline int32_t TypeBonus(const UnitDef& def, int32_t type, const char* stat, const std::vector<int32_t>& lordPair, double roll, int32_t statAtLevel)
	{
		const auto t = def.types.find(std::to_string(type));
		if (t == def.types.end() || lordPair.size() < 2 || lordPair[1] <= 0) return 0;
		const auto r = t->second.find(stat);
		if (r == t->second.end() || r->second.size() < 2) return 0;
		const double chosen = r->second[0] + (r->second[1] - r->second[0]) * std::clamp(roll, 0.0, 1.0);
		const double fraction = chosen / lordPair[1] - 1.0;
		return static_cast<int32_t>(std::lround(fraction * statAtLevel));
	}

	/*! Sets the add_* stats of a unit from its type and roll (call after the base stats change). */
	inline void ApplyType(UserUnitInfo& u, const UnitDef& def, double roll)
	{
		u.add_hp = TypeBonus(def, u.unit_type_id, "hp", def.hp, roll, u.base_hp);
		u.add_atk = TypeBonus(def, u.unit_type_id, "atk", def.atk, roll, u.base_atk);
		u.add_def = TypeBonus(def, u.unit_type_id, "def", def.def_, roll, u.base_def);
		u.add_heal = TypeBonus(def, u.unit_type_id, "rec", def.rec, roll, u.base_heal);
	}
}
