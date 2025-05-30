//
// Created by Tristan Cormier on 2024-10-21.
//

#include "MapWeather.h"
#include "MiscPackets.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Util.h"
#include "World.h"

/// Create the Weather object
Weather::Weather(uint32 zone, WeatherData const* weatherChances)
    : m_zone(zone), m_weatherChances(weatherChances)
{
    m_timer.SetInterval(sWorld->getIntConfig(CONFIG_INTERVAL_CHANGEWEATHER));
    m_type = WEATHER_TYPE_FINE;
    m_grade = 0;

    LOG_DEBUG("weather", "WORLD: Starting weather system for zone {} (change every {} minutes).", m_zone, (uint32)(m_timer.GetInterval() / (MINUTE * IN_MILLISECONDS)));
}

/// Launch a weather update
bool Weather::Update(uint32 diff)
{
    if (m_timer.GetCurrent() >= 0)
        m_timer.Update(diff);
    else
        m_timer.SetCurrent(0);

    ///- If the timer has passed, ReGenerate the weather
    if (m_timer.Passed())
    {
        m_timer.Reset();
        // update only if Regenerate has changed the weather
        if (ReGenerate())
        {
            ///- Weather will be removed if not updated (no players in zone anymore)
            if (!UpdateWeather())
                return false;
        }
    }

    sScriptMgr->OnWeatherUpdate(this, diff);
    return true;
}

/// Calculate the new weather
bool Weather::ReGenerate()
{
    if (!m_weatherChances)
    {
        m_type = WEATHER_TYPE_FINE;
        m_grade = 0.0f;
        return false;
    }

    /// Weather statistics:
    ///- 30% - no change
    ///- 30% - weather gets better (if not fine) or change weather type
    ///- 30% - weather worsens (if not fine)
    ///- 10% - radical change (if not fine)
    uint32 u = urand(0, 99);

    if (u < 30)
        return false;

    // remember old values
    WEATHER_TYPE old_type = m_type;
    float old_grade = m_grade;

    //78 days between January 1st and March 20nd; 365/4=91 days by season
    // season source http://aa.usno.navy.mil/data/docs/EarthSeasons.html
    uint32 season = ((Acore::Time::GetDayInYear() - 78 + 365) / 91) % 4;

    static char const* seasonName[WEATHER_SEASONS] = { "spring", "summer", "fall", "winter" };
    LOG_DEBUG("weather", "Generating a change in {} weather for zone {}.", seasonName[season], m_zone);

    if ((u < 60) && (m_grade < 0.33333334f))                // Get fair
    {
        m_type = WEATHER_TYPE_FINE;
        m_grade = 0.0f;
    }

    if ((u < 60) && (m_type != WEATHER_TYPE_FINE))          // Get better
    {
        m_grade -= 0.33333334f;
        return true;
    }

    if ((u < 90) && (m_type != WEATHER_TYPE_FINE))          // Get worse
    {
        m_grade += 0.33333334f;
        return true;
    }

    if (m_type != WEATHER_TYPE_FINE)
    {
        /// Radical change:
        ///- if light -> heavy
        ///- if medium -> change weather type
        ///- if heavy -> 50% light, 50% change weather type

        if (m_grade < 0.33333334f)
        {
            m_grade = 0.9999f;                              // go nuts
            return true;
        }
        else
        {
            if (m_grade > 0.6666667f)
            {
                // Severe change, but how severe?
                uint32 rnd = urand(0, 99);
                if (rnd < 50)
                {
                    m_grade -= 0.6666667f;
                    return true;
                }
            }
            m_type = WEATHER_TYPE_FINE;                     // clear up
            m_grade = 0;
        }
    }

    // At this point, only weather that isn't doing anything remains but that have weather data
    uint32 chance1 = m_weatherChances->data[season].rainChance;
    uint32 chance2 = chance1 + m_weatherChances->data[season].snowChance;
    uint32 chance3 = chance2 + m_weatherChances->data[season].stormChance;

    uint32 rnd = urand(1, 100);
    if (rnd <= chance1)
        m_type = WEATHER_TYPE_RAIN;
    else if (rnd <= chance2)
        m_type = WEATHER_TYPE_SNOW;
    else if (rnd <= chance3)
        m_type = WEATHER_TYPE_STORM;
    else
        m_type = WEATHER_TYPE_FINE;

    /// New weather statistics (if not fine):
    ///- 85% light
    ///- 7% medium
    ///- 7% heavy
    /// If fine 100% sun (no fog)

    if (m_type == WEATHER_TYPE_FINE)
    {
        m_grade = 0.0f;
    }
    else if (u < 90)
    {
        m_grade = (float)rand_norm() * 0.3333f;
    }
    else
    {
        // Severe change, but how severe?
        rnd = urand(0, 99);
        if (rnd < 50)
            m_grade = (float)rand_norm() * 0.3333f + 0.3334f;
        else
            m_grade = (float)rand_norm() * 0.3333f + 0.6667f;
    }

    // return true only in case weather changes
    return m_type != old_type || m_grade != old_grade;
}

