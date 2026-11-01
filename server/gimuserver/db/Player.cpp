#include "App.hpp"
#include "Player.hpp"

#include <gimuserver/gme/NewPlayer.hpp>

#include <chrono>
#include <random>

using namespace drogon;
using namespace drogon::orm;

namespace
{
	int64_t NowUnix()
	{
		return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}

	std::string RandomDigits(size_t n)
	{
		static std::mt19937_64 g{ std::random_device{}() };
		std::uniform_int_distribution<int> d(0, 9);
		std::string s;
		for (size_t i = 0; i < n; i++)
			s += static_cast<char>('0' + d(g));
		if (s[0] == '0') s[0] = '1' + static_cast<char>(d(g) % 9); // keep the leading digit
		return s;
	}

	std::string RandomToken(size_t n)
	{
		static constexpr char kChars[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
		static std::mt19937_64 g{ std::random_device{}() };
		std::uniform_int_distribution<size_t> d(0, sizeof(kChars) - 2);
		std::string s;
		for (size_t i = 0; i < n; i++)
			s += kChars[d(g)];
		return s;
	}

	template <typename T>
	std::string ToJson(T v) // by value: the generated glaze helpers want a non-const object
	{
		std::string s;
		const auto ec = glz::write_json(v, s);
		if (ec) LOG_ERROR << "Db: cannot serialize: " << glz::format_error(ec, s);
		return s;
	}

	template <typename T>
	T FromJson(const std::string& s)
	{
		T v = {};
		const auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(v, s);
		if (ec) LOG_ERROR << "Db: cannot parse: " << glz::format_error(ec, s);
		return v;
	}

	Db::Player RowToPlayer(const Row& r)
	{
		Db::Player p;
		p.user_id = r["user_id"].as<std::string>();
		p.gumi_user_id = r["gumi_user_id"].as<std::string>();
		p.account_id = r["account_id"].as<std::string>();
		p.handle_name = r["handle_name"].as<std::string>();
		p.team = FromJson<UserTeamInfo>(r["team"].as<std::string>());
		p.energy_at = r["energy_at"].as<int64_t>();
		p.tutorial_end = r["tutorial_end"].as<int64_t>() != 0;
		p.last_login = r["last_login"].isNull() ? 0 : r["last_login"].as<int64_t>();
		return p;
	}
}

namespace Db
{
	const UserLevelMst* LevelRow(int32_t level)
	{
		for (const auto& lv : theServer()->cache().initializeResp().progression)
			if (lv.level == level)
				return &lv;
		return nullptr;
	}

	Task<std::pair<std::string, std::string>> AccountForDevice(std::string vid, std::string deviceName)
	{
		if (vid.empty()) vid = "no-vid";
		const auto res = co_await theDb()->execSqlCoro("SELECT gumi_user_id, token FROM accounts WHERE device_vid = $1", vid);
		if (!res.empty())
			co_return { res[0]["gumi_user_id"].as<std::string>(), res[0]["token"].as<std::string>() };

		const auto cnt = co_await theDb()->execSqlCoro("SELECT COUNT(*) AS n FROM accounts");
		const int64_t n = cnt[0]["n"].as<int64_t>() + 1;
		char id[16];
		std::snprintf(id, sizeof(id), "%08lld", static_cast<long long>(n));
		const std::string token = RandomToken(16);
		co_await theDb()->execSqlCoro("INSERT INTO accounts(gumi_user_id, device_vid, device_name, token, created_at) VALUES ($1, $2, $3, $4, $5)",
			std::string(id), vid, deviceName, token, NowUnix());
		LOG_INFO << "Db: new account " << id << " for device " << vid;
		co_return { std::string(id), token };
	}

