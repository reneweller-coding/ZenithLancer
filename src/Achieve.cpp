/**
 * @file Achieve.cpp
 * @brief The player's counters, the achievements and the statistics page.
 * @author Rene Weller
 */
#include "Achieve.h"

#include "Data.h"
#include "Files.h"
#include "Lang.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>

namespace zenith
{

namespace
{

/** @brief A value as the file keeps it: whole numbers without a point, others with three places. */
std::string number( double v )
{
    char buf[64];
    if ( v == std::floor( v ) && std::fabs( v ) < 1e15 )
        snprintf( buf, sizeof( buf ), "%.0f", v );
    else
        snprintf( buf, sizeof( buf ), "%.3f", v );
    return buf;
}

/** @brief A whole number with its thousands marked: "12.345", in English "12,345". */
std::string grouped( double v )
{
    long long   n = std::llround( v );
    std::string s = std::to_string( n < 0 ? -n : n ), out;
    char        mark = english() ? ',' : '.';
    for ( size_t i = 0; i < s.size(); ++i )
    {
        if ( i > 0 && ( s.size() - i ) % 3 == 0 ) out += mark;
        out += s[i];
    }
    return ( n < 0 ? "-" : "" ) + out;
}

/** @brief Whether a name is lower case ASCII, digits and "_" (docs/DATA.md). */
bool goodName( const std::string &s )
{
    if ( s.empty() ) return false;
    for ( char c : s )
        if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_' ) ) return false;
    return true;
}

/** @brief Reads a number; false if the text is not one whole. */
bool toDouble( const std::string &s, double &v )
{
    if ( s.empty() ) return false;
    char *end = nullptr;
    v = strtod( s.c_str(), &end );
    return end != s.c_str() && *end == 0;
}

}   // namespace

void Feats::add( const std::string &key, double v ) { m_counters[key] += v; }

void Feats::max( const std::string &key, double v )
{
    auto it = m_counters.find( key );
    if ( it == m_counters.end() || v > it->second ) m_counters[key] = v;
}

void Feats::min( const std::string &key, double v )
{
    auto it = m_counters.find( key );
    if ( it == m_counters.end() || v < it->second ) m_counters[key] = v;
}

bool Feats::once( const std::string &key, const std::string &what )
{
    if ( !m_seen[key].insert( what ).second ) return false;
    add( key );
    return true;
}

double Feats::get( const std::string &key, double def ) const
{
    auto it = m_counters.find( key );
    return it != m_counters.end() ? it->second : def;
}

bool Feats::has( const std::string &key ) const { return m_counters.count( key ) > 0; }

bool Feats::unlocked( const std::string &name ) const { return m_unlocked.count( name ) > 0; }

void Feats::load( const std::string &path )
{
    // "<counter> <value>", "seen <counter> <thing> <thing> ...", "unlocked <achievement> <date>"
    *this = Feats();
    std::ifstream in( path );
    std::string   line;
    while ( std::getline( in, line ) )
    {
        if ( !line.empty() && line.back() == '\r' ) line.pop_back();
        std::istringstream words( line );
        std::string        key, value;
        if ( !( words >> key >> value ) ) continue;
        if ( key == "seen" )
        {
            std::set<std::string> &s = m_seen[value];
            std::string            what;
            while ( words >> what ) s.insert( what );
        }
        else if ( key == "unlocked" )
        {
            std::string date;
            std::getline( words >> std::ws, date );
            m_unlocked[value] = date;
        }
        else
        {
            double v = 0.0;
            if ( toDouble( value, v ) ) m_counters[key] = v;
        }
    }
}

bool Feats::save( const std::string &path ) const
{
    FILE *f = beginWrite( path );
    if ( !f ) return false;
    for ( const auto &[k, v] : m_counters ) fprintf( f, "%s %s\n", k.c_str(), number( v ).c_str() );
    for ( const auto &[k, s] : m_seen )
    {
        fprintf( f, "seen %s", k.c_str() );
        for ( const std::string &x : s ) fprintf( f, " %s", x.c_str() );
        fprintf( f, "\n" );
    }
    for ( const auto &[n, d] : m_unlocked ) fprintf( f, "unlocked %s %s\n", n.c_str(), d.c_str() );
    return endWrite( f, path );
}

bool AchievementList::load( const std::string &path )
{
    *this = AchievementList();
    std::vector<Block> blocks;
    if ( !readBlocks( path, { "achievement", "stat" }, blocks, m_errors ) ) return false;
    const std::vector<std::string> &keys = counterKeys();
    auto known = [&]( const std::string &k ) { return std::find( keys.begin(), keys.end(), k ) != keys.end(); };
    std::set<std::string> names, stats;
    for ( const Block &b : blocks )
    {
        std::string what = b.m_type + " \"" + b.m_name + "\"";
        auto        error = [&]( const std::string &where, const std::string &msg ) {
            m_errors.push_back( where + ": " + msg );
        };
        if ( !goodName( b.m_name ) )
        {
            error( b.m_head.where(), what + ": a name is lower case ASCII, digits and _" );
            continue;
        }
        if ( b.m_head.m_args.size() > 1 || !b.m_head.m_kv.empty() )
            error( b.m_head.where(), what + ": only the name belongs on its first line" );
        if ( !b.m_prose.empty() ) error( b.m_head.where(), what + ": \">\" lines do not belong here" );
        // A text: one word in quotes ("=" in it would split it into a named value).
        auto text = [&]( const Line &l, std::string &out ) {
            if ( l.m_args.size() != 1 ) error( l.where(), what + ": \"" + l.m_key + "\" takes one text in quotes" );
            out = l.arg( 0 );
        };
        auto require = [&]( const std::string &value, const char *key ) {
            if ( value.empty() ) error( b.m_head.where(), what + " has no " + key );
        };
        // Named values: only "lower=" on a need line.
        for ( const Line &l : b.m_lines )
            for ( const auto &kv : l.m_kv )
                if ( !( l.m_key == "need" && kv.first == "lower" ) )
                    error( l.where(), what + ": unknown value \"" + kv.first + "\" on " + l.m_key );
        if ( b.m_type == "achievement" )
        {
            if ( !names.insert( b.m_name ).second ) error( b.m_head.where(), what + " is defined twice" );
            AchievementDef a;
            a.m_name = b.m_name;
            a.m_where = b.m_head.where();
            bool hasNeed = false;
            for ( const Line &l : b.m_lines )
            {
                const std::string &k = l.m_key;
                if ( k == "title" ) text( l, a.m_title );
                else if ( k == "title_en" ) text( l, a.m_titleEn );
                else if ( k == "desc" ) text( l, a.m_desc );
                else if ( k == "desc_en" ) text( l, a.m_descEn );
                else if ( k == "hidden" ) a.m_hidden = l.argNum( 0, 1 ) != 0;
                else if ( k == "need" )
                {
                    // need <counter> <value> [lower=1]
                    hasNeed = true;
                    a.m_key = l.arg( 0 );
                    a.m_lower = l.integer( "lower", 0 ) != 0;
                    if ( l.m_args.size() != 2 || !toDouble( l.arg( 1 ), a.m_need ) || a.m_need < 0 )
                        error( l.where(), what + ": need takes a counter and a value of 0 or more" );
                    else if ( !known( a.m_key ) )
                        error( l.where(), what + ": unknown counter \"" + a.m_key + "\" (see counterKeys())" );
                }
                else
                    error( l.where(), what + ": unknown line \"" + k + "\"" );
            }
            require( a.m_title, "title" );
            require( a.m_titleEn, "title_en (English)" );
            require( a.m_desc, "desc" );
            require( a.m_descEn, "desc_en (English)" );
            if ( !hasNeed ) error( b.m_head.where(), what + " has no need line" );
            m_achievements.push_back( a );
        }
        else
        {
            if ( !stats.insert( b.m_name ).second ) error( b.m_head.where(), what + " is defined twice" );
            if ( !known( b.m_name ) ) error( b.m_head.where(), what + ": unknown counter (see counterKeys())" );
            StatDef s;
            s.m_key = b.m_name;
            s.m_where = b.m_head.where();
            for ( const Line &l : b.m_lines )
            {
                const std::string &k = l.m_key;
                if ( k == "title" ) text( l, s.m_title );
                else if ( k == "title_en" ) text( l, s.m_titleEn );
                else if ( k == "show" )
                {
                    std::string v = l.arg( 0 );
                    if ( v == "count" ) s.m_show = StatShowE::COUNT;
                    else if ( v == "time" ) s.m_show = StatShowE::TIME;
                    else if ( v == "clock" ) s.m_show = StatShowE::CLOCK;
                    else if ( v == "credits" ) s.m_show = StatShowE::CREDITS;
                    else error( l.where(), what + ": show is count, time, clock or credits, not \"" + v + "\"" );
                }
                else
                    error( l.where(), what + ": unknown line \"" + k + "\"" );
            }
            require( s.m_title, "title" );
            require( s.m_titleEn, "title_en (English)" );
            m_stats.push_back( s );
        }
    }
    if ( m_achievements.empty() ) m_warnings.push_back( path + ": no achievements" );
    return m_errors.empty();
}

void AchievementList::check( const Catalog &cat )
{
    int cubes = 0, plans = 0, secrets = 0;
    for ( const auto &[n, s] : cat.m_story ) cubes += s.m_type == "cube" ? 1 : 0;
    for ( const auto &[n, lv] : cat.m_levels ) secrets += lv.m_isSecret ? 1 : 0;
    // The goods a blueprint unlocks are the hidden ones.
    for ( const auto &[n, g] : cat.m_weapons ) plans += g.m_hidden ? 1 : 0;
    for ( const auto &[n, g] : cat.m_sidekicks ) plans += g.m_hidden ? 1 : 0;
    for ( const auto &[n, g] : cat.m_ships ) plans += g.m_hidden ? 1 : 0;
    for ( const auto &[n, g] : cat.m_generators ) plans += g.m_hidden ? 1 : 0;
    for ( const auto &[n, g] : cat.m_shields ) plans += g.m_hidden ? 1 : 0;
    for ( const auto &[n, g] : cat.m_specials ) plans += g.m_hidden ? 1 : 0;
    int lastDiff = (int)cat.m_difficulties.size() - 1;
    for ( const AchievementDef &a : m_achievements )
    {
        // More than there is can never be reached.
        auto most = [&]( const char *key, int there, const std::string &what ) {
            if ( a.m_key == key && a.m_need > there )
                m_errors.push_back( a.m_where + ": achievement \"" + a.m_name + "\" needs " + key + " " +
                                    number( a.m_need ) + ", but " + what + " " + std::to_string( there ) );
        };
        most( "cubes", cubes, "the log crystals number" );
        most( "plans", plans, "the blueprints number" );
        most( "secrets", secrets, "the secret missions number" );
        most( "campaign_diff", lastDiff, "the last difficulty's index is" );
        most( "max_front", 11, "the front guns' last level is" );
        if ( a.m_key.rfind( "episode", 0 ) == 0 && !cat.m_episodes.count( atoi( a.m_key.c_str() + 7 ) ) )
            m_errors.push_back( a.m_where + ": achievement \"" + a.m_name + "\" watches " + a.m_key +
                                ", the data has no such episode" );
    }
}

const AchievementDef *AchievementList::find( const std::string &name ) const
{
    for ( const AchievementDef &a : m_achievements )
        if ( a.m_name == name ) return &a;
    return nullptr;
}

bool reached( const Feats &f, const AchievementDef &a )
{
    if ( !f.has( a.m_key ) ) return false;
    double v = f.get( a.m_key );
    return a.m_lower ? v <= a.m_need : v >= a.m_need;
}

float progress( const Feats &f, const AchievementDef &a )
{
    if ( f.unlocked( a.m_name ) || reached( f, a ) ) return 1.0f;
    if ( a.m_lower || a.m_need <= 0.0 ) return 0.0f;
    return (float)std::clamp( f.get( a.m_key ) / a.m_need, 0.0, 1.0 );
}

std::vector<const AchievementDef *> newlyUnlocked( Feats &f, const AchievementList &list, const std::string &date )
{
    std::vector<const AchievementDef *> out;
    for ( const AchievementDef &a : list.m_achievements )
        if ( !f.unlocked( a.m_name ) && reached( f, a ) )
        {
            f.m_unlocked[a.m_name] = date;
            out.push_back( &a );
        }
    return out;
}

std::string statText( const Feats &f, const StatDef &s )
{
    double v = f.get( s.m_key );
    char   buf[48];
    switch ( s.m_show )
    {
    case StatShowE::TIME:
    {
        long long minutes = (long long)( v / 60.0 );
        snprintf( buf, sizeof( buf ), "%lld:%02lld", minutes / 60, minutes % 60 );
        return buf;
    }
    case StatShowE::CLOCK:
    {
        if ( !f.has( s.m_key ) ) return "–";
        long long seconds = std::llround( v );
        snprintf( buf, sizeof( buf ), "%lld:%02lld", seconds / 60, seconds % 60 );
        return buf;
    }
    case StatShowE::CREDITS: return grouped( v ) + ( english() ? " Cr" : " Kr" );
    case StatShowE::COUNT: break;
    }
    return grouped( v );
}

std::string today()
{
    char   buf[16];
    time_t now = time( nullptr );
    strftime( buf, sizeof( buf ), "%d.%m.%Y", localtime( &now ) );
    return buf;
}

std::string featsPath() { return userDir() + "feats.txt"; }

std::string achievementsPath( const std::string &dataDir ) { return dataDir + "/lang/achievements.txt"; }

const std::vector<std::string> &counterKeys()
{
    static const std::vector<std::string> keys = {
        "kills",          "bosses",       "boss_nohit",   "bossretries",    "missions",      "nodamage",
        "allkills",       "deaths",       "seconds",      "credits",        "spent",         "max_credits",
        "max_front",      "cubes",        "plans",        "secrets",        "episode1",      "episode2",
        "episode3",       "episode4",     "episode5",     "campaign_diff",  "campaign_coop", "coop_missions",
        "arcade_runs",    "arcade2_runs", "arcade_clean", "bossrush_done",  "bossrush_best", "survival_seconds",
        "survival_daily", "ngplus",       "practice",     "replays_watched", "max_chain",   "grazes",
        "school",         "weekly",       "seen",         "heard",           "gold",        "challenges"
    };
    return keys;
}

}   // namespace zenith
