#include "App.hpp"
#include "MigrationManager.hpp"

using MigrationMap = std::unordered_map<std::string, std::function<void(drogon::orm::DbClientPtr& db)>>;

#define migrate(name, func) map.insert_or_assign(name, [](drogon::orm::DbClientPtr& p) func )

/*!
* Register all the available migrations
* @param map Map to register
*/
static void RegisterMigrations(MigrationMap& map)
{
	// DDMMYYYY_MigrationName

	migrate("29082025_CreateDefaultTables", {
		p->execSqlSync(
			"CREATE TABLE gumi_live_users("
			"id TEXT PRIMARY KEY NOT NULL"
			");"
		);
		p->execSqlSync(
			"CREATE TABLE userinfo("
			"id TEXT PRIMARY KEY NOT NULL,"
			"gumi_user_id TEXT NOT NULL,"
			"device_id TEXT NOT NULL,"
			"username TEXT NOT NULL DEFAULT '',"
			"level INTEGER(3) NOT NULL,"
			"debug_mode INTEGER(1) NOT NULL DEFAULT 0,"
			"exp INTEGER(3) NOT NULL DEFAULT 0,"
			"max_unit_count INTEGER(9) NOT NULL DEFAULT 0,"
			"max_friend_count INTEGER(9) NOT NULL DEFAULT 0,"
			"zel INTEGER(9) NOT NULL DEFAULT 0,"
			"karma INTEGER(9) NOT NULL DEFAULT 0,"
			"brave_coin INTEGER(9) NOT NULL DEFAULT 0,"
			"max_warehouse_count INTEGER(9) NOT NULL DEFAULT 0,"
			"want_gift TEXT NOT NULL DEFAULT '',"
			"free_gems INTEGER(4) NOT NULL DEFAULT 0,"
			"paid_gems INTEGER(4) NOT NULL DEFAULT 0,"
			"active_deck INTEGER(1) NOT NULL DEFAULT 0,"
			"summon_tickets INTEGER(4) NOT NULL DEFAULT 0,"
			"rainbow_coins INTEGER(4) NOT NULL DEFAULT 0,"
			"colosseum_tickets INTEGER(4) NOT NULL DEFAULT 0,"
			"active_arena_deck INTEGER(1) NOT NULL DEFAULT 0,"
			"total_brave_points INTEGER(9) NOT NULL DEFAULT 0,"
			"avail_brave_points INTEGER(9) NOT NULL DEFAULT 0,"
			"energy INTEGER(10) NOT NULL DEFAULT 0"
			");"
		);
	});

	// bfanew: persistent players (gimuserver/db/Player.hpp)
	migrate("30102026_PlayerTables", {
		p->execSqlSync(
			"CREATE TABLE accounts("
			"gumi_user_id TEXT PRIMARY KEY NOT NULL,"
			"device_vid TEXT NOT NULL,"
			"device_name TEXT NOT NULL DEFAULT '',"
			"token TEXT NOT NULL,"
			"created_at INTEGER NOT NULL"
			");"
		);
		p->execSqlSync("CREATE UNIQUE INDEX accounts_device ON accounts(device_vid);");
		p->execSqlSync(
			"CREATE TABLE players("
			"user_id TEXT PRIMARY KEY NOT NULL,"
			"gumi_user_id TEXT NOT NULL,"
			"account_id TEXT NOT NULL,"
			"handle_name TEXT NOT NULL DEFAULT '',"
			"team TEXT NOT NULL,"
			"energy_at INTEGER NOT NULL DEFAULT 0,"
			"tutorial_end INTEGER NOT NULL DEFAULT 1,"
			"created_at INTEGER NOT NULL"
			");"
		);
		p->execSqlSync("CREATE UNIQUE INDEX players_gumi ON players(gumi_user_id);");
		p->execSqlSync(
			"CREATE TABLE player_units("
			"user_unit_id INTEGER PRIMARY KEY AUTOINCREMENT,"
			"user_id TEXT NOT NULL,"
			"unit_id INTEGER NOT NULL,"
			"data TEXT NOT NULL"
			");"
		);
		p->execSqlSync("CREATE INDEX player_units_user ON player_units(user_id);");
		p->execSqlSync(
			"CREATE TABLE player_decks("
			"user_id TEXT NOT NULL,"
			"deck_num INTEGER NOT NULL,"
			"slot INTEGER NOT NULL,"
			"user_unit_id INTEGER NOT NULL,"
			"deck_type INTEGER NOT NULL DEFAULT 1,"
			"member_type INTEGER NOT NULL DEFAULT 0,"
			"PRIMARY KEY(user_id, deck_num, slot)"
			");"
		);
		p->execSqlSync(
			"CREATE TABLE player_clears("
			"user_id TEXT NOT NULL,"
			"mission_id INTEGER NOT NULL,"
			"clear_date TEXT NOT NULL,"
			"clear_cnt INTEGER NOT NULL DEFAULT 1,"
			"most_time INTEGER NOT NULL DEFAULT 0,"
			"get_bc INTEGER NOT NULL DEFAULT 0,"
			"get_hc INTEGER NOT NULL DEFAULT 0,"
			"PRIMARY KEY(user_id, mission_id)"
			");"
		);
		p->execSqlSync(
			"CREATE TABLE player_permits("
			"user_id TEXT NOT NULL,"
			"kind INTEGER NOT NULL,"
			"place_id TEXT NOT NULL,"
			"PRIMARY KEY(user_id, kind, place_id)"
			");"
		);
		p->execSqlSync(
			"CREATE TABLE player_runs("
			"serial_id TEXT PRIMARY KEY NOT NULL,"
			"user_id TEXT NOT NULL,"
			"mission_id INTEGER NOT NULL,"
			"started_at INTEGER NOT NULL"
			");"
		);
		p->execSqlSync(
			"CREATE TABLE player_items("
			"user_id TEXT NOT NULL,"
			"item_id INTEGER NOT NULL,"
			"count INTEGER NOT NULL DEFAULT 0,"
			"PRIMARY KEY(user_id, item_id)"
			");"
		);
	});

	// bfanew: items equipped for quests (ItemEdit)
	migrate("30102026_PlayerEquip", {
		p->execSqlSync(
			"CREATE TABLE player_equip("
			"user_id TEXT NOT NULL,"
			"slot INTEGER NOT NULL,"
			"item_id INTEGER NOT NULL,"
			"count INTEGER NOT NULL DEFAULT 1,"
			"PRIMARY KEY(user_id, slot)"
			");"
		);
	});

	// bfanew: town facility / location levels and the karma injected toward the next level
	// (TownFacilityUpdate); kind 1 = facility (TownFacilityMst id), 2 = location (TownLocationMst id)
	migrate("31102026_PlayerTown", {
		p->execSqlSync(
			"CREATE TABLE player_town("
			"user_id TEXT NOT NULL,"
			"kind INTEGER NOT NULL,"
			"id INTEGER NOT NULL,"
			"lv INTEGER NOT NULL DEFAULT 1,"
			"karma INTEGER NOT NULL DEFAULT 0,"
			"PRIMARY KEY(user_id, kind, id)"
			");"
		);
	});

	// bfanew: village item collection (TownUpdate): per location the taps left, the pre-rolled drops
	// ("itemId:zel:karma,..." consumed from the first) and when an emptied location refills
	migrate("31102026_PlayerTownCollect", {
		p->execSqlSync(
			"CREATE TABLE player_town_collect("
			"user_id TEXT NOT NULL,"
			"location_id INTEGER NOT NULL,"
			"tap_cnt INTEGER NOT NULL DEFAULT 0,"
			"drops TEXT NOT NULL DEFAULT '',"
			"refill_at INTEGER NOT NULL DEFAULT 0,"
			"PRIMARY KEY(user_id, location_id)"
			");"
		);
	});

	// bfanew: dungeon keys (Parade keys): the keys held per key id with the day of the last daily
	// grant, and the parade windows a key opened (DungeonKeyUse, gme/Keys.hpp)
	migrate("31102026_PlayerKeys", {
		p->execSqlSync(
			"CREATE TABLE player_keys("
			"user_id TEXT NOT NULL,"
			"key_id INTEGER NOT NULL,"
			"count INTEGER NOT NULL DEFAULT 0,"
			"last_day INTEGER NOT NULL DEFAULT -1,"
			"PRIMARY KEY(user_id, key_id)"
			");"
		);
		p->execSqlSync(
			"CREATE TABLE player_parade("
			"user_id TEXT NOT NULL,"
			"dungeon_id INTEGER NOT NULL,"
			"until INTEGER NOT NULL DEFAULT 0,"
			"missions TEXT NOT NULL DEFAULT '',"
			"PRIMARY KEY(user_id, dungeon_id)"
			");"
		);
	});

	// bfanew: the Arena record (gme/Arena.hpp, db/Player.hpp ArenaState)
	migrate("01112026_PlayerArena", {
		p->execSqlSync(
			"CREATE TABLE player_arena("
			"user_id TEXT PRIMARY KEY,"
			"rank_point INTEGER NOT NULL DEFAULT 0,"
			"battle_cnt INTEGER NOT NULL DEFAULT 0,"
			"win_cnt INTEGER NOT NULL DEFAULT 0,"
			"accept_cnt INTEGER NOT NULL DEFAULT 0,"
			"accept_win INTEGER NOT NULL DEFAULT 0,"
			"max_rank INTEGER NOT NULL DEFAULT 1,"
			"series_win INTEGER NOT NULL DEFAULT 0,"
			"max_series INTEGER NOT NULL DEFAULT 0,"
			"battle_id TEXT NOT NULL DEFAULT '',"
			"enemy TEXT NOT NULL DEFAULT '',"
			"suggested TEXT NOT NULL DEFAULT '',"
			"fight_at INTEGER NOT NULL DEFAULT 0,"
			"archive TEXT NOT NULL DEFAULT ''"
			");"
		);
	});

	// bfanew: friends (gme/Friends.hpp), the players' last login and the helper of a mission run
	migrate("01112026_Friends", {
		p->execSqlSync(
			"CREATE TABLE player_friends("
			"user_id TEXT NOT NULL,"
			"friend_id TEXT NOT NULL,"
			"state INTEGER NOT NULL DEFAULT 0,"
			"favorite INTEGER NOT NULL DEFAULT 0,"
			"since INTEGER NOT NULL DEFAULT 0,"
			"PRIMARY KEY(user_id, friend_id)"
			");"
		);
		p->execSqlSync("ALTER TABLE players ADD COLUMN last_login INTEGER NOT NULL DEFAULT 0;");
		p->execSqlSync("ALTER TABLE player_runs ADD COLUMN helper TEXT NOT NULL DEFAULT '';");
	});

#if 0
	migrate("08032025_CreateUserUnitsTable", {
		p->execSqlSync(
			"CREATE TABLE IF NOT EXISTS user_units ("
			"id INTEGER PRIMARY KEY AUTOINCREMENT," // Add: Auto-incrementing primary key as per PR comment
			"user_id TEXT NOT NULL," // Keep: Links unit to a user
			"unit_id TEXT NOT NULL" // Keep: Stores the unit identifier
			");"
		);
	});
#endif
}

