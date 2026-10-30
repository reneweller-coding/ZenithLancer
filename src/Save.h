/**
 * @file Save.h
 * @brief What is kept between runs: the settings, the profile (best scores, what is unlocked, the log crystals
 *        ever read) and the campaign's save slots. Plain "key value" text files in the player's folder.
 * @author Rene Weller
 */
#pragma once

#include "Data.h"

#include <set>
#include <string>
#include <vector>

namespace zenith
{

/** @brief The options. */
struct Settings
{
    float       m_music = 0.7f;
    float       m_effects = 0.8f;
    bool        m_fullscreen = false;
    int         m_width = 0, m_height = 0;   ///< the window (0: by the screen)
    int         m_fps = 60;                  ///< 0: no limit
    bool        m_vsync = true;
    float       m_shake = 1.0f;
    bool        m_autofire = false;
    bool        m_mouse = false;             ///< the mouse steers
    bool        m_hitbox = true;             ///< the pilot's hit circle shown
    bool        m_rumble = true;             ///< the pad's motors
    std::string m_language;                  ///< de, en ("" the system's)
    float       m_textSize = 1.0f;
    std::vector<std::string> m_binds;        ///< Input's lines
    float       m_voices = 0.9f;             ///< the speakers' volume (0: silent, the texts stay)
    int         m_bullets = 0;               ///< enemy bullets: 0 as made, 1 strong (outlined), 2 red-green, 3 blue-yellow
    float       m_speed = 1.0f;              ///< the game's pace (an aid: 0.7 .. 1)
    bool        m_autoBomb = false;          ///< the special fires by itself when the armour is nearly gone
    bool        m_softFlash = false;         ///< flashes and white-outs dimmed
    int         m_quality = -1;              ///< graphics: 0 performance, 1 balanced, 2 quality (-1: by the machine)
    bool        m_battery = false;           ///< saving the battery: 30 fps, balanced graphics at most
    std::vector<std::string> m_mods;         ///< the mods switched on (folders in <user>/mods)
    std::string m_paint[2];                  ///< each pilot's paint (data/lang/paints.txt; "" the hull's own)

    /** @brief Reads them (missing values stay at their defaults). */
    void load( const std::string &path );
    /** @brief Writes them. */
    bool save( const std::string &path ) const;
};

/** @brief A best score. */
struct HighScore
{
    std::string m_name;
    long long   m_score = 0;
    int         m_episode = 1;
    std::string m_pilot;
};

/** @brief What the player has reached across all games. */
struct Profile
{
    int                                          m_episodes = 1;       ///< episodes open in the campaign
    int                                          m_arcadeEpisodes = 1; ///< ... and in the arcade
    int                                          m_maxDiff = 5;        ///< the highest difficulty open (index)
    bool                                         m_bossRush = false;   ///< open after the campaign's end
    std::set<std::string>                        m_cubes;              ///< log crystals ever found
    std::map<std::string, std::vector<HighScore>> m_scores;            ///< "<mode>_<difficulty>" -> the top ten
    long long                                    m_kills = 0;
    double                                       m_seconds = 0;
    std::set<std::string>                        m_flown;              ///< missions ever flown (the practice lists them)
    std::set<std::string>                        m_bosses;             ///< missions whose boss was met (practice: the boss alone)
    std::map<std::string, long long>             m_daily;              ///< survival: the best score of a day (YYYYMMDD)
    std::map<std::string, long long>             m_weekly;             ///< the weekly flight: the best score of a week
    std::set<std::string>                        m_seen;               ///< enemies met (the archive shows them)
    std::set<std::string>                        m_heard;              ///< music pieces heard (the archive plays them)
    bool                                         m_school = false;     ///< the flight school finished
    int                                          m_scoring = 0;        ///< the scoring the lists were made under (2: chains)
    std::map<std::string, long long>             m_challenges;         ///< each challenge code's best score
    std::map<std::string, int>                   m_medals;             ///< each mission's best medal (1 bronze, 2 silver, 3 gold)

    void load( const std::string &path );
    bool save( const std::string &path ) const;

    /**
     * @brief Whether a score makes the list.
     * @param[in] key    the list
     * @param[in] score  the score
     * @return its place (0 based), -1 if not
     */
    int place( const std::string &key, long long score ) const;

    /** @brief Puts a score in its list (top ten kept). */
    void add( const std::string &key, const HighScore &h );
};

/** @brief A campaign in progress. */
struct Campaign
{
    int                      m_difficulty = 2;
    int                      m_episode = 1;
    std::vector<std::string> m_choices;         ///< the missions open now
    std::string              m_last;            ///< the mission flown last
    long long                m_credits = 0;
    long long                m_score = 0;
    Loadout                  m_load;
    std::set<std::string>    m_done;            ///< missions flown
    std::set<std::string>    m_cubes;
    std::set<std::string>    m_plans;           ///< goods unlocked by plans
    std::set<std::string>    m_keys;
    std::set<std::string>    m_owned;           ///< goods bought once (the workshop lists them)
    int                      m_missions = 0;
    long long                m_kills = 0;
    double                   m_seconds = 0;
    std::string              m_stamp;           ///< when it was saved
    bool                     m_coop = false;    ///< two pilots fly
    bool                     m_has2 = false;    ///< the second pilot has flown here (has gear and money)
    Loadout                  m_load2;           ///< the second pilot's gear
    long long                m_credits2 = 0;    ///< ... money
    long long                m_score2 = 0;      ///< ... score
    int                      m_plus = 0;        ///< New Game+: the rounds finished before this one

    void load( const std::string &path );
    bool save( const std::string &path ) const;
};

/** @return a slot's file */
std::string slotPath( int slot );

}   // namespace zenith
