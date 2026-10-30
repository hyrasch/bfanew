#pragma once

#include "ServerConfig.hpp"
#include <gimuserver/db/UnitTable.hpp>

/*!
* Cache of the server
*/
class ServerCache final : public trantor::NonCopyable
{
public:
	/*!
	* Setup the server cache.
	* @param[in] serverObj Configuration object of the plugin
	*/
	void Setup(const Json::Value& serverObj);

	/*!
	* Gets the banner configuration (dls).
	* @return DLS string
	*/
	inline const auto& dls() const { return m_dls; }

	/*!
	* Gets the server feature configuration.
	* @return Feature string
	*/
	inline const auto& feature() const { return m_feature; }

	/*!
	* Gets the common portion of the initialize response.
	* @return Initialize respose
	*/
	inline const auto& initializeResp() const { return m_initrsp;  }

	/*!
	* Gets the common portion of the user info response.
	* @return User info response
	*/
	inline const auto& userInfoResp() const { return m_userrsp; }

	/*!
	* Gets the cached slot response.
	* @return ControlCenter response
	*/
	inline const auto& braveSlotsResp() const { return m_controlCenterRsp; }

	/*!
	* Server config.
	* @return Server config
	*/
	inline const auto& serverConfig() const { return m_serverConfig; }

	/*!
	* bfanew: the unit table (deploy/units.json).
	*/
	inline const UnitTable& units() const { return m_units; }

	/*! bfanew: the evolution table (deploy/evos.json). */
	inline const EvoTable& evos() const { return m_evos; }

	/*! The evolution of a unit, nullptr if it has none. */
	inline const EvoDef* evo(int32_t unitId) const
	{
		const auto it = m_evos.find(std::to_string(unitId));
		return it == m_evos.end() ? nullptr : &it->second;
	}

	/*! A unit definition, nullptr if unknown. */
	inline const UnitDef* unit(int32_t unitId) const
	{
		const auto it = m_units.find(std::to_string(unitId));
		return it == m_units.end() ? nullptr : &it->second;
	}

	/*! Total exp needed to be at a level of an exp pattern (0 for level 1 / unknown). */
	inline int64_t expForLevel(int32_t pattern, int32_t lv) const
	{
		for (const auto& e : m_initrsp.exp_pattern)
			if (e.id == pattern && e.lv == lv)
				return e.need_exp;
		return 0;
	}

	/*! The level reached with a total exp in an exp pattern, capped. */
	inline int32_t levelForExp(int32_t pattern, int64_t exp, int32_t maxLv) const
	{
		int32_t best = 1;
		for (const auto& e : m_initrsp.exp_pattern)
			if (e.id == pattern && e.need_exp <= exp && e.lv > best && e.lv <= maxLv)
				best = e.lv;
		return best;
	}

private:
	/*!
	* DLS cached JSON.
	*/
	std::string m_dls;

	/*!
	* bfanew: unit table.
	*/
	UnitTable m_units;

	/*!
	* bfanew: evolution table.
	*/
	EvoTable m_evos;

	/*!
	* Cached data of response.
	*/
	FeatureCheck m_feature;

	/*!
	* Cached common data of the Initialize response
	*/
	InitializeResp m_initrsp;

	/*!
	* Cached slot response
	*/
	SlotGameInfoR m_controlCenterRsp;

	/*!
	* Server configuration.
	*/
	ServerConfig m_serverConfig;

	/*!
	* User info response.
	*/
	UserInfoResp m_userrsp;
};
