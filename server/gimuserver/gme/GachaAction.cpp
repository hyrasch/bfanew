#include "App.hpp"
#include "Handlers.hpp"

#include <chrono>
#include <random>

/*
* GachaAction ("F7JvPk5H"): the Summon button. Sent by GachaActionScene::initConnect.
*
* Request: group "1IR86sAv" with the gacha id, the number of summons and the ticket flag
* (GachaActionRequest::createBody).
*
* Response, as the classic (non-"resummon") GachaActionScene path consumes it:
*   - "Km35HAXv" OpeUserUnit list: user_unit_id (-> UserState::setSelectUserUnitID and
*     GachaUnitsList), change-rate info, GachaEffectMst id (-> door animation);
*   - "4ceMWH6k" the WHOLE unit box including the new units: UserUnitInfoResponse clears the
*     client's list before adding, and GachaActionScene::updateEvent then looks the new
*     unit up in that list (a missing unit is a null deref, i.e. a crash);
*   - "fEi17cnx" updated team info (gems / honor points).
*   Do NOT send "sdu3jvhH" (UserUnitResummonInfoSg) here: it switches the scene to the
*   resummon flow whose manager was never initialised, and the client dies.
*
* The new units are stored (gimuserver/db/Player.hpp) and the gems / honor points are
* charged for good. The pool is still the six launch heroes; the real pool/rates come with
* the phase-1 content work.
*/

namespace
{
	struct PoolUnit
	{
		int32_t unit_id;
		int32_t rarity;
		const char* element;
		int32_t hp, atk, def, rec;
		int32_t leader_skill_id, bb_id;
	};

	// F_UNIT_MST base stats (Ver1084). Element: 1 fire, 2 water, 3 earth, 4 thunder, 5 light, 6 dark.
	constexpr PoolUnit kStarters2[] = {
		{ 10011, 2, "fire",    1030, 500, 410, 320, 100, 10011 }, // Vargas
		{ 20011, 2, "water",   1000, 490, 360, 420, 110, 20011 }, // Selena
		{ 30011, 2, "earth",   1010, 500, 500, 150, 120, 30011 }, // Lance
		{ 40011, 2, "thunder", 1180, 580, 320, 120, 130, 40011 }, // Eze
		{ 50011, 2, "light",    970, 500, 500, 500, 140, 50011 }, // Atro
		{ 60011, 2, "dark",    1500, 430, 530, 120, 150, 60011 }, // Magress
	};
	constexpr PoolUnit kStarters3[] = {
		{ 10012, 3, "fire",    1484, 630, 517, 404, 100, 10012 }, // Burning Vargas
		{ 20012, 3, "water",   1377, 636, 422, 624, 110, 20012 }, // Glacial Selena
		{ 30012, 3, "earth",   1419, 648, 630, 223, 120, 30012 }, // Jade Lance
		{ 40012, 3, "thunder", 1593, 731, 378, 189, 130, 40012 }, // Thunder Eze
		{ 50012, 3, "light",   1397, 630, 630, 630, 140, 50012 }, // Light Atro
		{ 60012, 3, "dark",    2228, 542, 715, 195, 150, 60012 }, // Dark Magress
	};
	constexpr PoolUnit kStarters4[] = {
		{ 10013, 4, "fire",    1995, 826, 725, 632, 101, 10013 }, // Fire Dragon Vargas
		{ 20013, 4, "water",   1928, 826, 661, 743, 111, 20013 }, // Ice Knight Selena
		{ 30013, 4, "earth",   1980, 832, 844, 410, 121, 30013 }, // Earth Knight Lance
		{ 40013, 4, "thunder", 2205, 906, 618, 384, 131, 40013 }, // Thunder Knight Eze
		{ 50013, 4, "light",   1887, 819, 819, 819, 141, 50013 }, // Holy Knight Atro
		{ 60013, 4, "dark",    2651, 743, 860, 381, 151, 60013 }, // Dark Knight Magress
	};

	constexpr int32_t kRareGacha = 2000;
	constexpr int32_t kGemCost = 1; // bfanew: cheap summons (the gacha table says 1 Gem too)
	constexpr int32_t kHonorCost = 200;

	// GachaEffectMst ids (deploy/system/gacha_effects.json) per gacha and rarity.
	int32_t effectFor(int32_t gachaId, int32_t rarity)
	{
		if (gachaId == kRareGacha) // Rare Summon: 8 gold->R, 11 red->SR, 16 rainbow->SSR
			return rarity >= 5 ? 16 : rarity == 4 ? 11 : 8;
		return rarity;        // Honor Summon: ids 1..5 map 1:1 to rarity
	}

