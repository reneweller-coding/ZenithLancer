/**
 * @file Data.cpp
 * @brief Reading the data files.
 * @author Rene Weller
 */
#include "Data.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace zenith
{

namespace fs = std::filesystem;

namespace
{

/** @brief The palette's named colours (models and patterns may use them instead of "#rrggbb"). */
const std::map<std::string, Color> &palette()
{
    static const std::map<std::string, Color> p = {
        { "white", { 240, 240, 240, 255 } },  { "black", { 14, 14, 18, 255 } },
        { "steel", { 150, 160, 172, 255 } },  { "gunmetal", { 72, 78, 88, 255 } },
        { "graphite", { 44, 46, 52, 255 } },  { "silver", { 200, 206, 214, 255 } },
        { "ivory", { 228, 222, 204, 255 } },  { "sand", { 196, 172, 128, 255 } },
        { "rust", { 150, 72, 40, 255 } },     { "crimson", { 178, 30, 44, 255 } },
        { "red", { 230, 50, 40, 255 } },      { "orange", { 255, 140, 30, 255 } },
        { "amber", { 255, 180, 40, 255 } },   { "gold", { 230, 186, 70, 255 } },
        { "yellow", { 250, 230, 70, 255 } },  { "lime", { 160, 240, 60, 255 } },
        { "green", { 60, 190, 80, 255 } },    { "olive", { 100, 110, 60, 255 } },
        { "teal", { 30, 150, 150, 255 } },    { "cyan", { 60, 220, 255, 255 } },
        { "sky", { 110, 180, 255, 255 } },    { "blue", { 50, 90, 220, 255 } },
        { "navy", { 26, 36, 90, 255 } },      { "violet", { 140, 80, 230, 255 } },
        { "magenta", { 230, 60, 200, 255 } }, { "pink", { 255, 130, 180, 255 } },
        { "bone", { 214, 204, 180, 255 } },   { "flesh", { 170, 100, 90, 255 } },
        { "moss", { 70, 100, 50, 255 } },     { "chitin", { 60, 44, 40, 255 } },
        { "plasma", { 120, 200, 255, 255 } }, { "ember", { 255, 96, 32, 255 } },
    };
    return p;
}

/** @brief Splits a line into words: "quoted text" is one word, "#" outside quotes ends the line. */
std::vector<std::string> words( const std::string &s )
{
    std::vector<std::string> out;
    std::string              cur;
    bool                     quoted = false, any = false;
    for ( size_t i = 0; i < s.size(); ++i )
    {
        char c = s[i];
        if ( quoted )
        {
            if ( c == '"' )
                quoted = false;
            else
                cur += c;
            continue;
        }
        if ( c == '"' )
        {
            quoted = true;
            any = true;
            continue;
        }
        if ( c == '#' )
        {
            // "#" starts a comment unless it begins a colour ("color=#ff0000", or "#ff0000" as a word of its own).
            bool hexNext = i + 1 < s.size() && isxdigit( (unsigned char)s[i + 1] );
            bool colour = hexNext && ( ( !cur.empty() && cur.back() == '=' ) || ( cur.empty() && !out.empty() ) );
            if ( !colour ) break;
        }
        if ( c == ' ' || c == '\t' || c == '\r' )
        {
            if ( !cur.empty() || any ) out.push_back( cur );
            cur.clear();
            any = false;
            continue;
        }
        cur += c;
        any = true;
    }
    if ( !cur.empty() || any ) out.push_back( cur );
    return out;
}

/** @brief Turns words into a line. */
Line makeLine( const std::vector<std::string> &w, size_t first, const std::string &file, int no )
{
    Line l;
    l.m_file = file;
    l.m_line = no;
    if ( first < w.size() ) l.m_key = w[first];
    for ( size_t i = first + 1; i < w.size(); ++i )
    {
        size_t eq = w[i].find( '=' );
        if ( eq != std::string::npos && eq > 0 )
            l.m_kv[w[i].substr( 0, eq )] = w[i].substr( eq + 1 );
        else
            l.m_args.push_back( w[i] );
    }
    return l;
}

float toNum( const std::string &s, float def )
{
    if ( s.empty() ) return def;
    char *end = nullptr;
    float v = strtof( s.c_str(), &end );
    return end == s.c_str() ? def : v;
}

Range toRange( const std::string &s, float def )
{
    size_t dots = s.find( ".." );
    if ( dots == std::string::npos )
    {
        float v = toNum( s, def );
        return { v, v };
    }
    return { toNum( s.substr( 0, dots ), def ), toNum( s.substr( dots + 2 ), def ) };
}

/** @brief Checks a line's named values against the known ones. */
void checkKeys( const Line &l, const std::set<std::string> &known, std::vector<std::string> &warnings )
{
    for ( const auto &kv : l.m_kv )
        if ( !known.count( kv.first ) )
            warnings.push_back( l.where() + ": unknown value \"" + kv.first + "\" on \"" + l.m_key + "\"" );
}

/** @brief "a-b" or "a" as a level span. */
void span( const std::string &s, int &from, int &to, int defFrom, int defTo )
{
    from = defFrom;
    to = defTo;
    if ( s.empty() ) return;
    size_t dash = s.find( '-' );
    if ( dash == std::string::npos )
        from = to = atoi( s.c_str() );
    else
    {
        from = atoi( s.substr( 0, dash ).c_str() );
        to = atoi( s.substr( dash + 1 ).c_str() );
    }
}

const std::set<std::string> ShotKeys = { "lv",    "mode",   "n",      "dx",     "spread", "angle", "speed",
                                         "dmg",   "size",   "pierce", "life",   "home",   "amp",   "freq",
                                         "radius", "color", "sprite", "every",  "x",      "y",     "accel",
                                         "range", "chain",  "width",  "bounce", "drag",   "spin",  "jitter",
                                         "delay", "length", "trail",  "split",  "slow",   "push", "phase" };
const std::set<std::string> FireKeys = { "every", "delay", "speed", "n",     "spread", "dir",  "bullet", "color",
                                         "burst", "gap",   "rot",   "accel", "from",   "until", "x",     "z",
                                         "mirror", "cap",  "spawn", "turn",  "dur",    "warn", "width",  "size",
                                         "damage", "life", "diff",  "aim",   "arc",    "sweep", "parts", "path",
                                         "side",  "amp",   "freq",  "stop",  "wait",   "cx",   "cy",     "r",
                                         "pts",   "exit",  "phase" };
const std::set<std::string> WaveKeys = { "n",     "form",  "x",     "y",     "spacing", "gap",   "path", "side",
                                         "mirror", "speed", "amp",  "freq",  "stop",    "wait",  "dir",  "turn",
                                         "cx",    "cy",    "r",     "pts",   "dur",     "rows",  "cols", "dx",
                                         "dy",    "spread", "repeat", "every", "diff",   "range", "leave", "accel",
                                         "delay", "hp",    "drop",  "value", "item",    "exit",  "face", "phase",
                                         "item2", "chance", "loop",  "orbit", "seed",   "hold",  "clear", "bonus" };

}   // namespace

Color parseColor( const std::string &text, Color def )
{
    if ( text.empty() ) return def;
    if ( text[0] == '#' )
    {
        std::string h = text.substr( 1 );
        auto        hex = []( const std::string &s ) { return (unsigned char)strtol( s.c_str(), nullptr, 16 ); };
        if ( h.size() == 3 )
            return { (unsigned char)( hex( h.substr( 0, 1 ) ) * 17 ), (unsigned char)( hex( h.substr( 1, 1 ) ) * 17 ),
                     (unsigned char)( hex( h.substr( 2, 1 ) ) * 17 ), 255 };
        if ( h.size() == 6 ) return { hex( h.substr( 0, 2 ) ), hex( h.substr( 2, 2 ) ), hex( h.substr( 4, 2 ) ), 255 };
        if ( h.size() == 8 )
            return { hex( h.substr( 0, 2 ) ), hex( h.substr( 2, 2 ) ), hex( h.substr( 4, 2 ) ),
                     hex( h.substr( 6, 2 ) ) };
        return def;
    }
    auto it = palette().find( text );
    return it != palette().end() ? it->second : def;
}

std::string Line::str( const std::string &k, const std::string &def ) const
{
    auto it = m_kv.find( k );
    return it != m_kv.end() ? it->second : def;
}

float Line::num( const std::string &k, float def ) const
{
    auto it = m_kv.find( k );
    return it != m_kv.end() ? toRange( it->second, def ).m_a : def;
}

Range Line::range( const std::string &k, float def ) const
{
    auto it = m_kv.find( k );
    return it != m_kv.end() ? toRange( it->second, def ) : Range{ def, def };
}

Color Line::color( const std::string &k, Color def ) const
{
    auto it = m_kv.find( k );
    return it != m_kv.end() ? parseColor( it->second, def ) : def;
}

float Line::argNum( size_t i, float def ) const
{
    return i < m_args.size() ? toNum( m_args[i], def ) : def;
}

bool readBlocks( const std::string &path, const std::vector<std::string> &types, std::vector<Block> &out,
                 std::vector<std::string> &errors )
{
    std::ifstream in( path, std::ios::binary );
    if ( !in )
    {
        errors.push_back( path + ": cannot be read" );
        return false;
    }
    std::string file = fs::path( path ).filename().string();
    std::string parent = fs::path( path ).parent_path().filename().string();
    if ( !parent.empty() && parent != "data" ) file = parent + "/" + file;
    std::string line;
    int         no = 0;
    Block      *cur = nullptr;
    while ( std::getline( in, line ) )
    {
        ++no;
        if ( !line.empty() && line.back() == '\r' ) line.pop_back();
        if ( no == 1 && line.size() >= 3 && (unsigned char)line[0] == 0xEF ) line = line.substr( 3 );   // BOM
        size_t first = line.find_first_not_of( " \t" );
        if ( first == std::string::npos ) continue;
        if ( line[first] == '>' )
        {
            if ( !cur )
            {
                errors.push_back( file + ":" + std::to_string( no ) + ": prose outside a block" );
                continue;
            }
            std::string text = line.substr( first + 1 );
            if ( !text.empty() && text[0] == ' ' ) text = text.substr( 1 );
            cur->m_prose.push_back( text );
            continue;
        }
        if ( line[first] == '#' ) continue;
        std::vector<std::string> w = words( line.substr( first ) );
        if ( w.empty() ) continue;
        // A block starts at the line's beginning; the block's own lines are indented.
        if ( first == 0 && std::find( types.begin(), types.end(), w[0] ) != types.end() )
        {
            out.push_back( {} );
            cur = &out.back();
            cur->m_type = w[0];
            cur->m_name = w.size() > 1 ? w[1] : "";
            cur->m_head = makeLine( w, 0, file, no );
            continue;
        }
        if ( !cur )
        {
            errors.push_back( file + ":" + std::to_string( no ) + ": \"" + w[0] + "\" outside a block" );
            continue;
        }
        cur->m_lines.push_back( makeLine( w, 0, file, no ) );
    }
    return true;
}

namespace
{

void span( const std::string &s, int &from, int &to, int defFrom, int defTo );

FireDef readFire( const Line &l, std::vector<std::string> &warnings )
{
    checkKeys( l, FireKeys, warnings );
    FireDef f;
    f.m_where = l.where();
    f.m_pattern = l.arg( 0, "aimed" );
    f.m_every = l.num( "every", f.m_every );
    f.m_delay = l.num( "delay", f.m_delay );
    f.m_speed = l.num( "speed", f.m_speed );
    f.m_n = l.integer( "n", f.m_n );
    f.m_spread = l.num( "spread", f.m_spread );
    f.m_dir = l.num( "dir", f.m_dir );
    f.m_bullet = l.str( "bullet", f.m_bullet );
    f.m_color = l.color( "color", f.m_color );
    f.m_burst = l.integer( "burst", f.m_burst );
    f.m_gap = l.num( "gap", f.m_gap );
    f.m_rot = l.num( "rot", f.m_rot );
    f.m_accel = l.num( "accel", f.m_accel );
    f.m_from = l.num( "from", f.m_from );
    f.m_until = l.num( "until", f.m_until );
    f.m_x = l.num( "x", f.m_x );
    f.m_z = l.num( "z", f.m_z );
    f.m_mirror = l.integer( "mirror", 0 ) != 0;
    f.m_cap = l.integer( "cap", 0 );
    f.m_spawn = l.str( "spawn", l.arg( 1 ) );
    f.m_turn = l.num( "turn", f.m_turn );
    f.m_dur = l.num( "dur", f.m_dur );
    f.m_warn = l.num( "warn", f.m_warn );
    f.m_width = l.num( "width", f.m_width );
    f.m_size = l.num( "size", 0.0f );
    f.m_damage = l.num( "damage", 0.0f );
    f.m_life = l.num( "life", f.m_life );
    if ( l.has( "diff" ) )
    {
        f.m_onlyDiff = true;
        span( l.str( "diff" ), f.m_minDiff, f.m_maxDiff, 0, 99 );
        if ( l.str( "diff" ).find( '-' ) == std::string::npos ) f.m_maxDiff = 99;
    }
    f.m_aim = l.integer( "aim", -1 );
    f.m_line = l;
    f.m_parts = l.integer( "parts", -1 );
    if ( f.m_pattern == "missile" && f.m_bullet == "orb" ) f.m_bullet = "missile";
    if ( f.m_pattern == "mine" && f.m_bullet == "orb" ) f.m_bullet = "mine";
    return f;
}

ShotDef readShot( const Line &l, std::vector<std::string> &warnings )
{
    checkKeys( l, ShotKeys, warnings );
    ShotDef s;
    s.m_kind = l.arg( 0, "row" );
    span( l.str( "lv" ), s.m_from, s.m_to, 1, 11 );
    s.m_mode = l.integer( "mode", 0 );
    s.m_p = l;
    return s;
}

/** @brief The common head of the workshop's goods. */
template <typename T> bool goodsLine( T &d, const Line &l )
{
    if ( l.m_key == "title" ) d.m_title = l.arg( 0 );
    else if ( l.m_key == "desc" ) d.m_desc = l.arg( 0 );
    else if ( l.m_key == "price" ) d.m_price = (int)l.argNum( 0, (float)d.m_price );
    else if ( l.m_key == "tier" ) d.m_tier = (int)l.argNum( 0, 1 );
    else if ( l.m_key == "hidden" ) d.m_hidden = l.argNum( 0, 1 ) != 0;
    else return false;
    return true;
}

void loadoutLine( Loadout &o, const Line &l )
{
    if ( l.m_key == "ship" ) o.m_ship = l.arg( 0 );
    else if ( l.m_key == "front" )
    {
        o.m_front = l.arg( 0 );
        o.m_frontLv = (int)l.argNum( 1, 1 );
    }
    else if ( l.m_key == "rear" )
    {
        o.m_rear = l.arg( 0 );
        o.m_rearLv = (int)l.argNum( 1, 1 );
    }
    else if ( l.m_key == "side" )
    {
        o.m_sideL = l.arg( 0 ) == "-" ? "" : l.arg( 0 );
        o.m_sideR = l.arg( 1 ) == "-" ? "" : l.arg( 1 );
    }
    else if ( l.m_key == "gen" ) o.m_gen = l.arg( 0 );
    else if ( l.m_key == "shield" ) o.m_shield = l.arg( 0 );
    else if ( l.m_key == "special" ) o.m_special = l.arg( 0 );
}

void unknown( const Block &b, const Line &l, std::vector<std::string> &errors )
{
    errors.push_back( l.where() + ": \"" + l.m_key + "\" is not known in a " + b.m_type );
}

}   // namespace

bool Catalog::load( const std::string &dir )
{
    m_dir = dir;
    const std::vector<std::string> types = { "model",  "enemy",     "front",  "rear",   "sidekick", "ship",
                                             "generator", "shield", "special", "level", "episode",  "cube",
                                             "briefing", "radio",   "intro",   "ending", "bio",     "difficulty", "debrief",
                                             "pilot",   "theme" };
    std::vector<std::string> files;
    std::error_code          e;
    if ( !fs::is_directory( dir, e ) )
    {
        m_errors.push_back( "no data folder: " + dir );
        return false;
    }
    for ( const auto &entry : fs::recursive_directory_iterator( dir, e ) )
    {
        if ( !entry.is_regular_file() || entry.path().extension() != ".txt" ) continue;
        std::string rel = fs::relative( entry.path(), dir, e ).generic_string();
        // Stories: German always, other languages lie beside it (story/en ...) and are not read here.
        if ( rel.rfind( "story/", 0 ) == 0 && rel.rfind( "story/de/", 0 ) != 0 ) continue;
        if ( rel.rfind( "lang/", 0 ) == 0 ) continue;
        files.push_back( entry.path().string() );
    }
    std::sort( files.begin(), files.end() );
    std::vector<Block> blocks;
    for ( const std::string &f : files ) readBlocks( f, types, blocks, m_errors );

    for ( const Block &b : blocks )
    {
        std::string where = b.m_head.where();
        auto        dup = [&]( bool exists ) {
            if ( exists ) m_errors.push_back( where + ": " + b.m_type + " \"" + b.m_name + "\" is defined twice" );
        };
        if ( b.m_name.empty() && b.m_type != "theme" )
        {
            m_errors.push_back( where + ": " + b.m_type + " without a name" );
            continue;
        }
        if ( b.m_type == "model" )
        {
            dup( m_models.count( b.m_name ) > 0 );
            ModelDef &m = m_models[b.m_name];
            m = {};
            m.m_name = b.m_name;
            m.m_where = where;
            m.m_scale = b.m_head.num( "scale", 1.0f );
            for ( const Line &l : b.m_lines )
            {
                if ( l.m_key == "part" )
                {
                    Line p = l;
                    p.m_key = l.arg( 0 );
                    if ( !p.m_args.empty() ) p.m_args.erase( p.m_args.begin() );
                    m.m_parts.push_back( p );
                }
                else if ( l.m_key == "scale" )
                    m.m_scale = l.argNum( 0, 1.0f );
                else
                    unknown( b, l, m_errors );
            }
        }
        else if ( b.m_type == "enemy" )
        {
            dup( m_enemies.count( b.m_name ) > 0 );
            EnemyDef &d = m_enemies[b.m_name];
            d = {};
            d.m_name = b.m_name;
            d.m_model = b.m_name;
            d.m_where = where;
            d.m_path.m_key = "straight";
            for ( const Line &l : b.m_lines )
            {
                const std::string &k = l.m_key;
                if ( k == "title" ) d.m_title = l.arg( 0 );
                else if ( k == "model" ) d.m_model = l.arg( 0 );
                else if ( k == "kind" )
                {
                    std::string v = l.arg( 0 );
                    d.m_kind = v == "ground" ? EnemyKindE::GROUND
                             : v == "boss"   ? EnemyKindE::BOSS
                             : v == "part"   ? EnemyKindE::PART
                             : v == "obstacle" ? EnemyKindE::OBSTACLE
                                               : EnemyKindE::AIR;
                    if ( v != "air" && v != "ground" && v != "boss" && v != "part" && v != "obstacle" )
                        m_errors.push_back( l.where() + ": unknown kind " + v );
                }
                else if ( k == "hp" ) d.m_hp = l.argNum( 0, d.m_hp );
                else if ( k == "radius" ) d.m_radius = l.argNum( 0, d.m_radius );
                else if ( k == "score" ) d.m_score = (int)l.argNum( 0, (float)d.m_score );
                else if ( k == "speed" ) d.m_speed = l.argNum( 0, d.m_speed );
                else if ( k == "path" )
                {
                    checkKeys( l, WaveKeys, m_warnings );
                    d.m_path = l;
                    d.m_path.m_key = l.arg( 0, "straight" );
                }
                else if ( k == "face" ) d.m_face = l.arg( 0, "move" );
                else if ( k == "fire" ) d.m_fire.push_back( readFire( l, m_warnings ) );
                else if ( k == "drop" )
                {
                    DropDef dr;
                    dr.m_what = l.arg( 0 );
                    dr.m_chance = l.argNum( 1, 1.0f );
                    dr.m_value = l.integer( "value", 0 );
                    dr.m_item = l.str( "item" );
                    dr.m_mode = l.str( "mode" );
                    d.m_drops.push_back( dr );
                }
                else if ( k == "explode" ) d.m_explode = l.arg( 0, "small" );
                else if ( k == "contact" ) d.m_contact = l.argNum( 0, d.m_contact );
                else if ( k == "part" ) d.m_parts.push_back( { l.arg( 0 ), l.num( "x" ), l.num( "z" ), l.num( "y" ) } );
                else if ( k == "shielded" ) d.m_shielded = l.argNum( 0, 1 ) != 0;
                else if ( k == "aimhead" ) d.m_aimHead = l.argNum( 0, 1 ) != 0;
                else if ( k == "scale" ) d.m_scale = l.argNum( 0, 1.0f );
                else if ( k == "solid" ) d.m_solid = l.argNum( 0, 1 ) != 0;
                else if ( k == "invulnerable" ) d.m_invulnerable = l.argNum( 0, 1 ) != 0;
                else if ( k == "life" ) d.m_life = l.argNum( 0, 40.0f );
                else unknown( b, l, m_errors );
            }
            if ( d.m_kind == EnemyKindE::GROUND && d.m_path.m_key == "straight" && d.m_path.m_args.empty() )
                d.m_path.m_key = "ground";
            if ( d.m_kind == EnemyKindE::BOSS && d.m_path.m_args.empty() ) d.m_path.m_key = "boss";
        }
        else if ( b.m_type == "front" || b.m_type == "rear" )
        {
            dup( m_weapons.count( b.m_name ) > 0 );
            WeaponDef &w = m_weapons[b.m_name];
            w = {};
            w.m_name = b.m_name;
            w.m_slot = b.m_type;
            w.m_where = where;
            m_order.push_back( b.m_name );
            for ( const Line &l : b.m_lines )
            {
                if ( goodsLine( w, l ) ) continue;
                if ( l.m_key == "rate" ) w.m_rate = toRange( l.arg( 0 ), 6 );
                else if ( l.m_key == "energy" ) w.m_energy = toRange( l.arg( 0 ), 5 );
                else if ( l.m_key == "sound" ) w.m_sound = l.arg( 0 );
                else if ( l.m_key == "mode1" ) w.m_mode1 = l.arg( 0 );
                else if ( l.m_key == "mode2" )
                {
                    w.m_mode2 = l.arg( 0 );
                    w.m_modes = 2;
                }
                else if ( l.m_key == "shot" ) w.m_shots.push_back( readShot( l, m_warnings ) );
                else unknown( b, l, m_errors );
            }
            if ( w.m_shots.empty() ) m_errors.push_back( where + ": gun " + w.m_name + " has no shot lines" );
        }
        else if ( b.m_type == "sidekick" )
        {
            dup( m_sidekicks.count( b.m_name ) > 0 );
            SidekickDef &s = m_sidekicks[b.m_name];
            s = {};
            s.m_name = b.m_name;
            s.m_model = b.m_name;
            s.m_where = where;
            m_order.push_back( b.m_name );
            for ( const Line &l : b.m_lines )
            {
                if ( goodsLine( s, l ) ) continue;
                if ( l.m_key == "model" ) s.m_model = l.arg( 0 );
                else if ( l.m_key == "place" ) s.m_place = l.arg( 0, "fixed" );
                else if ( l.m_key == "aim" ) s.m_aim = l.arg( 0, "forward" );
                else if ( l.m_key == "rate" ) s.m_rate = l.argNum( 0, 3 );
                else if ( l.m_key == "ammo" ) s.m_ammo = (int)l.argNum( 0, 0 );
                else if ( l.m_key == "reload" ) s.m_reload = l.argNum( 0, 0 );
                else if ( l.m_key == "charge" ) s.m_charge = l.argNum( 0, 0 );
                else if ( l.m_key == "shot" ) s.m_shots.push_back( readShot( l, m_warnings ) );
                else unknown( b, l, m_errors );
            }
        }
        else if ( b.m_type == "ship" )
        {
            dup( m_ships.count( b.m_name ) > 0 );
            ShipDef &s = m_ships[b.m_name];
            s = {};
            s.m_name = b.m_name;
            s.m_model = b.m_name;
            s.m_where = where;
            m_order.push_back( b.m_name );
            for ( const Line &l : b.m_lines )
            {
                if ( goodsLine( s, l ) ) continue;
                if ( l.m_key == "model" ) s.m_model = l.arg( 0 );
                else if ( l.m_key == "armor" ) s.m_armor = l.argNum( 0, s.m_armor );
                else if ( l.m_key == "speed" ) s.m_speed = l.argNum( 0, s.m_speed );
                else if ( l.m_key == "accel" ) s.m_accel = l.argNum( 0, s.m_accel );
                else if ( l.m_key == "radius" ) s.m_radius = l.argNum( 0, s.m_radius );
                else if ( l.m_key == "gen" ) s.m_genBonus = l.argNum( 0, 0 );
                else unknown( b, l, m_errors );
            }
        }
        else if ( b.m_type == "generator" )
        {
            dup( m_generators.count( b.m_name ) > 0 );
            GeneratorDef &g = m_generators[b.m_name];
            g = {};
            g.m_name = b.m_name;
            g.m_where = where;
            m_order.push_back( b.m_name );
            for ( const Line &l : b.m_lines )
            {
                if ( goodsLine( g, l ) ) continue;
                if ( l.m_key == "power" ) g.m_power = l.argNum( 0, g.m_power );
                else if ( l.m_key == "capacity" ) g.m_capacity = l.argNum( 0, g.m_capacity );
                else unknown( b, l, m_errors );
            }
        }
        else if ( b.m_type == "shield" )
        {
            dup( m_shields.count( b.m_name ) > 0 );
            ShieldDef &s = m_shields[b.m_name];
            s = {};
            s.m_name = b.m_name;
            s.m_where = where;
            m_order.push_back( b.m_name );
            for ( const Line &l : b.m_lines )
            {
                if ( goodsLine( s, l ) ) continue;
                if ( l.m_key == "max" ) s.m_max = l.argNum( 0, s.m_max );
                else if ( l.m_key == "regen" ) s.m_regen = l.argNum( 0, s.m_regen );
                else if ( l.m_key == "cost" ) s.m_cost = l.argNum( 0, s.m_cost );
                else if ( l.m_key == "delay" ) s.m_delay = l.argNum( 0, s.m_delay );
                else unknown( b, l, m_errors );
            }
        }
        else if ( b.m_type == "special" )
        {
            dup( m_specials.count( b.m_name ) > 0 );
            SpecialDef &s = m_specials[b.m_name];
            s = {};
            s.m_name = b.m_name;
            s.m_where = where;
            s.m_p.m_file = b.m_head.m_file;
            s.m_p.m_line = b.m_head.m_line;
            m_order.push_back( b.m_name );
            for ( const Line &l : b.m_lines )
            {
                if ( goodsLine( s, l ) ) continue;
                if ( l.m_key == "kind" ) s.m_kind = l.arg( 0 );
                else if ( l.m_key == "charges" ) s.m_charges = (int)l.argNum( 0, 2 );
                else s.m_p.m_kv[l.m_key] = l.arg( 0 );
            }
        }
        else if ( b.m_type == "level" )
        {
            dup( m_levels.count( b.m_name ) > 0 );
            LevelDef &lv = m_levels[b.m_name];
            lv = {};
            lv.m_name = b.m_name;
            lv.m_file = b.m_head.m_file;
            lv.m_where = where;
            uint32_t h = 2166136261u;
            for ( char c : b.m_name ) h = ( h ^ (unsigned char)c ) * 16777619u;
            lv.m_seed = h;
            for ( const Line &l : b.m_lines )
            {
                const std::string &k = l.m_key;
                if ( k == "title" ) lv.m_title = l.arg( 0 );
                else if ( k == "episode" ) lv.m_episode = (int)l.argNum( 0, 1 );
                else if ( k == "theme" ) lv.m_theme = l.arg( 0 );
                else if ( k == "music" ) lv.m_music = l.arg( 0 );
                else if ( k == "briefing" ) lv.m_briefing = l.arg( 0 );
                else if ( k == "debrief" ) lv.m_debrief = l.arg( 0 );
                else if ( k == "scroll" ) lv.m_scroll = l.argNum( 0, 60 );
                else if ( k == "length" ) lv.m_length = l.argNum( 0, 180 );
                else if ( k == "next" ) lv.m_next = l.m_args;
                else if ( k == "secret" )
                {
                    lv.m_secret = l.arg( 0 );
                    lv.m_secretKey = l.str( "key" );
                }
                else if ( k == "issecret" ) lv.m_isSecret = l.argNum( 0, 1 ) != 0;
                else if ( k == "arcade" ) lv.m_arcade = (int)l.argNum( 0, 0 );
                else if ( k == "map" )
                {
                    lv.m_mapX = l.argNum( 0, 0.5f );
                    lv.m_mapY = l.argNum( 1, 0.5f );
                }
                else if ( k == "seed" ) lv.m_seed = (uint32_t)l.argNum( 0, 1 );
                else if ( k == "at" )
                {
                    LevelEvent ev;
                    ev.m_t = l.argNum( 0, -1 );
                    ev.m_type = l.arg( 1 );
                    if ( ev.m_t < 0 || ev.m_type.empty() )
                    {
                        m_errors.push_back( l.where() + ": \"at <seconds> <what> ...\" expected" );
                        continue;
                    }
                    ev.m_p = l;
                    ev.m_p.m_key = ev.m_type;
                    ev.m_p.m_args.erase( ev.m_p.m_args.begin(), ev.m_p.m_args.begin() + 2 );
                    static const std::set<std::string> evTypes = { "wave",  "ground", "boss", "pickup", "text",
                                                                   "scroll", "music", "warn", "end",    "clear",
                                                                   "obstacle", "theme" };
                    if ( !evTypes.count( ev.m_type ) )
                    {
                        m_errors.push_back( l.where() + ": unknown event " + ev.m_type );
                        continue;
                    }
                    if ( ev.m_type == "wave" || ev.m_type == "ground" || ev.m_type == "boss" || ev.m_type == "obstacle" )
                        checkKeys( ev.m_p, WaveKeys, m_warnings );
                    if ( l.has( "diff" ) ) span( l.str( "diff" ), ev.m_minDiff, ev.m_maxDiff, 0, 99 );
                    if ( l.has( "diff" ) && l.str( "diff" ).find( '-' ) == std::string::npos ) ev.m_maxDiff = 99;
                    int   repeat = l.integer( "repeat", 1 );
                    float every = l.num( "every", 1.0f );
                    for ( int i = 0; i < std::max( 1, repeat ); ++i )
                    {
                        LevelEvent r = ev;
                        r.m_t = ev.m_t + every * i;
                        if ( i > 0 && r.m_p.has( "seed" ) ) r.m_p.m_kv["seed"] = std::to_string( r.m_p.integer( "seed" ) + i );
                        r.m_p.m_kv["seed"] = r.m_p.has( "seed" ) ? r.m_p.m_kv["seed"] : std::to_string( l.m_line * 31 + i );
                        lv.m_events.push_back( r );
                    }
                }
                else unknown( b, l, m_errors );
            }
            std::stable_sort( lv.m_events.begin(), lv.m_events.end(),
                              []( const LevelEvent &a, const LevelEvent &c ) { return a.m_t < c.m_t; } );
        }
        else if ( b.m_type == "episode" )
        {
            int id = atoi( b.m_name.c_str() );
            dup( m_episodes.count( id ) > 0 );
            EpisodeDef &ep = m_episodes[id];
            ep = {};
            ep.m_id = id;
            ep.m_where = where;
            for ( const Line &l : b.m_lines )
            {
                const std::string &k = l.m_key;
                if ( k == "title" ) ep.m_title = l.arg( 0 );
                else if ( k == "subtitle" ) ep.m_subtitle = l.arg( 0 );
                else if ( k == "first" ) ep.m_first = l.arg( 0 );
                else if ( k == "intro" ) ep.m_intro = l.arg( 0 );
                else if ( k == "ending" ) ep.m_ending = l.arg( 0 );
                else if ( k == "credits" ) ep.m_credits = (int)l.argNum( 0, 0 );
                else if ( k == "arcade" ) ep.m_arcade = l.m_args;
                else if ( k == "ship" || k == "front" || k == "rear" || k == "side" || k == "gen" || k == "shield" ||
                          k == "special" )
                    loadoutLine( ep.m_loadout, l );
                else unknown( b, l, m_errors );
            }
        }
        else if ( b.m_type == "cube" || b.m_type == "briefing" || b.m_type == "radio" || b.m_type == "intro" ||
                  b.m_type == "ending" || b.m_type == "bio" || b.m_type == "debrief" )
        {
            dup( m_story.count( b.m_name ) > 0 );
            StoryDef &s = m_story[b.m_name];
            s = {};
            s.m_type = b.m_type;
            s.m_name = b.m_name;
            s.m_text = b.m_prose;
            for ( const Line &l : b.m_lines )
            {
                if ( l.m_key == "title" ) s.m_title = l.arg( 0 );
                else if ( l.m_key == "from" ) s.m_from = l.arg( 0 );
                else if ( l.m_key == "portrait" ) s.m_portrait = l.arg( 0 );
                else unknown( b, l, m_errors );
            }
        }
        else if ( b.m_type == "difficulty" )
        {
            DifficultyDef d;
            d.m_name = b.m_name;
            for ( const Line &l : b.m_lines )
            {
                const std::string &k = l.m_key;
                if ( k == "title" ) d.m_title = l.arg( 0 );
                else if ( k == "desc" ) d.m_desc = l.arg( 0 );
                else if ( k == "hp" ) d.m_hp = l.argNum( 0, 1 );
                else if ( k == "fire" ) d.m_fire = l.argNum( 0, 1 );
                else if ( k == "speed" ) d.m_speed = l.argNum( 0, 1 );
                else if ( k == "damage" ) d.m_damage = l.argNum( 0, 1 );
                else if ( k == "credits" ) d.m_credits = l.argNum( 0, 1 );
                else if ( k == "revenge" ) d.m_revenge = l.argNum( 0, 1 ) != 0;
                else if ( k == "hidden" ) d.m_hidden = l.argNum( 0, 1 ) != 0;
                else unknown( b, l, m_errors );
            }
            m_difficulties.push_back( d );
        }
        else if ( b.m_type == "pilot" )
        {
            ArcadeShip a;
            a.m_name = b.m_name;
            for ( const Line &l : b.m_lines )
            {
                if ( l.m_key == "title" ) a.m_title = l.arg( 0 );
                else if ( l.m_key == "desc" ) a.m_desc = l.arg( 0 );
                else loadoutLine( a.m_loadout, l );
            }
            m_arcadeShips.push_back( a );
        }
        else if ( b.m_type == "theme" )
            m_themes.push_back( b );
    }
    if ( m_difficulties.empty() ) m_difficulties.push_back( { "normal", "Normal" } );
    check();
    return m_errors.empty();
}

void Catalog::check()
{
    auto need = [&]( bool ok, const std::string &where, const std::string &what ) {
        if ( !ok ) m_errors.push_back( where + ": " + what );
    };
    auto wantModel = [&]( const std::string &m, const std::string &where ) {
        if ( !m.empty() && !m_models.count( m ) ) m_warnings.push_back( where + ": model \"" + m + "\" is missing" );
    };
    for ( const auto &[n, d] : m_enemies )
    {
        wantModel( d.m_model, d.m_where );
        for ( const PartRef &p : d.m_parts ) need( m_enemies.count( p.m_enemy ) > 0, d.m_where, "part " + p.m_enemy + " unknown" );
        for ( const FireDef &f : d.m_fire )
        {
            if ( f.m_pattern == "spawn" ) need( m_enemies.count( f.m_spawn ) > 0, f.m_where, "spawn " + f.m_spawn + " unknown" );
            if ( f.m_pattern == "radio" && !m_story.count( f.m_spawn ) )
                m_warnings.push_back( f.m_where + ": radio \"" + f.m_spawn + "\" is missing" );
        }
    }
    for ( const auto &[n, s] : m_ships ) wantModel( s.m_model, s.m_where );
    for ( const auto &[n, s] : m_sidekicks ) wantModel( s.m_model, s.m_where );
    for ( const auto &[n, lv] : m_levels )
    {
        for ( const LevelEvent &ev : lv.m_events )
        {
            if ( ev.m_type == "wave" || ev.m_type == "ground" || ev.m_type == "boss" || ev.m_type == "obstacle" )
                need( m_enemies.count( ev.m_p.arg( 0 ) ) > 0, ev.m_p.where(), "enemy \"" + ev.m_p.arg( 0 ) + "\" unknown" );
            if ( ev.m_type == "theme" && !theme( ev.m_p.arg( 0 ) ) )
                m_warnings.push_back( ev.m_p.where() + ": theme \"" + ev.m_p.arg( 0 ) + "\" is missing" );
            if ( ev.m_type == "text" && !m_story.count( ev.m_p.arg( 0 ) ) )
                m_warnings.push_back( ev.m_p.where() + ": story \"" + ev.m_p.arg( 0 ) + "\" is missing" );
            // A plan names a workshop good, dropped by a pickup or by a whole wave.
            std::string plan = ev.m_type == "pickup" && ev.m_p.arg( 0 ) == "plan" ? ev.m_p.str( "item" )
                             : ev.m_p.str( "drop" ) == "plan"                     ? ev.m_p.str( "item" )
                                                                                  : "";
            if ( !plan.empty() && !m_weapons.count( plan ) && !m_sidekicks.count( plan ) && !m_ships.count( plan ) &&
                 !m_generators.count( plan ) && !m_shields.count( plan ) && !m_specials.count( plan ) )
                m_errors.push_back( ev.m_p.where() + ": plan for an unknown good \"" + plan + "\"" );
            if ( ev.m_type == "pickup" && ev.m_p.arg( 0 ) == "cube" && !m_story.count( ev.m_p.str( "item" ) ) )
                m_warnings.push_back( ev.m_p.where() + ": cube \"" + ev.m_p.str( "item" ) + "\" has no text" );
        }
        for ( const std::string &nx : lv.m_next ) need( m_levels.count( nx ) > 0, lv.m_where, "next mission " + nx + " unknown" );
        if ( !lv.m_secret.empty() ) need( m_levels.count( lv.m_secret ) > 0, lv.m_where, "secret " + lv.m_secret + " unknown" );
        if ( !lv.m_briefing.empty() && !m_story.count( lv.m_briefing ) )
            m_warnings.push_back( lv.m_where + ": briefing \"" + lv.m_briefing + "\" is missing" );
    }
    auto loadout = [&]( const Loadout &o, const std::string &where ) {
        need( m_ships.count( o.m_ship ) > 0, where, "ship " + o.m_ship + " unknown" );
        need( m_weapons.count( o.m_front ) > 0, where, "front " + o.m_front + " unknown" );
        if ( !o.m_rear.empty() ) need( m_weapons.count( o.m_rear ) > 0, where, "rear " + o.m_rear + " unknown" );
        if ( !o.m_sideL.empty() ) need( m_sidekicks.count( o.m_sideL ) > 0, where, "sidekick " + o.m_sideL + " unknown" );
        if ( !o.m_sideR.empty() ) need( m_sidekicks.count( o.m_sideR ) > 0, where, "sidekick " + o.m_sideR + " unknown" );
        need( m_generators.count( o.m_gen ) > 0, where, "generator " + o.m_gen + " unknown" );
        need( m_shields.count( o.m_shield ) > 0, where, "shield " + o.m_shield + " unknown" );
        if ( !o.m_special.empty() ) need( m_specials.count( o.m_special ) > 0, where, "special " + o.m_special + " unknown" );
    };
    for ( const auto &[id, ep] : m_episodes )
    {
        need( m_levels.count( ep.m_first ) > 0, ep.m_where, "first mission " + ep.m_first + " unknown" );
        for ( const std::string &a : ep.m_arcade ) need( m_levels.count( a ) > 0, ep.m_where, "arcade mission " + a + " unknown" );
        loadout( ep.m_loadout, ep.m_where );
    }
    for ( const ArcadeShip &a : m_arcadeShips ) loadout( a.m_loadout, "pilot " + a.m_name );
}

template <typename M> static const typename M::mapped_type *find( const M &m, const std::string &n )
{
    auto it = m.find( n );
    return it != m.end() ? &it->second : nullptr;
}

const EnemyDef *Catalog::enemy( const std::string &n ) const { return find( m_enemies, n ); }
const WeaponDef *Catalog::weapon( const std::string &n ) const { return find( m_weapons, n ); }
const SidekickDef *Catalog::sidekick( const std::string &n ) const { return find( m_sidekicks, n ); }
const ShipDef *Catalog::ship( const std::string &n ) const { return find( m_ships, n ); }
const GeneratorDef *Catalog::generator( const std::string &n ) const { return find( m_generators, n ); }
const ShieldDef *Catalog::shield( const std::string &n ) const { return find( m_shields, n ); }
const SpecialDef *Catalog::special( const std::string &n ) const { return find( m_specials, n ); }
const LevelDef *Catalog::level( const std::string &n ) const { return find( m_levels, n ); }
const StoryDef *Catalog::story( const std::string &n ) const { return find( m_story, n ); }
const ModelDef *Catalog::model( const std::string &n ) const { return find( m_models, n ); }

const DifficultyDef &Catalog::difficulty( int i ) const
{
    return m_difficulties[(size_t)std::clamp( i, 0, (int)m_difficulties.size() - 1 )];
}

const Block *Catalog::theme( const std::string &n ) const
{
    for ( const Block &b : m_themes )
        if ( b.m_name == n ) return &b;
    return nullptr;
}

int upgradePrice( const WeaponDef &w, int lv )
{
    // Each level costs more than the one before: level 2 about 0.6 of the gun, level 11 about 3.8 times it.
    return (int)std::lround( w.m_price * ( 0.25f + 0.35f * lv ) / 10.0f ) * 10;
}

int weaponValue( const WeaponDef &w, int lv )
{
    int v = w.m_price;
    for ( int i = 1; i < lv; ++i ) v += upgradePrice( w, i );
    return v;
}

float weaponDps( const WeaponDef &w, int lv, int mode )
{
    float t = ( lv - 1 ) / 10.0f, per = 0.0f;
    for ( const ShotDef &s : w.m_shots )
    {
        if ( lv < s.m_from || lv > s.m_to || ( s.m_mode != 0 && s.m_mode != mode ) ) continue;
        float u = s.m_to > s.m_from ? ( lv - s.m_from ) / (float)( s.m_to - s.m_from ) : 0.0f;
        float n = std::max( 1.0f, std::round( s.m_p.range( "n", 1 ).at( u ) ) );
        float d = s.m_p.range( "dmg", 4 ).at( u );
        float every = std::max( 1.0f, s.m_p.num( "every", 1 ) );
        if ( s.m_kind == "side" ) n *= 2;
        if ( s.m_kind == "chain" ) n = 1.0f + 0.7f * s.m_p.range( "chain", 2 ).at( u );
        if ( s.m_kind == "flame" ) n *= 1.5f;
        per += n * d / every;
    }
    return per * w.m_rate.at( t );
}

void parseLoadout( Loadout &o, const std::string &text )
{
    size_t a = 0;
    while ( a < text.size() )
    {
        size_t      b = text.find( ',', a );
        std::string one = text.substr( a, b == std::string::npos ? std::string::npos : b - a );
        size_t      eq = one.find( '=' );
        if ( eq != std::string::npos )
        {
            std::string k = one.substr( 0, eq ), v = one.substr( eq + 1 );
            int         lv = 1;
            size_t      colon = v.find( ':' );
            if ( colon != std::string::npos )
            {
                lv = std::clamp( atoi( v.substr( colon + 1 ).c_str() ), 1, 11 );
                v = v.substr( 0, colon );
            }
            if ( v == "-" ) v.clear();
            if ( k == "ship" ) o.m_ship = v;
            else if ( k == "front" ) o.m_front = v, o.m_frontLv = lv;
            else if ( k == "rear" ) o.m_rear = v, o.m_rearLv = lv;
            else if ( k == "left" ) o.m_sideL = v;
            else if ( k == "right" ) o.m_sideR = v;
            else if ( k == "gen" ) o.m_gen = v;
            else if ( k == "shield" ) o.m_shield = v;
            else if ( k == "special" ) o.m_special = v;
        }
        if ( b == std::string::npos ) break;
        a = b + 1;
    }
}

}   // namespace zenith
