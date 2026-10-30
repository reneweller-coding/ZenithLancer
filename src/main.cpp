/**
 * @file main.cpp
 * @brief ZENITH LANCER: the command line, the headless tools (data check, autopilot runs), then the game.
 *
 *   ZenithLancer [--mute] [--windowed | --fullscreen] [--size WxH] [--data DIR] [--assets DIR]
 *                [--level NAME [--difficulty N] [--arcade] [--auto]]
 *   ZenithLancer --validate                       every data file, every reference (no window)
 *   ZenithLancer --sim NAME [--difficulty N] [--seed S] [--mortal]   one mission by the autopilot (no window)
 *   ZenithLancer --simall [--difficulty N] [--episode E]            every mission by the autopilot (no window)
 *                (both take --loadout "ship=lancer,front=pulse:5,rear=tailgun:3,left=pip,right=-,gen=dynamo,...")
 *   ZenithLancer --weapons                         every gun's damage and energy a second at levels 1, 6, 11
 *   ZenithLancer --shot FILE --level NAME [--at SECONDS] [--theme T] [--loadout ...]   a picture of a mission
 *   ZenithLancer --gallery FILE                    a picture of every model
 *   ZenithLancer --dumpsfx DIR                     every sound effect as a WAV
 * @author Rene Weller
 */
#include "App.h"
#include "Audio.h"
#include "Data.h"
#include "Sim.h"

#include <raylib.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

using namespace zenith;

namespace
{

std::string dataDir( const std::string &given )
{
    if ( !given.empty() ) return given;
    std::string beside = std::string( GetApplicationDirectory() ) + "data";
    std::error_code e;
    if ( std::filesystem::is_directory( beside, e ) ) return beside;
    return std::string( ZENITH_ROOT ) + "/data";
}

/** @brief The gear an autopilot run flies with: the episode's own, raised to what a player would have by then. */
Loadout simLoadout( const Catalog &cat, const LevelDef &lv )
{
    Loadout o;
    auto    ep = cat.m_episodes.find( lv.m_episode );
    if ( ep != cat.m_episodes.end() ) o = ep->second.m_loadout;
    // A player in the middle of an episode has upgraded: two levels per episode and a bit.
    o.m_frontLv = std::min( 11, o.m_frontLv + 1 + lv.m_episode );
    o.m_rearLv = std::min( 11, o.m_rearLv + lv.m_episode );
    return o;
}

int validate( const std::string &dir )
{
    Catalog cat;
    cat.load( dir );
    for ( const std::string &w : cat.m_warnings ) printf( "warning: %s\n", w.c_str() );
    for ( const std::string &e : cat.m_errors ) printf( "error: %s\n", e.c_str() );
    size_t events = 0;
    for ( const auto &[n, lv] : cat.m_levels ) events += lv.m_events.size();
    printf( "%zu models, %zu enemies, %zu guns, %zu sidekicks, %zu hulls, %zu generators, %zu shields, %zu specials\n",
            cat.m_models.size(), cat.m_enemies.size(), cat.m_weapons.size(), cat.m_sidekicks.size(), cat.m_ships.size(),
            cat.m_generators.size(), cat.m_shields.size(), cat.m_specials.size() );
    printf( "%zu missions (%zu events), %zu episodes, %zu story texts, %zu difficulties, %zu arcade pilots\n",
            cat.m_levels.size(), events, cat.m_episodes.size(), cat.m_story.size(), cat.m_difficulties.size(), cat.m_arcadeShips.size() );
    printf( "%s: %zu errors, %zu warnings\n", cat.m_errors.empty() ? "OK" : "FAILED", cat.m_errors.size(), cat.m_warnings.size() );
    return cat.m_errors.empty() ? 0 : 1;
}

int weapons( const std::string &dir )
{
    Catalog cat;
    cat.load( dir );
    for ( const std::string &e : cat.m_errors ) printf( "error: %s\n", e.c_str() );
    printf( "%-12s %-5s %-4s %7s %6s %6s %6s %7s %7s %7s\n", "gun", "slot", "tier", "price", "dps1", "dps6", "dps11", "nrg/s1", "nrg/s6", "nrg/s11" );
    for ( int tier = 0; tier <= 6; ++tier )
        for ( const auto &[n, w] : cat.m_weapons )
        {
            if ( w.m_tier != tier && !( tier == 6 && ( w.m_tier < 0 || w.m_tier > 5 ) ) ) continue;
            float e[3];
            int   lv[3] = { 1, 6, 11 };
            for ( int i = 0; i < 3; ++i ) e[i] = w.m_energy.at( ( lv[i] - 1 ) / 10.0f ) * w.m_rate.at( ( lv[i] - 1 ) / 10.0f );
            printf( "%-12s %-5s %-4d %7d %6.0f %6.0f %6.0f %7.0f %7.0f %7.0f%s\n", n.c_str(), w.m_slot.c_str(), w.m_tier, w.m_price,
                    weaponDps( w, 1 ), weaponDps( w, 6 ), weaponDps( w, 11 ), e[0], e[1], e[2], w.m_hidden ? "  (plan)" : "" );
        }
    return 0;
}

int sim( const std::string &dir, const std::string &level, int diff, uint32_t seed, bool mortal, int episode,
         const std::string &loadout )
{
    Catalog cat;
    cat.load( dir );
    // Errors are told, but the missions that can be flown are flown.
    for ( const std::string &e : cat.m_errors ) printf( "error: %s\n", e.c_str() );
    int fails = cat.m_errors.empty() ? 0 : 1;
    printf( "%-22s %5s %7s %9s %8s %7s %6s %6s\n", "mission", "won", "time", "kills", "credits", "damage", "deaths", "boss" );
    for ( const auto &[n, lv] : cat.m_levels )
    {
        if ( !level.empty() && n != level ) continue;
        if ( episode > 0 && lv.m_episode != episode ) continue;
        Loadout load = simLoadout( cat, lv );
        parseLoadout( load, loadout );
        SimResult r = simulate( cat, lv, diff, load, seed, !mortal );
        printf( "%-22s %5s %6.0fs %4d/%-4d %8d %7.0f %6d %5.0fs\n", n.c_str(), r.m_won ? "yes" : "NO", r.m_time, r.m_kills,
                r.m_spawned, r.m_credits, r.m_damage, r.m_deaths, r.m_bossTime );
        fflush( stdout );
        if ( !r.m_won ) ++fails;
    }
    return fails == 0 ? 0 : 1;
}

}   // namespace

