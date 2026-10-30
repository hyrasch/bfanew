#pragma once

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>

#include <optional>
#include <string>
#include <vector>

/*
* bfanew: persistent player state (step 4 of CLAUDE.md).
*
* Tables (migration "30102026_PlayerTables" in MigrationManager.cpp), all in deploy/gme.sqlite:
*   accounts       gumi_user_id (what /accounts/guest/login/ answers), device vid, token, user_id
*   players        user_id (the 16-digit id the client carries in every request), account_id,
*                  handle name, the UserTeamInfo as JSON (wallet, level, energy...), energy_at
*   player_units   user_unit_id (autoincrement), the UserUnitInfo as JSON
*   player_decks   (deck_num, slot) -> user_unit_id
*   player_clears  cleared missions (UserClearMissionInfo fields)
*   player_permits permitted places (kind 1 area 2 dungeon 3 mission 4 land 5 gate)
*   player_runs    MissionStart serial id -> mission id
*   player_items   item id -> count (pickups; not sent to the client yet, UserWarehouseInfo is a mock)
*
* The packet structs are stored as JSON (glaze) so that the schema does not have to mirror
* their 40-odd fields; the columns next to them are the ones queried.
*/
namespace Db
{
	struct Player
	{
		std::string user_id;
		std::string gumi_user_id;
		std::string account_id;
		std::string handle_name;
		UserTeamInfo team = {};
		int64_t energy_at = 0; //!< unix time the energy value in team was computed at
		bool tutorial_end = true;
	};

	/*! The level table row for a level (nullptr if none). */
	const UserLevelMst* LevelRow(int32_t level);

	/*! Guest login: the account for a device (created on first sight). Returns {gumi_user_id, token}. */
	drogon::Task<std::pair<std::string, std::string>> AccountForDevice(std::string vid, std::string deviceName);

	/*! The player of a gumi account, created with the new-player template when missing (Initialize). */
	drogon::Task<Player> PlayerForAccount(std::string gumiUserId);

	/*! The player behind a request's login info (user id first, gumi id as fallback). */
	drogon::Task<std::optional<Player>> LoadPlayer(const LoginInfoReq& login);

	/*! Writes the wallet / level / energy back. */
	drogon::Task<void> SavePlayer(const Player& p);

	/*! Brings the energy up to date (1 point per ServerConfig::energyRegenSeconds, capped at the max). */
	void RegenEnergy(Player& p);

	drogon::Task<std::vector<UserUnitInfo>> LoadUnits(std::string userId);
	/*! Inserts a unit; its user_unit_id is assigned by the database and written back. */
	drogon::Task<void> AddUnit(std::string userId, UserUnitInfo& unit);

	drogon::Task<std::vector<UserPartyDeckInfo>> LoadDecks(std::string userId);
	drogon::Task<void> SaveDecks(std::string userId, const std::vector<UserPartyDeckInfo>& decks);

	drogon::Task<std::vector<UserClearMissionInfo>> LoadClears(std::string userId);
	drogon::Task<void> AddClear(std::string userId, int32_t missionId, std::string date);

	drogon::Task<std::vector<PermitPlace>> LoadPermits(std::string userId);
	drogon::Task<void> AddPermit(std::string userId, int kind, std::string placeId);

	drogon::Task<void> RememberRun(std::string serialId, std::string userId, int32_t missionId);
	/*! Mission id of a run of this player, 0 if unknown. */
	drogon::Task<int32_t> MissionForRun(std::string serialId, std::string userId);

	/*! Adds pickups given as "itemId:count,...". */
	drogon::Task<void> AddItems(std::string userId, std::string items);
}