	Task<Player> PlayerForAccount(std::string gumiUserId)
	{
		const auto res = co_await theDb()->execSqlCoro("SELECT * FROM players WHERE gumi_user_id = $1", gumiUserId);
		if (!res.empty())
		{
			auto p = RowToPlayer(res[0]);
			RegenEnergy(p);
			co_return p;
		}

		// new player: the template of NewPlayer.hpp
		const auto& scfg = theServer()->cache().serverConfig();
		Player p;
		p.gumi_user_id = gumiUserId;
		p.user_id = RandomDigits(16);
		p.account_id = RandomDigits(8);
		p.handle_name = NewPlayer::kHandleName;
		p.team.user_id = p.user_id;
		NewPlayer::FillTeam(p.team, scfg);
		p.energy_at = NowUnix();

		co_await theDb()->execSqlCoro(
			"INSERT INTO players(user_id, gumi_user_id, account_id, handle_name, team, energy_at, tutorial_end, created_at) VALUES ($1, $2, $3, $4, $5, $6, 1, $7)",
			p.user_id, p.gumi_user_id, p.account_id, p.handle_name, ToJson(p.team), p.energy_at, p.energy_at);

		// the starter unit in slot 0 of every squad, like the tutorial would leave it
		auto starter = NewPlayer::StarterUnit();
		starter.user_id = p.user_id;
		co_await AddUnit(p.user_id, starter);
		std::vector<UserPartyDeckInfo> decks;
		for (int i = 0; i < 10; i++)
		{
			UserPartyDeckInfo d = {};
			d.deck_num = i;
			d.deck_type = 1;
			d.user_unit_id = starter.user_unit_id;
			d.member_type = 0;
			d.disp_order = 0;
			decks.emplace_back(d);
		}
		co_await SaveDecks(p.user_id, decks);

		for (const auto& [kind, id] : NewPlayer::Permits())
			co_await AddPermit(p.user_id, kind, id);
		for (const auto mission : NewPlayer::ClearedMissions())
			co_await AddClear(p.user_id, mission, "2013-11-01 00:00:00");

		LOG_INFO << "Db: new player " << p.user_id << " (account " << p.account_id << ") for gumi user " << gumiUserId;
		co_return p;
	}

	Task<std::optional<Player>> LoadPlayer(const LoginInfoReq& login)
	{
		Result res;
		if (!login.user_id.empty())
			res = co_await theDb()->execSqlCoro("SELECT * FROM players WHERE user_id = $1", login.user_id);
		if (res.empty() && !login.gumi_live_userid.empty())
			res = co_await theDb()->execSqlCoro("SELECT * FROM players WHERE gumi_user_id = $1", login.gumi_live_userid);
		if (res.empty())
			co_return std::nullopt;
		auto p = RowToPlayer(res[0]);
		RegenEnergy(p);
		co_return p;
	}

	Task<std::optional<Player>> LoadPlayerByAccountId(std::string accountId)
	{
		const auto res = co_await theDb()->execSqlCoro("SELECT * FROM players WHERE account_id = $1", accountId);
		if (res.empty())
			co_return std::nullopt;
		auto p = RowToPlayer(res[0]);
		RegenEnergy(p);
		co_return p;
	}

	Task<void> SavePlayer(const Player& p)
	{
		co_await theDb()->execSqlCoro("UPDATE players SET team = $1, energy_at = $2, handle_name = $3, tutorial_end = $4 WHERE user_id = $5",
			ToJson(p.team), p.energy_at, p.handle_name, p.tutorial_end ? 1 : 0, p.user_id);
	}

	void RegenEnergy(Player& p)
	{
		const auto now = NowUnix();
		const auto regen = static_cast<int64_t>(theServer()->cache().serverConfig().energyRegenSeconds);
		if (regen <= 0 || p.energy_at <= 0)
		{
			p.energy_at = now;
			return;
		}
		if (p.team.action_point >= p.team.max_action_point)
		{
			p.energy_at = now;
			p.team.action_rest_timer = 0;
			return;
		}
		const auto elapsed = now - p.energy_at;
		const auto points = elapsed / regen;
		if (points > 0)
		{
			p.team.action_point = static_cast<int32_t>(std::min<int64_t>(p.team.max_action_point, p.team.action_point + points));
			p.energy_at += points * regen;
		}
		if (p.team.action_point >= p.team.max_action_point)
		{
			p.energy_at = now;
			p.team.action_rest_timer = 0;
		}
		else
			p.team.action_rest_timer = static_cast<int32_t>(regen - (now - p.energy_at));
	}

