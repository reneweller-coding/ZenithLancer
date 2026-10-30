/**
 * @file Achieve.h
 * @brief Achievements and the statistics page. The player's counters across all games (Feats: kills, bosses, time
 *        flown, the best Boss Rush ...) live in feats.txt in the player's folder as plain "key value" lines. The
 *        achievements and the statistics shown are read from data/lang/achievements.txt (the Catalog skips lang/,
 *        so its blocks do not disturb --validate); an achievement watches one counter and is reached when the
 *        counter gets to its value. The game only feeds the counters (counterKeys() says when); newlyUnlocked()
 *        tells what they now reach.
 * @author Rene Weller
 */
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

namespace zenith
{

struct Catalog;

/** @brief The player's counters across all games and the achievements reached. */
struct Feats
{
    std::map<std::string, double>                m_counters;   ///< counter -> its value
    std::map<std::string, std::set<std::string>> m_seen;       ///< counter -> the things once() counted for it
    std::map<std::string, std::string>           m_unlocked;   ///< achievement -> the date it was reached

    /**
     * @brief Counts up.
     * @param[in] key  the counter
     * @param[in] v    by how much
     */
    void add( const std::string &key, double v = 1.0 );

    /**
     * @brief Keeps the highest value (the longest survival, the highest difficulty finished).
     * @param[in] key  the counter
     * @param[in] v    the value; the counter takes it if it is higher or the counter was never set
     */
    void max( const std::string &key, double v );

    /**
     * @brief Keeps the lowest value (a best time).
     * @param[in] key  the counter
     * @param[in] v    the value; the counter takes it if it is lower or the counter was never set
     */
    void min( const std::string &key, double v );

    /**
     * @brief Counts a thing once ever: a log crystal, a blueprint or a secret mission found again counts no more.
     * @param[in] key   the counter
     * @param[in] what  the thing's name
     * @return whether it was new (and counted)
     */
    bool once( const std::string &key, const std::string &what );

    /**
     * @brief A counter's value.
     * @param[in] key  the counter
     * @param[in] def  when it was never set
     * @return its value
     */
    double get( const std::string &key, double def = 0.0 ) const;

    /**
     * @brief Whether a counter was ever set (a best time of 0 and none at all differ).
     * @param[in] key  the counter
     * @return true if it was
     */
    bool has( const std::string &key ) const;

    /**
     * @brief Whether an achievement is reached.
     * @param[in] name  the achievement
     * @return true if it is
     */
    bool unlocked( const std::string &name ) const;

    /**
     * @brief Reads them (what is missing starts from nothing).
     * @param[in] path  the file
     */
    void load( const std::string &path );

    /**
     * @brief Writes them whole or not at all.
     * @param[in] path  the file
     * @return whether all of it was written
     */
    bool save( const std::string &path ) const;
};

/** @brief How a statistic is shown. */
enum class StatShowE : int
{
    COUNT,    ///< a whole number ("12.345", in English "12,345")
    TIME,     ///< seconds as hours and minutes ("12:05")
    CLOCK,    ///< seconds as minutes and seconds ("7:42", a dash when never set): best times
    CREDITS   ///< a whole number with its currency ("12.345 Kr", "12,345 Cr")
};

/** @brief An achievement. */
struct AchievementDef
{
    std::string m_name;
    std::string m_title;
    std::string m_titleEn;          ///< in English
    std::string m_desc;
    std::string m_descEn;           ///< in English
    std::string m_key;              ///< the counter it watches
    double      m_need = 1.0;       ///< the value it waits for
    bool        m_lower = false;    ///< reached at m_need or below (a best time), not at m_need or above
    bool        m_hidden = false;   ///< its title and text stay secret until it is reached
    std::string m_where;            ///< file:line
};

/** @brief A line of the statistics page. */
struct StatDef
{
    std::string m_key;                         ///< the counter shown
    std::string m_title;
    std::string m_titleEn;                     ///< in English
    StatShowE   m_show = StatShowE::COUNT;
    std::string m_where;                       ///< file:line
};

/** @brief The achievements and the statistics page, as data/lang/achievements.txt describes them. */
struct AchievementList
{
    std::vector<AchievementDef> m_achievements;   ///< in the file's order
    std::vector<StatDef>        m_stats;          ///< in the file's order
    std::vector<std::string>    m_errors;
    std::vector<std::string>    m_warnings;

    /**
     * @brief Reads the file: unknown lines and values, missing texts (German or English), unknown counters and
     *        names given twice are errors.
     * @param[in] path  the file (achievementsPath())
     * @return false if there were errors (m_errors)
     */
    bool load( const std::string &path );

    /**
     * @brief Checks the values against the game's data: no more log crystals, blueprints or secret missions asked
     *        for than there are, no difficulty past the last; adds to m_errors.
     * @param[in] cat  the game's data
     */
    void check( const Catalog &cat );

