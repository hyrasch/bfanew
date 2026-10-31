#pragma once

#include <cstdint>
#include <string>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/*
* bfanew: the content phase (deploy/phase.json, written by tools/build_phase.py): the ids the
* client's filtered MST tables contain. Handlers refuse anything outside it, so a modified client
* cannot play ahead of the phase (CLAUDE.md "Phasing design", lever 4). Without the file nothing
* is enforced (and the server says so at startup).
*/
/*! deploy/system/vortex_schedule.json: one open window set per special area / dungeon group. */
struct VortexEntry
{
	std::string name;
	int32_t area = 0;
	std::vector<int32_t> dungeons;
	std::vector<int32_t> weekdays;            //!< 0 = Sunday .. 6 = Saturday, empty = every day
	std::vector<std::vector<int32_t>> hours;  //!< [start, end) hour pairs, empty = all day
	std::string period_text;
};

struct VortexSchedule
{
	std::string _doc;
	int32_t utc_offset_hours = 0;
	std::vector<VortexEntry> entries;
};

/*! deploy/system/phases/<NN>/gacha_pools.json (tools/build_pools.py): one pool per summon. */
struct GachaPool
{
	std::string name;
	std::map<std::string, int32_t> rates;                //!< rarity -> draw weight
	std::map<std::string, std::vector<int32_t>> units;   //!< rarity -> unit ids
	std::map<std::string, std::string> names;            //!< unit id -> English name (sgtext_units)
};

struct GachaPools
{
	std::string _doc;
	std::map<std::string, GachaPool> pools;              //!< gacha id -> pool
};

struct PhaseTable
{
	int32_t id = 0;       //!< phase number: tables in deploy/system/phases/<id, 2 digits>/ win
	std::string cutoff;
	std::vector<int32_t> units;
	std::vector<int32_t> missions;
	std::vector<int32_t> dungeons;
	std::vector<int32_t> areas;
	std::vector<int32_t> lands;
	std::map<std::string, std::vector<int32_t>> dungeon_missions; //!< Vortex dungeon id -> its mission ids
	std::map<std::string, int32_t> area_parents;                  //!< Vortex area id -> the folder area it is listed under
};

class Phase
{
public:
	void Load(const PhaseTable& t)
	{
		m_id = t.id;
		m_cutoff = t.cutoff;
		m_units = { t.units.begin(), t.units.end() };
		m_missions = { t.missions.begin(), t.missions.end() };
		m_enforced = !m_units.empty() || !m_missions.empty();
		m_dungeonMissions.clear(); m_vortexDungeonOf.clear(); m_areaParents.clear();
		for (const auto& [a, parent] : t.area_parents) m_areaParents[std::stoi(a)] = parent;
		for (const auto& [d, ms] : t.dungeon_missions)
		{
			const auto id = std::stoi(d);
			m_dungeonMissions[id] = ms;
			for (const auto m : ms) m_vortexDungeonOf[m] = id;
		}
	}
	void Clear() { m_id = 0; m_cutoff.clear(); m_units.clear(); m_missions.clear(); m_dungeonMissions.clear(); m_vortexDungeonOf.clear(); m_areaParents.clear(); m_enforced = false; }
	/*! The folder area a Vortex area is listed under, 0 for none. */
	int32_t areaParent(int32_t areaId) const
	{
		const auto it = m_areaParents.find(areaId);
		return it == m_areaParents.end() ? 0 : it->second;
	}
	/*! The missions of a Vortex dungeon (empty when unknown). */
	const std::vector<int32_t>& dungeonMissions(int32_t dungeonId) const
	{
		static const std::vector<int32_t> none;
		const auto it = m_dungeonMissions.find(dungeonId);
		return it == m_dungeonMissions.end() ? none : it->second;
	}
	/*! The Vortex dungeon a mission belongs to, 0 for a normal quest. */
	int32_t vortexDungeonOf(int32_t missionId) const
	{
		const auto it = m_vortexDungeonOf.find(missionId);
		return it == m_vortexDungeonOf.end() ? 0 : it->second;
	}

	bool enforced() const { return m_enforced; }
	int32_t id() const { return m_id; }
	const std::string& cutoff() const { return m_cutoff; }
	/*! Whether the unit / mission is in the phase (everything is when nothing is enforced). */
	bool hasUnit(int32_t id) const { return !m_enforced || m_units.contains(id); }
	bool hasMission(int32_t id) const { return !m_enforced || m_missions.contains(id); }
	size_t unitCount() const { return m_units.size(); }
	size_t missionCount() const { return m_missions.size(); }

private:
	int32_t m_id = 0;
	std::string m_cutoff;
	std::unordered_set<int32_t> m_units;
	std::unordered_set<int32_t> m_missions;
	std::unordered_map<int32_t, std::vector<int32_t>> m_dungeonMissions;
	std::unordered_map<int32_t, int32_t> m_vortexDungeonOf;
	std::unordered_map<int32_t, int32_t> m_areaParents;
	bool m_enforced = false;
};
