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
 *   ZenithLancer --langcheck PREFIX|all            the texts that lack their English (stories, titles, descriptions)
 *   ZenithLancer --replaytest [MISSION]            each mission of episode 1 and the boss rush recorded and played
 *                back from the file; the ends must be the same to the bit (no window; --difficulty, --seed, --mortal)
 *   ZenithLancer --achievements                    checks data/lang/achievements.txt and lists what it holds
 *   ZenithLancer --shot FILE --level NAME [--at SECONDS] [--theme T] [--loadout ...]   a picture of a mission
 *   ZenithLancer --screen NAME --shot FILE         a picture of a screen (title main options controls scores credits
 *                logbook slots difficulty episode story plan shop pilots results failed continue name pause)
 *   ZenithLancer --apptest SCRIPT                  plays a script through the screens (tools/apptest_*.txt; set
 *                ZENITH_HOME to a test folder so the real saves stay untouched)
 *   ZenithLancer --gallery FILE                    a picture of every model
 *   ZenithLancer --dumpsfx DIR                     every sound effect as a WAV
 * @author Rene Weller
 */
#include "Achieve.h"
#include "App.h"
#include "Audio.h"
#include "Data.h"
#include "Lang.h"
#include "Replay.h"
#include "Sim.h"
#include "Survival.h"

#include <raylib.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

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

/**
 * @brief The gear an autopilot run flies with: what a player has bought by the middle of the mission's episode
 *        (docs/DESIGN.md's prices against the credits an episode earns). --loadout changes it.
 */
Loadout simLoadout( const Catalog &cat, const LevelDef &lv ) { return midLoadout( cat, lv.m_episode ); }

