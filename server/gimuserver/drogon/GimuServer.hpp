#pragma once

#include <drogon/plugins/Plugin.h>

#include <gimuserver/drogon/ServerCache.hpp>
#include <gimuserver/utils/DumpLog.hpp>

#include <mutex>
#include <unordered_map>

/*!
* Implements the plugin of the system server. This is used to contain and caches everything.
*/
class GimuServer final : public drogon::Plugin<GimuServer>
{
public:
    /*!
    * Constructs a GimuServer object.
    */
    GimuServer();

    /*!
    * This method is called by drogon to initialize and start the plugin.
    * @param[in] config Plugin configuration
    */
    void initAndStart(const Json::Value& config) override;

    /*!
    * This method is called by drogon to shutdown the plugin.
    */
    void shutdown() override;

    /*!
    * Gets the server cache.
    * @return Server cache
    */
    inline auto& cache() { return m_cache; }

    /*!
    * bfanew dev helper: re-reads deploy/system/*.json into the cache without a restart.
    * Not thread-safe against in-flight requests; meant for local development only.
    */
    inline void reloadCache() { m_cache.Setup(m_serverConfigJson); }

    /*!
    * bfanew: remembers which mission a MissionStart serial id belongs to, because MissionEnd only
    * sends the serial id. In-memory only (no persistence yet).
    */
    inline void rememberRun(const std::string& serialId, int32_t missionId)
    {
        std::lock_guard<std::mutex> lock(m_runsMutex);
        m_runs[serialId] = missionId;
    }

    /*! Mission id of a run, 0 if unknown. */
    inline int32_t missionForRun(const std::string& serialId)
    {
        std::lock_guard<std::mutex> lock(m_runsMutex);
        const auto it = m_runs.find(serialId);
        return it == m_runs.end() ? 0 : it->second;
    }

    /*!
    * Gets the DLC 404 logger.
    * @return DLC logger
    */
    inline auto& dlcLog() { return m_dlc_error_log; }

    /*!
    * Tries to open an HTTP Dump log.
    * @param reqId ID of the request
    * @param log Dump log to open
    * @return The opened dump log
    */
    inline DumpLog& tryOpenHttpDumpLog(std::string_view reqId, DumpLog& log) const
    {
        if (m_have_log)
        {
            auto t = std::time(nullptr);
            auto tm = *std::localtime(&t);

            std::ostringstream fileName;
            fileName << m_http_log_prefix;
            fileName << "_";
            fileName << reqId;
            fileName << "_";
            fileName << std::put_time(&tm, "%Y%m%d_%H%M%S");
            fileName << m_http_log_suffix;
            log.open(fileName.str());
        }

        return log;
    }

private:
    /*!
    * DLC error file.
    */
    DumpLog m_dlc_error_log;

    /*!
    * Http log prefix.
    */
    std::string m_http_log_prefix;

    /*!
    * Http log suffix.
    */
    std::string m_http_log_suffix;

    /*!
    * Enable debug logging.
    */
    bool m_have_log;

    /*!
    * Server cache.
    */
    ServerCache m_cache;

    /*!
    * Copy of the plugin's "server" config object, kept for reloadCache().
    */
    Json::Value m_serverConfigJson;

    /*! serial id -> mission id of the runs started since the server came up (bfanew). */
    std::unordered_map<std::string, int32_t> m_runs;
    std::mutex m_runsMutex;
};
