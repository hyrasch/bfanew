#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

/*
* bfanew: the content phase (deploy/phase.json, written by tools/build_phase.py): the ids the
* client's filtered MST tables contain. Handlers refuse anything outside it, so a modified client
* cannot play ahead of the phase (CLAUDE.md "Phasing design", lever 4). Without the file nothing
* is enforced (and the server says so at startup).
*/
struct PhaseTable
{
	std::string cutoff;
	std::vector<int32_t> units;
	std::vector<int32_t> missions;
	std::vector<int32_t> dungeons;
	std::vector<int32_t> areas;
	std::vector<int32_t> lands;
};

class Phase
{
public:
	void Load(const PhaseTable& t)
	{
		m_cutoff = t.cutoff;
		m_units = { t.units.begin(), t.units.end() };
		m_missions = { t.missions.begin(), t.missions.end() };
		m_enforced = !m_units.empty() || !m_missions.empty();
	}
	void Clear() { m_cutoff.clear(); m_units.clear(); m_missions.clear(); m_enforced = false; }

	bool enforced() const { return m_enforced; }
	const std::string& cutoff() const { return m_cutoff; }
	/*! Whether the unit / mission is in the phase (everything is when nothing is enforced). */
	bool hasUnit(int32_t id) const { return !m_enforced || m_units.contains(id); }
	bool hasMission(int32_t id) const { return !m_enforced || m_missions.contains(id); }
	size_t unitCount() const { return m_units.size(); }
	size_t missionCount() const { return m_missions.size(); }

private:
	std::string m_cutoff;
	std::unordered_set<int32_t> m_units;
	std::unordered_set<int32_t> m_missions;
	bool m_enforced = false;
};
