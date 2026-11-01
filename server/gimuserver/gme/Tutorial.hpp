#pragma once
#include "App.hpp"
#include "NewPlayer.hpp"
#include "Presents.hpp"
#include "Vortex.hpp"
#include <string>
#include <vector>

/*
* bfanew: the tutorial (CLAUDE.md "Tutorial"; net/tutorial.kdl has the ids, keys and fields).
*
* The client drives it: with tutorial_end_flag 0 the login goes to TutorialScene1 (name and
* element), CreateUser makes the player (the three starter units of DefineMst initial_unit for the
* element), then TutorialStepScene plays content/tuto/tuto<N>.txt for the chapter and
* checkChapter advances the chapter from the state the server sends: 1 (Tilith, the first battle,
* mission 1) -> 9 once mission 1 is cleared (Karl, elements, mission 2) -> 10 once mission 2 is
* cleared and the wallet holds 5 gems (the summon lesson, gacha type 20000) -> 11 (farewell) ->
* 12 = done, which is the one TutorialUpdate the step scene sends. The Skip button of the tutorial
* battle (TutorialSkip) jumps to chapter 10. Chapters 50-54 (Karl poisoned, synthesis, the item
* tutorial) are the later "triggerable" tutorial of the town footer, 2-8 the 2013 order (fusion,
* squads), both left to the client.
*/
namespace Tutorial
{
	constexpr int32_t kStart = 1;   //!< the intro chapter (tuto1.txt)
	constexpr int32_t kSkipTo = 10; //!< where the skip of the tutorial battle lands (the gems and summon lesson)
	constexpr int32_t kDone = 12;
	constexpr int32_t kGems = 5;    //!< what chapter 10 expects in the wallet ("You received 5 Gems")
	constexpr int32_t kRewardGems = 3;      //!< the completion reward the client advertises (TutorialSkipPopupScene: 3 Gems, 100,000 Zel)
	constexpr int32_t kRewardZel = 100000;
	constexpr int32_t kGacha = 20000; //!< the tutorial summon (gacha type 20000, SummonsTopScene::getGachaInfoList lists it only while the tutorial plays)

	struct Starter { int32_t unit_id; int32_t member_type; int32_t disp_order; };

	inline std::vector<std::string> Split(const std::string& s, char sep)
	{
		std::vector<std::string> out;
		size_t start = 0;
		while (start <= s.size())
		{
			const auto end = s.find(sep, start);
			out.push_back(s.substr(start, end == std::string::npos ? std::string::npos : end - start));
			if (end == std::string::npos) break;
			start = end + 1;
		}
		return out;
	}

	/*! The starter set of an element: DefineMst initial_unit (21ovwqYT, parsed by LoginScene::setInitialUnits):
	 *  "<element>-<disp order>@<member type>@<unit id>:...,<element>-..." (member type 0 = the leader). */
	inline std::vector<Starter> Starters(int32_t element)
	{
		std::vector<Starter> out;
		const std::string& s = theServer()->cache().initializeResp().defines.initial_unit;
		for (const auto& entry : Split(s, ','))
		{
			const auto dash = entry.find('-');
			if (dash == std::string::npos || std::atoi(entry.substr(0, dash).c_str()) != element) continue;
			for (const auto& unit : Split(entry.substr(dash + 1), ':'))
			{
				const auto parts = Split(unit, '@');
				if (parts.size() != 3) continue;
				out.push_back({ std::atoi(parts[2].c_str()), std::atoi(parts[1].c_str()), std::atoi(parts[0].c_str()) });
			}
		}
		return out;
	}