void Weather::SendWeatherUpdateToPlayer(Player* player)
{
    WorldPackets::Misc::Weather weather(GetWeatherState(), m_grade);
    player->SendDirectMessage(weather.Write());
}

/// Send the new weather to all players in the zone
bool Weather::UpdateWeather()
{
    ///- Send the weather packet to all players in this zone
    if (m_grade >= 1)
        m_grade = 0.9999f;
    else if (m_grade < 0)
        m_grade = 0.0001f;

    WeatherState state = GetWeatherState();

    WorldPackets::Misc::Weather weather(state, m_grade);

    //- Returns false if there were no players found to update
    if (!sWorld->SendZoneMessage(m_zone, weather.Write()))
        return false;

    ///- Log the event
    char const* wthstr;
    switch (state)
    {
        case WEATHER_STATE_FOG:
            wthstr = "fog";
            break;
        case WEATHER_STATE_LIGHT_RAIN:
            wthstr = "light rain";
            break;
        case WEATHER_STATE_MEDIUM_RAIN:
            wthstr = "medium rain";
            break;
        case WEATHER_STATE_HEAVY_RAIN:
            wthstr = "heavy rain";
            break;
        case WEATHER_STATE_LIGHT_SNOW:
            wthstr = "light snow";
            break;
        case WEATHER_STATE_MEDIUM_SNOW:
            wthstr = "medium snow";
            break;
        case WEATHER_STATE_HEAVY_SNOW:
            wthstr = "heavy snow";
            break;
        case WEATHER_STATE_LIGHT_SANDSTORM:
            wthstr = "light sandstorm";
            break;
        case WEATHER_STATE_MEDIUM_SANDSTORM:
            wthstr = "medium sandstorm";
            break;
        case WEATHER_STATE_HEAVY_SANDSTORM:
            wthstr = "heavy sandstorm";
            break;
        case WEATHER_STATE_THUNDERS:
            wthstr = "thunders";
            break;
        case WEATHER_STATE_BLACKRAIN:
            wthstr = "blackrain";
            break;
        case WEATHER_STATE_FINE:
        default:
            wthstr = "fine";
            break;
    }

    LOG_DEBUG("weather", "Change the weather of zone {} to {}.", m_zone, wthstr);
    sScriptMgr->OnWeatherChange(this, state, m_grade);
    return true;
}

/// Set the weather
void Weather::SetWeather(WEATHER_TYPE type, float grade)
{
    if (m_type == type && m_grade == grade)
        return;

    m_type = type;
    m_grade = grade;
    UpdateWeather();
}

/// Get the sound number associated with the current weather
WeatherState Weather::GetWeatherState() const
{
    if (m_grade < 0.27f)
        return WEATHER_STATE_FINE;

    switch (m_type)
    {
        case WEATHER_TYPE_RAIN:
            if (m_grade < 0.40f)
                return WEATHER_STATE_LIGHT_RAIN;
            else if (m_grade < 0.70f)
                return WEATHER_STATE_MEDIUM_RAIN;
            else
                return WEATHER_STATE_HEAVY_RAIN;
        case WEATHER_TYPE_SNOW:
            if (m_grade < 0.40f)
                return WEATHER_STATE_LIGHT_SNOW;
            else if (m_grade < 0.70f)
                return WEATHER_STATE_MEDIUM_SNOW;
            else
                return WEATHER_STATE_HEAVY_SNOW;
        case WEATHER_TYPE_STORM:
            if (m_grade < 0.40f)
                return WEATHER_STATE_LIGHT_SANDSTORM;
            else if (m_grade < 0.70f)
                return WEATHER_STATE_MEDIUM_SANDSTORM;
            else
                return WEATHER_STATE_HEAVY_SANDSTORM;
        case WEATHER_TYPE_BLACKRAIN:
            return WEATHER_STATE_BLACKRAIN;
        case WEATHER_TYPE_THUNDERS:
            return WEATHER_STATE_THUNDERS;
        case WEATHER_TYPE_FINE:
        default:
            return WEATHER_STATE_FINE;
    }
}

/////////////

#include "Log.h"
#include "MiscPackets.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "User.h"
#include <memory>