	Task<std::vector<UserUnitInfo>> LoadUnits(std::string userId)
	{
		std::vector<UserUnitInfo> out;
		const auto res = co_await theDb()->execSqlCoro("SELECT user_unit_id, data FROM player_units WHERE user_id = $1 ORDER BY user_unit_id", userId);
		for (const auto& r : res)
		{
			auto u = FromJson<UserUnitInfo>(r["data"].as<std::string>());
			u.user_unit_id = static_cast<int32_t>(r["user_unit_id"].as<int64_t>());
			u.user_id = userId;
			out.emplace_back(std::move(u));
		}
		co_return out;
	}

	Task<void> AddUnit(std::string userId, UserUnitInfo& unit)
	{
		unit.user_id = userId;
		co_await theDb()->execSqlCoro("INSERT INTO player_units(user_id, unit_id, data) VALUES ($1, $2, $3)", userId, static_cast<int64_t>(unit.unit_id), ToJson(unit));
		const auto res = co_await theDb()->execSqlCoro("SELECT last_insert_rowid() AS id");
		unit.user_unit_id = static_cast<int32_t>(res[0]["id"].as<int64_t>());
		// keep the stored copy consistent with the id the client will see
		co_await theDb()->execSqlCoro("UPDATE player_units SET data = $1 WHERE user_unit_id = $2", ToJson(unit), static_cast<int64_t>(unit.user_unit_id));
	}

	Task<void> UpdateUnit(std::string userId, const UserUnitInfo& unit)
	{
		co_await theDb()->execSqlCoro("UPDATE player_units SET unit_id = $1, data = $2 WHERE user_id = $3 AND user_unit_id = $4",
			static_cast<int64_t>(unit.unit_id), ToJson(unit), userId, static_cast<int64_t>(unit.user_unit_id));
	}

	Task<void> RemoveUnits(std::string userId, const std::vector<int32_t>& userUnitIds)
	{
		for (const auto id : userUnitIds)
		{
			co_await theDb()->execSqlCoro("DELETE FROM player_units WHERE user_id = $1 AND user_unit_id = $2", userId, static_cast<int64_t>(id));
			co_await theDb()->execSqlCoro("DELETE FROM player_decks WHERE user_id = $1 AND user_unit_id = $2", userId, static_cast<int64_t>(id));
		}
	}

	Task<std::vector<UserPartyDeckInfo>> LoadDecks(std::string userId)
	{
		std::vector<UserPartyDeckInfo> out;
		const auto res = co_await theDb()->execSqlCoro("SELECT deck_num, slot, user_unit_id, deck_type, member_type FROM player_decks WHERE user_id = $1 ORDER BY deck_num, slot", userId);
		for (const auto& r : res)
		{
			UserPartyDeckInfo d = {};
			d.deck_num = static_cast<int32_t>(r["deck_num"].as<int64_t>());
			d.disp_order = static_cast<int32_t>(r["slot"].as<int64_t>());
			d.user_unit_id = static_cast<int32_t>(r["user_unit_id"].as<int64_t>());
			d.deck_type = static_cast<int32_t>(r["deck_type"].as<int64_t>());
			d.member_type = static_cast<int32_t>(r["member_type"].as<int64_t>());
			out.emplace_back(d);
		}
		co_return out;
	}