	/*! Gives a player with an empty box the starter set of an element (the template starter when the
	 *  define has none), leading every squad. Returns false when the box was not empty. */
	inline drogon::Task<bool> GiveStarters(Db::Player& p, int32_t element)
	{
		if (!(co_await Db::LoadUnits(p.user_id)).empty()) co_return false;
		auto set = Starters(element);
		if (set.empty()) set = Starters(1);
		std::vector<UserUnitInfo> units;
		for (const auto& s : set)
		{
			const auto* def = theServer()->cache().unit(s.unit_id);
			if (!def) continue;
			auto u = NewPlayer::UnitFromTable(s.unit_id, *def, 1);
			u.user_id = p.user_id;
			co_await Db::AddUnit(p.user_id, u);
			units.push_back(u);
		}
		if (units.empty())
		{
			auto starter = NewPlayer::StarterUnit();
			starter.user_id = p.user_id;
			co_await Db::AddUnit(p.user_id, starter);
			units.push_back(starter);
			set = { { starter.unit_id, 0, 0 } };
		}
		std::vector<UserPartyDeckInfo> decks;
		for (int i = 0; i < 10; i++)
			for (size_t k = 0; k < units.size(); k++)
			{
				UserPartyDeckInfo d = {};
				d.deck_num = i;
				d.deck_type = 1;
				d.user_unit_id = units[k].user_unit_id;
				d.member_type = k < set.size() ? set[k].member_type : 1;
				d.disp_order = k < set.size() ? set[k].disp_order : static_cast<int32_t>(k);
				decks.emplace_back(d);
			}
		co_await Db::SaveDecks(p.user_id, decks);
		LOG_INFO << "Tutorial: starters of element " << element << " for " << p.user_id << " (" << units.size() << " units)";
		co_return true;
	}

	/*! The chapter the login info carries. */
	inline int32_t Status(const Db::Player& p)
	{
		if (p.tutorial_end) return kDone;
		return p.tutorial_status > 0 ? p.tutorial_status : kStart;
	}

	/*! UserTeamArchive (zI2tJB7R): the counters TutorialStepScene::checkChapter reads (fusions, syntheses). */
	inline UserTeamArchive Archive(const Db::Player& p)
	{
		UserTeamArchive a = {};
		a.user_id = p.user_id;
		a.unit_mix_cnt = p.unit_mix_cnt;
		a.item_mix_cnt = p.item_mix_cnt;
		return a;
	}

	/*! The world a player who went through (or skipped) the first battles has: missions 1 and 2
	 *  cleared, the new-player permits (dungeon 10 and its first mission). */
	inline drogon::Task<void> OpenWorld(Db::Player& p)
	{
		for (const auto mission : NewPlayer::ClearedMissions())
			if (!(co_await Db::IsCleared(p.user_id, mission)))
				co_await Db::AddClear(p.user_id, mission, "2013-11-01 00:00:00");
		for (const auto& [kind, id] : NewPlayer::Permits())
			co_await Db::AddPermit(p.user_id, kind, id);
	}

	/*! The Skip of the tutorial battle: the client jumps to chapter 10, which wants the first
	 *  missions behind it and 5 gems in the wallet. Not saved. */
	inline drogon::Task<void> Skip(Db::Player& p)
	{
		p.tutorial_status = kSkipTo;
		co_await OpenWorld(p);
		co_await GiveStarters(p, 1);
		if (p.team.brave_coin < kGems)
		{
			p.team.free_gems += kGems - p.team.brave_coin;
			p.team.brave_coin = kGems;
		}
	}

	/*! The tutorial is over (chapter 12, or the dev route). Not saved. */
	inline drogon::Task<void> Finish(Db::Player& p)
	{
		if (!p.tutorial_end)
		{
			// the completion reward ("Complete Tutorial to receive these Rewards! 3 Gems, 100,000 Zel"),
			// delivered the way the client announces it ("Gifts have been awarded to you. Visit your
			// presents box to receive them"): two presents in the Gift Box (gme/Presents.hpp)
			const int64_t now = Vortex::Now();
			co_await Db::AddPresent(p.user_id, Presents::Gems, 0, kRewardGems, "Tutorial Complete: 3 Gems", now, now + Presents::kExpireDays * 86400);
			co_await Db::AddPresent(p.user_id, Presents::Zel, 0, kRewardZel, "Tutorial Complete: 100,000 Zel", now, now + Presents::kExpireDays * 86400);
		}
		p.tutorial_end = true;
		p.tutorial_status = kDone;
		co_await OpenWorld(p);
		co_await GiveStarters(p, 1);
	}
}
