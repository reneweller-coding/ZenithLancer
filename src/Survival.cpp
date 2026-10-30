/**
 * @file Survival.cpp
 * @brief The survival mission: the campaign's waves, bosses and grounds, put together by a tier that rises with the
 *        time.
 * @author Rene Weller
 */
#include "Survival.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace zenith
{

namespace
{

// The run's shape. Balanced with the autopilot under arcade rules on Normal (--sim survival --mortal): it falls
// after about 16 minutes, 13 to 19 over 15 seeds (docs/DESIGN.md, "Survival").
constexpr float CycleLen = 180.0f;        ///< seconds of timeline from one boss to the next (and one ground)
constexpr int   Cycles = 20;              ///< bosses in the hour
constexpr float TierPerMinute = 0.3f;     ///< how fast the waves move through the episodes (one in 3:20)
constexpr float TierTop = 5.9f;           ///< the last mission's tier: past it the enemies grow tougher instead
constexpr float HpPerTier = 1.6f;         ///< a wave flown a tier after its own mission holds this much more
constexpr float HpPastTop = 0.5f;         ///< past the last mission: this much more for each tier beyond it
constexpr float HpPerMinute = 0.06f;      ///< every wave this much more for each minute flown ...
constexpr float HpMinutes = 25.0f;        ///< ... up to this minute
constexpr float BossBase = 800.0f;        ///< the hit points a boss may have with its parts at the start ...
constexpr float BossPerMinute = 150.0f;   ///< ... and more for each minute

/** @brief A wave of a campaign mission that the survival mission may borrow. */
struct Borrowed
{
    const Line     *m_line = nullptr;   ///< its line in the mission
    const EnemyDef *m_def = nullptr;    ///< the enemy
    std::string     m_type;             ///< wave, ground or obstacle
    float           m_tier = 1.0f;      ///< its mission's tier
    std::string     m_theme;            ///< its mission's ground
};

/** @brief A boss of the campaign. */
struct BossRef
{
    const Line     *m_line = nullptr;   ///< the line it enters with
    const EnemyDef *m_def = nullptr;
    float           m_tier = 1.0f;       ///< the tier of the first mission it appears in
    bool            m_great = false;     ///< the boss of an episode's middle, last but one or last mission
    int             m_met = 0;           ///< how often it came in this run
    float           m_strength = 0.0f;   ///< its hit points with all its parts
};

/** @brief A campaign mission whose ground, music and speed a stretch of the survival mission takes. */
struct Stage
{
    const LevelDef *m_level = nullptr;
    float           m_tier = 1.0f;
};

/** @brief All the survival mission may take from the campaign. */
struct Pool
{
    std::vector<Borrowed> m_waves;
    std::vector<BossRef>  m_bosses;
    std::vector<Stage>    m_stages;
};

/** @return a number as a named value's text */
std::string text( float v )
{
    char buf[32];
    snprintf( buf, sizeof( buf ), "%g", v );
    return buf;
}

/**
 * @brief A mission's place in the campaign: episode 1's first mission is 1.0, episode 5's tenth 5.9; a secret
 *        mission counts as its episode's seventh.
 * @return the tier (-1 for a mission outside the episodes, such as the boss rush)
 */
float tierOf( const std::string &name, int *index = nullptr )
{
    if ( name.size() < 4 || name[0] != 'e' || !std::isdigit( (unsigned char)name[1] ) || name[2] != '_' ) return -1.0f;
    int ep = name[1] - '0';
    int idx = name[3] == 's' ? 7 : std::atoi( name.c_str() + 3 );
    if ( ep < 1 || idx < 1 ) return -1.0f;
    if ( index ) *index = name[3] == 's' ? 0 : idx;
    return (float)ep + ( std::min( idx, 10 ) - 1 ) * 0.1f;
}

/** @return an enemy's hit points with all its parts (and theirs) */
float strength( const Catalog &cat, const EnemyDef &d, int depth = 0 )
{
    float hp = d.m_hp;
    if ( depth < 4 )
        for ( const PartRef &p : d.m_parts )
            if ( const EnemyDef *pd = cat.enemy( p.m_enemy ) ) hp += strength( cat, *pd, depth + 1 );
    return hp;
}

/** @brief Gathers the campaign's waves, bosses and grounds (in the catalog's order, so the same for every run). */
Pool gather( const Catalog &cat )
{
    Pool                                  pool;
    std::set<std::pair<std::string, int>> seen;   // a "repeat" line is one wave, however often it is flown
    for ( const auto &[name, lv] : cat.m_levels )
    {
        int   idx = 0;
        float tier = tierOf( name, &idx );
        if ( tier < 0 ) continue;
        if ( !lv.m_isSecret && cat.theme( lv.m_theme ) ) pool.m_stages.push_back( { &lv, tier } );
        for ( const LevelEvent &ev : lv.m_events )
        {
            const EnemyDef *d = cat.enemy( ev.m_p.arg( 0 ) );
            if ( !d ) continue;
            if ( ev.m_type == "boss" && d->m_kind == EnemyKindE::BOSS )
            {
                bool great = !lv.m_isSecret && ( idx == 5 || idx == 9 || idx == 10 );
                auto have = std::find_if( pool.m_bosses.begin(), pool.m_bosses.end(),
                                          [&]( const BossRef &b ) { return b.m_def == d; } );
                if ( have == pool.m_bosses.end() )
                    pool.m_bosses.push_back( { &ev.m_p, d, tier, great, 0, strength( cat, *d ) } );
                else
                {
                    have->m_great = have->m_great || great;
                    if ( tier < have->m_tier )
                    {
                        have->m_tier = tier;
                        have->m_line = &ev.m_p;
                    }
                }
                continue;
            }
            bool fits = ( ev.m_type == "wave" && d->m_kind == EnemyKindE::AIR ) ||
                        ( ev.m_type == "ground" && d->m_kind == EnemyKindE::GROUND ) ||
                        ( ev.m_type == "obstacle" && d->m_kind == EnemyKindE::OBSTACLE );
            if ( !fits || !seen.insert( { ev.m_p.m_file, ev.m_p.m_line } ).second ) continue;
            pool.m_waves.push_back( { &ev.m_p, d, ev.m_type, tier, lv.m_theme } );
        }
    }
    return pool;
}

/** @return the tier the waves have come to after t seconds of timeline */
float tierAt( float t ) { return 1.0f + t / 60.0f * TierPerMinute; }

/** @return a share of the way from minute a to minute b (0 before a, 1 after b) */
float ramp( float minute, float a, float b ) { return clampf( ( minute - a ) / ( b - a ), 0.0f, 1.0f ); }

/**
 * @brief A new ground: a mission of the episode the waves will be in, over a ground not flown before (else any but
 *        the one flown now).
 */
const Stage *pickStage( const Pool &pool, float tier, const std::set<std::string> &flown, const std::string &now,
                        Rng &rng )
{
    int ep = std::clamp( (int)std::min( tier, TierTop ), 1, 5 );
    for ( int pass = 0; pass < 3; ++pass )
    {
        std::vector<const Stage *> c;
        for ( const Stage &s : pool.m_stages )
        {
            if ( pass < 2 && ( (int)s.m_tier != ep || s.m_tier > tier + 0.5f ) ) continue;
            if ( pass == 0 && flown.count( s.m_level->m_theme ) ) continue;
            if ( s.m_level->m_theme == now ) continue;
            c.push_back( &s );
        }
        if ( !c.empty() ) return c[(size_t)rng.below( (int)c.size() )];
    }
    return &pool.m_stages[0];
}

/**
 * @brief A wave to borrow. Fliers come from the tier reached and up to `reach` behind it, the nearer the likelier;
 *        ground units and obstacles only from missions over the same ground (they belong to it), up to the tier of
 *        the mission that ground was taken from.
 */
const Borrowed *pickWave( const Pool &pool, const std::string &type, float tier, float reach, const Stage &stage,
                          const Borrowed *last, Rng &rng )
{
    float top = std::min( tier, TierTop );
    bool  air = type == "wave";
    float hi = ( air ? top : std::min( std::max( top, stage.m_tier ), top + 0.5f ) ) + 0.05f;
    float lo = air ? top - reach : 0.0f;
    std::vector<std::pair<const Borrowed *, float>> c;
    float                                           sum = 0.0f;
    for ( const Borrowed &b : pool.m_waves )
    {
        if ( b.m_type != type || b.m_tier > hi || b.m_tier < lo || &b == last ) continue;
        if ( !air && b.m_theme != stage.m_level->m_theme ) continue;
        float w = air ? 1.25f - ( top - b.m_tier ) / std::max( 0.1f, reach ) : 1.0f;
        c.push_back( { &b, std::max( 0.1f, w ) } );
        sum += c.back().second;
    }
    if ( c.empty() ) return nullptr;
    float r = rng.range( 0.0f, sum );
    for ( const auto &[b, w] : c )
    {
        r -= w;
        if ( r <= 0.0f ) return b;
    }
    return c.back().first;
}

/**
 * @brief A boss for the end of a cycle: one the waves have reached, of the kind wanted, not met before; once all of
 *        them are met, the one met least often of the latest.
 */
BossRef *pickBoss( Pool &pool, float tier, bool great, Rng &rng )
{
    float top = std::min( tier, TierTop ) + 0.1f;
    for ( int pass = 0; pass < 4; ++pass )
    {
        std::vector<BossRef *> c;
        for ( BossRef &b : pool.m_bosses )
        {
            if ( b.m_tier > top || ( pass < 3 && b.m_tier < top - 1.2f ) ) continue;
            if ( pass < 2 && b.m_met > 0 ) continue;
            if ( pass == 0 && b.m_great != great ) continue;
            c.push_back( &b );
        }
        if ( c.empty() ) continue;
        std::stable_sort( c.begin(), c.end(), [&]( const BossRef *a, const BossRef *b ) {
            if ( a->m_met != b->m_met ) return a->m_met < b->m_met;
            return std::fabs( a->m_tier - tier ) < std::fabs( b->m_tier - tier );
        } );
        int      choice = c.size() > 1 && c[1]->m_met == c[0]->m_met ? 2 : 1;
        BossRef *b = c[(size_t)rng.below( choice )];
        ++b->m_met;
        return b;
    }
    return nullptr;
}

/** @brief Turns a wave about the field's middle (the other side, the other way round). */
void flip( Line &p, const EnemyDef &d )
{
    if ( p.has( "pts" ) || d.m_path.has( "pts" ) ) return;   // splines stay as they were drawn
    p.m_kv["x"] = text( FieldW - p.num( "x", FieldW * 0.5f ) );
    auto turn = [&]( const char *k, auto how ) {
        if ( p.has( k ) ) p.m_kv[k] = text( how( p.num( k ) ) );
        else if ( d.m_path.has( k ) ) p.m_kv[k] = text( how( d.m_path.num( k ) ) );
    };
    turn( "side", []( float v ) { return v < 0 ? 1.0f : -1.0f; } );
    turn( "cx", []( float v ) { return FieldW - v; } );
    turn( "dir", []( float v ) { return -v; } );
}

/**
 * @brief A borrowed wave as an event of the survival mission: the mission's loot and switches taken off, more ships
 *        in it the later it is, tougher with every minute and the further the war has come past its own mission.
 */
LevelEvent borrow( const Borrowed &b, float t, float tier, float density, Rng &rng )
{
    LevelEvent ev;
    ev.m_t = t;
    ev.m_type = b.m_type;
    ev.m_p = *b.m_line;
    Line &p = ev.m_p;
    for ( const char *k : { "item", "item2", "value", "bonus", "chance", "diff", "repeat", "every", "hold", "seed" } )
        p.m_kv.erase( k );
    std::string drop = p.str( "drop" );
    if ( drop != "shield" && drop != "armor" ) p.m_kv.erase( "drop" );
    p.m_kv["seed"] = std::to_string( 1 + rng.below( 999999 ) );
    int n = p.integer( "n", 1 );
    if ( n >= 3 && density > 1.0f )
    {
        std::string form = p.str( "form", "column" );
        int         m = (int)std::lround( n * density );
        if ( form == "grid" )
        {
            int cols = std::max( 1, p.integer( "cols", n ) );
            m = ( m + cols - 1 ) / cols * cols;
        }
        else if ( form == "line" || form == "v" || form == "arc" )
        {
            // As wide as it was: the ships close up.
            m = std::min( m, n + 4 );
            float sp = p.num( "spacing", 60.0f );
            p.m_kv["spacing"] = text( form == "arc" ? sp * n / m : sp * ( n - 1 ) / ( m - 1 ) );
        }
        p.m_kv["n"] = std::to_string( m );
    }
    float within = std::max( 0.0f, std::min( tier, TierTop ) - b.m_tier );
    float past = std::max( 0.0f, tier - TierTop );
    float hp = std::pow( HpPerTier, within ) * ( 1.0f + HpPastTop * past ) *
               ( 1.0f + HpPerMinute * std::min( t / 60.0f, HpMinutes ) );
    if ( hp > 1.02f ) p.m_kv["hp"] = text( hp * p.num( "hp", 1.0f ) );
    if ( rng.chance( 0.5f ) ) flip( p, *b.m_def );
    return ev;
}

/** @return an event without values but its words */
LevelEvent event( float t, const std::string &type, std::vector<std::string> args = {} )
{
    LevelEvent ev;
    ev.m_t = t;
    ev.m_type = type;
    ev.m_p.m_key = type;
    ev.m_p.m_args = std::move( args );
    return ev;
}

}   // namespace

LevelDef survival( const Catalog &cat, uint32_t seed )
{
    LevelDef lv;
    lv.m_name = "survival";
    lv.m_title = "Überleben";
    lv.m_titleEn = "Survival";
    lv.m_seed = seed * 2654435761u + 5u;   // the grounds' look follows the seed too
    Rng rng;
    rng.seed( seed ^ 0x51a7e5u );
    Pool pool = gather( cat );
    if ( pool.m_stages.empty() ) return lv;

    std::set<std::string> flown;
    const Stage          *stage = pickStage( pool, tierAt( CycleLen * 0.5f ), flown, "", rng );
    flown.insert( stage->m_level->m_theme );
    lv.m_theme = stage->m_level->m_theme;
    lv.m_music = stage->m_level->m_music;
    lv.m_scroll = stage->m_level->m_scroll;

    const Borrowed *last = nullptr;
    for ( int k = 0; k < Cycles; ++k )
    {
        float c0 = k * CycleLen;
        float warnT = c0 + CycleLen - 12.0f;
        // The waves: one at a time at first, soon two and three at once, closer together and fuller.
        for ( float t = c0 + ( k == 0 ? 3.0f : 4.0f ); t < warnT - 8.0f; )
        {
            float minute = t / 60.0f;
            float tier = tierAt( t );
            float density = 1.0f + ramp( minute, 2.0f, 16.0f );
            float reach = 0.6f + 0.6f * ramp( minute, 0.0f, 30.0f );
            int   atOnce = 1 + ( rng.chance( 0.7f * ramp( minute, 1.0f, 10.0f ) ) ? 1 : 0 ) +
                           ( rng.chance( 0.8f * ramp( minute, 4.0f, 12.0f ) ) ? 1 : 0 );
            for ( int j = 0; j < atOnce; ++j )
            {
                float           u = rng.unit();
                std::string     type = u < 0.22f ? "ground" : u < 0.3f ? "obstacle" : "wave";
                const Borrowed *b = pickWave( pool, type, tier, reach, *stage, last, rng );
                if ( !b ) b = pickWave( pool, "wave", tier, reach, *stage, last, rng );
                if ( !b ) b = pickWave( pool, "wave", tier, 5.0f, *stage, last, rng );
                if ( !b ) continue;
                float at = t + ( j == 0 ? 0.0f : rng.range( 0.8f, 2.5f ) );
                lv.m_events.push_back( borrow( *b, at, tier, density, rng ) );
                last = b;
            }
            t += lerpf( 4.5f, 3.0f, ramp( minute, 0.0f, 15.0f ) ) * rng.range( 0.8f, 1.25f );
        }
        // The boss: every other one a great one, the last the greatest there is.
        BossRef *boss = nullptr;
        if ( k == Cycles - 1 )
        {
            for ( BossRef &b : pool.m_bosses )
                if ( b.m_great && ( !boss || b.m_tier > boss->m_tier ) ) boss = &b;
        }
        else
            boss = pickBoss( pool, tierAt( warnT ), k % 2 == 1, rng );
        if ( boss )
        {
            int         ep = std::clamp( (int)boss->m_tier, 1, 5 );
            std::string music = k == Cycles - 1 ? "final"
                                : boss->m_great ? "e" + std::to_string( ep ) + "_boss"
                                                : "boss";
            lv.m_events.push_back( event( warnT - 1.0f, "music", { music } ) );
            lv.m_events.push_back( event( warnT, "warn" ) );
            LevelEvent b;
            b.m_t = warnT + 3.0f;
            b.m_type = "boss";
            b.m_p = *boss->m_line;
            b.m_p.m_kv.erase( "hold" );
            // A late episode's boss is cut down to what an arcade ship can carry at this hour.
            float budget = BossBase + BossPerMinute * warnT / 60.0f;
            float hp = std::max( 0.2f, std::min( 1.0f, budget / std::max( 1.0f, boss->m_strength ) ) );
            if ( hp < 0.99f ) b.m_p.m_kv["hp"] = text( hp );
            lv.m_events.push_back( b );
            // What the boss leaves: a full shield and a patch of armour.
            LevelEvent sh = event( warnT + 5.0f, "pickup", { "shield" } );
            sh.m_p.m_kv["x"] = "300";
            lv.m_events.push_back( sh );
            LevelEvent ar = event( warnT + 5.0f, "pickup", { "armor" } );
            ar.m_p.m_kv["x"] = "420";
            lv.m_events.push_back( ar );
        }
        if ( k == Cycles - 1 ) break;
        // After each boss a new ground (the app hides the change in a flash) of the episode the waves will be in
        // halfway through the next cycle, with its mission's music and speed.
        float after = warnT + 7.0f;
        stage = pickStage( pool, tierAt( after + CycleLen * 0.5f ), flown, stage->m_level->m_theme, rng );
        flown.insert( stage->m_level->m_theme );
        lv.m_events.push_back( event( after, "theme", { stage->m_level->m_theme } ) );
        lv.m_events.push_back( event( after, "scroll", { text( stage->m_level->m_scroll ) } ) );
        lv.m_events.push_back( event( after, "music", { stage->m_level->m_music } ) );
    }
    float end = Cycles * CycleLen;
    lv.m_events.push_back( event( end, "end" ) );
    std::stable_sort( lv.m_events.begin(), lv.m_events.end(),
                      []( const LevelEvent &a, const LevelEvent &c ) { return a.m_t < c.m_t; } );
    lv.m_length = end + 60.0f * Cycles;   // the bosses hold the timeline: room for the fights
    return lv;
}

uint32_t dailySeed()
{
    time_t    now = time( nullptr );
    const tm *lt = localtime( &now );
    char      date[16];
    snprintf( date, sizeof( date ), "%04d%02d%02d", lt ? lt->tm_year + 1900 : 2026, lt ? lt->tm_mon + 1 : 1,
              lt ? lt->tm_mday : 1 );
    return weekSeed( date );
}

std::string thisWeek()
{
    time_t    now = time( nullptr );
    const tm *lt = localtime( &now );
    char      week[16] = "2026-W01";
    if ( lt ) strftime( week, sizeof( week ), "%G-W%V", lt );
    return week;
}

uint32_t weekSeed( const std::string &week )
{
    // FNV-1a over the text, then stirred: neighbouring days and weeks get unrelated seeds.
    uint32_t h = 2166136261u;
    for ( char c : week ) h = ( h ^ (unsigned char)c ) * 16777619u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    return h ? h : 1u;
}

namespace
{

const char Base32[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

/** @return the check of a code's 45 bits (5 bits) */
int codeCheck( uint64_t bits )
{
    uint32_t h = 2166136261u;
    for ( int i = 0; i < 6; ++i ) h = ( h ^ (uint32_t)( ( bits >> ( 8 * i ) ) & 0xff ) ) * 16777619u;
    return (int)( ( h ^ ( h >> 5 ) ^ ( h >> 10 ) ^ ( h >> 20 ) ) & 31u );
}

}   // namespace

std::string challengeCode( uint32_t seed, int diff, int rules )
{
    // Bits: 32 seed, 3 difficulty, 8 rules, 2 the code's version, 5 the check.
    uint64_t bits = (uint64_t)seed | ( (uint64_t)( diff & 7 ) << 32 ) | ( (uint64_t)( rules & 255 ) << 35 ) | ( 1ull << 43 );
    bits |= (uint64_t)codeCheck( bits ) << 45;
    std::string out;
    for ( int i = 9; i >= 0; --i )
    {
        out += Base32[( bits >> ( 5 * i ) ) & 31u];
        if ( i == 5 ) out += '-';
    }
    return out;
}

bool readChallenge( const std::string &code, uint32_t &seed, int &diff, int &rules )
{
    uint64_t bits = 0;
    int      n = 0;
    for ( char c : code )
    {
        c = (char)toupper( (unsigned char)c );
        if ( c == ' ' || c == '-' ) continue;
        if ( c == 'O' ) c = '0';
        if ( c == 'I' || c == 'L' ) c = '1';
        const char *at = strchr( Base32, c );
        if ( !c || !at ) return false;
        bits = ( bits << 5 ) | (uint64_t)( at - Base32 );
        if ( ++n > 10 ) return false;
    }
    if ( n != 10 || ( ( bits >> 43 ) & 3u ) != 1u ) return false;
    if ( (int)( ( bits >> 45 ) & 31u ) != codeCheck( bits & ( ( 1ull << 45 ) - 1 ) ) ) return false;
    seed = (uint32_t)( bits & 0xffffffffu );
    diff = (int)( ( bits >> 32 ) & 7u );
    rules = (int)( ( bits >> 35 ) & 255u );
    return true;
}

int weeklyRules( uint32_t seed )
{
    int a = (int)( seed % 8u ), b = (int)( ( seed >> 8 ) % 7u );
    if ( b >= a ) ++b;
    return ( 1 << a ) | ( 1 << b );
}

}   // namespace zenith
