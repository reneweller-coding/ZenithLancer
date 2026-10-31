/**
 * @file Sim.cpp
 * @brief The game's rules: the timeline, the paths, the fire patterns, the guns, hits and pickups.
 * @author Rene Weller
 */
#include "Sim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace zenith
{

namespace
{

/** @brief The paths (PathState::m_kind). */
enum PathKindE : int
{
    P_STRAIGHT, P_DIVE, P_SINE, P_SWOOP, P_UTURN, P_HOVER, P_STRAFE, P_ZIGZAG, P_CHASE, P_ORBIT, P_LOOP, P_RISE,
    P_DRIFT, P_BOSS, P_SPLINE, P_GROUND, P_STILL, P_ARC
};

int pathKind( const std::string &n )
{
    static const char *names[] = { "straight", "dive",  "sine",  "swoop", "uturn", "hover",  "strafe",
                                   "zigzag",   "chase", "orbit", "loop",  "rise",  "drift",  "boss",
                                   "spline",   "ground", "still", "arc" };
    for ( int i = 0; i < (int)( sizeof( names ) / sizeof( names[0] ) ); ++i )
        if ( n == names[i] ) return i;
    return P_STRAIGHT;
}

/** @brief A point on a Catmull-Rom spline through pts at t in [0, 1]. */
Vector2 spline( const std::vector<Vector2> &pts, float t )
{
    if ( pts.empty() ) return { 0, 0 };
    if ( pts.size() == 1 ) return pts[0];
    float f = clampf( t, 0.0f, 1.0f ) * ( pts.size() - 1 );
    int   i = std::min( (int)f, (int)pts.size() - 2 );
    float u = f - i;
    auto  P = [&]( int k ) { return pts[(size_t)std::clamp( k, 0, (int)pts.size() - 1 )]; };
    Vector2 p0 = P( i - 1 ), p1 = P( i ), p2 = P( i + 1 ), p3 = P( i + 2 );
    float   u2 = u * u, u3 = u2 * u;
    auto    c = [&]( float a, float b, float cc, float d ) {
        return 0.5f * ( 2 * b + ( -a + cc ) * u + ( 2 * a - 5 * b + 4 * cc - d ) * u2 + ( -a + 3 * b - 3 * cc + d ) * u3 );
    };
    return { c( p0.x, p1.x, p2.x, p3.x ), c( p0.y, p1.y, p2.y, p3.y ) };
}

/** @brief Bullet looks: radius and damage by kind. */
struct BulletKind
{
    const char *m_name;
    float       m_radius;
    float       m_damage;
};
const BulletKind EnemyBullets[] = { { "orb", 6, 6 },     { "needle", 4, 5 }, { "big", 11, 12 }, { "ring", 8, 8 },
                                    { "missile", 6, 10 }, { "mine", 9, 10 }, { "shard", 5, 5 }, { "wave", 7, 7 } };

}   // namespace

Loadout midLoadout( const Catalog &cat, int episode )
{
    static const char *mid[7] = {
        "",
        "ship=lancer,front=pulse:5,rear=tailgun:2,left=pip,right=-,gen=dynamo,shield=ward,special=nova_s",
        "ship=bastion,front=hornet:5,rear=sweep:3,left=glimmer,right=pip,gen=fusion,shield=mantle,special=drones",
        "ship=corsair,front=prism:5,rear=orbiter:4,left=warden,right=lancet,gen=plasmaheart,shield=fortress,special=timeslow",
        "ship=warhawk,front=helix:6,rear=halo:4,left=hydra,right=phoenix,gen=starforge,shield=prismshield,special=supernova",
        "ship=seraphim,front=tempest:7,rear=hailstorm:5,left=seraph,right=mirror,gen=singularity,shield=halo_s,special=lance",
        "ship=seraphim,front=tempest:9,rear=hailstorm:7,left=seraph,right=mirror,gen=singularity,shield=halo_s,special=lance" };
    Loadout o;
    auto    ep = cat.m_episodes.find( episode );
    if ( ep != cat.m_episodes.end() ) o = ep->second.m_loadout;
    if ( episode >= 1 && episode <= 6 ) parseLoadout( o, mid[episode] );
    // Goods this data does not know fall back to the episode's own.
    if ( ep != cat.m_episodes.end() )
    {
        const Loadout &e = ep->second.m_loadout;
        if ( !cat.ship( o.m_ship ) ) o.m_ship = e.m_ship;
        if ( !cat.weapon( o.m_front ) ) o.m_front = e.m_front;
        if ( !o.m_rear.empty() && !cat.weapon( o.m_rear ) ) o.m_rear = e.m_rear;
        if ( !o.m_sideL.empty() && !cat.sidekick( o.m_sideL ) ) o.m_sideL = e.m_sideL;
        if ( !o.m_sideR.empty() && !cat.sidekick( o.m_sideR ) ) o.m_sideR = e.m_sideR;
        if ( !cat.generator( o.m_gen ) ) o.m_gen = e.m_gen;
        if ( !cat.shield( o.m_shield ) ) o.m_shield = e.m_shield;
        if ( !o.m_special.empty() && !cat.special( o.m_special ) ) o.m_special = e.m_special;
    }
    return o;
}

LevelDef bossRush( const Catalog &cat )
{
    LevelDef rush;
    rush.m_name = "bossrush";
    rush.m_title = "Bossjagd";
    rush.m_theme = "space";
    rush.m_music = "boss";
    rush.m_scroll = 40;
    // The great bosses: those of each episode's middle, its last but one and its last mission, in order, each
    // episode over its own ground.
    std::vector<std::pair<const EnemyDef *, std::string>> bosses;
    const char *grounds[] = { "space", "foundry", "ocean", "asteroid", "jungle", "corona", "e6_storm" };
    for ( const auto &[id, ep] : cat.m_episodes )
        for ( const char *m : { "_05", "_09", "_10" } )
            if ( const LevelDef *lv = cat.level( "e" + std::to_string( id ) + m ) )
                for ( const LevelEvent &ev : lv->m_events )
                    if ( ev.m_type == "boss" )
                        if ( const EnemyDef *b = cat.enemy( ev.m_p.arg( 0 ) ) )
                        {
                            bool seen = false;
                            for ( const auto &have : bosses ) seen = seen || have.first == b;
                            if ( !seen ) bosses.push_back( { b, id >= 1 && id <= 6 ? grounds[id] : "space" } );
                        }
    float       t = 2.0f;
    std::string ground = "space";
    for ( const auto &[b, place] : bosses )
    {
        if ( place != ground )
        {
            LevelEvent th;
            th.m_t = t;
            th.m_type = "theme";
            th.m_p.m_key = "theme";
            th.m_p.m_args = { place };
            rush.m_events.push_back( th );
            ground = place;
        }
        LevelEvent w;
        w.m_t = t;
        w.m_type = "warn";
        w.m_p.m_key = "warn";
        rush.m_events.push_back( w );
        LevelEvent ev;
        ev.m_t = t + 2.0f;
        ev.m_type = "boss";
        ev.m_p.m_key = "boss";
        ev.m_p.m_args = { b->m_name };
        rush.m_events.push_back( ev );
        LevelEvent pk;
        pk.m_t = t + 4.0f;
        pk.m_type = "pickup";
        pk.m_p.m_key = "pickup";
        pk.m_p.m_args = { "shield" };
        rush.m_events.push_back( pk );
        t += 6.0f;
    }
    LevelEvent end;
    end.m_t = t;
    end.m_type = "end";
    end.m_p.m_key = "end";
    rush.m_events.push_back( end );
    rush.m_length = t + 5.0f + 90.0f * bosses.size();   // the bosses hold the timeline: room for the fights
    return rush;
}

int spriteOf( const std::string &name )
{
    static const char *names[] = { "orb",   "needle", "big",   "ring",  "missile", "mine",  "shard",
                                   "wave",  "pulse",  "bolt",  "laser", "plasma",  "rocket", "flame",
                                   "spark", "star",   "ripple", "bomb", "rail",    "petal", "needle_p", "orbit" };
    for ( int i = 0; i < (int)( sizeof( names ) / sizeof( names[0] ) ); ++i )
        if ( name == names[i] ) return i;
    return 8;
}

void World::begin( const Catalog *cat, const LevelDef *level, ModeE mode, int difficulty, uint32_t seed )
{
    *this = World();
    m_cat = cat;
    m_level = level;
    m_mode = mode;
    m_diffIndex = difficulty;
    m_diff = cat->difficulty( difficulty );
    m_rng.seed( seed ^ ( level ? level->m_seed : 7u ) );
    m_scroll = m_scrollGoal = level ? level->m_scroll : 60.0f;
    m_noTimeline = level == nullptr;
    if ( level ) m_music = level->m_music;
    m_srng.seed( seed * 2246822519u ^ ( level ? level->m_seed : 7u ) ^ 0x5c3e7u );
    if ( level ) setStructures( level->m_theme, true );
    m_swarmEp = nullptr;
    if ( level && level->m_name != "survival" && ( mode == ModeE::CAMPAIGN || mode == ModeE::ARCADE ) )
    {
        auto it = m_cat->m_episodes.find( level->m_episode );
        if ( it != m_cat->m_episodes.end() && !it->second.m_swarm.empty() && it->second.m_swarmEvery > 0 ) m_swarmEp = &it->second;
    }
    m_swarmT = m_swarmEp ? 6.0f : 0.0f;
    // The harder difficulties grow into their full strength over the first two episodes, while the workshop's gear
    // is still small (the autopilot lost episode 1's boss on Impossible every time, 30.10.2026).
    if ( level && level->m_episode <= 2 && level->m_name != "survival" && level->m_name != "bossrush" )
    {
        float ease = level->m_episode == 1 ? 0.6f : 0.85f;
        for ( float *v : { &m_diff.m_hp, &m_diff.m_fire, &m_diff.m_damage } )
            if ( *v > 1.0f ) *v = 1.0f + ( *v - 1.0f ) * ease;
    }
}

void World::addPlayer( int i, const Loadout &load, int lives )
{
    Player &p = m_players[i];
    p = Player();
    p.m_active = true;
    p.m_load = load;
    p.m_lives = lives;
    p.m_pos = { FieldW * ( i == 0 ? ( m_players[1].m_active ? 0.4f : 0.5f ) : 0.6f ), FieldH - 110.0f };
    refit( i );
    p.m_armor = p.m_armorMax;
    p.m_shield = p.m_shieldDef ? p.m_shieldDef->m_max : 0.0f;
    p.m_energy = p.m_energyMax;
    p.m_charges = p.m_special ? p.m_special->m_charges : 0;
    p.m_invuln = 1.5f;
}

void World::setPlus( int plus )
{
    // Each round finished: tougher hulls, more and faster fire, harder hits, and the money to answer it.
    m_plus = std::max( 0, plus );
    float k = (float)m_plus;
    m_diff.m_hp *= 1.0f + 0.35f * k;
    m_diff.m_fire *= 1.0f + 0.2f * k;
    m_diff.m_speed *= 1.0f + 0.06f * k;
    m_diff.m_damage *= 1.0f + 0.15f * k;
    m_diff.m_credits *= 1.0f + 0.25f * k;
    if ( m_plus >= 2 ) m_diff.m_revenge = true;
}

void World::setRules( int rules )
{
    m_rules = rules;
    if ( rule( RuleE::DOUBLE ) ) m_diff.m_fire *= 2.0f;
    if ( rule( RuleE::FAST ) ) m_diff.m_speed *= 1.33f;
    if ( rule( RuleE::TOUGH ) ) m_diff.m_hp *= 1.6f;
}

void World::refit( int i )
{
    Player        &p = m_players[i];
    const Loadout &o = p.m_load;
    p.m_ship = m_cat->ship( o.m_ship );
    if ( !p.m_ship && !m_cat->m_ships.empty() ) p.m_ship = &m_cat->m_ships.begin()->second;
    p.m_front = m_cat->weapon( o.m_front );
    p.m_rear = o.m_rear.empty() ? nullptr : m_cat->weapon( o.m_rear );
    p.m_gen = m_cat->generator( o.m_gen );
    p.m_shieldDef = m_cat->shield( o.m_shield );
    p.m_special = o.m_special.empty() ? nullptr : m_cat->special( o.m_special );
    const std::string *sides[2] = { &o.m_sideL, &o.m_sideR };
    for ( int s = 0; s < 2; ++s )
    {
        const SidekickDef *d = sides[s]->empty() ? nullptr : m_cat->sidekick( *sides[s] );
        if ( p.m_side[s].m_def != d )
        {
            p.m_side[s] = Sidekick();
            p.m_side[s].m_def = d;
            p.m_side[s].m_ammo = d ? d->m_ammo : 0;
            p.m_side[s].m_pos = p.m_pos;
        }
        p.m_side[s].m_side = s == 0 ? -1 : 1;
    }
    p.m_armorMax = p.m_ship ? p.m_ship->m_armor : 20.0f;
    p.m_energyMax = p.m_gen ? p.m_gen->m_capacity : 50.0f;
}

void World::emit( EvE t, Vector2 at, float a, float b, const std::string &name, Color c )
{
    SimEvent e;
    e.m_type = t;
    e.m_pos = at;
    e.m_a = a;
    e.m_b = b;
    e.m_name = name;
    e.m_color = c;
    m_events.push_back( e );
}

int World::boss() const
{
    for ( size_t i = 0; i < m_enemies.size(); ++i )
        if ( m_enemies[i].m_def->m_kind == EnemyKindE::BOSS && !m_enemies[i].m_dead ) return (int)i;
    return -1;
}

void World::markShut()
{
    m_shut.assign( m_enemies.size(), 0 );
    for ( const Enemy &pe : m_enemies )
        if ( pe.m_parent >= 0 && pe.m_parent < (int)m_enemies.size() && !pe.m_dead && !pe.m_gone &&
             m_enemies[(size_t)pe.m_parent].m_def->m_shielded )
            m_shut[(size_t)pe.m_parent] = 1;
}

int World::nearestEnemy( Vector2 from, float range, int skip ) const
{
    int   best = -1;
    float bd = range * range;
    for ( size_t i = 0; i < m_enemies.size(); ++i )
    {
        const Enemy &e = m_enemies[i];
        if ( (int)i == skip || e.m_dead || e.m_gone || e.m_rise > 0 || e.m_def->m_invulnerable ||
             ( e.m_def->m_scenery && e.m_def->m_fire.empty() ) )
            continue;
        // Seeking weapons leave a shielded body alone while its parts live: they go for the parts.
        if ( i < m_shut.size() && m_shut[i] ) continue;
        if ( e.m_pos.y < -20 || e.m_pos.y > FieldH + 10 || e.m_pos.x < -10 || e.m_pos.x > FieldW + 10 ) continue;
        float d = dist2( from, e.m_pos );
        if ( d < bd )
        {
            bd = d;
            best = (int)i;
        }
    }
    return best;
}

int World::nearestPlayer( Vector2 from ) const
{
    int   best = -1;
    float bd = 1e12f;
    for ( int i = 0; i < 2; ++i )
    {
        const Player &p = m_players[i];
        if ( !p.m_active || !p.m_alive ) continue;
        float d = dist2( from, p.m_pos );
        if ( d < bd )
        {
            bd = d;
            best = i;
        }
    }
    return best;
}

Vector2 World::worldPos( const Enemy &e, Vector2 off ) const
{
    // The enemy's frame: +z is its nose (its heading), +x its right side.
    Vector2 fwd = dirOf( e.m_face );
    Vector2 right = { -fwd.y, fwd.x };
    float   s = ( e.m_ground ? GroundFactor : 1.0f ) * e.m_def->m_scale;
    return { e.m_pos.x + ( right.x * off.x + fwd.x * off.y ) * s, e.m_pos.y + ( right.y * off.x + fwd.y * off.y ) * s };
}

void World::setStructures( const std::string &theme, bool fill )
{
    m_structSets.clear();
    const Block *b = m_cat ? m_cat->theme( theme ) : nullptr;
    if ( !b ) return;
    for ( const Line &l : b->m_lines )
        if ( l.m_key == "structures" )
        {
            StructureSet s;
            s.m_every = std::max( 10.0f, l.num( "every", 110.0f ) );
            s.m_group = std::max( 1, (int)l.num( "group", 3.0f ) );
            for ( const std::string &n : l.m_args )
                if ( const EnemyDef *d = m_cat->enemy( n ) ) s.m_defs.push_back( d );
            s.m_dist = s.m_every * 0.3f;
            if ( s.m_defs.empty() ) continue;
            // At a mission's start the ground the field shows is built up already, as the ground further on will be;
            // guns only in its upper part, away from the pilots.
            bool armed = false;
            for ( const EnemyDef *d : s.m_defs ) armed = armed || !d->m_fire.empty();
            if ( fill )
                for ( float y = armed ? FieldH * 0.4f : FieldH - 30.0f; y > -40.0f; y -= s.m_every * m_srng.range( 0.6f, 1.4f ) )
                    spawnStructures( s, y );
            m_structSets.push_back( s );
        }
}

void World::spawnStructures( StructureSet &s, float y )
{
    // A group of them at y (just above the field's top edge as the ground comes), turned at random; none on another or
    // on a ground unit there.
    int n = 1 + (int)m_srng.below( s.m_group );
    for ( int tries = 0, placed = 0; placed < n && tries < 3 * n + 3; ++tries )
    {
        const EnemyDef *d = s.m_defs[(size_t)m_srng.below( (int)s.m_defs.size() )];
        Vector2         at = { m_srng.range( 36.0f, FieldW - 36.0f ), y - m_srng.range( 0.0f, 50.0f ) };
        bool            free = true;
        for ( const Enemy &e : m_enemies )
        {
            if ( e.m_dead || e.m_gone || !e.m_ground || std::fabs( e.m_pos.y - y ) > 120.0f ) continue;
            float r = ( d->m_radius * d->m_scale + e.m_def->m_radius * e.m_def->m_scale ) * GroundFactor * 0.9f;
            if ( dist2( e.m_pos, at ) < r * r )
            {
                free = false;
                break;
            }
        }
        if ( !free ) continue;
        ++placed;
        int i = spawn( d, at, nullptr );
        if ( i >= 0 ) m_enemies[(size_t)i].m_face = m_enemies[(size_t)i].m_head = 90.0f * (float)m_srng.below( 4 ) + m_srng.range( -12.0f, 12.0f );
    }
}

void World::spawnSwarm()
{
    // A squad of one kind, one after another along its own path, from a random place along the top (swoops from a
    // random side). Their own random numbers: the waves' stay as they were.
    const EpisodeDef &ep = *m_swarmEp;
    const EnemyDef   *d = m_cat->enemy( ep.m_swarm[(size_t)m_srng.below( (int)ep.m_swarm.size() )] );
    if ( !d ) return;
    int   n = 3 + (int)m_srng.below( std::max( 1, ep.m_swarmGroup - 2 ) );
    float x = m_srng.range( 90.0f, FieldW - 90.0f );
    Line  path = d->m_path;
    path.m_kv["path"] = d->m_path.m_key;
    path.m_kv["x"] = std::to_string( x );
    if ( m_srng.below( 2 ) ) path.m_kv["side"] = path.num( "side", 1.0f ) > 0 ? "-1" : "1";
    for ( int i = 0; i < n; ++i )
    {
        Pending pd;
        pd.m_t = m_clock + 0.32f * (float)i;
        pd.m_def = d;
        pd.m_pos = { x, -40.0f };
        pd.m_path = path;
        m_pending.push_back( pd );
    }
}

void World::turn( int idx, float face )
{
    if ( idx < 0 || idx >= (int)m_enemies.size() ) return;
    m_enemies[(size_t)idx].m_face = m_enemies[(size_t)idx].m_head = face;
    // Parts follow their parent (parts of parts come after it in the list).
    for ( Enemy &e : m_enemies )
        if ( e.m_parent >= 0 && e.m_parent < (int)m_enemies.size() )
        {
            const Enemy &par = m_enemies[(size_t)e.m_parent];
            e.m_pos = worldPos( par, e.m_off );
            e.m_face = e.m_head = par.m_face;
        }
}

int World::spawn( const EnemyDef *def, Vector2 pos, const Line *pathLine, int parent )
{
    if ( !def ) return -1;
    Enemy e;
    e.m_def = def;
    e.m_id = m_nextId++;
    e.m_pos = e.m_base = pos;
    e.m_parent = parent;
    e.m_ground = def->m_kind == EnemyKindE::GROUND;
    e.m_hpMax = e.m_hp = def->m_hp * m_diff.m_hp;
    e.m_face = 0.0f;
    e.m_fireT.resize( def->m_fire.size() );
    e.m_fireN.assign( def->m_fire.size(), 0 );
    e.m_burstLeft.assign( def->m_fire.size(), 0 );
    for ( size_t f = 0; f < def->m_fire.size(); ++f )
        e.m_fireT[f] = def->m_fire[f].m_delay + m_rng.range( 0.0f, 0.25f ) * def->m_fire[f].m_every;

    std::string pname = pathLine && pathLine->has( "path" ) ? pathLine->str( "path" ) : def->m_path.m_key;
    PathState  &ps = e.m_path;
    ps.m_kind = pathKind( pname );
    // Values come from the wave's line first, then the enemy's own path line.
    auto val = [&]( const char *k, float def0 ) {
        if ( pathLine && pathLine->has( k ) ) return pathLine->num( k, def0 );
        return def->m_path.num( k, def0 );
    };
    ps.m_drive = ( pathLine && ( pathLine->has( "dir" ) || pathLine->has( "speed" ) ) ) || def->m_path.has( "dir" ) ||
                 def->m_path.has( "speed" );
    ps.m_speed = val( "speed", def->m_speed );
    ps.m_amp = val( "amp", 80.0f );
    ps.m_freq = val( "freq", 0.5f );
    ps.m_stop = val( "stop", def->m_kind == EnemyKindE::BOSS ? 170.0f : 160.0f );
    ps.m_wait = val( "wait", 3.0f );
    ps.m_dir = val( "dir", 0.0f );
    ps.m_turn = val( "turn", 90.0f );
    ps.m_cx = val( "cx", FieldW * 0.5f );
    ps.m_cy = val( "cy", 300.0f );
    ps.m_r = val( "r", 120.0f );
    ps.m_dur = val( "dur", 4.0f );
    ps.m_accel = val( "accel", 0.0f );
    ps.m_exit = val( "exit", 0.0f );
    ps.m_side = val( "side", 1.0f ) < 0 ? -1 : 1;
    ps.m_phase = val( "phase", 0.0f );
    std::string pts = pathLine && pathLine->has( "pts" ) ? pathLine->str( "pts" ) : def->m_path.str( "pts" );
    if ( !pts.empty() )
    {
        // "x,y;x,y;..." in field units.
        size_t a = 0;
        while ( a < pts.size() )
        {
            size_t  b = pts.find( ';', a );
            std::string one = pts.substr( a, b == std::string::npos ? std::string::npos : b - a );
            float   x = 0, y = 0;
            if ( sscanf( one.c_str(), "%f,%f", &x, &y ) == 2 ) ps.m_pts.push_back( { x, y } );
            if ( b == std::string::npos ) break;
            a = b + 1;
        }
        if ( !ps.m_pts.empty() ) e.m_pos = e.m_base = ps.m_pts[0];
    }
    // Where the path starts.
    switch ( ps.m_kind )
    {
    case P_SWOOP:
    case P_STRAFE:
        if ( !( pathLine && pathLine->has( "x" ) ) || ps.m_kind == P_STRAFE )
            e.m_pos.x = ps.m_side > 0 ? -50.0f : FieldW + 50.0f;
        if ( ps.m_kind == P_STRAFE ) e.m_pos.y = pathLine && pathLine->has( "y" ) ? pathLine->num( "y" ) : 150.0f;
        e.m_base = e.m_pos;
        break;
    case P_RISE:
        e.m_pos.y = FieldH + 50.0f;
        e.m_base = e.m_pos;
        break;
    case P_ORBIT: e.m_path.m_phase = std::atan2( pos.y - ps.m_cy, pos.x - ps.m_cx ); break;
    default: break;
    }
    if ( pathLine && pathLine->has( "face" ) ) e.m_faceAs = pathLine->str( "face" );
    const std::string &faceMode = e.m_faceAs.empty() ? def->m_face : e.m_faceAs;
    if ( faceMode == "fixed" ) e.m_face = ps.m_dir;
    if ( faceMode == "up" || ps.m_kind == P_RISE ) e.m_face = 180.0f;
    if ( parent >= 0 )
    {
        e.m_ground = m_enemies[parent].m_ground;
        e.m_path.m_kind = P_STILL;
        e.m_face = m_enemies[parent].m_face;
    }
    e.m_head = e.m_face;
    int idx = (int)m_enemies.size();
    m_enemies.push_back( e );
    if ( def->m_swarm ) ++m_stats.m_swarm;
    else if ( ( def->m_kind != EnemyKindE::OBSTACLE || !def->m_invulnerable ) && !def->m_scenery ) ++m_stats.m_spawned;
    for ( const PartRef &pr : def->m_parts )
    {
        const EnemyDef *pd = m_cat->enemy( pr.m_enemy );
        if ( !pd ) continue;
        int pi = spawn( pd, pos, nullptr, idx );
        if ( pi >= 0 )
        {
            m_enemies[(size_t)pi].m_off = { pr.m_x, pr.m_z };
            m_enemies[(size_t)pi].m_lift = pr.m_y * def->m_scale;
        }
    }
    if ( def->m_kind == EnemyKindE::BOSS )
    {
        m_bossTitle = def->m_title.empty() ? def->m_name : def->m_title;
        m_bossTitleEn = def->m_titleEn;
        m_hold = true;
    }
    return idx;
}

void World::spawnWave( const Line &p, bool ground )
{
    const EnemyDef *def = m_cat->enemy( p.arg( 0 ) );
    if ( !def ) return;
    int         n = std::max( 1, p.integer( "n", 1 ) );
    std::string form = p.str( "form", n > 1 ? "column" : "single" );
    float       x = p.num( "x", FieldW * 0.5f );
    float       y = p.num( "y", ground ? -60.0f : -40.0f );
    float       spacing = p.num( "spacing", 60.0f );
    float       gap = p.num( "gap", 0.4f );
    float       hp = p.num( "hp", 1.0f );
    bool        mirror = p.integer( "mirror", 0 ) != 0;
    int         wave = m_waves++;
    m_waveLeft.push_back( 0 );
    m_waveKilled.push_back( 0 );
    m_waveSize.push_back( 0 );
    m_waveLast.push_back( { x, 0 } );
    DropDef wd;
    if ( p.has( "drop" ) )
    {
        wd.m_what = p.str( "drop" );
        wd.m_item = p.str( "item" );
        wd.m_value = p.integer( "value", 0 );
    }
    m_waveDrop.push_back( wd );
    m_waveBonus.push_back( p.integer( "bonus", -1 ) );

    Rng r;
    r.seed( (uint32_t)p.integer( "seed", 1 ) * 7919u + m_rng.next() % 3u );
    auto add = [&]( Vector2 at, float delay, bool flip ) {
        Pending pd;
        pd.m_t = m_clock + delay;
        pd.m_def = def;
        pd.m_pos = at;
        pd.m_path = p;
        pd.m_path.m_kv["path"] = p.str( "path", def->m_path.m_key );
        if ( flip )
        {
            pd.m_pos.x = FieldW - at.x;
            float side = p.num( "side", 1.0f );
            pd.m_path.m_kv["side"] = side < 0 ? "1" : "-1";
            if ( p.has( "cx" ) ) pd.m_path.m_kv["cx"] = std::to_string( FieldW - p.num( "cx" ) );
            if ( p.has( "dir" ) ) pd.m_path.m_kv["dir"] = std::to_string( -p.num( "dir" ) );
            if ( p.has( "pts" ) )
            {
                std::string out, pts = p.str( "pts" );
                size_t      a = 0;
                while ( a < pts.size() )
                {
                    size_t      b = pts.find( ';', a );
                    std::string one = pts.substr( a, b == std::string::npos ? std::string::npos : b - a );
                    float       px = 0, py = 0;
                    if ( sscanf( one.c_str(), "%f,%f", &px, &py ) == 2 )
                    {
                        char buf[64];
                        snprintf( buf, sizeof( buf ), "%s%g,%g", out.empty() ? "" : ";", FieldW - px, py );
                        out += buf;
                    }
                    if ( b == std::string::npos ) break;
                    a = b + 1;
                }
                pd.m_path.m_kv["pts"] = out;
            }
        }
        pd.m_wave = wave;
        pd.m_hp = hp;
        m_pending.push_back( pd );
        ++m_waveLeft[(size_t)wave];
        ++m_waveSize[(size_t)wave];
    };
    for ( int k = 0; k < ( mirror ? 2 : 1 ); ++k )
    {
        bool flip = k == 1;
        for ( int i = 0; i < n; ++i )
        {
            float   c = i - ( n - 1 ) * 0.5f;
            Vector2 at = { x, y };
            float   delay = 0.0f;
            if ( form == "line" ) at.x = x + c * spacing;
            else if ( form == "v" )
            {
                at.x = x + c * spacing;
                at.y = y - std::fabs( c ) * spacing * 0.6f;
            }
            else if ( form == "arc" )
            {
                float a = ( n > 1 ? c / ( ( n - 1 ) * 0.5f ) : 0.0f ) * 60.0f * Deg;
                at.x = x + std::sin( a ) * spacing * n * 0.5f;
                at.y = y - ( 1.0f - std::cos( a ) ) * spacing * 1.5f;
            }
            else if ( form == "random" )
            {
                float range = p.num( "range", FieldW * 0.8f );
                at.x = x + r.range( -range * 0.5f, range * 0.5f );
                delay = i * gap;
            }
            else if ( form == "sides" )
            {
                at.x = ( i % 2 == 0 ) ? x : FieldW - x;
                delay = i * gap;
            }
            else if ( form == "grid" )
            {
                int cols = std::max( 1, p.integer( "cols", n ) );
                int row = i / cols, col = i % cols;
                at.x = x + ( col - ( cols - 1 ) * 0.5f ) * spacing;
                at.y = y - row * p.num( "dy", spacing );
            }
            else   // column, single
                delay = i * gap;
            delay += p.num( "delay", 0.0f );
            add( at, delay, flip );
        }
    }
    (void)ground;
}

void World::runEvent( const LevelEvent &ev )
{
    if ( m_diffIndex < ev.m_minDiff || m_diffIndex > ev.m_maxDiff ) return;
    const Line &p = ev.m_p;
    const std::string &t = ev.m_type;
    if ( t == "wave" || t == "ground" || t == "obstacle" ) spawnWave( p, t == "ground" );
    else if ( t == "boss" )
    {
        const EnemyDef *d = m_cat->enemy( p.arg( 0 ) );
        if ( d )
        {
            Line pl = p;
            pl.m_kv["path"] = p.str( "path", d->m_path.m_key );
            int   first = spawn( d, { p.num( "x", FieldW * 0.5f ), p.num( "y", -120.0f ) }, &pl );
            float hp = p.num( "hp", 1.0f );
            // hp=: the boss and its parts (all spawned with it) hold that much more or less.
            if ( first >= 0 && hp != 1.0f )
                for ( size_t i = (size_t)first; i < m_enemies.size(); ++i )
                {
                    m_enemies[i].m_hp *= hp;
                    m_enemies[i].m_hpMax *= hp;
                }
            m_hold = true;
            emit( EvE::BOSSSTART, { FieldW * 0.5f, 0 }, 0, 0, d->m_name );
        }
    }
    else if ( t == "pickup" )
    {
        std::string k = p.arg( 0 );
        PickE       kind = k == "power"    ? PickE::POWER
                         : k == "shield"   ? PickE::SHIELD
                         : k == "armor"    ? PickE::ARMOR
                         : k == "weapon"   ? PickE::WEAPON
                         : k == "rear"     ? PickE::REAR
                         : k == "side"     ? PickE::SIDE
                         : k == "life"     ? PickE::LIFE
                         : k == "special"  ? PickE::SPECIAL
                         : k == "cube"     ? PickE::CUBE
                         : k == "key"      ? PickE::KEY
                         : k == "plan"     ? PickE::PLAN
                                           : PickE::CREDITS;
        if ( kind == PickE::LIFE && m_mode != ModeE::ARCADE ) kind = PickE::CREDITS;
        dropAt( kind, { p.num( "x", FieldW * 0.5f ), p.num( "y", -20.0f ) }, p.integer( "value", 500 ), p.str( "item" ) );
    }
    else if ( t == "text" ) emit( EvE::TEXT, { 0, 0 }, 0, 0, p.arg( 0 ) );
    else if ( t == "scroll" ) m_scrollGoal = p.argNum( 0, m_scrollGoal );
    else if ( t == "music" )
    {
        m_music = p.arg( 0 );
        emit( EvE::MUSIC, { 0, 0 }, 0, 0, m_music );
    }
    else if ( t == "warn" ) emit( EvE::WARN, { FieldW * 0.5f, FieldH * 0.4f }, 0, 0, p.arg( 0 ) );
    else if ( t == "theme" )
    {
        emit( EvE::THEME, { 0, 0 }, 0, 0, p.arg( 0 ) );
        setStructures( p.arg( 0 ) );
    }
    else if ( t == "clear" )
    {
        for ( Bullet &b : m_bullets )
            if ( !b.m_player ) b.m_dead = true;
    }
    else if ( t == "end" ) m_endT = p.argNum( 0, 3.0f );
}

void World::runTimeline()
{
    if ( m_noTimeline || !m_level ) return;
    const std::vector<LevelEvent> &evs = m_level->m_events;
    if ( m_freeDone.size() != evs.size() ) m_freeDone.assign( evs.size(), 0 );
    if ( !m_hold ) m_time += Dt * m_slow;
    while ( m_next < evs.size() && evs[m_next].m_t <= m_time && !m_hold )
    {
        size_t i = m_next++;
        if ( m_freeDone[i] ) continue;
        runEvent( evs[i] );
        if ( m_hold && evs[i].m_type == "boss" )
            // What follows with hold=0 runs on the clock while the boss lives (radio lines during the fight).
            for ( size_t j = i + 1; j < evs.size(); ++j )
                if ( evs[j].m_p.str( "hold" ) == "0" && !m_freeDone[j] )
                {
                    m_free.push_back( { m_clock + ( evs[j].m_t - evs[i].m_t ), j } );
                    m_freeDone[j] = 1;
                }
    }
    for ( size_t k = 0; k < m_free.size(); )
        if ( m_free[k].first <= m_clock )
        {
            runEvent( evs[m_free[k].second] );
            m_free.erase( m_free.begin() + (long)k );
        }
        else
            ++k;
    // The pending ships of columns.
    for ( size_t i = 0; i < m_pending.size(); )
    {
        if ( m_pending[i].m_t <= m_clock )
        {
            Pending pd = m_pending[i];
            m_pending.erase( m_pending.begin() + (long)i );
            int idx = spawn( pd.m_def, pd.m_pos, &pd.m_path );
            if ( idx >= 0 )
            {
                Enemy &e = m_enemies[(size_t)idx];
                e.m_wave = pd.m_wave;
                e.m_hp *= pd.m_hp;
                e.m_hpMax *= pd.m_hp;
                if ( pd.m_rise > 0 )
                {
                    e.m_rise = pd.m_rise;
                    e.m_pos = e.m_base = pd.m_pos;
                    e.m_entered = true;
                    // The pad's fliers are extra: whoever wrecks the pad in time never meets them.
                    e.m_extra = true;
                    if ( !e.m_def->m_swarm )
                    {
                        --m_stats.m_spawned;
                        ++m_stats.m_swarm;
                    }
                }
            }
        }
        else
            ++i;
    }
    // The end: after "end", or when the timeline has run out, or past its length with nothing left.
    bool eventsDone = m_next >= m_level->m_events.size();
    if ( m_endT < 0 && eventsDone && !m_hold && m_pending.empty() )
    {
        bool any = false;
        for ( const Enemy &e : m_enemies )
            if ( !e.m_dead && !e.m_gone && e.m_def->m_kind != EnemyKindE::OBSTACLE && !e.m_def->m_scenery && !e.m_def->m_swarm &&
                 e.m_pos.y < FieldH )
                any = true;
        if ( !any || m_time > m_level->m_length + 20.0f ) m_endT = 3.0f;
    }
    if ( m_endT >= 0 )
    {
        m_endT -= Dt;
        if ( m_endT <= 0 ) m_finished = true;
    }
}

void World::step( const PlayerInput in[2] )
{
    m_events.clear();
    if ( m_finished || m_failed ) return;
    m_clock += Dt;
    m_stats.m_time = m_clock;
    if ( m_slowT > 0 )
    {
        m_slowT -= Dt;
        m_slow = m_slowT > 0 ? 0.4f : 1.0f;
    }
    m_scroll += clampf( m_scrollGoal - m_scroll, -40.0f * Dt, 40.0f * Dt );
    m_scrollPos += m_scroll * Dt;
    if ( m_swarmEp && !m_hold && m_endT < 0 && m_next < m_level->m_events.size() && m_time < m_level->m_length - 12.0f )
    {
        m_swarmT -= Dt;
        if ( m_swarmT <= 0 )
        {
            spawnSwarm();
            m_swarmT = m_swarmEp->m_swarmEvery * m_srng.range( 0.6f, 1.4f );
        }
    }
    if ( m_level )
        for ( StructureSet &s : m_structSets )
        {
            s.m_dist -= m_scroll * GroundFactor * Dt;
            while ( s.m_dist <= 0.0f )
            {
                spawnStructures( s );
                s.m_dist += s.m_every * m_srng.range( 0.6f, 1.4f );
            }
        }
    runTimeline();
    markShut();
    for ( int i = 0; i < 2; ++i )
    {
        if ( !m_players[i].m_active ) continue;
        movePlayer( i, in[i] );
        moveSidekicks( i, in[i] );
        firePlayer( i, in[i] );
    }
    moveEnemies();
    markShut();
    for ( size_t i = 0; i < m_enemies.size(); ++i ) fireEnemy( (int)i );
    moveBullets();
    moveLasers();
    movePickups();
    collide();
    for ( Player &p : m_players )
        if ( p.m_chainT > 0.0f && ( p.m_chainT -= Dt ) <= 0.0f ) p.m_chain = 0;
    compact();
    markShut();
    m_bullets.insert( m_bullets.end(), m_born.begin(), m_born.end() );
    m_born.clear();
    int b = boss();
    float goal = b >= 0 ? m_enemies[(size_t)b].m_hp / std::max( 1.0f, m_enemies[(size_t)b].m_hpMax ) : 0.0f;
    m_bossShown += ( goal - m_bossShown ) * std::min( 1.0f, Dt * 6.0f );
    // All pilots out?
    bool anyAlive = false, anyLives = false;
    for ( const Player &p : m_players )
        if ( p.m_active )
        {
            anyAlive = anyAlive || p.m_alive;
            anyLives = anyLives || p.m_lives > 0;
        }
    if ( !anyAlive && !anyLives )
    {
        bool waited = true;
        for ( const Player &p : m_players )
            if ( p.m_active && p.m_dead < 2.0f ) waited = false;
        if ( waited ) m_failed = true;
    }
}

// ---------------------------------------------------------------------------------------------------- the pilots

void World::movePlayer( int i, const PlayerInput &in )
{
    Player &p = m_players[i];
    if ( !p.m_alive )
    {
        p.m_dead += Dt;
        // Arcade: back after a breath, without harm for a while.
        if ( p.m_lives > 0 && p.m_dead > 1.6f )
        {
            --p.m_lives;
            revive( p );
        }
        // The campaign for two: a pilot shot down comes back after a while, as long as the other one still flies.
        const Player &o = m_players[1 - i];
        if ( m_mode == ModeE::CAMPAIGN && o.m_active && o.m_alive && p.m_dead > 8.0f ) revive( p );
        return;
    }
    float   speed = p.m_ship ? p.m_ship->m_speed : 330.0f;
    float   accel = p.m_ship ? p.m_ship->m_accel : 2600.0f;
    if ( in.m_focus ) speed *= 0.45f;
    Vector2 want;
    if ( in.m_mouse )
    {
        Vector2 d = sub( in.m_target, p.m_pos );
        float   l = len( d );
        float   s = std::min( speed, l * 12.0f );
        want = l > 0.5f ? mul( d, s / l ) : Vector2{ 0, 0 };
    }
    else
    {
        Vector2 st = { in.m_x, in.m_y };
        float   l = len( st );
        if ( l > 1.0f ) st = mul( st, 1.0f / l );
        want = mul( st, speed );
    }
    Vector2 dv = sub( want, p.m_vel );
    float   l = len( dv ), maxDv = accel * Dt;
    if ( l > maxDv ) dv = mul( dv, maxDv / l );
    p.m_vel = add( p.m_vel, dv );
    p.m_pos = add( p.m_pos, mul( p.m_vel, Dt ) );
    p.m_pos.x = clampf( p.m_pos.x, 18.0f, FieldW - 18.0f );
    p.m_pos.y = clampf( p.m_pos.y, 40.0f, FieldH - 24.0f );
    p.m_bank += ( clampf( p.m_vel.x / std::max( 1.0f, speed ), -1, 1 ) - p.m_bank ) * std::min( 1.0f, Dt * 8.0f );
    if ( p.m_invuln > 0 ) p.m_invuln -= Dt;
    if ( p.m_barrier > 0 ) p.m_barrier -= Dt;
    if ( p.m_magnet > 0 ) p.m_magnet -= Dt;
    // The generator feeds the guns and the shield.
    float power = p.m_gen ? p.m_gen->m_power : 30.0f;
    if ( p.m_ship ) power *= 1.0f + p.m_ship->m_genBonus;
    p.m_energy = std::min( p.m_energyMax, p.m_energy + power * Dt );
    if ( rule( RuleE::NOSHIELD ) )
        p.m_shield = 0.0f;
    else if ( p.m_shieldDef )
    {
        if ( p.m_shieldWait > 0 )
            p.m_shieldWait -= Dt;
        else if ( p.m_shield < p.m_shieldDef->m_max )
        {
            float add0 = std::min( p.m_shieldDef->m_regen * Dt, p.m_shieldDef->m_max - p.m_shield );
            float cost = add0 * p.m_shieldDef->m_cost;
            // The shield takes what the guns leave over, but never the last fifth of the energy.
            if ( p.m_energy - cost > p.m_energyMax * 0.2f || !p.m_firing )
            {
                if ( p.m_energy >= cost )
                {
                    p.m_shield += add0;
                    p.m_energy -= cost;
                }
            }
        }
    }
    if ( in.m_mode && p.m_rear && p.m_rear->m_modes > 1 )
    {
        p.m_rearMode = p.m_rearMode == 1 ? 2 : 1;
        emit( EvE::MODE, p.m_pos, (float)p.m_rearMode );
    }
    if ( in.m_special ) useSpecial( i );
}

void World::useSpecial( int i )
{
    Player &p = m_players[i];
    if ( !p.m_special ) return;
    if ( p.m_charges <= 0 )
    {
        emit( EvE::EMPTY, p.m_pos );
        return;
    }
    --p.m_charges;
    const SpecialDef &s = *p.m_special;
    const std::string &k = s.m_kind;
    float time = s.m_p.num( "time", 4.0f );
    emit( EvE::NOVA, p.m_pos, s.m_p.num( "radius", 400.0f ), 0, k, s.m_p.color( "color", { 140, 220, 255, 255 } ) );
    if ( k == "nova" )
    {
        float r = s.m_p.num( "radius", 400.0f ), dmg = s.m_p.num( "power", 120.0f );
        for ( Bullet &b : m_bullets )
            if ( !b.m_player && dist2( b.m_pos, p.m_pos ) < r * r ) b.m_dead = true;
        for ( size_t e = 0; e < m_enemies.size(); ++e )
            if ( !m_enemies[e].m_dead && dist2( m_enemies[e].m_pos, p.m_pos ) < r * r ) hitEnemy( (int)e, dmg, i, m_enemies[e].m_pos );
        emit( EvE::SHAKE, p.m_pos, 0.8f );
    }
    else if ( k == "barrier" ) p.m_barrier = time;
    else if ( k == "drones" ) p.m_drones = time;
    else if ( k == "timeslow" ) m_slowT = time;
    else if ( k == "repair" ) p.m_armor = std::min( p.m_armorMax, p.m_armor + s.m_p.num( "power", p.m_armorMax * 0.5f ) );
    else if ( k == "magnet" ) p.m_magnet = time;
    else if ( k == "lance" ) p.m_drones = -time;   // negative: the lance (a wide beam) instead of drones
    else if ( k == "shield" )
    {
        // Overcharge: the shield and the generator full at once, and a moment without harm.
        p.m_shield = p.m_shieldDef ? p.m_shieldDef->m_max : 0.0f;
        p.m_energy = p.m_energyMax;
        p.m_barrier = std::max( p.m_barrier, s.m_p.num( "time", 0.6f ) );
    }
    else if ( k == "missiles" )
    {
        // A salvo of homing missiles fanned out from the ship.
        int   n = std::max( 1, s.m_p.integer( "count", 12 ) );
        float dmg = s.m_p.num( "power", 30.0f );
        Color c = s.m_p.color( "color", { 255, 140, 80, 255 } );
        for ( int j = 0; j < n; ++j )
        {
            Bullet b;
            b.m_player = true;
            b.m_owner = i;
            b.m_pos = b.m_origin = p.m_pos;
            b.m_vel = mul( dirOf( 180.0f - 80.0f + 160.0f * j / std::max( 1, n - 1 ) ), 420.0f );
            b.m_accel = 700.0f;
            b.m_home = 330.0f;
            b.m_damage = dmg;
            b.m_radius = 4.0f;
            b.m_life = 2.2f;
            b.m_sprite = spriteOf( "rocket" );
            b.m_color = c;
            m_bullets.push_back( b );
        }
    }
    else if ( k == "mines" )
    {
        // A ring of mines round the ship that bursts after a moment.
        int   n = std::max( 1, s.m_p.integer( "count", 8 ) );
        float dmg = s.m_p.num( "power", 60.0f );
        Color c = s.m_p.color( "color", { 255, 90, 90, 255 } );
        for ( int j = 0; j < n; ++j )
        {
            Bullet b;
            b.m_player = true;
            b.m_owner = i;
            b.m_pos = b.m_origin = p.m_pos;
            b.m_vel = mul( dirOf( 360.0f * j / n ), 260.0f );
            b.m_drag = 2.2f;
            b.m_damage = dmg;
            b.m_explode = s.m_p.num( "radius", 90.0f );
            b.m_radius = 7.0f;
            b.m_life = s.m_p.num( "time", 1.6f );
            b.m_sprite = spriteOf( "bomb" );
            b.m_color = c;
            m_bullets.push_back( b );
        }
    }
}

void World::moveSidekicks( int i, const PlayerInput &in )
{
    Player &p = m_players[i];
    for ( Sidekick &s : p.m_side )
    {
        if ( !s.m_def ) continue;
        Vector2 goal;
        const std::string &place = s.m_def->m_place;
        if ( place == "orbit" )
        {
            float a = m_clock * 3.0f + ( s.m_side < 0 ? 0.0f : Pi );
            goal = { p.m_pos.x + std::cos( a ) * 46.0f, p.m_pos.y + std::sin( a ) * 46.0f };
        }
        else if ( place == "trail" )
            goal = { p.m_pos.x + s.m_side * 30.0f, p.m_pos.y + 46.0f };
        else if ( place == "front" )
            goal = { p.m_pos.x + s.m_side * 24.0f, p.m_pos.y - 40.0f };
        else
            goal = { p.m_pos.x + s.m_side * 38.0f, p.m_pos.y + 6.0f };
        float follow = place == "trail" ? 6.0f : 22.0f;
        s.m_pos = add( s.m_pos, mul( sub( goal, s.m_pos ), std::min( 1.0f, Dt * follow ) ) );
        if ( s.m_def->m_ammo > 0 && s.m_ammo < s.m_def->m_ammo && s.m_def->m_reload > 0 )
        {
            s.m_reload += Dt;
            if ( s.m_reload >= s.m_def->m_reload )
            {
                s.m_reload = 0;
                ++s.m_ammo;
            }
        }
        if ( s.m_def->m_charge > 0 ) s.m_charge = std::min( s.m_def->m_charge, s.m_charge + Dt );
    }
    (void)in;
}

void World::fireShots( int i, const std::vector<ShotDef> &shots, int level, int maxLevel, int mode, int volley,
                       Vector2 from, float baseDir, bool sidekick, const Sidekick *sk )
{
    Player &pl = m_players[i];
    (void)maxLevel;
    for ( const ShotDef &s : shots )
    {
        if ( level < s.m_from || level > s.m_to ) continue;
        if ( s.m_mode != 0 && s.m_mode != mode ) continue;
        const Line &p = s.m_p;
        int every = std::max( 1, p.integer( "every", 1 ) );
        if ( volley % every != 0 ) continue;
        float t = s.m_to > s.m_from ? ( level - s.m_from ) / (float)( s.m_to - s.m_from ) : 0.0f;
        auto  V = [&]( const char *k, float d ) { return p.range( k, d ).at( t ); };
        int   n = std::max( 1, (int)std::lround( V( "n", 1 ) ) );
        float dx = V( "dx", 10 ), spread = V( "spread", 0 ), angle = V( "angle", 0 );
        float speed = V( "speed", 820 ), dmg = V( "dmg", 4 ), size = V( "size", 4 );
        float life = V( "life", 1.3f ), home = V( "home", 0 ), amp = V( "amp", 0 ), freq = V( "freq", 0 );
        float radius = V( "radius", 0 ), accel = V( "accel", 0 ), drag = V( "drag", 0 ), jitter = V( "jitter", 0 );
        int   pierce = (int)std::lround( V( "pierce", 0 ) ), bounce = (int)std::lround( V( "bounce", 0 ) );
        int   split = (int)std::lround( V( "split", 0 ) );
        float ox = V( "x", 0 ), oy = V( "y", 0 ), length = V( "length", 0 ), slow = V( "slow", 0 );
        float phase = V( "phase", 0 ) * Deg;
        Color col = p.color( "color", { 120, 220, 255, 255 } );
        int   sprite = spriteOf( p.str( "sprite", s.m_kind == "homing" ? "rocket" : s.m_kind == "flame" ? "flame" : "pulse" ) );
        Vector2 origin = { from.x + ox, from.y + oy };
        const std::string &k = s.m_kind;
        auto shoot = [&]( Vector2 at, float dir, float spd ) {
            Bullet b;
            b.m_player = true;
            b.m_owner = i;
            b.m_pos = b.m_origin = at;
            // jitter: a little scatter of the aim (degrees either way).
            if ( jitter > 0 ) dir += m_rng.range( -jitter, jitter );
            b.m_vel = mul( dirOf( dir ), spd );
            b.m_radius = size;
            b.m_damage = dmg;
            b.m_slow = slow;
            b.m_phase = phase;
            b.m_life = life;
            b.m_home = home;
            b.m_amp = amp;
            b.m_freq = freq;
            b.m_explode = radius;
            b.m_accel = accel;
            b.m_drag = drag;
            b.m_pierce = pierce;
            b.m_bounce = bounce;
            b.m_split = split;
            b.m_sprite = sprite;
            b.m_color = col;
            b.m_length = length;
            m_bullets.push_back( b );
            return m_bullets.size() - 1;
        };
        if ( k == "row" || k == "bounce" )
            for ( int j = 0; j < n; ++j ) shoot( { origin.x + ( j - ( n - 1 ) * 0.5f ) * dx, origin.y }, baseDir + angle, speed );
        else if ( k == "fan" || k == "back" || k == "ripple" )
        {
            float base = baseDir + angle + ( k == "back" ? 180.0f : 0.0f );
            for ( int j = 0; j < n; ++j )
            {
                float a = n > 1 ? base - spread * 0.5f + spread * j / ( n - 1 ) : base;
                shoot( origin, a, speed );
            }
        }
        else if ( k == "side" )
            for ( int j = 0; j < n; ++j )
            {
                float off = ( j - ( n - 1 ) * 0.5f ) * dx;
                shoot( { origin.x - 8, origin.y + off }, baseDir - angle, speed );
                shoot( { origin.x + 8, origin.y + off }, baseDir + angle, speed );
            }
        else if ( k == "homing" )
            for ( int j = 0; j < n; ++j )
            {
                float a = ( n > 1 ? baseDir - spread * 0.5f + spread * j / ( n - 1 ) : baseDir ) + angle;
                size_t bi = shoot( { origin.x + ( j - ( n - 1 ) * 0.5f ) * dx, origin.y }, a, speed );
                if ( m_bullets[bi].m_home <= 0 ) m_bullets[bi].m_home = 240.0f;
            }
        else if ( k == "wave" )
            for ( int j = 0; j < n; ++j )
            {
                size_t bi = shoot( { origin.x + ( j - ( n - 1 ) * 0.5f ) * dx, origin.y }, baseDir + angle, speed );
                // amp may be negative (the swing starts the other way); unset it is 30.
                if ( !p.has( "amp" ) ) m_bullets[bi].m_amp = 30.0f;
                if ( m_bullets[bi].m_freq <= 0 ) m_bullets[bi].m_freq = 3.0f;
                if ( j % 2 == 1 ) m_bullets[bi].m_amp = -m_bullets[bi].m_amp;
            }
        else if ( k == "burst" || k == "flame" )
            for ( int j = 0; j < n; ++j )
            {
                float a = baseDir + angle + m_rng.range( -spread * 0.5f, spread * 0.5f );
                size_t bi = shoot( origin, a, speed * m_rng.range( 0.8f, 1.2f ) );
                if ( k == "flame" )
                {
                    if ( !p.has( "slow" ) ) m_bullets[bi].m_slow = 2.5f;
                    if ( m_bullets[bi].m_pierce == 0 ) m_bullets[bi].m_pierce = 3;
                }
            }
        else if ( k == "bomb" )
            for ( int j = 0; j < n; ++j )
            {
                size_t bi = shoot( { origin.x + ( j - ( n - 1 ) * 0.5f ) * dx, origin.y }, baseDir + angle, speed );
                if ( m_bullets[bi].m_explode <= 0 ) m_bullets[bi].m_explode = 60.0f;
            }
        else if ( k == "orbit" )
            for ( int j = 0; j < n; ++j )
            {
                float  a = 360.0f * j / n + m_clock * 90.0f;
                Vector2 at = add( origin, mul( dirOf( a ), V( "range", 50 ) ) );
                size_t bi = shoot( at, a + 90.0f, 0.0f );
                m_bullets[bi].m_spin = V( "spin", 300 );
                m_bullets[bi].m_origin = { V( "range", 50 ), a };
            }
        else if ( k == "beam" )
        {
            // A beam: everything in its column (up to the first solid enemy unless it pierces) takes the hit now; the
            // ground's structures do not stop it, it burns through them on its way.
            float width = V( "width", 10 );
            pl.m_beam = true;
            pl.m_beamWidth = std::max( pl.m_beamWidth, width );
            pl.m_beamColor = col;
            pl.m_beams.push_back( { origin, width, col } );
            float stopY = -50.0f;
            int   first = -1;
            if ( pierce == 0 )
                for ( size_t e = 0; e < m_enemies.size(); ++e )
                {
                    const Enemy &en = m_enemies[e];
                    if ( en.m_dead || en.m_gone || en.m_pos.y > origin.y || !en.m_def->m_solid || en.m_def->m_scenery ) continue;
                    float r = en.m_def->m_radius * ( en.m_ground ? GroundFactor : 1.0f );
                    if ( std::fabs( en.m_pos.x - origin.x ) < r + width * 0.5f && en.m_pos.y > stopY )
                    {
                        stopY = en.m_pos.y;
                        first = (int)e;
                    }
                }
            for ( size_t e = 0; e < m_enemies.size(); ++e )
            {
                const Enemy &en = m_enemies[e];
                if ( en.m_dead || en.m_gone || en.m_rise > 0 || en.m_pos.y > origin.y || en.m_pos.y < -20 ) continue;
                float r = en.m_def->m_radius * ( en.m_ground ? GroundFactor : 1.0f );
                if ( std::fabs( en.m_pos.x - origin.x ) < r + width * 0.5f
                     && ( pierce > 0 || (int)e == first || ( en.m_def->m_scenery && en.m_pos.y > stopY ) ) )
                    hitEnemy( (int)e, dmg, i, { origin.x, en.m_pos.y } );
            }
            pl.m_beamWidth = width;
        }
        else if ( k == "chain" )
        {
            float   range = V( "range", 260 );
            int     links = (int)std::lround( V( "chain", 2 ) );
            Vector2 at = origin;
            int     last = -1;
            for ( int c = 0; c <= links; ++c )
            {
                int e = nearestEnemy( at, c == 0 ? range : range * 0.6f, last );
                if ( e < 0 ) break;
                SimEvent ev;
                ev.m_type = EvE::CHAIN;
                ev.m_pos = at;
                ev.m_to = m_enemies[(size_t)e].m_pos;
                ev.m_color = col;
                m_events.push_back( ev );
                hitEnemy( e, dmg * ( c == 0 ? 1.0f : 0.7f ), i, m_enemies[(size_t)e].m_pos );
                at = m_enemies[(size_t)e].m_pos;
                last = e;
            }
        }
        else
            shoot( origin, baseDir + angle, speed );
        (void)jitter;
    }
    (void)sidekick;
    (void)sk;
}

void World::firePlayer( int i, const PlayerInput &in )
{
    Player &p = m_players[i];
    p.m_beam = false;
    p.m_beamWidth = 0.0f;
    p.m_beams.clear();
    p.m_firing = in.m_fire && p.m_alive;
    if ( p.m_frontT > 0 ) p.m_frontT -= Dt;
    if ( p.m_rearT > 0 ) p.m_rearT -= Dt;
    if ( !p.m_alive ) return;
    // The special's drones and lance fire by themselves.
    if ( p.m_drones > 0 )
    {
        p.m_drones -= Dt;
        if ( (int)( m_clock * 60 ) % 8 == 0 )
            for ( int d = 0; d < 4; ++d )
            {
                float   a = m_clock * 200.0f + d * 90.0f;
                Vector2 at = add( p.m_pos, mul( dirOf( a ), 60.0f ) );
                int     e = nearestEnemy( at, 600.0f );
                float   dir = e >= 0 ? angleOf( sub( m_enemies[(size_t)e].m_pos, at ) ) : 180.0f;
                Bullet  b;
                b.m_player = true;
                b.m_owner = i;
                b.m_pos = at;
                b.m_vel = mul( dirOf( dir ), 900.0f );
                b.m_damage = 6.0f;
                b.m_radius = 4;
                b.m_life = 1.2f;
                b.m_sprite = spriteOf( "spark" );
                b.m_color = { 160, 255, 200, 255 };
                m_bullets.push_back( b );
            }
    }
    else if ( p.m_drones < 0 )
    {
        p.m_drones = std::min( 0.0f, p.m_drones + Dt );
        ShotDef lance;
        lance.m_kind = "beam";
        lance.m_p.m_kv = { { "width", "60" }, { "dmg", "5" }, { "pierce", "1" }, { "color", "#ffe6a0" } };
        std::vector<ShotDef> v = { lance };
        fireShots( i, v, 1, 1, 0, 0, { p.m_pos.x, p.m_pos.y - 20 }, 180.0f, false, nullptr );
    }
    if ( !in.m_fire ) return;
    int frontLv = std::clamp( p.m_load.m_frontLv, 1, 11 );
    if ( p.m_front && p.m_frontT <= 0 && !rule( RuleE::REAR ) )
    {
        float t = ( frontLv - 1 ) / 10.0f;
        float cost = p.m_front->m_energy.at( t );
        if ( p.m_energy >= cost )
        {
            p.m_energy -= cost;
            p.m_frontT += 1.0f / std::max( 0.5f, p.m_front->m_rate.at( t ) );
            if ( p.m_frontT < 0 ) p.m_frontT = 0;
            fireShots( i, p.m_front->m_shots, frontLv, 11, 0, p.m_frontVolley++, { p.m_pos.x, p.m_pos.y - 18 }, 180.0f,
                       false, nullptr );
            emit( EvE::SHOT, p.m_pos, 0, (float)i, p.m_front->m_sound );
        }
    }
    int rearLv = std::clamp( p.m_load.m_rearLv, 1, 11 );
    if ( p.m_rear && p.m_rearT <= 0 )
    {
        float t = ( rearLv - 1 ) / 10.0f;
        float cost = p.m_rear->m_energy.at( t );
        if ( p.m_energy >= cost )
        {
            p.m_energy -= cost;
            p.m_rearT += 1.0f / std::max( 0.5f, p.m_rear->m_rate.at( t ) );
            if ( p.m_rearT < 0 ) p.m_rearT = 0;
            fireShots( i, p.m_rear->m_shots, rearLv, 11, p.m_rearMode, p.m_rearVolley++, { p.m_pos.x, p.m_pos.y + 4 },
                       180.0f, false, nullptr );
            emit( EvE::SHOT, p.m_pos, 1, (float)i, p.m_rear->m_sound );
        }
    }
    for ( Sidekick &s : p.m_side )
    {
        if ( !s.m_def ) continue;
        if ( s.m_t > 0 ) s.m_t -= Dt;
        if ( s.m_t > 0 ) continue;
        if ( s.m_def->m_ammo > 0 && s.m_ammo <= 0 ) continue;
        if ( s.m_def->m_charge > 0 && s.m_charge < s.m_def->m_charge ) continue;
        float dir = 180.0f;
        if ( s.m_def->m_aim == "nearest" )
        {
            int e = nearestEnemy( s.m_pos, 700.0f );
            if ( e < 0 ) continue;
            dir = angleOf( sub( m_enemies[(size_t)e].m_pos, s.m_pos ) );
        }
        s.m_t = 1.0f / std::max( 0.2f, s.m_def->m_rate );
        if ( s.m_def->m_ammo > 0 ) --s.m_ammo;
        s.m_charge = 0;
        fireShots( i, s.m_def->m_shots, 1, 1, 0, s.m_volley++, s.m_pos, dir, true, &s );
    }
}

// ---------------------------------------------------------------------------------------------------- the enemies

void World::moveEnemy( Enemy &e )
{
    PathState &ps = e.m_path;
    float      dt = Dt * m_slow;
    e.m_t += dt;
    float   t = e.m_t;
    Vector2 old = e.m_pos;
    float   groundV = m_scroll * GroundFactor;
    switch ( ps.m_kind )
    {
    case P_STRAIGHT:
    {
        ps.m_speed += ps.m_accel * dt;
        e.m_pos = add( e.m_pos, mul( dirOf( ps.m_dir ), ps.m_speed * dt ) );
        break;
    }
    case P_DIVE:
    {
        // Down, leaning towards the pilot for the first stretch, faster and faster.
        int     pl = nearestPlayer( e.m_pos );
        float   tx = pl >= 0 ? m_players[pl].m_pos.x : e.m_pos.x;
        float   lean = t < 1.2f ? clampf( ( tx - e.m_pos.x ) * 1.2f, -ps.m_turn * 2, ps.m_turn * 2 ) : 0.0f;
        e.m_vel.x += ( lean - e.m_vel.x ) * std::min( 1.0f, dt * 3.0f );
        e.m_vel.y = ps.m_speed * ( 0.7f + std::min( 1.0f, t * 0.6f ) * 0.6f );
        e.m_pos = add( e.m_pos, mul( e.m_vel, dt ) );
        break;
    }
    case P_SINE:
        e.m_pos.y += ps.m_speed * dt;
        e.m_pos.x = e.m_base.x + ps.m_amp * std::sin( 2 * Pi * ps.m_freq * t + ps.m_phase );
        break;
    case P_SWOOP:
    {
        // From the side down in a curve through (cx, cy) and out at the other side, in m_dur seconds.
        float   u = t / std::max( 0.5f, ps.m_dur );
        Vector2 a = e.m_base;
        Vector2 c = { ps.m_cx, ps.m_cy };
        Vector2 b = { ps.m_side > 0 ? FieldW + 60.0f : -60.0f, a.y + ( c.y - a.y ) * 0.6f };
        Vector2 ctl = { 2 * c.x - 0.5f * ( a.x + b.x ), 2 * c.y - 0.5f * ( a.y + b.y ) };
        if ( u <= 1.0f )
        {
            float v = 1 - u;
            e.m_pos = { v * v * a.x + 2 * v * u * ctl.x + u * u * b.x, v * v * a.y + 2 * v * u * ctl.y + u * u * b.y };
        }
        else
            e.m_pos = add( e.m_pos, mul( norm( sub( b, ctl ) ), ps.m_speed * dt ) );
        break;
    }
    case P_ARC:
    {
        // In from above, bending away to the side.
        float ang = ps.m_dir + ps.m_side * std::min( 110.0f, ps.m_turn * std::max( 0.0f, t - 0.6f ) );
        e.m_pos = add( e.m_pos, mul( dirOf( -ang ), ps.m_speed * dt ) );
        break;
    }
    case P_UTURN:
    {
        if ( e.m_pos.y < ps.m_stop && ps.m_phase == 0 )
            e.m_pos.y += ps.m_speed * dt;
        else
        {
            if ( ps.m_phase == 0 ) ps.m_phase = t;
            float a = ( t - ps.m_phase ) * ps.m_speed / std::max( 20.0f, ps.m_r * 0.5f );
            if ( a < Pi )
            {
                float r = ps.m_r * 0.5f;
                float side = e.m_base.x < FieldW * 0.5f ? 1.0f : -1.0f;
                e.m_pos.x = e.m_base.x + side * r * ( 1 - std::cos( a ) );
                e.m_pos.y = ps.m_stop + r * std::sin( a );
            }
            else
                e.m_pos.y -= ps.m_speed * dt;
        }
        break;
    }
    case P_HOVER:
    {
        // Down to its height, there a while (swaying), then away.
        float stopT = ps.m_phase;
        if ( stopT == 0 && e.m_pos.y < ps.m_stop )
        {
            float d = ps.m_stop - e.m_pos.y;
            e.m_pos.y += std::max( 30.0f, std::min( ps.m_speed, d * 3.0f ) ) * dt;
            if ( d < 2.0f ) ps.m_phase = t;
        }
        else
        {
            if ( stopT == 0 ) ps.m_phase = stopT = t;
            float h = t - stopT;
            if ( h < ps.m_wait )
                e.m_pos.x = e.m_base.x + ps.m_amp * 0.4f * std::sin( 2 * Pi * ps.m_freq * h );
            else
                e.m_pos = add( e.m_pos, mul( dirOf( ps.m_exit ), ps.m_speed * std::min( 1.0f, ( h - ps.m_wait ) ) * dt ) );
        }
        break;
    }
    case P_STRAFE:
        e.m_pos.x += ps.m_side * ps.m_speed * dt;
        e.m_pos.y = e.m_base.y + ps.m_amp * 0.25f * std::sin( 2 * Pi * ps.m_freq * t );
        break;
    case P_ZIGZAG:
    {
        e.m_pos.y += ps.m_speed * dt;
        float period = 1.0f / std::max( 0.05f, ps.m_freq );
        float ph = std::fmod( t, period ) / period;
        float tri = ph < 0.5f ? ph * 4 - 1 : 3 - ph * 4;
        e.m_pos.x = e.m_base.x + ps.m_amp * tri;
        break;
    }
    case P_CHASE:
    {
        int pl = nearestPlayer( e.m_pos );
        if ( e.m_vel.x == 0 && e.m_vel.y == 0 ) e.m_vel = mul( dirOf( ps.m_dir ), ps.m_speed );
        if ( pl >= 0 && t < ps.m_dur * 2.0f )
        {
            float want = angleOf( sub( m_players[pl].m_pos, e.m_pos ) );
            float have = angleOf( e.m_vel );
            float d = clampf( wrapDeg( want - have ), -ps.m_turn * dt, ps.m_turn * dt );
            e.m_vel = mul( dirOf( have + d ), ps.m_speed );
        }
        e.m_pos = add( e.m_pos, mul( e.m_vel, dt ) );
        break;
    }
    case P_ORBIT:
    {
        // In to the circle, round it for m_dur seconds, then down and out.
        float   w = ps.m_speed / std::max( 30.0f, ps.m_r );
        float   a = ps.m_phase + w * t * ps.m_side;
        Vector2 goal = { ps.m_cx + std::cos( a ) * ps.m_r, ps.m_cy + std::sin( a ) * ps.m_r };
        if ( t > ps.m_dur + 1.0f )
            e.m_pos.y += ps.m_speed * dt;
        else
            e.m_pos = add( e.m_pos, mul( sub( goal, e.m_pos ), std::min( 1.0f, dt * ( t < 1.0f ? 2.5f : 8.0f ) ) ) );
        break;
    }
    case P_LOOP:
    {
        if ( ps.m_phase == 0 && e.m_pos.y < ps.m_stop )
            e.m_pos.y += ps.m_speed * dt;
        else
        {
            if ( ps.m_phase == 0 ) ps.m_phase = t;
            float a = ( t - ps.m_phase ) * ps.m_speed / std::max( 20.0f, ps.m_r );
            if ( a < 2 * Pi )
            {
                float side = e.m_base.x < FieldW * 0.5f ? 1.0f : -1.0f;
                e.m_pos.x = e.m_base.x + side * ps.m_r * ( 1 - std::cos( a ) );
                e.m_pos.y = ps.m_stop - ps.m_r * std::sin( a ) * 0.8f;
            }
            else
                e.m_pos.y += ps.m_speed * 1.3f * dt;
        }
        break;
    }
    case P_RISE:
        e.m_pos.y -= ps.m_speed * dt;
        e.m_pos.x = e.m_base.x + ps.m_amp * 0.3f * std::sin( 2 * Pi * ps.m_freq * t );
        break;
    case P_DRIFT:
        e.m_pos.y += ps.m_speed * dt;
        e.m_pos.x += std::sin( t * 0.7f + e.m_id ) * 12.0f * dt + ps.m_dir * dt;
        break;
    case P_BOSS:
    {
        if ( e.m_pos.y < ps.m_stop && ps.m_phase == 0 )
            e.m_pos.y += std::max( 40.0f, std::min( ps.m_speed, ( ps.m_stop - e.m_pos.y ) * 2.0f ) ) * dt;
        else
        {
            if ( ps.m_phase == 0 ) ps.m_phase = t;
            float h = t - ps.m_phase;
            float amp = ps.m_amp > 0 ? ps.m_amp : 140.0f;
            float hp = e.m_hp / std::max( 1.0f, e.m_hpMax );
            float f = ps.m_freq * ( hp < 0.4f ? 1.6f : 1.0f );
            Vector2 goal = { ps.m_cx + amp * std::sin( 2 * Pi * f * 0.5f * h ),
                             ps.m_stop + 30.0f * std::sin( 2 * Pi * f * 0.37f * h ) };
            e.m_pos = add( e.m_pos, mul( sub( goal, e.m_pos ), std::min( 1.0f, dt * 2.0f ) ) );
        }
        break;
    }
    case P_SPLINE:
    {
        float u = t / std::max( 0.5f, ps.m_dur );
        if ( u <= 1.0f && !ps.m_pts.empty() )
            e.m_pos = spline( ps.m_pts, u );
        else
            e.m_pos = add( e.m_pos, mul( norm( e.m_vel ), ps.m_speed * dt ) );
        break;
    }
    case P_GROUND:
        e.m_pos.y += groundV * dt;
        if ( ps.m_drive )
            e.m_pos = add( e.m_pos, mul( dirOf( ps.m_dir ), ps.m_speed * GroundFactor * dt ) );
        break;
    case P_STILL: break;
    }
    if ( e.m_ground && ps.m_kind != P_GROUND && ps.m_kind != P_STILL ) e.m_pos.y += groundV * dt;
    if ( ps.m_kind != P_STILL )
    {
        Vector2 v = mul( sub( e.m_pos, old ), 1.0f / std::max( 1e-4f, dt ) );
        if ( len( v ) > 1.0f && ps.m_kind != P_SPLINE ) e.m_vel = v;
        if ( ps.m_kind == P_SPLINE ) e.m_vel = len( v ) > 1.0f ? v : e.m_vel;
    }
    // Facing and leaning.
    const std::string &face = e.m_faceAs.empty() ? e.m_def->m_face : e.m_faceAs;
    float want = e.m_face;
    if ( face == "move" && len( e.m_vel ) > 20.0f && !e.m_ground ) want = angleOf( e.m_vel );
    else if ( face == "down" ) want = 0.0f;
    else if ( face == "up" ) want = 180.0f;
    else if ( face == "player" )
    {
        int pl = nearestPlayer( e.m_pos );
        if ( pl >= 0 ) want = angleOf( sub( m_players[pl].m_pos, e.m_pos ) );
    }
    else if ( face == "spin" ) want = e.m_face + 90.0f * dt * ( e.m_id % 2 ? 1 : -1 );
    if ( face == "spin" )
        e.m_face = want;
    else
        e.m_face += clampf( wrapDeg( want - e.m_face ), -240.0f * dt, 240.0f * dt );
    float side = 0.0f;
    if ( !e.m_ground && len( e.m_vel ) > 1 )
    {
        Vector2 f = dirOf( e.m_face );
        side = ( e.m_vel.x * -f.y + e.m_vel.y * f.x ) / std::max( 60.0f, ps.m_speed );
    }
    e.m_bank += ( clampf( side, -1, 1 ) - e.m_bank ) * std::min( 1.0f, dt * 5.0f );
    if ( e.m_def->m_aimHead )
    {
        int pl = nearestPlayer( e.m_pos );
        if ( pl >= 0 )
        {
            float w = angleOf( sub( m_players[pl].m_pos, e.m_pos ) );
            e.m_head += clampf( wrapDeg( w - e.m_head ), -150.0f * dt, 150.0f * dt );
        }
    }
    else
        e.m_head = e.m_face;
}

void World::moveEnemies()
{
    for ( size_t i = 0; i < m_enemies.size(); ++i )
    {
        Enemy &e = m_enemies[i];
        if ( e.m_dead || e.m_gone ) continue;
        if ( e.m_flash > 0 ) e.m_flash -= Dt * 6.0f;
        if ( e.m_rise > 0 )
        {
            e.m_rise = std::max( 0.0f, e.m_rise - Dt * m_slow );
            e.m_pos.y += m_scroll * GroundFactor * Dt * m_slow;
            e.m_base = e.m_pos;
            e.m_t = 0.0f;
            continue;
        }
        if ( e.m_parent >= 0 )
        {
            const Enemy &par = m_enemies[(size_t)e.m_parent];
            if ( par.m_dead || par.m_gone )
            {
                if ( par.m_dead ) killEnemy( (int)i, -1 );
                else e.m_gone = true;
                continue;
            }
            e.m_t += Dt * m_slow;
            e.m_pos = worldPos( par, e.m_off );
            e.m_vel = par.m_vel;
            if ( e.m_def->m_face == "player" || e.m_def->m_aimHead )
            {
                int pl = nearestPlayer( e.m_pos );
                if ( pl >= 0 )
                {
                    float w = angleOf( sub( m_players[pl].m_pos, e.m_pos ) );
                    e.m_head += clampf( wrapDeg( w - e.m_head ), -150.0f * Dt, 150.0f * Dt );
                }
                e.m_face = par.m_face;
            }
            else
            {
                e.m_face = par.m_face;
                e.m_head = e.m_face;
            }
            e.m_bank = par.m_bank;
            e.m_entered = par.m_entered;
            continue;
        }
        moveEnemy( e );
        float r = e.m_def->m_radius;
        bool  inside = e.m_pos.y > -r && e.m_pos.y < FieldH + r && e.m_pos.x > -r && e.m_pos.x < FieldW + r;
        if ( inside ) e.m_entered = true;
        bool out = e.m_pos.y > FieldH + r + 60 || e.m_pos.x < -r - 140 || e.m_pos.x > FieldW + r + 140 ||
                   e.m_pos.y < -r - 260;
        if ( ( e.m_entered && out ) || e.m_t > e.m_def->m_life + ( e.m_def->m_kind == EnemyKindE::BOSS ? 1e6f : 0.0f ) )
        {
            if ( e.m_def->m_kind != EnemyKindE::BOSS ) e.m_gone = true;
        }
    }
}

void World::enemyBullet( const Enemy &e, const FireDef &f, Vector2 at, float dir, float speed )
{
    Bullet b;
    b.m_player = false;
    b.m_pos = b.m_origin = at;
    b.m_vel = mul( dirOf( dir ), speed * m_diff.m_speed );
    b.m_src = e.m_def;
    int k = spriteOf( f.m_bullet );
    if ( k > 7 ) k = 0;
    b.m_sprite = k;
    b.m_radius = f.m_size > 0 ? f.m_size : EnemyBullets[k].m_radius;
    b.m_damage = ( f.m_damage > 0 ? f.m_damage : EnemyBullets[k].m_damage );
    b.m_color = f.m_color;
    b.m_accel = f.m_accel;
    b.m_life = f.m_life;
    if ( f.m_pattern == "missile" ) b.m_home = f.m_turn > 0 ? f.m_turn : 90.0f;
    if ( f.m_pattern == "mine" )
    {
        b.m_mine = true;
        b.m_drag = 1.2f;
        b.m_split = std::max( 6, f.m_n * 4 );
        b.m_life = f.m_life < 8.0f ? f.m_life : 2.2f;
    }
    m_bullets.push_back( b );
    (void)e;
}

void World::fireEnemy( int idx )
{
    Enemy &e = m_enemies[(size_t)idx];
    if ( e.m_dead || e.m_gone || e.m_def->m_fire.empty() || e.m_rise > 0 ) return;
    // Only on the field (and not in its last bottom strip, where shooting down would be unfair).
    if ( e.m_pos.y < 4 || e.m_pos.y > FieldH - 90 || e.m_pos.x < 0 || e.m_pos.x > FieldW ) return;
    int pl = nearestPlayer( e.m_pos );
    if ( pl < 0 ) return;
    float hpFrac = e.m_hp / std::max( 1.0f, e.m_hpMax );
    float dt = Dt * m_slow;
    bool  partsLive = false;
    for ( const Enemy &o : m_enemies )
        if ( o.m_parent == idx && !o.m_dead && !o.m_gone ) partsLive = true;
    for ( size_t f = 0; f < e.m_def->m_fire.size(); ++f )
    {
        const FireDef &fd = e.m_def->m_fire[f];
        if ( fd.m_onlyDiff && ( m_diffIndex < fd.m_minDiff || m_diffIndex > fd.m_maxDiff ) ) continue;
        if ( hpFrac > fd.m_from || hpFrac <= fd.m_until ) continue;
        if ( ( fd.m_parts == 0 && partsLive ) || ( fd.m_parts == 1 && !partsLive ) ) continue;
        if ( fd.m_cap > 0 && e.m_fireN[f] >= fd.m_cap ) continue;
        e.m_fireT[f] -= dt;
        if ( e.m_fireT[f] > 0 ) continue;
        if ( e.m_burstLeft[f] <= 0 ) e.m_burstLeft[f] = std::max( 1, fd.m_burst );
        --e.m_burstLeft[f];
        e.m_fireT[f] = e.m_burstLeft[f] > 0 ? fd.m_gap : fd.m_every / std::max( 0.1f, m_diff.m_fire );
        if ( e.m_burstLeft[f] == 0 ) ++e.m_fireN[f];
        for ( int side = 0; side < ( fd.m_mirror ? 2 : 1 ); ++side )
        {
            Vector2 at = worldPos( e, { side == 0 ? fd.m_x : -fd.m_x, fd.m_z } );
            float   aim = angleOf( sub( m_players[pl].m_pos, at ) );
            // The mirrored muzzle mirrors its directions too.
            float   mdir = side == 0 ? fd.m_dir : -fd.m_dir, mrot = side == 0 ? fd.m_rot : -fd.m_rot;
            float   speed = fd.m_speed;
            const std::string &pt = fd.m_pattern;
            if ( pt == "aimed" || pt == "missile" )
                for ( int j = 0; j < fd.m_n; ++j )
                {
                    float a = fd.m_n > 1 ? aim - fd.m_spread * ( fd.m_n - 1 ) * 0.5f + fd.m_spread * j : aim;
                    enemyBullet( e, fd, at, a, speed );
                }
            else if ( pt == "fan" )
            {
                float base = mdir + ( e.m_def->m_face == "fixed" || e.m_def->m_face == "spin" ? e.m_face : 0.0f );
                for ( int j = 0; j < fd.m_n; ++j )
                {
                    float a = fd.m_n > 1 ? base - fd.m_spread * ( fd.m_n - 1 ) * 0.5f + fd.m_spread * j : base;
                    enemyBullet( e, fd, at, a, speed );
                }
            }
            else if ( pt == "ring" || pt == "spiral" )
            {
                e.m_spin += fd.m_rot;
                for ( int j = 0; j < fd.m_n; ++j ) enemyBullet( e, fd, at, fd.m_dir + e.m_spin + 360.0f * j / fd.m_n, speed );
            }
            else if ( pt == "down" )
                for ( int j = 0; j < fd.m_n; ++j )
                    enemyBullet( e, fd, { at.x + ( j - ( fd.m_n - 1 ) * 0.5f ) * fd.m_spread, at.y }, fd.m_dir, speed );
            else if ( pt == "rain" )
                for ( int j = 0; j < fd.m_n; ++j )
                    enemyBullet( e, fd, at, fd.m_dir + m_rng.range( -fd.m_spread, fd.m_spread ), speed * m_rng.range( 0.8f, 1.15f ) );
            else if ( pt == "wall" )
                for ( int j = 0; j < fd.m_n; ++j )
                {
                    // A row across the field with one gap where the pilot can slip through.
                    int gapAt = (int)( m_rng.unit() * fd.m_n );
                    if ( j == gapAt || j == gapAt + 1 ) continue;
                    enemyBullet( e, fd, { ( j + 0.5f ) * FieldW / fd.m_n, at.y }, fd.m_dir, speed );
                }
            else if ( pt == "mine" )
                for ( int j = 0; j < std::max( 1, fd.m_n / 4 ); ++j ) enemyBullet( e, fd, at, aim + m_rng.range( -30.0f, 30.0f ), speed );
            else if ( pt == "laser" )
            {
                Laser l;
                l.m_owner = idx;
                l.m_off = { side == 0 ? fd.m_x : -fd.m_x, fd.m_z };
                l.m_aim = fd.m_aim >= 0 ? fd.m_aim == 1 : ( fd.m_spread == 0 && fd.m_dir == 0 && fd.m_rot == 0 );
                l.m_dir = l.m_aim ? aim : mdir;
                l.m_sweep = mrot;
                l.m_warn = fd.m_warn;
                l.m_dur = fd.m_dur;
                l.m_width = fd.m_width;
                l.m_damage = fd.m_damage > 0 ? fd.m_damage : 40.0f;
                l.m_color = fd.m_color;
                l.m_from = at;
                m_lasers.push_back( l );
                emit( EvE::LASER, at, 0, 0, "", fd.m_color );
            }
            else if ( pt == "radio" )
            {
                emit( EvE::TEXT, at, 0, 0, fd.m_spawn );
                e.m_fireN[f] = 1000000;   // once
            }
            else if ( pt == "spawn" )
            {
                // spawn=a|b|c: the one of the mission's episode (e3_...), else the first.
                const EnemyDef *d = nullptr;
                std::string     want = m_level ? "e" + std::to_string( m_level->m_episode ) + "_" : "";
                for ( size_t a = 0; a <= fd.m_spawn.size(); )
                {
                    size_t          b = std::min( fd.m_spawn.find( '|', a ), fd.m_spawn.size() );
                    std::string     n = fd.m_spawn.substr( a, b - a );
                    const EnemyDef *c = m_cat->enemy( n );
                    if ( c && !d ) d = c;
                    if ( c && !want.empty() && n.rfind( want, 0 ) == 0 )
                    {
                        d = c;
                        break;
                    }
                    a = b + 1;
                }
                if ( d )
                {
                    // Joins at the next step (spawning now would move the enemies under our feet).
                    Pending pd;
                    pd.m_t = m_clock;
                    pd.m_def = d;
                    pd.m_pos = at;
                    // The fire line may give the launched enemy its own path (path= and its values).
                    if ( fd.m_line.has( "path" ) )
                    {
                        pd.m_path = fd.m_line;
                        if ( side == 1 && fd.m_line.has( "side" ) )
                            pd.m_path.m_kv["side"] = fd.m_line.num( "side" ) < 0 ? "1" : "-1";
                    }
                    else
                    {
                        pd.m_path = d->m_path;
                        pd.m_path.m_kv["path"] = d->m_path.m_key;
                    }
                    // From the ground (a launch pad) it rises into the field first.
                    if ( e.m_ground ) pd.m_rise = RiseTime;
                    m_pending.push_back( pd );
                }
            }
            if ( pt != "spawn" && pt != "laser" && pt != "radio" ) emit( EvE::ENEMYSHOT, at, 0, 0, fd.m_bullet, fd.m_color );
        }
    }
}

// ---------------------------------------------------------------------------------------------------- bullets

namespace
{
/** @brief A pilot's shot bursts into splinters (split=n): a fan forward, weaker and without splitting again. */
void splinter( std::vector<Bullet> &out, const Bullet &b, Rng &rng )
{
    Vector2 d = len( b.m_vel ) > 1.0f ? norm( b.m_vel ) : Vector2{ 0, -1 };
    float   base = angleOf( d );
    for ( int j = 0; j < b.m_split; ++j )
    {
        Bullet s = b;
        float  a = base + ( b.m_split > 1 ? -50.0f + 100.0f * j / ( b.m_split - 1 ) : 0.0f ) + rng.range( -6.0f, 6.0f );
        s.m_vel = mul( dirOf( a ), std::max( 450.0f, len( b.m_vel ) * 0.8f ) );
        s.m_damage = b.m_damage * 0.45f;
        s.m_radius = std::max( 2.0f, b.m_radius * 0.7f );
        s.m_split = 0;
        s.m_explode = 0;
        s.m_age = 0;
        s.m_life = 0.45f;
        s.m_home = 0;
        s.m_amp = 0;
        s.m_spin = 0;
        s.m_pierce = 0;
        s.m_dead = false;
        out.push_back( s );
    }
}
}   // namespace

void World::moveBullets()
{
    for ( Bullet &b : m_bullets )
    {
        if ( b.m_dead ) continue;
        float dt = b.m_player ? Dt : Dt * m_slow;
        b.m_age += dt;
        if ( b.m_age > b.m_life )
        {
            b.m_dead = true;
            if ( b.m_split > 0 && !b.m_player )
            {
                // A mine bursts into a ring.
                for ( int j = 0; j < b.m_split; ++j )
                {
                    Bullet s;
                    s.m_pos = s.m_origin = b.m_pos;
                    s.m_vel = mul( dirOf( 360.0f * j / b.m_split + b.m_age * 40.0f ), 150.0f * m_diff.m_speed );
                    s.m_radius = 5;
                    s.m_damage = 6;
                    s.m_life = 5;
                    s.m_sprite = 0;
                    s.m_color = b.m_color;
                    m_born.push_back( s );
                }
                emit( EvE::EXPLODE, b.m_pos, 0, 0, "", b.m_color );
                continue;
            }
            if ( b.m_player && b.m_split > 0 ) splinter( m_born, b, m_rng );
            if ( b.m_player && b.m_explode > 0 )
            {
                for ( size_t e = 0; e < m_enemies.size(); ++e )
                    if ( !m_enemies[e].m_dead && dist2( m_enemies[e].m_pos, b.m_pos ) < b.m_explode * b.m_explode )
                        hitEnemy( (int)e, b.m_damage, b.m_owner, m_enemies[e].m_pos );
                emit( EvE::EXPLODE, b.m_pos, 1, 0, "", b.m_color );
            }
            continue;
        }
        if ( b.m_spin != 0 )
        {
            // Orbiting the ship.
            const Player &p = m_players[b.m_owner];
            b.m_origin.y += b.m_spin * dt;
            b.m_pos = add( p.m_pos, mul( dirOf( b.m_origin.y ), b.m_origin.x ) );
            continue;
        }
        if ( b.m_home > 0 )
        {
            Vector2 target = { -1, -1 };
            if ( b.m_player )
            {
                int e = nearestEnemy( b.m_pos, 900.0f );
                if ( e >= 0 ) target = m_enemies[(size_t)e].m_pos;
            }
            else if ( b.m_age < 2.5f )
            {
                int p = nearestPlayer( b.m_pos );
                if ( p >= 0 ) target = m_players[p].m_pos;
            }
            if ( target.x >= 0 )
            {
                float spd = len( b.m_vel );
                float want = angleOf( sub( target, b.m_pos ) );
                float have = angleOf( b.m_vel );
                float d = clampf( wrapDeg( want - have ), -b.m_home * dt, b.m_home * dt );
                b.m_vel = mul( dirOf( have + d ), spd );
            }
        }
        if ( b.m_accel != 0 )
        {
            float spd = len( b.m_vel );
            float ns = std::max( 0.0f, spd + b.m_accel * dt );
            if ( spd > 1e-3f ) b.m_vel = mul( b.m_vel, ns / spd );
        }
        if ( b.m_drag > 0 ) b.m_vel = mul( b.m_vel, std::max( 0.0f, 1.0f - b.m_drag * dt ) );
        if ( b.m_slow > 0 ) b.m_vel = mul( b.m_vel, std::max( 0.0f, 1.0f - b.m_slow * dt ) );
        b.m_pos = add( b.m_pos, mul( b.m_vel, dt ) );
        if ( b.m_amp != 0 && b.m_freq != 0 )
        {
            // Weaving across its way.
            Vector2 d = norm( b.m_vel );
            Vector2 side = { -d.y, d.x };
            float   w0 = std::sin( 2 * Pi * b.m_freq * ( b.m_age - dt ) + b.m_phase ), w1 = std::sin( 2 * Pi * b.m_freq * b.m_age + b.m_phase );
            b.m_pos = add( b.m_pos, mul( side, b.m_amp * ( w1 - w0 ) ) );
        }
        if ( b.m_bounce > 0 && ( b.m_pos.x < 0 || b.m_pos.x > FieldW ) )
        {
            b.m_vel.x = -b.m_vel.x;
            b.m_pos.x = clampf( b.m_pos.x, 0, FieldW );
            --b.m_bounce;
        }
        float m = b.m_player ? 40.0f : 30.0f;
        if ( b.m_pos.x < -m || b.m_pos.x > FieldW + m || b.m_pos.y < -m - 40 || b.m_pos.y > FieldH + m ) b.m_dead = true;
    }
}

void World::moveLasers()
{
    for ( Laser &l : m_lasers )
    {
        if ( l.m_dead ) continue;
        l.m_t += Dt * m_slow;
        if ( l.m_owner < 0 || l.m_owner >= (int)m_enemies.size() || m_enemies[(size_t)l.m_owner].m_dead ||
             m_enemies[(size_t)l.m_owner].m_gone || l.m_t > l.m_warn + l.m_dur )
        {
            l.m_dead = true;
            continue;
        }
        const Enemy &e = m_enemies[(size_t)l.m_owner];
        l.m_from = worldPos( e, l.m_off );
        if ( l.m_t > l.m_warn ) l.m_dir += l.m_sweep * Dt * m_slow;
        else if ( l.m_aim )
        {
            int pl = nearestPlayer( l.m_from );
            // While warning it still follows the pilot a little, then holds still.
            if ( pl >= 0 && l.m_t < l.m_warn * 0.6f )
            {
                float w = angleOf( sub( m_players[pl].m_pos, l.m_from ) );
                l.m_dir += clampf( wrapDeg( w - l.m_dir ), -60.0f * Dt, 60.0f * Dt );
            }
        }
    }
}

void World::movePickups()
{
    for ( Pickup &p : m_pickups )
    {
        if ( p.m_dead ) continue;
        p.m_t += Dt;
        int   pl = nearestPlayer( p.m_pos );
        bool  pulled = false;
        if ( pl >= 0 )
        {
            const Player &pp = m_players[pl];
            float         d = std::sqrt( dist2( pp.m_pos, p.m_pos ) );
            float         reach = pp.m_magnet > 0 ? 2000.0f : ( p.m_kind == PickE::CREDITS ? 110.0f : 70.0f );
            if ( d < reach )
            {
                pulled = true;
                p.m_vel = add( p.m_vel, mul( norm( sub( pp.m_pos, p.m_pos ) ), 2400.0f * Dt ) );
                float s = len( p.m_vel );
                if ( s > 700 ) p.m_vel = mul( p.m_vel, 700 / s );
            }
        }
        if ( !pulled )
        {
            p.m_vel.x *= 1.0f - 2.0f * Dt;
            p.m_vel.y += ( 70.0f - p.m_vel.y ) * std::min( 1.0f, 2.0f * Dt );
        }
        p.m_pos = add( p.m_pos, mul( p.m_vel, Dt ) );
        if ( p.m_pos.x < 10 ) p.m_vel.x = std::fabs( p.m_vel.x );
        if ( p.m_pos.x > FieldW - 10 ) p.m_vel.x = -std::fabs( p.m_vel.x );
        if ( p.m_pos.y > FieldH + 30 ) p.m_dead = true;
    }
}

// ---------------------------------------------------------------------------------------------------- hits

void World::dropAt( PickE k, Vector2 at, int value, const std::string &item )
{
    Pickup p;
    p.m_kind = k;
    p.m_pos = at;
    p.m_vel = { m_rng.range( -60.0f, 60.0f ), -80.0f };
    p.m_value = value;
    p.m_item = item;
    m_pickups.push_back( p );
}

void World::drop( const Enemy &e, const DropDef &d )
{
    if ( !d.m_mode.empty() )
    {
        bool arcade = m_mode == ModeE::ARCADE;
        if ( ( d.m_mode == "arcade" ) != arcade ) return;
    }
    if ( !m_rng.chance( d.m_chance ) ) return;
    const std::string &w = d.m_what;
    PickE k = w == "power"   ? PickE::POWER
            : w == "shield"  ? PickE::SHIELD
            : w == "armor"   ? PickE::ARMOR
            : w == "weapon"  ? PickE::WEAPON
            : w == "rear"    ? PickE::REAR
            : w == "side"    ? PickE::SIDE
            : w == "life"    ? PickE::LIFE
            : w == "special" ? PickE::SPECIAL
            : w == "cube"    ? PickE::CUBE
            : w == "key"     ? PickE::KEY
            : w == "plan"    ? PickE::PLAN
                             : PickE::CREDITS;
    // Arcade gear has no use in the campaign: it becomes money there.
    if ( m_mode != ModeE::ARCADE && ( k == PickE::POWER || k == PickE::WEAPON || k == PickE::REAR || k == PickE::SIDE ||
                                      k == PickE::LIFE ) )
        k = PickE::CREDITS;
    int value = d.m_value > 0 ? d.m_value : ( k == PickE::CREDITS ? std::max( 25, e.m_def->m_score / 2 ) : 0 );
    dropAt( k, e.m_pos, value, d.m_item );
}

void World::hitEnemy( int idx, float dmg, int owner, Vector2 at )
{
    Enemy &e = m_enemies[(size_t)idx];
    if ( e.m_dead || e.m_gone ) return;
    bool shielded = e.m_def->m_invulnerable;
    if ( e.m_def->m_shielded )
        for ( const Enemy &o : m_enemies )
            if ( o.m_parent == idx && !o.m_dead && !o.m_gone ) shielded = true;
    if ( !e.m_entered && e.m_def->m_kind != EnemyKindE::PART ) shielded = true;
    if ( shielded )
    {
        if ( m_rng.chance( 0.3f ) ) emit( EvE::SPARK, at );
        return;
    }
    e.m_hp -= rule( RuleE::GLASS ) ? dmg * 2.0f : dmg;
    bool big = e.m_def->m_kind == EnemyKindE::BOSS || e.m_def->m_kind == EnemyKindE::PART || e.m_hpMax > 400.0f;
    // A flicker, not a glare: a new flash only once the last has faded (steady fire would keep a ship white).
    if ( e.m_flash < 0.15f ) e.m_flash = big ? 0.35f : 0.8f;
    if ( m_rng.chance( 0.25f ) ) emit( EvE::HIT, at, dmg );
    if ( e.m_hp <= 0 ) killEnemy( idx, owner );
}

void World::killEnemy( int idx, int owner )
{
    Enemy &e = m_enemies[(size_t)idx];
    if ( e.m_dead ) return;
    e.m_dead = true;
    const EnemyDef &d = *e.m_def;
    int size = d.m_explode == "huge" ? 3 : d.m_explode == "large" ? 2 : d.m_explode == "medium" ? 1 : 0;
    SimEvent ev;
    ev.m_type = EvE::EXPLODE;
    ev.m_pos = e.m_pos;
    ev.m_a = (float)size;
    ev.m_ground = e.m_ground;
    ev.m_name = d.m_model;
    m_events.push_back( ev );
    if ( size >= 2 ) emit( EvE::SHAKE, e.m_pos, size == 3 ? 1.0f : 0.4f );
    if ( d.m_invulnerable ) return;
    if ( d.m_scenery )
    {
        ++m_stats.m_structures;
        // Points at the chain's multiplier; a structure is no link of the chain (or chains would never break).
        int who = owner >= 0 ? owner : 0;
        if ( m_players[who].m_active )
        {
            Player &p = m_players[who];
            p.m_score += (long long)std::lround( d.m_score * m_diff.m_credits * chainMult( p.m_chain ) );
        }
        // The money: about what the episode's enemies are worth, so it means as much in the last episode as in the first.
        static const float worth[] = { 1.0f, 1.0f, 1.8f, 3.0f, 4.5f, 6.5f, 8.0f };
        int ep = m_level ? std::clamp( m_level->m_episode, 0, 6 ) : 1;
        for ( DropDef dd : d.m_drops )
        {
            if ( dd.m_what == "credits" ) dd.m_value = (int)std::lround( dd.m_value * worth[ep] );
            drop( e, dd );
        }
        if ( const EnemyDef *ruin = d.m_ruin.empty() ? nullptr : m_cat->enemy( d.m_ruin ) )
        {
            float face = e.m_face;
            Vector2 at = e.m_pos;
            int   i = spawn( ruin, at, nullptr );   // (the enemies may move in memory: e is not used after this)
            if ( i >= 0 )
            {
                m_enemies[(size_t)i].m_face = m_enemies[(size_t)i].m_head = face;
                m_enemies[(size_t)i].m_entered = true;
            }
        }
        return;
    }
    bool extra = d.m_swarm || e.m_extra;
    if ( extra ) ++m_stats.m_swarmKills;
    else ++m_stats.m_kills;
    int score = (int)std::lround( d.m_score * m_diff.m_credits );
    int who = owner >= 0 ? owner : 0;
    if ( m_players[who].m_active )
    {
        // The chain: every kill before the last one's time runs out raises the score's multiplier (the money stays
        // as it is: the workshop's economy does not ride on it).
        Player &p = m_players[who];
        ++p.m_chain;
        p.m_chainT = chainTime();
        p.m_bestChain = std::max( p.m_bestChain, p.m_chain );
        m_stats.m_bestChain = std::max( m_stats.m_bestChain, p.m_chain );
        float mult = chainMult( p.m_chain ) * ( rule( RuleE::TOUGH ) ? 1.5f : 1.0f );
        p.m_score += (long long)std::lround( score * mult );
        if ( m_mode == ModeE::CAMPAIGN )
        {
            int money = (int)std::lround( score * ( e.m_extra ? std::min( d.m_worth, 0.2f ) : d.m_worth ) );
            m_players[who].m_credits += money;
            m_stats.m_credits += money;
        }
    }
    for ( const DropDef &dd : d.m_drops ) drop( e, dd );
    // Arcade: now and then a power-up from the stream of kills.
    if ( m_mode == ModeE::ARCADE && d.m_kind != EnemyKindE::PART && !extra )
    {
        m_powerKills += d.m_kind == EnemyKindE::BOSS ? 40 : ( d.m_hp > 80 ? 4 : 1 );
        if ( m_powerKills >= 26 )
        {
            m_powerKills = 0;
            float r = m_rng.unit();
            dropAt( r < 0.62f ? PickE::POWER : r < 0.8f ? PickE::WEAPON : r < 0.9f ? PickE::SHIELD : PickE::SIDE, e.m_pos, 0, "" );
        }
    }
    if ( e.m_wave >= 0 && (size_t)e.m_wave < m_waveKilled.size() )
    {
        ++m_waveKilled[(size_t)e.m_wave];
        m_waveLast[(size_t)e.m_wave] = e.m_pos;
        --m_waveLeft[(size_t)e.m_wave];
        if ( m_waveLeft[(size_t)e.m_wave] == 0 && m_waveKilled[(size_t)e.m_wave] == m_waveSize[(size_t)e.m_wave] )
        {
            const DropDef &wd = m_waveDrop[(size_t)e.m_wave];
            if ( !wd.m_what.empty() )
            {
                DropDef d2 = wd;
                d2.m_chance = 1.0f;
                drop( e, d2 );
            }
            else if ( m_mode == ModeE::CAMPAIGN )
            {
                int bonus = m_waveBonus[(size_t)e.m_wave];
                if ( bonus < 0 ) bonus = m_waveSize[(size_t)e.m_wave] >= 5 ? 10 * m_waveSize[(size_t)e.m_wave] : 0;
                if ( bonus > 0 ) dropAt( PickE::CREDITS, e.m_pos, bonus, "" );
            }
        }
    }
    if ( m_diff.m_revenge && d.m_kind == EnemyKindE::AIR && !m_enemies.empty() )
    {
        // On the hardest difficulties a dying ship leaves a last bullet at the pilot.
        int pl = nearestPlayer( e.m_pos );
        if ( pl >= 0 && dist2( m_players[pl].m_pos, e.m_pos ) > 140.0f * 140.0f )
        {
            Bullet b;
            b.m_pos = b.m_origin = e.m_pos;
            b.m_vel = mul( norm( sub( m_players[pl].m_pos, e.m_pos ) ), 170.0f * m_diff.m_speed );
            b.m_radius = 5;
            b.m_damage = 5;
            b.m_life = 6;
            b.m_sprite = 6;
            b.m_color = { 255, 120, 200, 255 };
            m_born.push_back( b );
        }
    }
    if ( d.m_kind == EnemyKindE::BOSS )
    {
        ++m_stats.m_bossKills;
        emit( EvE::BOSSDIE, e.m_pos, 0, 0, d.m_name );
        for ( Bullet &b : m_bullets )
            if ( !b.m_player ) b.m_dead = true;
        for ( Laser &l : m_lasers ) l.m_dead = true;
        // Everything the boss carried and launched goes with it.
        for ( Enemy &o : m_enemies )
            if ( !o.m_dead && o.m_parent < 0 && o.m_def->m_kind == EnemyKindE::AIR && o.m_wave < 0 ) o.m_gone = true;
        bool other = false;
        for ( size_t i = 0; i < m_enemies.size(); ++i )
            if ( (int)i != idx && !m_enemies[i].m_dead && m_enemies[i].m_def->m_kind == EnemyKindE::BOSS ) other = true;
        if ( !other ) m_hold = false;
        if ( m_mode == ModeE::ARCADE ) dropAt( PickE::LIFE, e.m_pos, 0, "" );
    }
}

void World::revive( Player &p )
{
    p.m_alive = true;
    p.m_dead = 0;
    p.m_pos = { FieldW * 0.5f, FieldH - 100.0f };
    p.m_vel = { 0, 0 };
    p.m_armor = p.m_armorMax;
    p.m_shield = p.m_shieldDef ? p.m_shieldDef->m_max : 0;
    p.m_energy = p.m_energyMax;
    p.m_invuln = 3.0f;
    if ( p.m_special ) p.m_charges = std::max( p.m_charges, p.m_special->m_charges );
}

void World::hurtPlayer( int i, float dmg, Vector2 at, const EnemyDef *by, const char *how )
{
    Player &p = m_players[i];
    if ( !p.m_alive || p.m_invuln > 0 ) return;
    if ( p.m_barrier > 0 )
    {
        emit( EvE::SPARK, at );
        return;
    }
    dmg *= m_diff.m_damage * ( rule( RuleE::GLASS ) ? 2.0f : 1.0f );
    p.m_damageTaken += dmg;
    m_stats.m_damage += dmg;
    ++p.m_hits;
    p.m_chain = 0;
    p.m_chainT = 0.0f;
    float fromShield = std::min( p.m_shield, dmg );
    p.m_shield -= fromShield;
    float rest = dmg - fromShield;
    p.m_shieldWait = p.m_shieldDef ? p.m_shieldDef->m_delay : 1.0f;
    emit( EvE::PLAYERHIT, at, dmg, rest <= 0 ? 1.0f : 0.0f );
    if ( rest > 0 )
    {
        p.m_armor -= rest;
        emit( EvE::SHAKE, p.m_pos, 0.25f + std::min( 0.5f, rest / 20.0f ) );
    }
    if ( p.m_armor <= 0 )
    {
        if ( m_immortal )
        {
            p.m_armor = p.m_armorMax;
            ++m_stats.m_deaths;
            return;
        }
        p.m_alive = false;
        p.m_dead = 0;
        ++m_stats.m_deaths;
        DeathNote note;
        note.m_pilot = i;
        note.m_t = m_clock;
        note.m_pos = p.m_pos;
        note.m_by = by ? by->m_name : "";
        note.m_how = how;
        note.m_boss = boss() >= 0;
        m_stats.m_deathNotes.push_back( note );
        emit( EvE::PLAYERDIE, p.m_pos );
        SimEvent ev;
        ev.m_type = EvE::EXPLODE;
        ev.m_pos = p.m_pos;
        ev.m_a = 2;
        m_events.push_back( ev );
        // Arcade: a death costs some of the gun's power.
        if ( m_mode == ModeE::ARCADE )
        {
            p.m_load.m_frontLv = std::max( 1, p.m_load.m_frontLv - 2 );
            p.m_load.m_rearLv = std::max( 1, p.m_load.m_rearLv - 1 );
        }
    }
}

void World::take( int i, Pickup &pk )
{
    Player &p = m_players[i];
    pk.m_dead = true;
    std::string what;
    switch ( pk.m_kind )
    {
    case PickE::CREDITS:
    {
        int v = (int)std::lround( pk.m_value * m_diff.m_credits );
        if ( m_mode == ModeE::CAMPAIGN )
        {
            p.m_credits += v;
            m_stats.m_credits += v;
        }
        p.m_score += v;
        what = "credits";
        break;
    }
    case PickE::POWER:
        if ( p.m_load.m_frontLv < 11 ) ++p.m_load.m_frontLv;
        else
        {
            p.m_load.m_rearLv = std::min( 11, p.m_load.m_rearLv + 1 );
            p.m_score += 1000;
        }
        what = "power";
        break;
    case PickE::SHIELD:
        p.m_shield = p.m_shieldDef ? p.m_shieldDef->m_max : 0;
        p.m_energy = p.m_energyMax;
        what = "shield";
        break;
    case PickE::ARMOR:
        p.m_armor = std::min( p.m_armorMax, p.m_armor + p.m_armorMax * 0.35f );
        what = "armor";
        break;
    case PickE::WEAPON:
    {
        std::string w = pk.m_item;
        if ( w.empty() || !m_cat->weapon( w ) )
        {
            // A front gun of this episode or before, not the one carried.
            std::vector<std::string> pool;
            int ep = m_level ? m_level->m_episode : 1;
            for ( const auto &[n, wd] : m_cat->m_weapons )
                if ( wd.m_slot == "front" && wd.m_tier <= ep + 1 && !wd.m_hidden && n != p.m_load.m_front ) pool.push_back( n );
            if ( !pool.empty() ) w = pool[(size_t)m_rng.below( (int)pool.size() )];
        }
        if ( !w.empty() ) p.m_load.m_front = w;
        refit( i );
        what = "weapon";
        break;
    }
    case PickE::REAR:
    {
        std::string w = pk.m_item;
        if ( w.empty() || !m_cat->weapon( w ) )
        {
            std::vector<std::string> pool;
            for ( const auto &[n, wd] : m_cat->m_weapons )
                if ( wd.m_slot == "rear" && !wd.m_hidden && n != p.m_load.m_rear ) pool.push_back( n );
            if ( !pool.empty() ) w = pool[(size_t)m_rng.below( (int)pool.size() )];
        }
        p.m_load.m_rear = w;
        refit( i );
        what = "weapon";
        break;
    }
    case PickE::SIDE:
    {
        std::string w = pk.m_item;
        if ( w.empty() || !m_cat->sidekick( w ) )
        {
            std::vector<std::string> pool;
            for ( const auto &[n, sd] : m_cat->m_sidekicks )
                if ( !sd.m_hidden ) pool.push_back( n );
            if ( !pool.empty() ) w = pool[(size_t)m_rng.below( (int)pool.size() )];
        }
        if ( p.m_load.m_sideL.empty() ) p.m_load.m_sideL = w;
        else if ( p.m_load.m_sideR.empty() ) p.m_load.m_sideR = w;
        else if ( m_rng.chance( 0.5f ) ) p.m_load.m_sideL = w;
        else p.m_load.m_sideR = w;
        refit( i );
        what = "side";
        break;
    }
    case PickE::LIFE:
        ++p.m_lives;
        what = "life";
        break;
    case PickE::SPECIAL:
        ++p.m_charges;
        what = "special";
        break;
    case PickE::CUBE:
        m_stats.m_cubes.push_back( pk.m_item );
        what = "cube";
        break;
    case PickE::KEY:
        m_stats.m_keys.push_back( pk.m_item );
        what = "key";
        break;
    case PickE::PLAN:
        m_stats.m_plans.push_back( pk.m_item );
        what = "plan";
        break;
    }
    SimEvent ev;
    ev.m_type = EvE::PICKUP;
    ev.m_pos = pk.m_pos;
    ev.m_name = what;
    ev.m_a = (float)pk.m_value;
    ev.m_b = (float)i;
    m_events.push_back( ev );
}

void World::collide()
{
    // The pilots' bullets against the enemies: parts first, so a bullet over a boss's gun hits the gun and not the
    // shielded hull beneath it.
    std::vector<size_t> order;
    order.reserve( m_enemies.size() );
    for ( size_t e = 0; e < m_enemies.size(); ++e )
        if ( m_enemies[e].m_parent >= 0 ) order.push_back( e );
    for ( size_t e = 0; e < m_enemies.size(); ++e )
        if ( m_enemies[e].m_parent < 0 ) order.push_back( e );
    for ( Bullet &b : m_bullets )
    {
        if ( b.m_dead || !b.m_player ) continue;
        for ( size_t e : order )
        {
            Enemy &en = m_enemies[e];
            if ( en.m_dead || en.m_gone || en.m_rise > 0 || (int)e == b.m_lastHit ) continue;
            float r = en.m_def->m_radius * ( en.m_ground ? GroundFactor : 1.0f ) * en.m_def->m_scale;
            if ( dist2( b.m_pos, en.m_pos ) > ( r + b.m_radius ) * ( r + b.m_radius ) ) continue;
            if ( !en.m_def->m_solid && en.m_def->m_invulnerable ) continue;
            hitEnemy( (int)e, b.m_damage, b.m_owner, b.m_pos );
            b.m_lastHit = (int)e;
            // A structure of the ground does not stop a shot: it flies on to what it was fired at.
            if ( en.m_def->m_scenery ) continue;
            if ( b.m_explode > 0 )
            {
                for ( size_t o = 0; o < m_enemies.size(); ++o )
                    if ( o != e && !m_enemies[o].m_dead && dist2( m_enemies[o].m_pos, b.m_pos ) < b.m_explode * b.m_explode )
                        hitEnemy( (int)o, b.m_damage * 0.6f, b.m_owner, m_enemies[o].m_pos );
                emit( EvE::EXPLODE, b.m_pos, 0, 1, "", b.m_color );
            }
            if ( b.m_pierce > 0 )
                --b.m_pierce;
            else
            {
                b.m_dead = true;
                if ( b.m_split > 0 ) splinter( m_born, b, m_rng );
                break;
            }
        }
    }
    // The enemies' bullets, lasers and hulls against the pilots.
    for ( int i = 0; i < 2; ++i )
    {
        Player &p = m_players[i];
        if ( !p.m_active || !p.m_alive ) continue;
        float r = p.m_ship ? p.m_ship->m_radius : 9.0f;
        for ( Bullet &b : m_bullets )
        {
            if ( b.m_dead || b.m_player ) continue;
            float hit = r + b.m_radius * 0.85f, d2 = dist2( b.m_pos, p.m_pos );
            if ( d2 < hit * hit )
            {
                b.m_dead = true;
                hurtPlayer( i, b.m_damage, b.m_pos, b.m_src, "bullet" );
                if ( !p.m_alive ) break;
            }
            else if ( !b.m_grazed && p.m_invuln <= 0 && d2 < ( hit + GrazeRange ) * ( hit + GrazeRange ) )
            {
                // A graze: a little score, and the chain waits on.
                b.m_grazed = true;
                ++p.m_grazes;
                ++m_stats.m_grazes;
                p.m_score += (long long)std::lround( 10.0f * chainMult( p.m_chain ) );
                if ( p.m_chain > 0 ) p.m_chainT = chainTime();
                emit( EvE::GRAZE, b.m_pos, 0, (float)i );
            }
        }
        for ( Laser &l : m_lasers )
        {
            if ( l.m_dead || l.m_t < l.m_warn ) continue;
            Vector2 d = dirOf( l.m_dir );
            Vector2 rel = sub( p.m_pos, l.m_from );
            float   along = rel.x * d.x + rel.y * d.y;
            if ( along < 0 ) continue;
            float across = std::fabs( rel.x * -d.y + rel.y * d.x );
            if ( across < l.m_width * 0.5f + r * 0.7f )
                hurtPlayer( i, l.m_damage * Dt * 4.0f, p.m_pos,
                            l.m_owner >= 0 && l.m_owner < (int)m_enemies.size() ? m_enemies[(size_t)l.m_owner].m_def : nullptr, "laser" );
        }
        for ( size_t e = 0; e < m_enemies.size(); ++e )
        {
            Enemy &en = m_enemies[e];
            if ( en.m_dead || en.m_gone || en.m_ground || en.m_rise > 0 ) continue;
            float er = en.m_def->m_radius * 0.75f * en.m_def->m_scale;
            if ( dist2( en.m_pos, p.m_pos ) < ( er + r ) * ( er + r ) )
            {
                if ( p.m_invuln <= 0 && p.m_barrier <= 0 )
                {
                    hurtPlayer( i, en.m_def->m_contact, p.m_pos, en.m_def, "ram" );
                    p.m_invuln = std::max( p.m_invuln, 0.5f );
                    if ( en.m_def->m_kind == EnemyKindE::AIR ) hitEnemy( (int)e, en.m_def->m_contact * 2.0f, i, en.m_pos );
                }
            }
        }
        for ( Pickup &pk : m_pickups )
            if ( !pk.m_dead && dist2( pk.m_pos, p.m_pos ) < 30.0f * 30.0f ) take( i, pk );
    }
}

void World::compact()
{
    // Indices of the enemies that stay (parents and lasers refer to them).
    std::vector<int> map( m_enemies.size(), -1 );
    int              n = 0;
    for ( size_t i = 0; i < m_enemies.size(); ++i )
    {
        const Enemy &e = m_enemies[i];
        bool         drop0 = e.m_dead || e.m_gone;
        if ( e.m_gone && !e.m_dead && e.m_wave >= 0 && (size_t)e.m_wave < m_waveLeft.size() ) --m_waveLeft[(size_t)e.m_wave];
        if ( !drop0 ) map[i] = n++;
    }
    if ( n != (int)m_enemies.size() )
    {
        std::vector<Enemy> keep;
        keep.reserve( (size_t)n );
        for ( size_t i = 0; i < m_enemies.size(); ++i )
            if ( map[i] >= 0 )
            {
                Enemy e = m_enemies[i];
                if ( e.m_parent >= 0 ) e.m_parent = map[(size_t)e.m_parent];
                keep.push_back( e );
            }
        // A part whose parent went away goes too (it is checked next step: parent -1 and kind part).
        m_enemies.swap( keep );
        for ( Laser &l : m_lasers ) l.m_owner = l.m_owner >= 0 && l.m_owner < (int)map.size() ? map[(size_t)l.m_owner] : -1;
        for ( Enemy &e : m_enemies )
            if ( e.m_def->m_kind == EnemyKindE::PART && e.m_parent < 0 ) e.m_gone = true;
    }
    m_bullets.erase( std::remove_if( m_bullets.begin(), m_bullets.end(), []( const Bullet &b ) { return b.m_dead; } ),
                     m_bullets.end() );
    m_lasers.erase( std::remove_if( m_lasers.begin(), m_lasers.end(), []( const Laser &l ) { return l.m_dead || l.m_owner < 0; } ),
                    m_lasers.end() );
    m_pickups.erase( std::remove_if( m_pickups.begin(), m_pickups.end(), []( const Pickup &p ) { return p.m_dead; } ),
                     m_pickups.end() );
    // Never more than this many enemy bullets (the oldest go first): the hardest patterns stay readable.
    if ( m_bullets.size() > 1600 ) m_bullets.erase( m_bullets.begin(), m_bullets.begin() + (long)( m_bullets.size() - 1600 ) );
}

// ---------------------------------------------------------------------------------------------------- the autopilot

PlayerInput autopilot( const World &w, int i )
{
    PlayerInput   in;
    const Player &p = w.m_players[i];
    in.m_fire = true;
    if ( !p.m_alive ) return in;
    // Where it would like to be: below the nearest enemy, low on the field; with a short-range gun (lightning,
    // flames) close enough to reach.
    Vector2 goal = { FieldW * 0.5f, FieldH * 0.8f };
    float   best = 1e9f;
    float   reach = 2000.0f;
    if ( p.m_front )
        for ( const ShotDef &s : p.m_front->m_shots )
        {
            float r = s.m_kind == "chain" ? s.m_p.range( "range", 260 ).m_b
                    : s.m_kind == "beam"  ? 2000.0f
                                          : s.m_p.range( "speed", 820 ).m_b * s.m_p.range( "life", 1.3f ).m_b;
            if ( s.m_from <= p.m_load.m_frontLv && s.m_kind != "orbit" ) reach = std::min( reach, std::max( r, 120.0f ) );
        }
    float targetY = -1.0f;
    // A shielded body whose parts still live is no target: its parts are.
    std::vector<char> shut( w.m_enemies.size(), 0 );
    for ( size_t e = 0; e < w.m_enemies.size(); ++e )
    {
        const Enemy &pe = w.m_enemies[e];
        if ( pe.m_parent >= 0 && !pe.m_dead && !pe.m_gone && w.m_enemies[(size_t)pe.m_parent].m_def->m_shielded )
            shut[(size_t)pe.m_parent] = 1;
    }
    for ( size_t ei = 0; ei < w.m_enemies.size(); ++ei )
    {
        const Enemy &e = w.m_enemies[ei];
        if ( shut[ei] ) continue;
        if ( e.m_dead || e.m_gone || e.m_rise > 0 || e.m_def->m_invulnerable || ( e.m_def->m_scenery && e.m_def->m_fire.empty() ) ||
             e.m_pos.y < 0 || e.m_pos.y > p.m_pos.y - 40 )
            continue;
        if ( reach < 560.0f && e.m_pos.y > FieldH * 0.75f ) continue;
        float score = std::fabs( e.m_pos.x - p.m_pos.x ) + ( p.m_pos.y - e.m_pos.y ) * 0.3f -
                      ( e.m_def->m_kind == EnemyKindE::BOSS ? 200.0f : 0.0f );
        if ( score < best )
        {
            best = score;
            goal.x = e.m_pos.x;
            targetY = e.m_pos.y;
        }
    }
    if ( targetY >= 0 && reach < 560.0f ) goal.y = clampf( targetY + reach * 0.75f, FieldH * 0.4f, FieldH * 0.8f );
    for ( const Pickup &pk : w.m_pickups )
        if ( std::fabs( pk.m_pos.y - p.m_pos.y ) < 220 && pk.m_pos.y > 200 )
        {
            goal = pk.m_pos;
            break;
        }
    // Only what is near matters for the next half second.
    std::vector<const Bullet *> near;
    for ( const Bullet &b : w.m_bullets )
        if ( !b.m_player && dist2( b.m_pos, p.m_pos ) < 320.0f * 320.0f ) near.push_back( &b );
    std::vector<const Enemy *> nearE;
    for ( const Enemy &e : w.m_enemies )
        if ( !e.m_dead && !e.m_gone && !e.m_ground && dist2( e.m_pos, p.m_pos ) < 420.0f * 420.0f ) nearE.push_back( &e );
    // Choose among moves the one with the least danger in the next half second.
    float speed = p.m_ship ? p.m_ship->m_speed : 330.0f;
    float bestScore = 1e18f;
    Vector2 bestDir = { 0, 0 };
    bool    bestFocus = false;
    for ( int c = 0; c < 17; ++c )
    {
        bool    focus = c >= 9;
        Vector2 d = c == 0 || c == 9 ? Vector2{ 0, 0 } : dirOf( ( c % 8 ) * 45.0f );
        float   s = focus ? speed * 0.45f : speed;
        float   danger = 0.0f;
        for ( float t = 0.05f; t <= 0.5f; t += 0.075f )
        {
            Vector2 at = add( p.m_pos, mul( d, s * t ) );
            if ( at.x < 18 || at.x > FieldW - 18 || at.y < 40 || at.y > FieldH - 24 ) danger += 50.0f;
            for ( const Bullet *bb : near )
            {
                const Bullet &b = *bb;
                Vector2 bp = add( b.m_pos, mul( b.m_vel, t ) );
                float   rr = ( p.m_ship ? p.m_ship->m_radius : 9.0f ) + b.m_radius + 10.0f;
                float   d2 = dist2( bp, at );
                if ( d2 < rr * rr * 4.0f ) danger += ( 1.0f - t ) * ( d2 < rr * rr ? 30.0f : 3.0f );
            }
            for ( const Laser &l : w.m_lasers )
            {
                if ( l.m_dead ) continue;
                // Where a sweeping beam will point by then.
                float   ahead = std::max( 0.0f, l.m_t + t - l.m_warn );
                float   swept = l.m_t > l.m_warn ? t : ahead;
                Vector2 ld = dirOf( l.m_dir + l.m_sweep * swept );
                Vector2 rel = sub( at, l.m_from );
                if ( rel.x * ld.x + rel.y * ld.y < 0 ) continue;
                float across = std::fabs( rel.x * -ld.y + rel.y * ld.x );
                if ( across < l.m_width + 24 ) danger += 20.0f;
            }
            for ( const Enemy *ee : nearE )
            {
                const Enemy &e = *ee;
                Vector2 ep = add( e.m_pos, mul( e.m_vel, t ) );
                float   rr = e.m_def->m_radius * e.m_def->m_scale + 20.0f;
                if ( dist2( ep, at ) < rr * rr ) danger += 25.0f;
            }
        }
        Vector2 end = add( p.m_pos, mul( d, s * 0.3f ) );
        float   score = danger * 100.0f + std::sqrt( dist2( end, goal ) ) * 0.5f;
        if ( score < bestScore )
        {
            bestScore = score;
            bestDir = d;
            bestFocus = focus;
        }
    }
    in.m_x = bestDir.x;
    in.m_y = bestDir.y;
    in.m_focus = bestFocus;
    // The special when it gets tight.
    in.m_special = p.m_charges > 0 && p.m_armor < p.m_armorMax * 0.35f && ( (int)( w.m_clock * 60 ) % 30 == 0 );
    in.m_mode = p.m_rear && p.m_rear->m_modes > 1 && (int)( w.m_clock * 60 ) % 900 == 0;
    return in;
}

SimResult simulate( const Catalog &cat, const LevelDef &level, int difficulty, const Loadout &load, uint32_t seed,
                    bool immortal, ModeE mode, int lives )
{
    World *w = new World();
    w->begin( &cat, &level, mode, difficulty, seed );
    w->m_immortal = immortal;
    w->addPlayer( 0, load, lives );
    SimResult r;
    r.m_level = level.m_name;
    float limit = level.m_length + 400.0f;
    while ( !w->finished() && !w->failed() && w->m_clock < limit )
    {
        PlayerInput in[2] = { autopilot( *w, 0 ), {} };
        bool held = w->m_hold;
        w->step( in );
        if ( held ) r.m_bossTime += Dt;
        if ( r.m_firstDeath < 0 && w->m_stats.m_deaths > 0 ) r.m_firstDeath = w->m_clock;
    }
    r.m_won = w->finished();
    r.m_time = w->m_clock;
    r.m_kills = w->m_stats.m_kills;
    r.m_swarm = w->m_stats.m_swarm;
    r.m_swarmKills = w->m_stats.m_swarmKills;
    r.m_structures = w->m_stats.m_structures;
    r.m_spawned = w->m_stats.m_spawned;
    r.m_credits = w->m_stats.m_credits;
    r.m_damage = w->m_stats.m_damage;
    r.m_deaths = w->m_stats.m_deaths;
    r.m_cubes = (int)w->m_stats.m_cubes.size();
    r.m_keys = (int)w->m_stats.m_keys.size();
    r.m_plans = (int)w->m_stats.m_plans.size();
    r.m_timeline = w->m_time;
    r.m_bosses = w->m_stats.m_bossKills;
    // What still holds the field when the time ran out (a boss that cannot be beaten shows here).
    if ( !r.m_won && !w->failed() )
        for ( const Enemy &e : w->m_enemies )
            if ( !e.m_dead && !e.m_gone && e.m_def->m_kind == EnemyKindE::BOSS ) r.m_stuck = e.m_def->m_name;
    delete w;
    return r;
}

}   // namespace zenith