	Task<void> SaveDecks(std::string userId, const std::vector<UserPartyDeckInfo>& decks)
	{
		co_await theDb()->execSqlCoro("DELETE FROM player_decks WHERE user_id = $1", userId);
		for (const auto& d : decks)
			co_await theDb()->execSqlCoro("INSERT INTO player_decks(user_id, deck_num, slot, user_unit_id, deck_type, member_type) VALUES ($1, $2, $3, $4, $5, $6)",
				userId, static_cast<int64_t>(d.deck_num), static_cast<int64_t>(d.disp_order), static_cast<int64_t>(d.user_unit_id), static_cast<int64_t>(d.deck_type), static_cast<int64_t>(d.member_type));
	}

	Task<std::vector<UserClearMissionInfo>> LoadClears(std::string userId)
	{
		std::vector<UserClearMissionInfo> out;
		const auto res = co_await theDb()->execSqlCoro("SELECT mission_id, clear_date, clear_cnt, most_time, get_bc, get_hc FROM player_clears WHERE user_id = $1 ORDER BY mission_id", userId);
		for (const auto& r : res)
		{
			UserClearMissionInfo c = {};
			c.user_id = userId;
			c.mission_id = static_cast<int32_t>(r["mission_id"].as<int64_t>());
			c.clear_date = r["clear_date"].as<std::string>();
			c.clear_cnt = static_cast<int32_t>(r["clear_cnt"].as<int64_t>());
			c.most_time = static_cast<int32_t>(r["most_time"].as<int64_t>());
			c.get_bc = static_cast<int32_t>(r["get_bc"].as<int64_t>());
			c.get_hc = static_cast<int32_t>(r["get_hc"].as<int64_t>());
			out.emplace_back(c);
		}
		co_return out;
	}

	Task<void> AddClear(std::string userId, int32_t missionId, std::string date)
	{
		co_await theDb()->execSqlCoro(
			"INSERT INTO player_clears(user_id, mission_id, clear_date, clear_cnt, most_time, get_bc, get_hc) VALUES ($1, $2, $3, 1, 0, 0, 0) "
			"ON CONFLICT(user_id, mission_id) DO UPDATE SET clear_cnt = clear_cnt + 1",
			userId, static_cast<int64_t>(missionId), date);
	}

	Task<bool> IsCleared(std::string userId, int32_t missionId)
	{
		const auto res = co_await theDb()->execSqlCoro("SELECT 1 FROM player_clears WHERE user_id = $1 AND mission_id = $2 LIMIT 1", userId, missionId);
		co_return res.size() > 0;
	}

	Task<std::vector<PermitPlace>> LoadPermits(std::string userId)
	{
		std::vector<PermitPlace> out;
		const auto res = co_await theDb()->execSqlCoro("SELECT kind, place_id FROM player_permits WHERE user_id = $1 ORDER BY kind, rowid", userId);
		for (const auto& r : res)
			NewPlayer::AddPermit(out, static_cast<int>(r["kind"].as<int64_t>()), r["place_id"].as<std::string>());
		co_return out;
	}

	Task<void> AddPermit(std::string userId, int kind, std::string placeId)
	{
		co_await theDb()->execSqlCoro("INSERT OR IGNORE INTO player_permits(user_id, kind, place_id) VALUES ($1, $2, $3)", userId, static_cast<int64_t>(kind), placeId);
	}

	Task<void> RememberRun(std::string serialId, std::string userId, int32_t missionId)
	{
		co_await theDb()->execSqlCoro("INSERT OR REPLACE INTO player_runs(serial_id, user_id, mission_id, started_at) VALUES ($1, $2, $3, $4)",
			serialId, userId, static_cast<int64_t>(missionId), NowUnix());
	}

	Task<int32_t> MissionForRun(std::string serialId, std::string userId)
	{
		const auto res = co_await theDb()->execSqlCoro("SELECT mission_id FROM player_runs WHERE serial_id = $1 AND user_id = $2", serialId, userId);
		if (res.empty())
			co_return 0;
		co_return static_cast<int32_t>(res[0]["mission_id"].as<int64_t>());
	}