/*!
* Gets all the runned migration (and optionally create the table)
* @param p Database pointer
* @param hash Output where to store all the hashes
*/
static void GetMigrationStatus(drogon::orm::DbClientPtr& p, std::vector<std::string>& hash)
{
	p->execSqlSync(
		"CREATE TABLE IF NOT EXISTS migration_status("
		"hash TEXT PRIMARY KEY NOT NULL"
		");"
	);
	auto res = p->execSqlSync(
		"SELECT hash FROM migration_status;"
	);
	if (res.size() > 0)
	{
		for (const auto& x : res)
		{
			const auto& str = x[0].as<std::string>();
			LOG_DEBUG << "Migration found: " << str;
			hash.emplace_back(str);
		}
	}
}

void MigrationManager::RunMigrations(drogon::orm::DbClientPtr ptr)
{
	MigrationMap migrations;
	RegisterMigrations(migrations);

	std::vector<std::string> runnedMigratons;
	GetMigrationStatus(ptr, runnedMigratons);

	try
	{
		for (const auto& [k, v] : migrations)
		{
			if (std::find(runnedMigratons.begin(), runnedMigratons.end(), k) != runnedMigratons.end())
				continue;

			LOG_INFO << "Execute migration " << k;
			v(ptr);
			ptr->execSqlSync("INSERT INTO migration_status(hash) VALUES ($1);", k);
		}
	}
	catch (drogon::orm::DrogonDbException ex)
	{
		LOG_ERROR << "Cannot execute migration: " << ex.base().what();
		drogon::app().quit();
	}

}