	std::mt19937& rng()
	{
		static std::mt19937 g{ std::random_device{}() };
		return g;
	}

	const PoolUnit* pick(int32_t gachaId)
	{
		std::uniform_int_distribution<int> six(0, 5);
		// bfanew: never hand out a unit outside the phase (deploy/phase.json); the pool is
		// in phase 1 today, the check matters once the real pools come in
		const auto& phase = theServer()->cache().phase();
		for (int tries = 0; tries < 50; ++tries)
		{
			const PoolUnit* u;
			if (gachaId == kRareGacha)
			{
				// launch Rare Summon: 3* most of the time, 4* sometimes
				std::uniform_int_distribution<int> pct(0, 99);
				u = pct(rng()) < 25 ? &kStarters4[six(rng())] : &kStarters3[six(rng())];
			}
			else
				u = &kStarters2[six(rng())];
			if (phase.hasUnit(u->unit_id)) return u;
		}
		return nullptr;
	}
}

HANDLEF(GachaAction)
{
	GachaActionReq req = {};
	const auto& ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(req, json);
	if (ec)
	{
		const auto& fmte = glz::format_error(ec, json);
		LOG_DEBUG << "Gme GachaAction Error during JSON read: " << fmte;
		co_return HandleResult::error("Deserialization error", fmte);
	}

	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player)
		co_return HandleResult::error("Unknown user", req.login_info.user_id);
	auto& p = *player;

	const auto gachaId = req.gacha.gacha_id;
	const auto count = std::max<int32_t>(1, req.gacha.request_count);
	LOG_DEBUG << "GachaAction: user " << p.user_id << " gacha " << gachaId << " x" << count << " ticket=" << req.gacha.use_ticket;

	// pay first: free gems before paid gems for the Rare Summon, honor points otherwise
	if (gachaId == kRareGacha)
	{
		const auto cost = kGemCost * count;
		// the gem balance the client shows and checks is brave_coin (03UGMHxF, the gacha table's
		// price is in it too); free_gems / paid_gems are the accounting split, kept in step
		if (p.team.brave_coin < cost)
			co_return HandleResult::error("Not enough gems");
		p.team.brave_coin -= cost;
		const auto fromFree = std::min(cost, p.team.free_gems);
		p.team.free_gems -= fromFree;
		p.team.paid_gems = std::max(0, p.team.paid_gems - (cost - fromFree));
	}
	else
	{
		const auto cost = kHonorCost * count;
		if (p.team.friend_point < cost)
			co_return HandleResult::error("Not enough honor points");
		p.team.friend_point -= cost;
	}

	const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());

	GachaActionResp resp = {};
	for (int32_t i = 0; i < count; i++)
	{
		const auto* picked = pick(gachaId);
		if (!picked)
			co_return HandleResult::error("No unit of this summon is available yet");
		const auto& u = *picked;
		UserUnitInfo d = {};
		d.unit_id = u.unit_id;
		// unit type (1 Lord, 2 Anima, 3 Breaker, 4 Guardian, 5 Oracle), random like the real summon
		d.unit_type_id = std::uniform_int_distribution<int>(1, 5)(rng());
		d.unit_lv = 1;
		d.exp = 0;
		d.total_exp = 0;
		d.base_hp = u.hp;  d.base_atk = u.atk;  d.base_def = u.def;  d.base_heal = u.rec;
		d.element = u.element;
		d.leader_skill_id = u.leader_skill_id;
		d.skill_id = u.bb_id;
		d.skill_lv = 1;
		d.new_flag = true;
		d.receive_date = now;
		co_await Db::AddUnit(p.user_id, d);

		OpeUserUnit ope = {};
		ope.user_unit_id = d.user_unit_id;
		ope.change_rate_info = "";
		ope.gacha_effect_id = effectFor(gachaId, u.rarity);
		resp.ope_units.emplace_back(ope);
	}

	co_await Db::SavePlayer(p);
	resp.team_info = p.team;
	resp.unit_info = co_await Db::LoadUnits(p.user_id); // the whole box

	std::string buffer{};
	const auto& ec2 = glz::write_json(resp, buffer);
	if (ec2)
	{
		const auto& glze = glz::format_error(ec2, buffer);
		LOG_DEBUG << "Gme GachaAction Error during JSON writing: " << glze;
		co_return HandleResult::error("Serialization error", glze);
	}

	co_return HandleResult::success(buffer);
}
