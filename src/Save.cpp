/**
 * @file Save.cpp
 * @brief Settings, profile and save slots.
 * @author Rene Weller
 */
#include "Save.h"

#include "Files.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

namespace zenith
{

namespace
{

/** @brief Reads "key rest-of-line" pairs. */
std::vector<std::pair<std::string, std::string>> pairs( const std::string &path )
{
    std::vector<std::pair<std::string, std::string>> out;
    std::ifstream                                    in( path );
    std::string                                      line;
    while ( std::getline( in, line ) )
    {
        if ( !line.empty() && line.back() == '\r' ) line.pop_back();
        size_t sp = line.find( ' ' );
        if ( sp == std::string::npos ) continue;
        out.push_back( { line.substr( 0, sp ), line.substr( sp + 1 ) } );
    }
    return out;
}

std::vector<std::string> split( const std::string &s )
{
    std::vector<std::string> out;
    std::istringstream       in( s );
    std::string              w;
    while ( in >> w ) out.push_back( w );
    return out;
}

std::string join( const std::set<std::string> &s )
{
    std::string out;
    for ( const std::string &x : s ) out += ( out.empty() ? "" : " " ) + x;
    return out;
}

void writeLoadout( FILE *f, const Loadout &o )
{
    fprintf( f, "ship %s\nfront %s %d\nrear %s %d\nside %s %s\ngen %s\nshield %s\nspecial %s\n", o.m_ship.c_str(),
             o.m_front.c_str(), o.m_frontLv, o.m_rear.empty() ? "-" : o.m_rear.c_str(), o.m_rearLv,
             o.m_sideL.empty() ? "-" : o.m_sideL.c_str(), o.m_sideR.empty() ? "-" : o.m_sideR.c_str(), o.m_gen.c_str(),
             o.m_shield.c_str(), o.m_special.empty() ? "-" : o.m_special.c_str() );
}

bool readLoadout( Loadout &o, const std::string &k, const std::string &v )
{
    std::vector<std::string> w = split( v );
    auto                     at = [&]( size_t i ) { return i < w.size() && w[i] != "-" ? w[i] : std::string(); };
    if ( k == "ship" ) o.m_ship = at( 0 );
    else if ( k == "front" )
    {
        o.m_front = at( 0 );
        o.m_frontLv = w.size() > 1 ? atoi( w[1].c_str() ) : 1;
    }
    else if ( k == "rear" )
    {
        o.m_rear = at( 0 );
        o.m_rearLv = w.size() > 1 ? atoi( w[1].c_str() ) : 1;
    }
    else if ( k == "side" )
    {
        o.m_sideL = at( 0 );
        o.m_sideR = at( 1 );
    }
    else if ( k == "gen" ) o.m_gen = at( 0 );
    else if ( k == "shield" ) o.m_shield = at( 0 );
    else if ( k == "special" ) o.m_special = at( 0 );
    else return false;
    return true;
}

}   // namespace

void Settings::load( const std::string &path )
{
    m_binds.clear();
    for ( const auto &[k, v] : pairs( path ) )
    {
        float f = (float)atof( v.c_str() );
        if ( k == "music" ) m_music = f;
        else if ( k == "effects" ) m_effects = f;
        else if ( k == "fullscreen" ) m_fullscreen = f != 0;
        else if ( k == "width" ) m_width = (int)f;
        else if ( k == "height" ) m_height = (int)f;
        else if ( k == "fps" ) m_fps = (int)f;
        else if ( k == "vsync" ) m_vsync = f != 0;
        else if ( k == "shake" ) m_shake = f;
        else if ( k == "autofire" ) m_autofire = f != 0;
        else if ( k == "mouse" ) m_mouse = f != 0;
        else if ( k == "hitbox" ) m_hitbox = f != 0;
        else if ( k == "rumble" ) m_rumble = f != 0;
        else if ( k == "textsize" ) m_textSize = f;
        else if ( k == "bind" ) m_binds.push_back( "bind " + v );
    }
}

bool Settings::save( const std::string &path ) const
{
    FILE *f = beginWrite( path );
    if ( !f ) return false;
    fprintf( f, "music %.2f\neffects %.2f\nfullscreen %d\nwidth %d\nheight %d\nfps %d\nvsync %d\nshake %.2f\n", m_music,
             m_effects, m_fullscreen ? 1 : 0, m_width, m_height, m_fps, m_vsync ? 1 : 0, m_shake );
    fprintf( f, "autofire %d\nmouse %d\nhitbox %d\ntextsize %.2f\nrumble %d\n", m_autofire ? 1 : 0, m_mouse ? 1 : 0,
             m_hitbox ? 1 : 0, m_textSize, m_rumble ? 1 : 0 );
    for ( const std::string &b : m_binds ) fprintf( f, "%s\n", b.c_str() );
    return endWrite( f, path );
}

void Profile::load( const std::string &path )
{
    for ( const auto &[k, v] : pairs( path ) )
    {
        if ( k == "episodes" ) m_episodes = std::max( 1, atoi( v.c_str() ) );
        else if ( k == "arcade" ) m_arcadeEpisodes = std::max( 1, atoi( v.c_str() ) );
        else if ( k == "maxdiff" ) m_maxDiff = atoi( v.c_str() );
        else if ( k == "bossrush" ) m_bossRush = atoi( v.c_str() ) != 0;
        else if ( k == "kills" ) m_kills = atoll( v.c_str() );
        else if ( k == "seconds" ) m_seconds = atof( v.c_str() );
        else if ( k == "cubes" )
            for ( const std::string &c : split( v ) ) m_cubes.insert( c );
        else if ( k == "score" )
        {
            // score <list> <score> <episode> <pilot> <name with spaces>
            std::vector<std::string> w = split( v );
            if ( w.size() < 5 ) continue;
            HighScore h;
            h.m_score = atoll( w[1].c_str() );
            h.m_episode = atoi( w[2].c_str() );
            h.m_pilot = w[3];
            for ( size_t i = 4; i < w.size(); ++i ) h.m_name += ( i > 4 ? " " : "" ) + w[i];
            m_scores[w[0]].push_back( h );
        }
    }
}

bool Profile::save( const std::string &path ) const
{
    FILE *f = beginWrite( path );
    if ( !f ) return false;
    fprintf( f, "episodes %d\narcade %d\nmaxdiff %d\nbossrush %d\nkills %lld\nseconds %.0f\ncubes %s\n", m_episodes,
             m_arcadeEpisodes, m_maxDiff, m_bossRush ? 1 : 0, m_kills, m_seconds, join( m_cubes ).c_str() );
    for ( const auto &[k, list] : m_scores )
        for ( const HighScore &h : list )
            fprintf( f, "score %s %lld %d %s %s\n", k.c_str(), h.m_score, h.m_episode, h.m_pilot.empty() ? "-" : h.m_pilot.c_str(),
                     h.m_name.c_str() );
    return endWrite( f, path );
}

int Profile::place( const std::string &key, long long score ) const
{
    if ( score <= 0 ) return -1;
    auto it = m_scores.find( key );
    if ( it == m_scores.end() ) return 0;
    int i = 0;
    for ( const HighScore &h : it->second )
    {
        if ( score > h.m_score ) return i;
        ++i;
    }
    return i < 10 ? i : -1;
}

void Profile::add( const std::string &key, const HighScore &h )
{
    std::vector<HighScore> &l = m_scores[key];
    l.push_back( h );
    std::stable_sort( l.begin(), l.end(), []( const HighScore &a, const HighScore &b ) { return a.m_score > b.m_score; } );
    if ( l.size() > 10 ) l.resize( 10 );
}

void Campaign::load( const std::string &path )
{
    *this = Campaign();
    for ( const auto &[k, v] : pairs( path ) )
    {
        if ( readLoadout( m_load, k, v ) ) continue;
        if ( k == "difficulty" ) m_difficulty = atoi( v.c_str() );
        else if ( k == "episode" ) m_episode = atoi( v.c_str() );
        else if ( k == "choices" ) m_choices = split( v );
        else if ( k == "last" ) m_last = v;
        else if ( k == "credits" ) m_credits = atoll( v.c_str() );
        else if ( k == "score" ) m_score = atoll( v.c_str() );
        else if ( k == "missions" ) m_missions = atoi( v.c_str() );
        else if ( k == "kills" ) m_kills = atoll( v.c_str() );
        else if ( k == "seconds" ) m_seconds = atof( v.c_str() );
        else if ( k == "stamp" ) m_stamp = v;
        else
        {
            std::set<std::string> *s = k == "done" ? &m_done : k == "cubes" ? &m_cubes : k == "plans" ? &m_plans
                                     : k == "keys" ? &m_keys : k == "owned" ? &m_owned : nullptr;
            if ( s )
                for ( const std::string &x : split( v ) ) s->insert( x );
        }
    }
}

bool Campaign::save( const std::string &path ) const
{
    FILE *f = beginWrite( path );
    if ( !f ) return false;
    char   stamp[32];
    time_t now = time( nullptr );
    strftime( stamp, sizeof( stamp ), "%d.%m.%Y %H:%M", localtime( &now ) );
    std::string choices;
    for ( const std::string &c : m_choices ) choices += ( choices.empty() ? "" : " " ) + c;
    fprintf( f, "difficulty %d\nepisode %d\nchoices %s\nlast %s\ncredits %lld\nscore %lld\nmissions %d\nkills %lld\nseconds %.0f\nstamp %s\n",
             m_difficulty, m_episode, choices.c_str(), m_last.c_str(), m_credits, m_score, m_missions, m_kills, m_seconds, stamp );
    writeLoadout( f, m_load );
    fprintf( f, "done %s\ncubes %s\nplans %s\nkeys %s\nowned %s\n", join( m_done ).c_str(), join( m_cubes ).c_str(),
             join( m_plans ).c_str(), join( m_keys ).c_str(), join( m_owned ).c_str() );
    return endWrite( f, path );
}

std::string slotPath( int slot ) { return userDir() + "slot" + std::to_string( slot + 1 ) + ".txt"; }

}   // namespace zenith