int validate( const std::string &dir, const std::vector<std::string> &mods )
{
    Catalog cat;
    cat.load( dir, mods );
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

int langCheck( const std::string &dir, const std::string &only )
{
    // What lacks its English: stories, titles and descriptions. "only" limits it to names with that prefix.
    Catalog cat;
    cat.load( dir );
    int  missing = 0;
    auto want = [&]( const std::string &name ) { return only.empty() || name.rfind( only, 0 ) == 0; };
    auto miss = [&]( const std::string &what ) {
        printf( "missing: %s\n", what.c_str() );
        ++missing;
    };
    for ( const auto &[n, s] : cat.m_story )
        if ( want( n ) && !cat.m_storyEn.count( n ) ) miss( "story " + n );
    for ( const auto &[n, e] : cat.m_enemies )
        if ( want( n ) && !e.m_title.empty() && e.m_titleEn.empty() ) miss( "enemy title_en " + n );
    for ( const auto &[n, l] : cat.m_levels )
        if ( want( n ) && l.m_titleEn.empty() ) miss( "level title_en " + n );
    for ( const auto &[id, ep] : cat.m_episodes )
        if ( want( "e" + std::to_string( id ) ) && ( ep.m_titleEn.empty() || ep.m_subtitleEn.empty() ) )
            miss( "episode " + std::to_string( id ) + " title_en/subtitle_en" );
    auto goods = [&]( const std::string &kind, const std::string &n, const std::string &t, const std::string &d ) {
        if ( want( n ) && ( t.empty() || d.empty() ) ) miss( kind + " " + n + " title_en/desc_en" );
    };
    for ( const auto &[n, w] : cat.m_weapons ) goods( w.m_slot, n, w.m_titleEn, w.m_descEn );
    for ( const auto &[n, s] : cat.m_sidekicks ) goods( "sidekick", n, s.m_titleEn, s.m_descEn );
    for ( const auto &[n, s] : cat.m_ships ) goods( "ship", n, s.m_titleEn, s.m_descEn );
    for ( const auto &[n, g] : cat.m_generators ) goods( "generator", n, g.m_titleEn, g.m_descEn );
    for ( const auto &[n, s] : cat.m_shields ) goods( "shield", n, s.m_titleEn, s.m_descEn );
    for ( const auto &[n, s] : cat.m_specials ) goods( "special", n, s.m_titleEn, s.m_descEn );
    for ( const DifficultyDef &d : cat.m_difficulties ) goods( "difficulty", d.m_name, d.m_titleEn, d.m_descEn );
    for ( const ArcadeShip &a : cat.m_arcadeShips ) goods( "pilot", a.m_name, a.m_titleEn, a.m_descEn );
    printf( "%d texts lack their English\n", missing );
    return missing == 0 ? 0 : 1;
}

int achievements( const std::string &dir )
{
    // The achievements and the statistics page: what the file holds, then what is wrong with it.
    AchievementList list;
    list.load( achievementsPath( dir ) );
    Catalog cat;
    cat.load( dir );
    list.check( cat );
    static const char *shows[] = { "count", "time", "clock", "credits" };
    int                hidden = 0;
    for ( const AchievementDef &a : list.m_achievements )
    {
        char need[64];
        snprintf( need, sizeof( need ), "%s %s %.10g", a.m_key.c_str(), a.m_lower ? "<=" : ">=", a.m_need );
        printf( "%-18s %-28s %-6s %s / %s\n", a.m_name.c_str(), need, a.m_hidden ? "hidden" : "", a.m_title.c_str(),
                a.m_titleEn.c_str() );
        hidden += a.m_hidden ? 1 : 0;
    }
    for ( const StatDef &s : list.m_stats )
        printf( "stat %-18s %-8s %s / %s\n", s.m_key.c_str(), shows[(int)s.m_show], s.m_title.c_str(),
                s.m_titleEn.c_str() );
    for ( const std::string &w : list.m_warnings ) printf( "warning: %s\n", w.c_str() );
    for ( const std::string &e : list.m_errors ) printf( "error: %s\n", e.c_str() );
    printf( "%s: %zu achievements (%d hidden), %zu statistics, %zu errors, %zu warnings\n",
            list.m_errors.empty() ? "OK" : "FAILED", list.m_achievements.size(), hidden, list.m_stats.size(),
            list.m_errors.size(), list.m_warnings.size() );
    return list.m_errors.empty() ? 0 : 1;
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
    if ( level == "bossrush" ) cat.m_levels["bossrush"] = bossRush( cat );
    if ( level == "survival" ) cat.m_levels["survival"] = survival( cat, seed );
    printf( "%-22s %5s %7s %9s %8s %7s %6s %6s %s\n", "mission", "won", "time", "kills", "credits", "damage", "deaths", "boss",
            " cubes/keys/plans  swarm  ground" );
    for ( const auto &[n, lv] : cat.m_levels )
    {
        if ( !level.empty() && n != level ) continue;
        if ( episode > 0 && lv.m_episode != episode ) continue;
        Loadout load = simLoadout( cat, lv );
        if ( n == "bossrush" && !cat.m_arcadeShips.empty() )
        {
            load = cat.m_arcadeShips[0].m_loadout;
            load.m_frontLv = 11;
            load.m_rear = load.m_rear.empty() ? "tailgun" : load.m_rear;
            load.m_rearLv = 9;
            load.m_gen = "singularity";
            load.m_shield = "halo_s";
        }
        // Survival is flown under arcade rules: the first arcade pilot's own gear and 3 lives.
        bool arcade = n == "survival" && !cat.m_arcadeShips.empty();
        if ( arcade ) load = cat.m_arcadeShips[0].m_loadout;
        parseLoadout( load, loadout );
        SimResult r =
            simulate( cat, lv, diff, load, seed, !mortal, arcade ? ModeE::ARCADE : ModeE::CAMPAIGN, arcade ? 2 : 0 );
        printf( "%-22s %5s %6.0fs %4d/%-4d %8d %7.0f %6d %5.0fs  %d/%d/%d  %4d/%-4d %4d\n", n.c_str(), r.m_won ? "yes" : "NO",
                r.m_time, r.m_kills, r.m_spawned, r.m_credits, r.m_damage, r.m_deaths, r.m_bossTime, r.m_cubes, r.m_keys, r.m_plans,
                r.m_swarmKills, r.m_swarm, r.m_structures );
        if ( arcade )
            printf( "%-22s first death %.0fs, timeline %.0fs, bosses %d\n", "", r.m_firstDeath, r.m_timeline,
                    r.m_bosses );
        if ( !r.m_stuck.empty() ) printf( "%-22s still on the field: %s\n", "", r.m_stuck.c_str() );
        fflush( stdout );
        if ( !r.m_won ) ++fails;
    }
    return fails == 0 ? 0 : 1;
}

/** @return the first thing two Worlds' ends differ in, to the bit ("" if none) */
std::string endDiff( const World &a, const World &b )
{
    if ( a.m_clock != b.m_clock ) return "clock";
    if ( a.m_rng.m_state != b.m_rng.m_state ) return "random state";
    if ( a.m_stats.m_kills != b.m_stats.m_kills || a.m_stats.m_credits != b.m_stats.m_credits ) return "kills/credits";
    if ( a.m_enemies.size() != b.m_enemies.size() || a.m_bullets.size() != b.m_bullets.size() )
        return "enemies/bullets";
    if ( a.finished() != b.finished() || a.failed() != b.failed() ) return "result";
    for ( int i = 0; i < 2; ++i )
    {
        const Player &p = a.m_players[i], &q = b.m_players[i];
        if ( p.m_score != q.m_score || p.m_credits != q.m_credits ) return "pilot score";
        if ( p.m_pos.x != q.m_pos.x || p.m_pos.y != q.m_pos.y ) return "pilot position";
        if ( p.m_armor != q.m_armor || p.m_alive != q.m_alive || p.m_lives != q.m_lives ) return "pilot armour";
    }
    return "";
}

/**
 * @brief --replaytest: the replay file checked, then each mission flown by the autopilot and recorded (its input
 *        quantized, a third of the time steering by the mouse), saved, read back and flown again from the file; the
 *        ends must be the same to the bit. Each mission twice: as it comes, and once fought again from the boss's
 *        checkpoint (a "retry" mark six seconds after the boss came, or when the fight is lost).
 */
int replayTest( const std::string &dir, const std::string &level, int diff, uint32_t seed, bool mortal,
                const std::string &loadout )
{
    namespace fs = std::filesystem;
    Catalog cat;
    cat.load( dir );
    for ( const std::string &e : cat.m_errors ) printf( "error: %s\n", e.c_str() );
    std::error_code e;
    std::string     tmp = ( fs::temp_directory_path( e ) / "zenith_replaytest" ).string();
    std::string     problem = replaySelfTest( tmp );
    int             fails = problem.empty() ? 0 : 1;
    if ( !problem.empty() ) printf( "replay file: FAILED: %s\n", problem.c_str() );
    cat.m_levels["bossrush"] = bossRush( cat );
    std::vector<std::string> names;
    for ( const auto &[n, lv] : cat.m_levels )
        if ( level.empty() ? lv.m_episode == 1 && n != "bossrush" : n == level ) names.push_back( n );
    if ( level.empty() ) names.push_back( "bossrush" );
    std::string file = ( fs::temp_directory_path( e ) / "zenith_replaytest.zlr" ).string();
    auto        live = std::make_unique<World>(), back = std::make_unique<World>();
    auto        liveSave = std::make_unique<World>(), backSave = std::make_unique<World>();
    printf( "%-12s %-5s %6s %7s %-11s %4s %6s %9s  %s\n", "mission", "run", "steps", "bytes", "marks", "won", "time",
            "score", "playback" );
    for ( const std::string &name : names )
        for ( int retry = 0; retry < 2; ++retry )
        {
            const LevelDef *lv = cat.level( name );
            if ( !lv ) continue;
            ReplayHeader h;
            h.m_version = ZENITH_VERSION;
            h.m_mode = name == "bossrush" ? ModeE::BOSSRUSH : ModeE::CAMPAIGN;
            h.m_mission = name;
            h.m_difficulty = diff;
            h.m_seed = seed;
            h.m_load[0] = simLoadout( cat, *lv );
            if ( name == "bossrush" && !cat.m_arcadeShips.empty() )
            {
                // As App::startBossRush: the first pilot's guns at full power, three lives.
                Loadout &o = h.m_load[0];
                o = cat.m_arcadeShips[0].m_loadout;
                o.m_frontLv = 11;
                o.m_rear = o.m_rear.empty() ? "tailgun" : o.m_rear;
                o.m_rearLv = 9;
                o.m_gen = "singularity";
                o.m_shield = "halo_s";
                h.m_lives[0] = 2;
            }
            parseLoadout( h.m_load[0], loadout );
            h.m_lang = "de";
            // Flown and recorded, as the app does it: the input quantized before the step, the checkpoint kept after
            // the step that brought the boss.
            startWorld( *live, cat, h );
            live->m_immortal = !mortal;
            ReplayRecorder rec;
            rec.begin( h );
            bool  saved = false, retried = false;
            float savedAt = 0.0f, limit = lv->m_length + 400.0f;
            for ( ;; )
            {
                if ( retry && saved && !retried && ( live->failed() || live->m_clock >= savedAt + 6.0f ) )
                {
                    rec.mark( ReplayMarkE::RETRY );
                    restoreBoss( *live, *liveSave );
                    retried = true;
                }
                if ( live->finished() || live->failed() || live->m_clock >= limit ) break;
                PlayerInput in[2] = { autopilot( *live, 0 ), {} };
                if ( ( rec.steps() / 600 ) % 3 == 2 )
                {
                    in[0].m_mouse = true;
                    in[0].m_target = add( live->m_players[0].m_pos, { in[0].m_x * 50.0f, in[0].m_y * 50.0f } );
                }
                in[0] = quantize( in[0] );
                in[1] = quantize( in[1] );
                rec.push( in );
                live->step( in );
                for ( const SimEvent &ev : live->m_events )
                    if ( ev.m_type == EvE::BOSSSTART )
                    {
                        *liveSave = *live;
                        saved = true;
                        savedAt = live->m_clock;
                    }
            }
            long long score = live->m_players[0].m_score + live->m_players[1].m_score;
            rec.finish( live->finished(), score, live->m_clock );
            std::string why;
            ReplayPlayer pl;
            if ( !rec.save( file ) ) why = "cannot save " + file;
            else if ( !pl.load( file ) ) why = "cannot load: " + pl.error();
            else if ( !startWorld( *back, cat, pl.header() ) ) why = "no mission " + pl.header().m_mission;
            else
            {
                // Flown again from the file alone: the marks before their step, the checkpoint as above.
                back->m_immortal = !mortal;
                for ( ;; )
                {
                    for ( ReplayMarkE m : pl.marks() )
                        if ( m == ReplayMarkE::RETRY ) restoreBoss( *back, *backSave );
                        else continueArcade( *back );
                    PlayerInput in[2];
                    if ( !pl.next( in ) ) break;
                    back->step( in );
                    for ( const SimEvent &ev : back->m_events )
                        if ( ev.m_type == EvE::BOSSSTART ) *backSave = *back;
                }
                why = endDiff( *live, *back );
                const ReplayHeader &g = pl.header();
                if ( why.empty() && ( g.m_endScore != score || g.m_time != live->m_clock ||
                                      ( g.m_end == ReplayEndE::WON ) != live->finished() ) )
                    why = "result in the header";
            }
            std::string marks = retried ? "retry@" + std::to_string( rec.header().m_marks[0].m_step )
                              : retry   ? "(no boss)"
                                        : "-";
            printf( "%-12s %-5s %6d %7zu %-11s %4s %5.0fs %9lld  %s\n", name.c_str(), retry ? "retry" : "plain",
                    rec.steps(), rec.bytes(), marks.c_str(), live->finished() ? "yes" : "NO", live->m_clock, score,
                    why.empty() ? "same" : ( "DIFFERS: " + why ).c_str() );
            fflush( stdout );
            if ( !why.empty() ) ++fails;
        }
    fs::remove( fs::path( file ), e );
    printf( "%s\n", fails == 0 ? "replays: all the same" : "replays: FAILED" );
    return fails == 0 ? 0 : 1;
}

/** @brief A golden replay: a flight of the autopilot kept in tests/golden, played back by every test run. */
struct Golden
{
    const char *m_name;
    const char *m_mission;
    ModeE       m_mode;
    int         m_diff;
    uint32_t    m_seed;
    int         m_pilots;
    int         m_rules;
    bool        m_retry;   ///< the boss fought again from its checkpoint
};

const Golden Goldens[] = {
    { "campaign_e1_01", "e1_01", ModeE::CAMPAIGN, 2, 101, 1, 0, false },
    { "boss_retry_e3_05", "e3_05", ModeE::CAMPAIGN, 3, 102, 1, 0, true },
    { "arcade2_e2_03", "e2_03", ModeE::ARCADE, 4, 103, 2, 0, false },
    { "survival_rules", "survival", ModeE::ARCADE, 2, 104, 1, (int)RuleE::DOUBLE | (int)RuleE::GLASS, false },
    { "bossrush", "bossrush", ModeE::BOSSRUSH, 2, 105, 1, 0, false },
};

/** @brief The generated missions a replay may name (the boss rush, the survival flight of its seed). */
void generated( Catalog &cat, const ReplayHeader &h )
{
    cat.m_levels["bossrush"] = bossRush( cat );
    if ( h.m_mission == "survival" ) cat.m_levels["survival"] = survival( cat, h.m_seed );
}

/** @brief Plays a replay back from its file alone; the World at its end. */
void playBack( World &w, World &save, const Catalog &cat, ReplayPlayer &pl )
{
    startWorld( w, cat, pl.header() );
    for ( ;; )
    {
        for ( ReplayMarkE m : pl.marks() )
            if ( m == ReplayMarkE::RETRY ) restoreBoss( w, save );
            else continueArcade( w );
        PlayerInput in[2];
        if ( !pl.next( in ) ) break;
        w.step( in );
        for ( const SimEvent &ev : w.m_events )
            if ( ev.m_type == EvE::BOSSSTART ) save = w;
    }
}

/**
 * @brief --goldenmake DIR: the golden replays recorded anew (after a change to the game that is meant): a campaign
 *        mission, a boss fought again from its checkpoint, the arcade for two on Impossible, a survival flight
 *        under two weekly rules and the boss rush, each flown by the autopilot as the app would record it.
 */
int goldenMake( const std::string &dataDir, const std::string &dir )
{
    namespace fs = std::filesystem;
    Catalog cat;
    cat.load( dataDir );
    std::error_code e;
    fs::create_directories( dir, e );
    auto w = std::make_unique<World>(), save = std::make_unique<World>();
    for ( const Golden &g : Goldens )
    {
        ReplayHeader h;
        h.m_version = ZENITH_VERSION;
        h.m_mode = g.m_mode;
        h.m_mission = g.m_mission;
        h.m_difficulty = g.m_diff;
        h.m_seed = g.m_seed;
        h.m_pilots = g.m_pilots;
        h.m_rules = g.m_rules;
        h.m_data = cat.m_hash;
        h.m_lang = "en";
        h.m_date = "2026-10-30 12:00:00";
        generated( cat, h );
        const LevelDef *lv = cat.level( g.m_mission );
        if ( !lv ) continue;
        for ( int i = 0; i < g.m_pilots; ++i )
        {
            if ( g.m_mode == ModeE::CAMPAIGN ) h.m_load[i] = midLoadout( cat, lv->m_episode );
            else if ( !cat.m_arcadeShips.empty() )
            {
                h.m_load[i] = cat.m_arcadeShips[(size_t)i % cat.m_arcadeShips.size()].m_loadout;
                h.m_lives[i] = 2;
            }
            if ( g.m_mode == ModeE::BOSSRUSH )
            {
                Loadout &o = h.m_load[i];
                o.m_frontLv = 11;
                o.m_rear = o.m_rear.empty() ? "tailgun" : o.m_rear;
                o.m_rearLv = 9;
                o.m_gen = "singularity";
                o.m_shield = "halo_s";
            }
        }
        startWorld( *w, cat, h );
        ReplayRecorder rec;
        rec.begin( h );
        bool  saved = false, retried = false;
        float savedAt = 0.0f, limit = lv->m_length + 400.0f;
        while ( !w->finished() && !w->failed() && w->m_clock < limit )
        {
            if ( g.m_retry && saved && !retried && w->m_clock >= savedAt + 6.0f )
            {
                rec.mark( ReplayMarkE::RETRY );
                restoreBoss( *w, *save );
                retried = true;
            }
            PlayerInput in[2] = { autopilot( *w, 0 ), g.m_pilots > 1 ? autopilot( *w, 1 ) : PlayerInput() };
            in[0] = quantize( in[0] );
            in[1] = quantize( in[1] );
            rec.push( in );
            w->step( in );
            for ( const SimEvent &ev : w->m_events )
                if ( ev.m_type == EvE::BOSSSTART )
                {
                    *save = *w;
                    saved = true;
                    savedAt = w->m_clock;
                }
        }
        long long score = w->m_players[0].m_score + w->m_players[1].m_score;
        rec.finish( w->finished(), score, w->m_clock );
        std::string file = ( fs::path( dir ) / ( std::string( g.m_name ) + ReplayExt ) ).string();
        printf( "%-18s %6d steps %7zu bytes %s %5.0fs %9lld\n", g.m_name, rec.steps(), rec.bytes(), w->finished() ? "won " : "lost",
                w->m_clock, score );
        if ( !rec.save( file ) ) return 1;
    }
    return 0;
}

/**
 * @brief --goldentest DIR: every golden replay played back; its end must be the recorded one to the bit (the clock,
 *        the score, the result). A difference means the simulation changed: wanted, then --goldenmake records them
 *        anew; else it is a bug.
 * @return 0 when all are the same
 */
int goldenTest( const std::string &dataDir, const std::string &dir )
{
    namespace fs = std::filesystem;
    Catalog cat;
    cat.load( dataDir );
    std::vector<ReplayHeader> list = listReplays( dir );
    std::sort( list.begin(), list.end(), []( const ReplayHeader &a, const ReplayHeader &b ) { return a.m_path < b.m_path; } );
    auto w = std::make_unique<World>(), save = std::make_unique<World>();
    int  fails = list.empty() ? 1 : 0;
    for ( const ReplayHeader &h : list )
    {
        ReplayPlayer pl;
        std::string  why;
        if ( !pl.load( h.m_path ) ) why = pl.error();
        else if ( h.m_data != cat.m_hash ) why = "the data changed since it was recorded (--goldenmake records it anew)";
        else
        {
            generated( cat, h );
            if ( !cat.level( h.m_mission ) ) why = "no mission " + h.m_mission;
            else
            {
                playBack( *w, *save, cat, pl );
                long long score = w->m_players[0].m_score + w->m_players[1].m_score;
                if ( w->m_clock != h.m_time ) why = "the clock";
                else if ( score != h.m_endScore ) why = "the score";
                else if ( w->finished() != ( h.m_end == ReplayEndE::WON ) ) why = "the result";
            }
        }
        printf( "%-24s %s\n", fs::path( h.m_path ).filename().string().c_str(), why.empty() ? "same" : ( "DIFFERS: " + why ).c_str() );
        if ( !why.empty() ) ++fails;
    }
    printf( "%s\n", fails == 0 ? "golden replays: all the same" : "golden replays: FAILED" );
    return fails == 0 ? 0 : 1;
}

}   // namespace