	/*! "itemId:count,..." -> pairs (bad entries skipped). */
	static std::vector<std::pair<int64_t, int64_t>> ParseItemList(const std::string& items)
	{
		std::vector<std::pair<int64_t, int64_t>> out;
		size_t pos = 0;
		while (pos < items.size())
		{
			const auto comma = items.find(',', pos);
			const auto entry = items.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
			pos = comma == std::string::npos ? items.size() : comma + 1;
			const auto colon = entry.find(':');
			if (colon == std::string::npos) continue;
			int64_t itemId = 0, count = 0;
			try
			{
				itemId = std::stoll(entry.substr(0, colon));
				count = std::stoll(entry.substr(colon + 1));
			}
			catch (...) { continue; }
			if (itemId <= 0 || count <= 0) continue;
			out.emplace_back(itemId, count);
		}
		return out;
	}

	Task<std::vector<UserWarehouseInfo>> LoadItems(std::string userId)
	{
		std::vector<UserWarehouseInfo> out;
		const auto res = co_await theDb()->execSqlCoro("SELECT item_id, count FROM player_items WHERE user_id = $1 AND count > 0 ORDER BY item_id", userId);
		for (const auto& r : res)
		{
			UserWarehouseInfo w = {};
			w.item_index = static_cast<int32_t>(r["item_id"].as<int64_t>());
			w.item_id = std::to_string(w.item_index);
			w.item_num = static_cast<int32_t>(r["count"].as<int64_t>());
			w.new_flg = 0;
			out.emplace_back(w);
		}
		co_return out;
	}

	Task<void> AddItems(std::string userId, std::string items)
	{
		for (const auto& [itemId, count] : ParseItemList(items))
			co_await theDb()->execSqlCoro(
				"INSERT INTO player_items(user_id, item_id, count) VALUES ($1, $2, $3) ON CONFLICT(user_id, item_id) DO UPDATE SET count = count + $3",
				userId, itemId, count);
	}

	Task<void> TakeItems(std::string userId, std::string items)
	{
		for (const auto& [itemId, count] : ParseItemList(items))
		{
			// $N must appear in numeric order: SQLite takes "$3" as a *name* numbered by first appearance
			// and Drogon binds positionally, so "count - $3 WHERE user_id = $1" got the user id as $3
			// (ItemSell credited the zel but never removed the items)
			co_await theDb()->execSqlCoro("UPDATE player_items SET count = MAX(0, count - $1) WHERE user_id = $2 AND item_id = $3", count, userId, itemId);
			co_await theDb()->execSqlCoro("DELETE FROM player_items WHERE user_id = $1 AND item_id = $2 AND count <= 0", userId, itemId);
			// the equipped count cannot exceed what is left
			co_await theDb()->execSqlCoro(
				"UPDATE player_equip SET count = MIN(count, COALESCE((SELECT count FROM player_items WHERE user_id = $1 AND item_id = $2), 0)) WHERE user_id = $1 AND item_id = $2",
				userId, itemId);
			co_await theDb()->execSqlCoro("DELETE FROM player_equip WHERE user_id = $1 AND count <= 0", userId);
		}
	}

	Task<std::vector<UserEquipItemInfo>> LoadEquip(std::string userId)
	{
		std::vector<UserEquipItemInfo> out;
		const auto res = co_await theDb()->execSqlCoro("SELECT slot, item_id, count FROM player_equip WHERE user_id = $1 AND count > 0 ORDER BY slot", userId);
		for (const auto& r : res)
		{
			UserEquipItemInfo e = {};
			e.disp_order = static_cast<int32_t>(r["slot"].as<int64_t>());
			e.item_id = std::to_string(r["item_id"].as<int64_t>());
			e.item_num = static_cast<int32_t>(r["count"].as<int64_t>());
			out.emplace_back(e);
		}
		co_return out;
	}

