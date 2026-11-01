#pragma once
#include <unordered_map>
#include <unordered_set>

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
*   player_items   item id -> count: the warehouse (UserWarehouseInfo), equipped items included
*   player_equip   slot -> item id, count: the items equipped for quests (UserEquipItemInfo)
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
		int32_t tutorial_status = 12;  //!< the tutorial chapter reached (gme/Tutorial.hpp; 12 = done)
		int32_t unit_mix_cnt = 0;      //!< fusions done (UserTeamArchive, read by the tutorial)
		int32_t item_mix_cnt = 0;      //!< syntheses done
		int64_t last_login = 0; //!< unix time of the last UserInfo (shown to friends)
		std::string scenario;         //!< story events watched: the client's UserScenarioInfoList save string (UserInfo N4XVE1uA)
		std::string special_scenario; //!< special scenarios watched (UserInfo 9yVsu21R)
		int64_t bonus_day = -1;       //!< game day of the last daily login bonus (gme/Presents.hpp)
		int32_t bonus_streak = 0;     //!< consecutive days logged in (picks the bonus of the cycle)
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
	/*! The player whose friend id (= account_id, what the client appends to web pages) is this. */
	drogon::Task<std::optional<Player>> LoadPlayerByAccountId(std::string accountId);

	/*! Brings the energy up to date (1 point per ServerConfig::energyRegenSeconds, capped at the max). */
	void RegenEnergy(Player& p);

	drogon::Task<std::vector<UserUnitInfo>> LoadUnits(std::string userId);
	/*! Inserts a unit; its user_unit_id is assigned by the database and written back. */
	drogon::Task<void> AddUnit(std::string userId, UserUnitInfo& unit, double typeRoll = 0.5);
	/*! user unit id -> the type roll (0..1) of the summon (Units::ApplyType), 0.5 when unknown. */
	drogon::Task<std::unordered_map<int32_t, double>> LoadUnitRolls(std::string userId);
	/*! Writes a changed unit back (fusion, evolution). */
	drogon::Task<void> UpdateUnit(std::string userId, const UserUnitInfo& unit);
	/*! Deletes units (and the squad slots that held them). */
	drogon::Task<void> RemoveUnits(std::string userId, const std::vector<int32_t>& userUnitIds);

	drogon::Task<std::vector<UserPartyDeckInfo>> LoadDecks(std::string userId);
	drogon::Task<void> SaveDecks(std::string userId, const std::vector<UserPartyDeckInfo>& decks);

	drogon::Task<std::vector<UserClearMissionInfo>> LoadClears(std::string userId);
	drogon::Task<void> AddClear(std::string userId, int32_t missionId, std::string date);
	/*! Whether the mission was already cleared once (first-clear rewards). */
	drogon::Task<bool> IsCleared(std::string userId, int32_t missionId);

	drogon::Task<std::vector<PermitPlace>> LoadPermits(std::string userId);
	drogon::Task<void> AddPermit(std::string userId, int kind, std::string placeId);

	drogon::Task<void> RememberRun(std::string serialId, std::string userId, int32_t missionId);
	/*! Mission id of a run of this player, 0 if unknown. */
	drogon::Task<int32_t> MissionForRun(std::string serialId, std::string userId);

	/*! The warehouse: one record per item owned (item_index = item id). */
	drogon::Task<std::vector<UserWarehouseInfo>> LoadItems(std::string userId);
	/*! Adds pickups given as "itemId:count,...". */
	drogon::Task<void> AddItems(std::string userId, std::string items);
	/*! Removes items given as "itemId:count,..." (clamped at 0; equipped counts follow). */
	drogon::Task<void> TakeItems(std::string userId, std::string items);

	drogon::Task<std::vector<UserEquipItemInfo>> LoadEquip(std::string userId);
	drogon::Task<void> SaveEquip(std::string userId, const std::vector<UserEquipItemInfo>& equip);

	/*! Town facility / location levels (player_town): kind 1 facility, 2 location -> {id -> {lv, karma}}. */
	struct TownLevel { int32_t lv = 1; int32_t karma = 0; };
	drogon::Task<std::unordered_map<int32_t, TownLevel>> LoadTown(std::string userId, int32_t kind);
	drogon::Task<void> SaveTown(std::string userId, int32_t kind, int32_t id, int32_t lv, int32_t karma);

	/*! Village item collection (player_town_collect): per location the taps left, the pre-rolled
	 *  drops ("itemId:zel:karma,...", entry [n - tap_cnt] is the next one) and the unix time an
	 *  emptied location refills (0 = never rolled). */
	struct TownCollect { int32_t tap_cnt = 0; std::string drops; int64_t refill_at = 0; };
	drogon::Task<std::unordered_map<int32_t, TownCollect>> LoadCollect(std::string userId);
	drogon::Task<void> SaveCollect(std::string userId, int32_t locationId, const TownCollect& c);

	/*! bfanew: dungeon keys (DungeonKeyUse): per key id the keys held and the game-clock day of the
	 *  last daily grant (-1 = never); and the parade windows a key opened: per dungeon until when
	 *  (unix seconds) and the missions of the tier used ("100600" or "100601,100614"). */
	struct KeyState { int32_t count = 0; int64_t last_day = -1; };
	struct Parade { int64_t until = 0; std::string missions; };
	using ParadeMap = std::unordered_map<int32_t, Parade>;
	drogon::Task<std::unordered_map<int32_t, KeyState>> LoadKeys(std::string userId);
	drogon::Task<void> SaveKey(std::string userId, int32_t keyId, int32_t count, int64_t lastDay);
	drogon::Task<ParadeMap> LoadParades(std::string userId);
	drogon::Task<void> SaveParade(std::string userId, int32_t dungeonId, int64_t until, std::string missions);

	/*! bfanew: the Arena record (gme/Arena.hpp): Arena Battle Points, the counts, the best rank
	 *  reached (its reward paid), the battle in progress (id, opponent as JSON) and the opponent
	 *  last suggested, and the time the orb regen counts from. */
	struct ArenaState
	{
		int32_t rank_point = 0;
		int32_t battle_cnt = 0;
		int32_t win_cnt = 0;
		int32_t accept_cnt = 0;
		int32_t accept_win = 0;
		int32_t max_rank = 1;
		int32_t series_win = 0;
		int32_t max_series = 0;
		std::string battle_id;
		std::string enemy;
		std::string suggested;
		int64_t fight_at = 0;
		std::string archive; //!< UserTeamArenaArchive as JSON (the Record statistics)
	};
	drogon::Task<ArenaState> LoadArena(std::string userId);
	drogon::Task<void> SaveArena(std::string userId, const ArenaState& st);
	/*! Up to limit other players (arena opponents), most recently created first. */
	drogon::Task<std::vector<Player>> ListOtherPlayers(std::string userId, int32_t limit);
	/*! Up to limit other players in random order, friends and pending requests excluded (FriendRandomSearch). */
	drogon::Task<std::vector<Player>> RandomPlayers(std::string userId, int32_t limit);
	drogon::Task<std::optional<Player>> LoadPlayerByUserId(std::string userId);
	/*! Records the login time (what friends see as the last login). */
	drogon::Task<void> TouchLogin(std::string userId, int64_t now);

	/*! bfanew: friends (gme/Friends.hpp). One row per direction: state 1 = friends, 2 = a request
	 *  from friend_id waiting for user_id's answer, 3 = user_id's request waiting on friend_id. */
	struct FriendRow { std::string friend_id; int32_t state = 0; int32_t favorite = 0; int64_t since = 0; };
	drogon::Task<std::vector<FriendRow>> LoadFriends(std::string userId);
	drogon::Task<void> SetFriend(std::string userId, std::string friendId, int32_t state, int64_t since);
	drogon::Task<void> RemoveFriend(std::string userId, std::string friendId);
	drogon::Task<void> SetFavorite(std::string userId, std::string friendId, int32_t favorite);

	/*! bfanew: the Gift Box (gme/Presents.hpp): a present waiting in the box or already claimed. */
	struct PresentRow { int64_t id = 0; int32_t type = 0; int32_t target_id = 0; int32_t count = 1; std::string description; int64_t created = 0; int64_t expires = 0; int64_t received = 0; };
	drogon::Task<std::vector<PresentRow>> LoadPresents(std::string userId);
	drogon::Task<void> AddPresent(std::string userId, int32_t type, int32_t targetId, int32_t count, std::string description, int64_t created, int64_t expires);
	drogon::Task<void> MarkPresent(int64_t id, int64_t received);

	/*! bfanew: gifts between friends (gme/Gifts.hpp). */
	struct GiftRow { int64_t id = 0; std::string from_user; std::string to_user; int32_t gift_id = 0; int64_t created = 0; int64_t received = 0; };
	drogon::Task<std::vector<GiftRow>> LoadGiftsTo(std::string userId);
	drogon::Task<void> AddGift(std::string fromUser, std::string toUser, int32_t giftId, int64_t created);
	drogon::Task<void> MarkGift(int64_t id, int64_t received);
	/*! The friends the player sent a gift to since a time (one gift per friend per day). */
	drogon::Task<std::unordered_set<std::string>> GiftsSentSince(std::string fromUser, int64_t since);

	/*! The run of a serial id: the mission and the helper picked ("" without one, "npc" for a fixed one). */
	struct Run { int32_t mission_id = 0; std::string helper; };
	drogon::Task<Run> RunFor(std::string serialId, std::string userId);
	drogon::Task<void> SetRunHelper(std::string serialId, std::string helper);
}