int main( int argc, char **argv )
{
    Options     o;
    std::string simLevel, dumpDir, loadout, langOnly;
    bool        doLang = false;
    std::vector<std::string> modDirs;   // --mod FOLDER: a mod's data over the game's (with --validate)
    bool        doAchieve = false;
    bool        doValidate = false, doSimAll = false, mortal = false, doWeapons = false;
    uint32_t    seed = 1;
    int         episode = 0;
    std::string replayLevel = "-";   // "-": no --replaytest
    std::string goldenMakeDir, goldenTestDir;
    for ( int i = 1; i < argc; ++i )
    {
        std::string a = argv[i];
        if ( a == "--help" || a == "-h" || a == "/?" )
        {
            printf( "ZenithLancer [--mute] [--windowed|--fullscreen] [--size WxH] [--data DIR] [--assets DIR]\n"
                    "             [--level NAME [--difficulty N] [--arcade] [--auto] [--loadout ...] [--theme T]]\n"
                    "  --validate [--mod FOLDER]  check every data file (and a mod's data over them)\n"
                    "  --sim NAME | --simall [--episode E] [--difficulty N] [--seed S] [--mortal] [--loadout ...]\n"
                    "  --weapons                  every gun's damage and energy a second\n"
                    "  --replaytest [MISSION]     record and play back episode 1 and the boss rush (or one mission)\n"
                    "  --shot FILE --level NAME [--at S] | --screen NAME --shot FILE | --gallery FILE\n"
                    "  --apptest SCRIPT           (with ZENITH_HOME set)    --dumpsfx DIR\n"
                    "  --bench [all]              heavy missions flown, frame times measured (<user>/bench.txt)\n"
                    "  --video FILE (--replay R | --level NAME) [--seconds S] [--fps N] [--size WxH]   an MP4 (ffmpeg)\n"
                    "  --deckcheck                every screen at 1280x800, the smallest text measured\n"
                    "  --goldentest DIR | --goldenmake DIR   the golden replays played back (or recorded anew)\n"
                    "loadout: \"ship=lancer,front=pulse:5,rear=tailgun:3,left=pip,right=-,gen=dynamo,shield=ward,special=nova_s\"\n" );
            return 0;
        }
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
        else if ( a == "--pilots" ) o.m_pilots = std::max( 1, std::min( 2, atoi( next().c_str() ) ) );
        else if ( a == "--shot" ) o.m_shot = next();
        else if ( a == "--at" ) o.m_at = (float)atof( next().c_str() );
        else if ( a == "--gallery" ) o.m_gallery = next();
        else if ( a == "--validate" ) doValidate = true;
        else if ( a == "--mod" ) modDirs.push_back( next() + "/data" );
        else if ( a == "--sim" ) simLevel = next();
        else if ( a == "--simall" ) doSimAll = true;
        else if ( a == "--seed" ) seed = (uint32_t)atoi( next().c_str() );
        else if ( a == "--mortal" ) mortal = true;
        else if ( a == "--episode" ) episode = atoi( next().c_str() );
        else if ( a == "--dumpsfx" ) dumpDir = next();
        else if ( a == "--loadout" ) loadout = o.m_loadout = next();
        else if ( a == "--weapons" ) doWeapons = true;
        else if ( a == "--langcheck" ) langOnly = next(), doLang = true;
        else if ( a == "--achievements" ) doAchieve = true;
        else if ( a == "--theme" ) o.m_theme = next();
        else if ( a == "--screen" ) o.m_screen = next();
        else if ( a == "--apptest" ) o.m_test = next();
        else if ( a == "--video" ) o.m_video = next();
        else if ( a == "--replay" ) o.m_replay = next();
        else if ( a == "--seconds" ) o.m_videoSeconds = (float)atof( next().c_str() );
        else if ( a == "--fps" ) o.m_videoFps = atoi( next().c_str() );
        else if ( a == "--from" ) o.m_videoFrom = (float)atof( next().c_str() );
        else if ( a == "--boss" ) o.m_videoBoss = true;
        else if ( a == "--lang" ) o.m_lang = next();
        else if ( a == "--deckcheck" ) o.m_deckCheck = true;
        else if ( a == "--goldenmake" ) goldenMakeDir = next();
        else if ( a == "--goldentest" ) goldenTestDir = next();
        else if ( a == "--bench" )
        {
            o.m_bench = true;
            if ( i + 1 < argc && std::string( argv[i + 1] ) == "all" ) o.m_benchAll = true, ++i;
        }
        else if ( a == "--replaytest" ) replayLevel = i + 1 < argc && argv[i + 1][0] != '-' ? next() : "";
    }
    if ( replayLevel != "-" )
        return replayTest( dataDir( o.m_data ), replayLevel, o.m_difficulty, seed, mortal, loadout );
    if ( !goldenMakeDir.empty() ) return goldenMake( dataDir( o.m_data ), goldenMakeDir );
    if ( !goldenTestDir.empty() ) return goldenTest( dataDir( o.m_data ), goldenTestDir );
    if ( doWeapons ) return weapons( dataDir( o.m_data ) );
    if ( doLang ) return langCheck( dataDir( o.m_data ), langOnly == "all" ? "" : langOnly );
    if ( doAchieve ) return achievements( dataDir( o.m_data ) );
    if ( doValidate ) return validate( dataDir( o.m_data ), modDirs );
    if ( !simLevel.empty() || doSimAll ) return sim( dataDir( o.m_data ), simLevel, o.m_difficulty, seed, mortal, episode, loadout );
    if ( !dumpDir.empty() )
    {
        Audio::dump( dumpDir );
        return 0;
    }
    if ( !o.m_shot.empty() || !o.m_gallery.empty() || !o.m_test.empty() || o.m_bench || !o.m_video.empty() || o.m_deckCheck )
        o.m_mute = true;
    // A video is 1080p, the Deck check at the Deck's screen, unless --size says otherwise.
    if ( !o.m_video.empty() && o.m_width <= 0 ) o.m_width = 1920, o.m_height = 1080;
    if ( o.m_deckCheck && o.m_width <= 0 ) o.m_width = 1280, o.m_height = 800;
    if ( !o.m_video.empty() && o.m_replay.empty() ) o.m_auto = true;
    if ( !o.m_shot.empty() ) o.m_auto = true;
    if ( !o.m_test.empty() )
    {
        // An app test starts from nothing: its test folder's saves, profile and settings go first.
        const char *home = getenv( "ZENITH_HOME" );
        if ( !home || !*home )
        {
            printf( "--apptest needs ZENITH_HOME (a test folder; the real saves stay untouched)\n" );
            return 2;
        }
        std::error_code e;
        for ( const char *f : { "slot1.txt", "slot2.txt", "slot3.txt", "profile.txt", "settings.txt", "feats.txt" } )
            std::filesystem::remove( std::filesystem::path( home ) / f, e );
        std::filesystem::remove_all( std::filesystem::path( home ) / "replays", e );
        std::filesystem::remove_all( std::filesystem::path( home ) / "ghosts", e );
        std::filesystem::remove_all( std::filesystem::path( home ) / "mods", e );
    }
    App app;
    if ( !app.init( o ) ) return 1;
    if ( !o.m_test.empty() )
    {
        int r = app.appTest();
        app.shutdown();
        return r;
    }
    if ( !o.m_video.empty() || o.m_deckCheck )
    {
        int r = o.m_deckCheck ? app.deckCheck() : app.video();
        app.shutdown();
        return r;
    }
    if ( !o.m_gallery.empty() )
        app.gallery( o.m_gallery );
    else
        app.run();
    app.shutdown();
    return 0;
}