	Task<void> SaveEquip(std::string userId, const std::vector<UserEquipItemInfo>& equip)
	{
		co_await theDb()->execSqlCoro("DELETE FROM player_equip WHERE user_id = $1", userId);
		for (const auto& e : equip)
		{
			int64_t itemId = 0;
			try { itemId = std::stoll(e.item_id); } catch (...) { continue; }
			if (itemId <= 0 || e.item_num <= 0) continue;
			co_await theDb()->execSqlCoro(
				"INSERT INTO player_equip(user_id, slot, item_id, count) VALUES ($1, $2, $3, $4) ON CONFLICT(user_id, slot) DO UPDATE SET item_id = $3, count = $4",
				userId, static_cast<int64_t>(e.disp_order), itemId, static_cast<int64_t>(e.item_num));
		}
	}
	Task<std::unordered_map<int32_t, TownLevel>> LoadTown(std::string userId, int32_t kind)
	{
		std::unordered_map<int32_t, TownLevel> out;
		const auto res = co_await theDb()->execSqlCoro("SELECT id, lv, karma FROM player_town WHERE user_id = $1 AND kind = $2", userId, static_cast<int64_t>(kind));
		for (const auto& r : res)
			out[static_cast<int32_t>(r["id"].as<int64_t>())] = TownLevel{ static_cast<int32_t>(r["lv"].as<int64_t>()), static_cast<int32_t>(r["karma"].as<int64_t>()) };
		co_return out;
	}

	Task<void> SaveTown(std::string userId, int32_t kind, int32_t id, int32_t lv, int32_t karma)
	{
		co_await theDb()->execSqlCoro(
			"INSERT INTO player_town(user_id, kind, id, lv, karma) VALUES ($1, $2, $3, $4, $5) ON CONFLICT(user_id, kind, id) DO UPDATE SET lv = $4, karma = $5",
			userId, static_cast<int64_t>(kind), static_cast<int64_t>(id), static_cast<int64_t>(lv), static_cast<int64_t>(karma));
	}

	Task<std::unordered_map<int32_t, KeyState>> LoadKeys(std::string userId)
	{
		std::unordered_map<int32_t, KeyState> out;
		const auto res = co_await theDb()->execSqlCoro("SELECT key_id, count, last_day FROM player_keys WHERE user_id = $1", userId);
		for (const auto& r : res)
			out[static_cast<int32_t>(r["key_id"].as<int64_t>())] = KeyState{ static_cast<int32_t>(r["count"].as<int64_t>()), r["last_day"].as<int64_t>() };
		co_return out;
	}

	Task<void> SaveKey(std::string userId, int32_t keyId, int32_t count, int64_t lastDay)
	{
		co_await theDb()->execSqlCoro(
			"INSERT INTO player_keys(user_id, key_id, count, last_day) VALUES ($1, $2, $3, $4) ON CONFLICT(user_id, key_id) DO UPDATE SET count = $3, last_day = $4",
			userId, static_cast<int64_t>(keyId), static_cast<int64_t>(count), lastDay);
	}

	Task<ParadeMap> LoadParades(std::string userId)
	{
		ParadeMap out;
		const auto res = co_await theDb()->execSqlCoro("SELECT dungeon_id, until, missions FROM player_parade WHERE user_id = $1", userId);
		for (const auto& r : res)
			out[static_cast<int32_t>(r["dungeon_id"].as<int64_t>())] = Parade{ r["until"].as<int64_t>(), r["missions"].as<std::string>() };
		co_return out;
	}

	Task<void> SaveParade(std::string userId, int32_t dungeonId, int64_t until, std::string missions)
	{
		co_await theDb()->execSqlCoro(
			"INSERT INTO player_parade(user_id, dungeon_id, until, missions) VALUES ($1, $2, $3, $4) ON CONFLICT(user_id, dungeon_id) DO UPDATE SET until = $3, missions = $4",
			userId, static_cast<int64_t>(dungeonId), until, missions);
	}