namespace WeatherMgr
{

    namespace
    {
        typedef std::unordered_map<uint32, std::unique_ptr<Weather>> WeatherMap;
        typedef std::unordered_map<uint32, WeatherData> WeatherZoneMap;

        WeatherMap m_weathers;
        WeatherZoneMap mWeatherZoneMap;

        WeatherData const* GetWeatherData(uint32 zone_id)
        {
            WeatherZoneMap::const_iterator itr = mWeatherZoneMap.find(zone_id);
            return (itr != mWeatherZoneMap.end()) ? &itr->second : nullptr;
        }
    }

    /// Find a Weather object by the given zoneid
    Weather* FindWeather(uint32 id)
    {
        WeatherMap::const_iterator itr = m_weathers.find(id);
        return (itr != m_weathers.end()) ? itr->second.get() : 0;
    }

    /// Remove a Weather object for the given zoneid
    void RemoveWeather(uint32 id)
    {
        // not called at the moment. Kept for completeness
        WeatherMap::iterator itr = m_weathers.find(id);

        if (itr != m_weathers.end())
            m_weathers.erase(itr);
    }

    /// Add a Weather object to the list
    Weather* AddWeather(uint32 zone_id)
    {
        WeatherData const* weatherChances = GetWeatherData(zone_id);

        // zone does not have weather, ignore
        if (!weatherChances)
            return nullptr;

        Weather* w = new Weather(zone_id, weatherChances);
        m_weathers[w->GetZone()].reset(w);
        w->ReGenerate();
        w->UpdateWeather();

        return w;
    }

    void LoadWeatherData()
    {
        uint32 oldMSTime = getMSTime();

        uint32 count = 0;

        QueryResult result = WorldDatabase.Query("SELECT "
                             "zone, spring_rain_chance, spring_snow_chance, spring_storm_chance,"
                             "summer_rain_chance, summer_snow_chance, summer_storm_chance,"
                             "fall_rain_chance, fall_snow_chance, fall_storm_chance,"
                             "winter_rain_chance, winter_snow_chance, winter_storm_chance,"
                             "ScriptName FROM game_weather");

        if (!result)
        {
            LOG_WARN("server.loading", ">> Loaded 0 weather definitions. DB table `game_weather` is empty.");
            LOG_INFO("server.loading", " ");
            return;
        }

        do
        {
            Field* fields = result->Fetch();

            uint32 zone_id = fields[0].Get<uint32>();

            WeatherData& wzc = mWeatherZoneMap[zone_id];

            for (uint8 season = 0; season < WEATHER_SEASONS; ++season)
            {
                wzc.data[season].rainChance  = fields[season * (MAX_WEATHER_TYPE - 1) + 1].Get<uint8>();
                wzc.data[season].snowChance  = fields[season * (MAX_WEATHER_TYPE - 1) + 2].Get<uint8>();
                wzc.data[season].stormChance = fields[season * (MAX_WEATHER_TYPE - 1) + 3].Get<uint8>();

                if (wzc.data[season].rainChance > 100)
                {
                    wzc.data[season].rainChance = 25;
                    LOG_ERROR("sql.sql", "Weather for zone {} season {} has wrong rain chance > 100%", zone_id, season);
                }

                if (wzc.data[season].snowChance > 100)
                {
                    wzc.data[season].snowChance = 25;
                    LOG_ERROR("sql.sql", "Weather for zone {} season {} has wrong snow chance > 100%", zone_id, season);
                }

                if (wzc.data[season].stormChance > 100)
                {
                    wzc.data[season].stormChance = 25;
                    LOG_ERROR("sql.sql", "Weather for zone {} season {} has wrong storm chance > 100%", zone_id, season);
                }
            }

            wzc.ScriptId = sObjectMgr->GetScriptId(fields[13].Get<std::string>());

            ++count;
        } while (result->NextRow());

        LOG_INFO("server.loading", ">> Loaded {} Weather Definitions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
        LOG_INFO("server.loading", " ");
    }

    void SendFineWeatherUpdateToPlayer(Player* player)
    {
        WorldPackets::Misc::Weather weather(WEATHER_STATE_FINE);
        player->SendDirectMessage(weather.Write());
    }

    void Update(uint32 diff)
    {
        ///- Send an update signal to Weather objects
        WeatherMap::iterator itr, next;
        for (itr = m_weathers.begin(); itr != m_weathers.end(); itr = next)
        {
            next = itr;
            ++next;

            ///- and remove Weather objects for zones with no player
            // As interval > WorldTick
            if (!itr->second->Update(diff))
                m_weathers.erase(itr);
        }
    }

} // namespace