    /**
     * @brief An achievement by its name.
     * @param[in] name  the name
     * @return it (nullptr if unknown)
     */
    const AchievementDef *find( const std::string &name ) const;
};

/**
 * @brief Whether the counters reach an achievement (a counter never set reaches nothing).
 * @param[in] f  the counters
 * @param[in] a  the achievement
 * @return true if they do
 */
bool reached( const Feats &f, const AchievementDef &a );

/**
 * @brief How far the counters are on the way to an achievement (for a bar on the achievements page).
 * @param[in] f  the counters
 * @param[in] a  the achievement
 * @return 0..1 (best times: 0 or 1)
 */
float progress( const Feats &f, const AchievementDef &a );

/**
 * @brief Marks the achievements the counters now reach and were not reached before.
 * @param[in,out] f     the counters (m_unlocked gains them)
 * @param[in]     list  the achievements
 * @param[in]     date  the date written beside each (today())
 * @return the ones reached just now, in the file's order
 */
std::vector<const AchievementDef *> newlyUnlocked( Feats &f, const AchievementList &list, const std::string &date );

/**
 * @brief A statistic's value as the page shows it, in the chosen language.
 * @param[in] f  the counters
 * @param[in] s  the statistic
 * @return the text
 */
std::string statText( const Feats &f, const StatDef &s );

/**
 * @brief Today as an achievement's date.
 * @return "30.10.2026"
 */
std::string today();

/**
 * @brief The counters' file in the player's folder.
 * @return its path (feats.txt)
 */
std::string featsPath();

/**
 * @brief The achievements' file in a data folder.
 * @param[in] dataDir  the data folder
 * @return its path (lang/achievements.txt in it)
 */
std::string achievementsPath( const std::string &dataDir );

/**
 * @brief The counters the game feeds; achievements and statistics may only watch these. When each is counted
 *        (every mode unless named; "Stats" is the mission's World::m_stats at its end, won or lost):
 *        - kills: add Stats::m_kills
 *        - bosses: add Stats::m_bossKills
 *        - boss_nohit: add 1 at a BOSSDIE event when no PLAYERHIT came since the boss's BOSSSTART
 *        - bossretries: add 1 at a BOSSDIE event while m_bossTries > 0 (won after a retry from the checkpoint)
 *        - missions: add 1 for every mission won (campaign, co-op campaign, arcade)
 *        - nodamage: add 1 for every mission won with Stats::m_damage == 0
 *        - allkills: add 1 for every mission won with Stats::m_kills >= Stats::m_spawned > 0
 *        - deaths: add Stats::m_deaths
 *        - seconds: add Stats::m_time (the seconds flown)
 *        - credits: add the credits a campaign mission earns, when they are banked
 *        - spent: add the credits paid in the workshop (purchases and upgrades)
 *        - max_credits: max with the credits a campaign holds, whenever they change
 *        - max_front: max with the front gun's level, whenever it is bought or upgraded
 *        - cubes: once( "cubes", name ) for every log crystal in Stats::m_cubes (any campaign, so all of them count
 *          across the branches)
 *        - plans: once( "plans", item ) for every blueprint in Stats::m_plans
 *        - secrets: once( "secrets", mission ) for every secret mission (LevelDef::m_isSecret) won
 *        - episode1 .. episode6: max with 1 when that campaign episode is finished (its ending shown)
 *        - campaign_diff: max with the difficulty index (0 Cadet .. 6 Zenith) when the campaign's last episode is
 *          finished
 *        - campaign_coop: max with 1 when the campaign's last episode is finished in co-op
 *        - coop_missions: add 1 for every mission won in the co-op campaign
 *        - arcade_runs / arcade2_runs: add 1 for every arcade run won to its end, alone / with two players
 *        - arcade_clean: add 1 for every arcade run won to its end without a continue
 *        - bossrush_done: add 1 for every Boss Rush finished
 *        - bossrush_best: min with the seconds of a Boss Rush finished on Normal (index 2) or harder
 *        - survival_seconds: max with the seconds a survival run lasted
 *        - survival_daily: add 1 for every daily survival run played to its end
 *        - ngplus: max with the New Game+ level of a campaign when it is started
 *        - practice: add 1 for every practice run started
 *        - replays_watched: add 1 for every replay watched (started)
 *        - max_chain: max with Stats::m_bestChain (the longest chain of kills)
 *        - grazes: add Stats::m_grazes
 *        - school: max with 1 when the flight school is finished
 *        - weekly: add 1 for every weekly flight played to its end
 *        - seen: max with the enemies the archive knows (Profile::m_seen)
 *        - heard: max with the music pieces heard (Profile::m_heard)
 *        - gold: max with the missions that have a gold medal (Profile::m_medals)
 *        - challenges: add 1 for every challenge (a code's flight) played to its end
 * @return the counters' names
 */
const std::vector<std::string> &counterKeys();

}   // namespace zenith