	Task<ArenaState> LoadArena(std::string userId)
	{
		ArenaState st;
		const auto res = co_await theDb()->execSqlCoro("SELECT rank_point, battle_cnt, win_cnt, accept_cnt, accept_win, max_rank, series_win, max_series, battle_id, enemy, suggested, fight_at, archive FROM player_arena WHERE user_id = $1", userId);
		if (res.empty()) co_return st;
		const auto& r = res[0];
		st.rank_point = static_cast<int32_t>(r["rank_point"].as<int64_t>());
		st.battle_cnt = static_cast<int32_t>(r["battle_cnt"].as<int64_t>());
		st.win_cnt = static_cast<int32_t>(r["win_cnt"].as<int64_t>());
		st.accept_cnt = static_cast<int32_t>(r["accept_cnt"].as<int64_t>());
		st.accept_win = static_cast<int32_t>(r["accept_win"].as<int64_t>());
		st.max_rank = static_cast<int32_t>(r["max_rank"].as<int64_t>());
		st.series_win = static_cast<int32_t>(r["series_win"].as<int64_t>());
		st.max_series = static_cast<int32_t>(r["max_series"].as<int64_t>());
		st.battle_id = r["battle_id"].as<std::string>();
		st.enemy = r["enemy"].as<std::string>();
		st.suggested = r["suggested"].as<std::string>();
		st.fight_at = r["fight_at"].as<int64_t>();
		st.archive = r["archive"].as<std::string>();
		co_return st;
	}

	Task<void> SaveArena(std::string userId, const ArenaState& st)
	{
		co_await theDb()->execSqlCoro(
			"INSERT INTO player_arena(user_id, rank_point, battle_cnt, win_cnt, accept_cnt, accept_win, max_rank, series_win, max_series, battle_id, enemy, suggested, fight_at, archive) "
			"VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14) ON CONFLICT(user_id) DO UPDATE SET rank_point = $2, battle_cnt = $3, win_cnt = $4, accept_cnt = $5, "
			"accept_win = $6, max_rank = $7, series_win = $8, max_series = $9, battle_id = $10, enemy = $11, suggested = $12, fight_at = $13, archive = $14",
			userId, static_cast<int64_t>(st.rank_point), static_cast<int64_t>(st.battle_cnt), static_cast<int64_t>(st.win_cnt), static_cast<int64_t>(st.accept_cnt),
			static_cast<int64_t>(st.accept_win), static_cast<int64_t>(st.max_rank), static_cast<int64_t>(st.series_win), static_cast<int64_t>(st.max_series),
			st.battle_id, st.enemy, st.suggested, st.fight_at, st.archive);
	}

	Task<std::vector<Player>> ListOtherPlayers(std::string userId, int32_t limit)
	{
		std::vector<Player> out;
		const auto res = co_await theDb()->execSqlCoro("SELECT * FROM players WHERE user_id <> $1 ORDER BY created_at DESC LIMIT $2", userId, static_cast<int64_t>(limit));
		for (const auto& r : res)
			out.push_back(RowToPlayer(r));
		co_return out;
	}

	Task<std::vector<Player>> RandomPlayers(std::string userId, int32_t limit)
	{
		std::vector<Player> out;
		const auto res = co_await theDb()->execSqlCoro(
			"SELECT * FROM players WHERE user_id <> $1 AND user_id NOT IN (SELECT friend_id FROM player_friends WHERE user_id = $1) ORDER BY RANDOM() LIMIT $2",
			userId, static_cast<int64_t>(limit));
		for (const auto& r : res)
			out.push_back(RowToPlayer(r));
		co_return out;
	}

	Task<std::optional<Player>> LoadPlayerByUserId(std::string userId)
	{
		const auto res = co_await theDb()->execSqlCoro("SELECT * FROM players WHERE user_id = $1", userId);
		if (res.empty())
			co_return std::nullopt;
		auto p = RowToPlayer(res[0]);
		RegenEnergy(p);
		co_return p;
	}