int main( int argc, char **argv )
{
    Options     o;
    std::string simLevel, dumpDir, loadout;
    bool        doValidate = false, doSimAll = false, mortal = false, doWeapons = false;
    uint32_t    seed = 1;
    int         episode = 0;
    for ( int i = 1; i < argc; ++i )
    {
        std::string a = argv[i];
        auto        next = [&]() { return i + 1 < argc ? std::string( argv[++i] ) : std::string(); };
        if ( a == "--mute" ) o.m_mute = true;
        else if ( a == "--windowed" ) o.m_fullscreen = 0;
        else if ( a == "--fullscreen" ) o.m_fullscreen = 1;
        else if ( a == "--size" ) sscanf( next().c_str(), "%dx%d", &o.m_width, &o.m_height );
        else if ( a == "--data" ) o.m_data = next();
        else if ( a == "--assets" ) o.m_assets = next();
        else if ( a == "--level" ) o.m_level = next();
        else if ( a == "--difficulty" ) o.m_difficulty = atoi( next().c_str() );
        else if ( a == "--arcade" ) o.m_mode = "arcade";
        else if ( a == "--auto" ) o.m_auto = true;
        else if ( a == "--shot" ) o.m_shot = next();
        else if ( a == "--at" ) o.m_at = (float)atof( next().c_str() );
        else if ( a == "--gallery" ) o.m_gallery = next();
        else if ( a == "--validate" ) doValidate = true;
        else if ( a == "--sim" ) simLevel = next();
        else if ( a == "--simall" ) doSimAll = true;
        else if ( a == "--seed" ) seed = (uint32_t)atoi( next().c_str() );
        else if ( a == "--mortal" ) mortal = true;
        else if ( a == "--episode" ) episode = atoi( next().c_str() );
        else if ( a == "--dumpsfx" ) dumpDir = next();
        else if ( a == "--loadout" ) loadout = o.m_loadout = next();
        else if ( a == "--weapons" ) doWeapons = true;
        else if ( a == "--theme" ) o.m_theme = next();
    }
    if ( doWeapons ) return weapons( dataDir( o.m_data ) );
    if ( doValidate ) return validate( dataDir( o.m_data ) );
    if ( !simLevel.empty() || doSimAll ) return sim( dataDir( o.m_data ), simLevel, o.m_difficulty, seed, mortal, episode, loadout );
    if ( !dumpDir.empty() )
    {
        Audio::dump( dumpDir );
        return 0;
    }
    if ( !o.m_shot.empty() || !o.m_gallery.empty() ) o.m_mute = true;
    if ( !o.m_shot.empty() ) o.m_auto = true;
    App app;
    if ( !app.init( o ) ) return 1;
    if ( !o.m_gallery.empty() )
        app.gallery( o.m_gallery );
    else
        app.run();
    app.shutdown();
    return 0;
}
