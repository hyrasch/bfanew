#pragma once

/*!
* Server configuration.
*/
struct ServerConfig
{
	/*!
	* Initial level.
	*/
	uint32_t initialLevel;

	/*!
	* Initial ZEL.
	*/
	uint32_t initialZel;

	/*!
	* Initial Karma.
	*/
	uint32_t initialKarma;

	/*!
	* Initial brave coins.
	*/
	uint32_t initialBraveCoins;

	/*!
	* Initial free gems.
	*/
	uint32_t initialFreeGems;

	/*!
	* Initial honor (friend) points.
	*/
	uint32_t initialFriendPoints;

	/*!
	* bfanew: seconds per energy point regenerated (0 = no regeneration).
	*/
	uint32_t energyRegenSeconds;
};