	Task<void> TouchLogin(std::string userId, int64_t now)
	{
		co_await theDb()->execSqlCoro("UPDATE players SET last_login = $1 WHERE user_id = $2", now, userId);
	}

	Task<std::vector<FriendRow>> LoadFriends(std::string userId)
	{
		std::vector<FriendRow> out;
		const auto res = co_await theDb()->execSqlCoro("SELECT friend_id, state, favorite, since FROM player_friends WHERE user_id = $1 ORDER BY since DESC", userId);
		for (const auto& r : res)
			out.push_back({ r["friend_id"].as<std::string>(), static_cast<int32_t>(r["state"].as<int64_t>()), static_cast<int32_t>(r["favorite"].as<int64_t>()), r["since"].as<int64_t>() });
		co_return out;
	}

	Task<void> SetFriend(std::string userId, std::string friendId, int32_t state, int64_t since)
	{
		co_await theDb()->execSqlCoro(
			"INSERT INTO player_friends(user_id, friend_id, state, favorite, since) VALUES ($1, $2, $3, 0, $4) "
			"ON CONFLICT(user_id, friend_id) DO UPDATE SET state = $3, since = $4",
			userId, friendId, static_cast<int64_t>(state), since);
	}

	Task<void> RemoveFriend(std::string userId, std::string friendId)
	{
		co_await theDb()->execSqlCoro("DELETE FROM player_friends WHERE user_id = $1 AND friend_id = $2", userId, friendId);
	}

	Task<void> SetFavorite(std::string userId, std::string friendId, int32_t favorite)
	{
		co_await theDb()->execSqlCoro("UPDATE player_friends SET favorite = $1 WHERE user_id = $2 AND friend_id = $3", static_cast<int64_t>(favorite), userId, friendId);
	}

	Task<Run> RunFor(std::string serialId, std::string userId)
	{
		Run run;
		const auto res = co_await theDb()->execSqlCoro("SELECT mission_id, helper FROM player_runs WHERE serial_id = $1 AND user_id = $2", serialId, userId);
		if (res.empty())
			co_return run;
		run.mission_id = static_cast<int32_t>(res[0]["mission_id"].as<int64_t>());
		run.helper = res[0]["helper"].isNull() ? "" : res[0]["helper"].as<std::string>();
		co_return run;
	}

	Task<void> SetRunHelper(std::string serialId, std::string helper)
	{
		co_await theDb()->execSqlCoro("UPDATE player_runs SET helper = $1 WHERE serial_id = $2", helper, serialId);
	}

	Task<std::unordered_map<int32_t, TownCollect>> LoadCollect(std::string userId)
	{
		std::unordered_map<int32_t, TownCollect> out;
		const auto res = co_await theDb()->execSqlCoro("SELECT location_id, tap_cnt, drops, refill_at FROM player_town_collect WHERE user_id = $1", userId);
		for (const auto& r : res)
			out[static_cast<int32_t>(r["location_id"].as<int64_t>())] = TownCollect{ static_cast<int32_t>(r["tap_cnt"].as<int64_t>()), r["drops"].as<std::string>(), r["refill_at"].as<int64_t>() };
		co_return out;
	}

	Task<void> SaveCollect(std::string userId, int32_t locationId, const TownCollect& c)
	{
		co_await theDb()->execSqlCoro(
			"INSERT INTO player_town_collect(user_id, location_id, tap_cnt, drops, refill_at) VALUES ($1, $2, $3, $4, $5) ON CONFLICT(user_id, location_id) DO UPDATE SET tap_cnt = $3, drops = $4, refill_at = $5",
			userId, static_cast<int64_t>(locationId), static_cast<int64_t>(c.tap_cnt), c.drops, c.refill_at);
	}

}
