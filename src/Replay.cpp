/**
 * @file Replay.cpp
 * @brief Recorded missions: rounding the input, the file (a "key value" header, then the body), recording, playing
 *        back, the folder's list.
 *
 *        The body holds one record per change. A byte with its high bit set is a run: this step and (byte & 0x7f)
 *        more are the same as the step before. Any other byte (1..0x3f) tells what changed, three bits a pilot
 *        (pilot 0 in the low bits): 1 the buttons (one byte follows), 2 the stick (two signed bytes, x then y,
 *        in 1/127), 4 the mouse target (two zigzag varints, x then y, the change in 1/ReplayGrid field units).
 *        Every pilot starts from the empty input.
 * @author Rene Weller
 */
#include "Replay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>

namespace zenith
{

namespace fs = std::filesystem;

namespace
{

const char Magic[] = "ZENITH LANCER REPLAY ";   ///< the first line, the format's version after it

/** @brief The buttons' bits. */
enum : uint8_t
{
    B_FIRE = 1,
    B_FOCUS = 2,
    B_MODE = 4,
    B_SPECIAL = 8,
    B_MOUSE = 16,
    B_ALL = 31
};

constexpr int StickSteps = 127;   ///< the stick's steps from the middle to an edge

/** @brief A pilot's input as stored: the buttons as bits, the stick and the target in whole steps. */
struct Packed
{
    uint8_t m_buttons = 0;
    int     m_x = 0, m_y = 0;     ///< the stick (-127..127)
    int     m_tx = 0, m_ty = 0;   ///< the target (1/ReplayGrid field units)
};

/** @return the lowest target step on an axis of this size */
int gridLow( float size ) { return (int)( -size * ReplayGrid ); }

/** @return the highest target step on an axis of this size */
int gridHigh( float size ) { return (int)( 2.0f * size * ReplayGrid ); }

int stickOf( float v )
{
    if ( !std::isfinite( v ) ) return 0;
    return (int)std::lround( clampf( v, -1.0f, 1.0f ) * StickSteps );
}

int gridOf( float v, float size )
{
    if ( !std::isfinite( v ) ) return 0;
    int g = (int)std::lround( clampf( v, -size, 2.0f * size ) * ReplayGrid );
    return std::clamp( g, gridLow( size ), gridHigh( size ) );
}

Packed pack( const PlayerInput &in )
{
    Packed p;
    p.m_buttons = ( in.m_fire ? B_FIRE : 0 ) | ( in.m_focus ? B_FOCUS : 0 ) | ( in.m_mode ? B_MODE : 0 ) |
                  ( in.m_special ? B_SPECIAL : 0 ) | ( in.m_mouse ? B_MOUSE : 0 );
    // The World reads the target while the mouse steers and the stick otherwise: the other stays empty.
    if ( in.m_mouse )
    {
        p.m_tx = gridOf( in.m_target.x, FieldW );
        p.m_ty = gridOf( in.m_target.y, FieldH );
    }
    else
    {
        p.m_x = stickOf( in.m_x );
        p.m_y = stickOf( in.m_y );
    }
    return p;
}

PlayerInput unpack( const Packed &p )
{
    PlayerInput in;
    in.m_fire = ( p.m_buttons & B_FIRE ) != 0;
    in.m_focus = ( p.m_buttons & B_FOCUS ) != 0;
    in.m_mode = ( p.m_buttons & B_MODE ) != 0;
    in.m_special = ( p.m_buttons & B_SPECIAL ) != 0;
    in.m_mouse = ( p.m_buttons & B_MOUSE ) != 0;
    in.m_x = (float)p.m_x / (float)StickSteps;
    in.m_y = (float)p.m_y / (float)StickSteps;
    in.m_target = { (float)p.m_tx / ReplayGrid, (float)p.m_ty / ReplayGrid };
    return in;
}

void putVarint( std::vector<uint8_t> &out, uint32_t v )
{
    while ( v >= 0x80 )
    {
        out.push_back( (uint8_t)( v | 0x80 ) );
        v >>= 7;
    }
    out.push_back( (uint8_t)v );
}

bool getVarint( const std::vector<uint8_t> &in, size_t &at, uint32_t &v )
{
    v = 0;
    for ( int shift = 0; shift < 35; shift += 7 )
    {
        if ( at >= in.size() ) return false;
        uint8_t b = in[at++];
        v |= (uint32_t)( b & 0x7f ) << shift;
        if ( !( b & 0x80 ) ) return true;
    }
    return false;
}

uint32_t zigzag( int v ) { return ( (uint32_t)v << 1 ) ^ (uint32_t)( v >> 31 ); }

int unzigzag( uint32_t v ) { return (int)( v >> 1 ) ^ -(int)( v & 1 ); }

/** @return the FNV-1a hash of the bytes (the body's check) */
uint32_t fnv( const std::vector<uint8_t> &b )
{
    uint32_t h = 2166136261u;
    for ( uint8_t c : b ) h = ( h ^ c ) * 16777619u;
    return h;
}

/**
 * @brief Reads one step from a body.
 * @param[in]     body  the body
 * @param[in,out] at    its next byte
 * @param[in,out] run   repeats of the last step still to give
 * @param[in,out] last  the step before, then this one
 * @return false if the body ends or is broken here
 */
bool readStep( const std::vector<uint8_t> &body, size_t &at, int &run, PlayerInput last[2] )
{
    if ( run > 0 )
    {
        --run;
        return true;
    }
    if ( at >= body.size() ) return false;
    uint8_t c = body[at++];
    if ( c & 0x80 )
    {
        run = c & 0x7f;
        return true;
    }
    if ( c == 0 || ( c & 0x40 ) ) return false;
    for ( int i = 0; i < 2; ++i )
    {
        int bits = ( c >> ( i * 3 ) ) & 7;
        if ( !bits ) continue;
        Packed p = pack( last[i] );
        if ( bits & 1 )
        {
            if ( at >= body.size() || ( body[at] & ~B_ALL ) ) return false;
            p.m_buttons = body[at++];
        }
        if ( bits & 2 )
        {
            if ( at + 2 > body.size() ) return false;
            p.m_x = (int8_t)body[at];
            p.m_y = (int8_t)body[at + 1];
            at += 2;
            if ( p.m_x < -StickSteps || p.m_y < -StickSteps ) return false;
        }
        if ( bits & 4 )
        {
            uint32_t dx = 0, dy = 0;
            if ( !getVarint( body, at, dx ) || !getVarint( body, at, dy ) ) return false;
            p.m_tx += unzigzag( dx );
            p.m_ty += unzigzag( dy );
            if ( p.m_tx < gridLow( FieldW ) || p.m_tx > gridHigh( FieldW ) || p.m_ty < gridLow( FieldH ) ||
                 p.m_ty > gridHigh( FieldH ) )
                return false;
        }
        last[i] = unpack( p );
    }
    return true;
}

std::string loadoutText( const Loadout &o )
{
    // parseLoadout's words; "-" for an empty slot (a level stays with it).
    auto v = []( const std::string &s ) { return s.empty() ? std::string( "-" ) : s; };
    return "ship=" + v( o.m_ship ) + ",front=" + v( o.m_front ) + ":" + std::to_string( o.m_frontLv ) + ",rear=" +
           v( o.m_rear ) + ":" + std::to_string( o.m_rearLv ) + ",left=" + v( o.m_sideL ) + ",right=" + v( o.m_sideR ) +
           ",gen=" + v( o.m_gen ) + ",shield=" + v( o.m_shield ) + ",special=" + v( o.m_special );
}

const char *markName( ReplayMarkE k ) { return k == ReplayMarkE::CONTINUE ? "continue" : "retry"; }

const char *endName( ReplayEndE e ) { return e == ReplayEndE::WON ? "won" : e == ReplayEndE::LOST ? "lost" : "open"; }

/** @brief Reads a line without its end (long lines are cut). @return false at the end of the file */
bool readLine( FILE *f, std::string &line )
{
    line.clear();
    int c;
    while ( ( c = fgetc( f ) ) != EOF )
    {
        if ( c == '\n' ) return true;
        if ( c != '\r' && line.size() < 4096 ) line += (char)c;
    }
    return !line.empty();
}

/**
 * @brief Reads a replay's header, up to and with its "body" line.
 * @param[in]  f       the file (at its start)
 * @param[out] h       the header
 * @param[out] bytes   the body's length (-1 if the body line is missing)
 * @param[out] check   the body's FNV-1a
 * @param[out] error   what is wrong ("" if nothing)
 * @return whether the first line is a replay's
 */
bool readHeader( FILE *f, ReplayHeader &h, long long &bytes, uint32_t &check, std::string &error )
{
    h = ReplayHeader();
    bytes = -1;
    check = 0;
    std::string line;
    const size_t magicLen = sizeof( Magic ) - 1;
    if ( !readLine( f, line ) || line.compare( 0, magicLen, Magic ) != 0 )
    {
        error = "not a replay";
        return false;
    }
    h.m_format = atoi( line.c_str() + magicLen );
    while ( readLine( f, line ) )
    {
        size_t      sp = line.find( ' ' );
        std::string k = line.substr( 0, sp ), v = sp == std::string::npos ? std::string() : line.substr( sp + 1 );
        if ( k == "version" ) h.m_version = v;
        else if ( k == "platform" ) h.m_platform = v;
        else if ( k == "mode" ) h.m_mode = modeOfName( v );
        else if ( k == "mission" ) h.m_mission = v;
        else if ( k == "difficulty" ) h.m_difficulty = atoi( v.c_str() );
        else if ( k == "ngplus" ) h.m_ngPlus = atoi( v.c_str() );
        else if ( k == "seed" ) h.m_seed = (uint32_t)strtoul( v.c_str(), nullptr, 10 );
        else if ( k == "pilots" ) h.m_pilots = atoi( v.c_str() );
        else if ( k.size() == 5 && ( k[4] == '0' || k[4] == '1' ) && k.compare( 0, 4, "load" ) == 0 )
        {
            Loadout &o = h.m_load[k[4] - '0'];
            o = Loadout();
            parseLoadout( o, v );
        }
        else if ( k == "lives0" || k == "lives1" ) h.m_lives[k[5] - '0'] = atoi( v.c_str() );
        else if ( k == "score0" || k == "score1" ) h.m_score[k[5] - '0'] = strtoll( v.c_str(), nullptr, 10 );
        else if ( k == "date" ) h.m_date = v;
        else if ( k == "lang" ) h.m_lang = v;
        else if ( k == "result" )
            h.m_end = v == "won" ? ReplayEndE::WON : v == "lost" ? ReplayEndE::LOST : ReplayEndE::OPEN;
        else if ( k == "endscore" ) h.m_endScore = strtoll( v.c_str(), nullptr, 10 );
        else if ( k == "time" ) h.m_time = strtof( v.c_str(), nullptr );
        else if ( k == "steps" ) h.m_steps = atoi( v.c_str() );
        else if ( k == "mark" )
        {
            ReplayMark m;
            char       what[32] = {};
            if ( sscanf( v.c_str(), "%d %31s", &m.m_step, what ) != 2 ||
                 ( strcmp( what, "retry" ) != 0 && strcmp( what, "continue" ) != 0 ) )
            {
                error = "unknown mark: " + v;
                continue;
            }
            m.m_kind = strcmp( what, "continue" ) == 0 ? ReplayMarkE::CONTINUE : ReplayMarkE::RETRY;
            h.m_marks.push_back( m );
        }
        else if ( k == "body" )
        {
            unsigned int c = 0;
            if ( sscanf( v.c_str(), "%lld %x", &bytes, &c ) != 2 ) bytes = -1;
            check = c;
            break;
        }
    }
    return true;
}

/** @return the local time as "YYYY-MM-DD hh:mm:ss" */
std::string now()
{
    std::time_t t = std::time( nullptr );
    std::tm     tm = {};
#ifdef _WIN32
    localtime_s( &tm, &t );
#else
    localtime_r( &t, &tm );
#endif
    char buf[32];
    std::strftime( buf, sizeof( buf ), "%Y-%m-%d %H:%M:%S", &tm );
    return buf;
}

}   // namespace

PlayerInput quantize( const PlayerInput &in ) { return unpack( pack( in ) ); }

const char *modeName( ModeE mode )
{
    switch ( mode )
    {
    case ModeE::ARCADE: return "arcade";
    case ModeE::BOSSRUSH: return "bossrush";
    case ModeE::PREVIEW: return "preview";
    default: return "campaign";
    }
}

ModeE modeOfName( const std::string &name )
{
    if ( name == "arcade" ) return ModeE::ARCADE;
    if ( name == "bossrush" ) return ModeE::BOSSRUSH;
    if ( name == "preview" ) return ModeE::PREVIEW;
    return ModeE::CAMPAIGN;
}

bool startWorld( World &w, const Catalog &cat, const ReplayHeader &head )
{
    const LevelDef *lv = cat.level( head.m_mission );
    if ( !lv ) return false;
    w.begin( &cat, lv, head.m_mode, head.m_difficulty, head.m_seed );
    for ( int i = 0; i < std::clamp( head.m_pilots, 1, 2 ); ++i )
    {
        w.addPlayer( i, head.m_load[i], head.m_lives[i] );
        w.m_players[i].m_score = head.m_score[i];
    }
    return true;
}

void restoreBoss( World &w, const World &save )
{
    w = save;
    for ( Player &p : w.m_players )
    {
        if ( !p.m_active ) continue;
        p.m_alive = true;
        p.m_dead = 0;
        p.m_armor = p.m_armorMax;
        p.m_shield = p.m_shieldDef ? p.m_shieldDef->m_max : 0.0f;
        p.m_energy = p.m_energyMax;
        p.m_invuln = 2.0f;
        if ( p.m_special ) p.m_charges = std::max( p.m_charges, p.m_special->m_charges );
        if ( w.m_mode == ModeE::BOSSRUSH ) p.m_lives = std::max( p.m_lives, 1 );
    }
    // The bullets of the moment go: the fight starts clean.
    for ( Bullet &b : w.m_bullets )
        if ( !b.m_player ) b.m_dead = true;
    w.m_lasers.clear();
}

void continueArcade( World &w )
{
    for ( Player &p : w.m_players )
    {
        if ( !p.m_active ) continue;
        p.m_lives = 3;
        p.m_score = 0;
        p.m_dead = 5.0f;
    }
    w.m_failed = false;
}

// ---------------------------------------------------------------------------------------------------- recording

void ReplayRecorder::begin( const ReplayHeader &head )
{
    clear();
    m_head = head;
    m_head.m_format = ReplayFormat;
    m_head.m_pilots = std::clamp( m_head.m_pilots, 1, 2 );
#if defined( _WIN32 )
    m_head.m_platform = "windows";
#elif defined( __APPLE__ )
    m_head.m_platform = "macos";
#else
    m_head.m_platform = "linux";
#endif
    if ( m_head.m_date.empty() ) m_head.m_date = now();
    m_head.m_end = ReplayEndE::OPEN;
    m_head.m_endScore = 0;
    m_head.m_time = 0.0f;
    m_head.m_steps = 0;
    m_head.m_marks.clear();
    m_head.m_path.clear();
    m_active = true;
}

void ReplayRecorder::push( const PlayerInput in[2] )
{
    if ( !m_active ) return;
    Packed  now[2] = { pack( in[0] ), pack( in[1] ) };
    uint8_t c = 0;
    for ( int i = 0; i < 2; ++i )
    {
        Packed was = pack( m_last[i] );
        if ( now[i].m_buttons != was.m_buttons ) c |= (uint8_t)( 1 << ( i * 3 ) );
        if ( now[i].m_x != was.m_x || now[i].m_y != was.m_y ) c |= (uint8_t)( 2 << ( i * 3 ) );
        if ( now[i].m_tx != was.m_tx || now[i].m_ty != was.m_ty ) c |= (uint8_t)( 4 << ( i * 3 ) );
    }
    ++m_head.m_steps;
    if ( c == 0 )
    {
        if ( ++m_run == 128 )
        {
            m_body.push_back( 0xff );
            m_run = 0;
        }
        return;
    }
    if ( m_run > 0 ) m_body.push_back( (uint8_t)( 0x80 | ( m_run - 1 ) ) );
    m_run = 0;
    m_body.push_back( c );
    for ( int i = 0; i < 2; ++i )
    {
        Packed was = pack( m_last[i] );
        int    bits = ( c >> ( i * 3 ) ) & 7;
        if ( bits & 1 ) m_body.push_back( now[i].m_buttons );
        if ( bits & 2 )
        {
            m_body.push_back( (uint8_t)(int8_t)now[i].m_x );
            m_body.push_back( (uint8_t)(int8_t)now[i].m_y );
        }
        if ( bits & 4 )
        {
            putVarint( m_body, zigzag( now[i].m_tx - was.m_tx ) );
            putVarint( m_body, zigzag( now[i].m_ty - was.m_ty ) );
        }
        m_last[i] = unpack( now[i] );
    }
}

void ReplayRecorder::mark( ReplayMarkE kind )
{
    if ( !m_active ) return;
    m_head.m_marks.push_back( { m_head.m_steps, kind } );
    m_head.m_end = ReplayEndE::OPEN;
}

void ReplayRecorder::finish( bool won, long long score, float time )
{
    if ( !m_active ) return;
    m_head.m_end = won ? ReplayEndE::WON : ReplayEndE::LOST;
    m_head.m_endScore = score;
    m_head.m_time = time;
}

bool ReplayRecorder::save( const std::string &path ) const
{
    if ( !m_active ) return false;
    std::vector<uint8_t> body = m_body;
    if ( m_run > 0 ) body.push_back( (uint8_t)( 0x80 | ( m_run - 1 ) ) );
    std::error_code e;
    fs::path        file( path );
    if ( file.has_parent_path() ) fs::create_directories( file.parent_path(), e );
    std::string tmp = path + ".tmp";
    FILE       *f = fopen( tmp.c_str(), "wb" );
    if ( !f ) return false;
    const ReplayHeader &h = m_head;
    fprintf( f, "%s%d\n", Magic, ReplayFormat );
    fprintf( f, "version %s\nplatform %s\nmode %s\nmission %s\ndifficulty %d\nngplus %d\nseed %u\npilots %d\n",
             h.m_version.c_str(), h.m_platform.c_str(), modeName( h.m_mode ), h.m_mission.c_str(), h.m_difficulty,
             h.m_ngPlus, h.m_seed, h.m_pilots );
    for ( int i = 0; i < h.m_pilots; ++i )
        fprintf( f, "load%d %s\nlives%d %d\nscore%d %lld\n", i, loadoutText( h.m_load[i] ).c_str(), i, h.m_lives[i], i,
                 h.m_score[i] );
    // The time with all of a float's digits: read back it is the World's clock to the bit.
    fprintf( f, "date %s\nlang %s\nresult %s\nendscore %lld\ntime %.9g\nsteps %d\n", h.m_date.c_str(), h.m_lang.c_str(),
             endName( h.m_end ), h.m_endScore, (double)h.m_time, h.m_steps );
    for ( const ReplayMark &m : h.m_marks ) fprintf( f, "mark %d %s\n", m.m_step, markName( m.m_kind ) );
    fprintf( f, "body %zu %08x\n", body.size(), fnv( body ) );
    bool ok = body.empty() || fwrite( body.data(), 1, body.size(), f ) == body.size();
    ok = !ferror( f ) && ok;
    ok = fclose( f ) == 0 && ok;
    if ( !ok )
    {
        fs::remove( fs::path( tmp ), e );
        return false;
    }
    fs::rename( fs::path( tmp ), file, e );
    return !e;
}

void ReplayRecorder::clear()
{
    m_head = ReplayHeader();
    m_body.clear();
    m_last[0] = m_last[1] = PlayerInput();
    m_run = 0;
    m_active = false;
}

// ---------------------------------------------------------------------------------------------------- playing back

bool ReplayPlayer::load( const std::string &path )
{
    m_head = ReplayHeader();
    m_body.clear();
    m_error.clear();
    rewind();
    FILE *f = fopen( path.c_str(), "rb" );
    if ( !f )
    {
        m_error = "cannot open " + path;
        return false;
    }
    long long bytes = -1;
    uint32_t  check = 0;
    bool      isReplay = readHeader( f, m_head, bytes, check, m_error );
    m_head.m_path = path;
    if ( isReplay && m_error.empty() )
    {
        if ( m_head.m_format > ReplayFormat || m_head.m_format < 1 )
            m_error = "format " + std::to_string( m_head.m_format ) + " (this game reads " +
                      std::to_string( ReplayFormat ) + ")";
        else if ( bytes < 0 || bytes > ( 1ll << 30 ) )
            m_error = "no body";
        else
        {
            m_body.resize( (size_t)bytes );
            if ( bytes > 0 && fread( m_body.data(), 1, m_body.size(), f ) != m_body.size() ) m_error = "body cut short";
            else if ( fnv( m_body ) != check ) m_error = "body damaged (check)";
        }
    }
    fclose( f );
    if ( m_error.empty() && ( m_head.m_pilots < 1 || m_head.m_pilots > 2 || m_head.m_steps < 0 ) )
        m_error = "bad header";
    // Every step must be in the body, and no more.
    if ( m_error.empty() )
    {
        size_t      at = 0;
        int         run = 0, n = 0;
        PlayerInput last[2];
        while ( readStep( m_body, at, run, last ) ) ++n;
        if ( at != m_body.size() || run != 0 || n != m_head.m_steps )
            m_error = "body broken (" + std::to_string( n ) + " of " + std::to_string( m_head.m_steps ) + " steps)";
    }
    if ( m_error.empty() )
        for ( size_t i = 0; i < m_head.m_marks.size(); ++i )
            if ( m_head.m_marks[i].m_step < 0 || m_head.m_marks[i].m_step > m_head.m_steps ||
                 ( i > 0 && m_head.m_marks[i].m_step < m_head.m_marks[i - 1].m_step ) )
                m_error = "bad mark";
    if ( !m_error.empty() )
    {
        m_body.clear();
        m_head.m_steps = 0;
        return false;
    }
    return true;
}

bool ReplayPlayer::next( PlayerInput in[2] )
{
    if ( atEnd() || !readStep( m_body, m_at, m_run, m_last ) )
    {
        in[0] = in[1] = PlayerInput();
        return false;
    }
    ++m_pos;
    in[0] = m_last[0];
    in[1] = m_last[1];
    return true;
}

std::vector<ReplayMarkE> ReplayPlayer::marks() const
{
    std::vector<ReplayMarkE> due;
    for ( const ReplayMark &m : m_head.m_marks )
        if ( m.m_step == m_pos ) due.push_back( m.m_kind );
    return due;
}

void ReplayPlayer::rewind()
{
    m_at = 0;
    m_pos = 0;
    m_run = 0;
    m_last[0] = m_last[1] = PlayerInput();
}

// ---------------------------------------------------------------------------------------------------- the folder

std::vector<ReplayHeader> listReplays( const std::string &dir )
{
    std::vector<ReplayHeader> out;
    std::error_code           e;
    try
    {
        for ( fs::directory_iterator it( fs::path( dir ), e ), end; !e && it != end; it.increment( e ) )
        {
            if ( !it->is_regular_file( e ) || it->path().extension() != ReplayExt ) continue;
            std::string path = it->path().string();
            FILE       *f = fopen( path.c_str(), "rb" );
            if ( !f ) continue;
            ReplayHeader h;
            long long    bytes = 0;
            uint32_t     check = 0;
            std::string  error;
            bool         ok = readHeader( f, h, bytes, check, error );
            fclose( f );
            if ( !ok ) continue;
            h.m_path = path;
            out.push_back( h );
        }
    }
    catch ( const std::exception & )
    {
        // A name the system cannot give as text: what was read so far is listed.
    }
    std::sort( out.begin(), out.end(), []( const ReplayHeader &a, const ReplayHeader &b ) {
        return a.m_date != b.m_date ? a.m_date > b.m_date : a.m_path > b.m_path;
    } );
    return out;
}

int pruneReplays( const std::string &dir, int keep )
{
    int kept = 0, removed = 0;
    for ( const ReplayHeader &h : listReplays( dir ) )
    {
        if ( fs::path( h.m_path ).filename().string().rfind( "best_", 0 ) == 0 ) continue;
        if ( kept < keep )
        {
            ++kept;
            continue;
        }
        std::error_code e;
        if ( fs::remove( fs::path( h.m_path ), e ) ) ++removed;
    }
    return removed;
}

// ---------------------------------------------------------------------------------------------------- the self-test

std::string replaySelfTest( const std::string &dir )
{
    std::error_code          e;
    fs::path                 root( dir ), list = fs::path( dir ) / "list";
    std::vector<std::string> made;
    fs::create_directories( list, e );
    auto same = []( const PlayerInput &a, const PlayerInput &b ) {
        return a.m_x == b.m_x && a.m_y == b.m_y && a.m_fire == b.m_fire && a.m_focus == b.m_focus &&
               a.m_mode == b.m_mode && a.m_special == b.m_special && a.m_mouse == b.m_mouse &&
               a.m_target.x == b.m_target.x && a.m_target.y == b.m_target.y;
    };
    auto test = [&]() -> std::string {
        // Two pilots' random input: phases of many changes and phases of none (long runs), the stick beyond its
        // edges, the mouse beyond the field.
        ReplayHeader h;
        h.m_version = "test";
        h.m_mode = ModeE::ARCADE;
        h.m_mission = "selftest";
        h.m_difficulty = 4;
        h.m_ngPlus = 2;
        h.m_seed = 0xdeadbeefu;
        h.m_pilots = 2;
        parseLoadout( h.m_load[0], "ship=bastion,front=hornet:5,rear=-:3,left=glimmer,right=-,gen=fusion" );
        h.m_lives[1] = 2;
        h.m_score[1] = 123456789012ll;
        h.m_lang = "en";
        ReplayRecorder rec;
        rec.begin( h );
        rec.mark( ReplayMarkE::CONTINUE );
        Rng r;
        r.seed( 99 );
        std::vector<PlayerInput> want;
        PlayerInput              cur[2];
        const int                steps = 30000;
        for ( int s = 0; s < steps; ++s )
        {
            float change = ( s / 2500 ) % 3 == 1 ? 0.0f : 0.25f;
            for ( PlayerInput &c : cur )
                if ( r.chance( change ) )
                {
                    c.m_x = r.range( -1.4f, 1.4f );
                    if ( r.chance( 0.5f ) ) c.m_y = r.range( -1.4f, 1.4f );
                    c.m_mouse = r.chance( 0.3f );
                    c.m_target = { r.range( -1000.0f, 1800.0f ), r.range( -1000.0f, 2000.0f ) };
                    c.m_fire = r.chance( 0.7f );
                    c.m_focus = r.chance( 0.2f );
                    c.m_mode = r.chance( 0.05f );
                    c.m_special = r.chance( 0.05f );
                }
            want.push_back( quantize( cur[0] ) );
            want.push_back( quantize( cur[1] ) );
            if ( !same( quantize( want.back() ), want.back() ) ) return "quantize is not stable";
            rec.push( cur );
            if ( s == 4500 ) rec.mark( ReplayMarkE::RETRY );
        }
        rec.finish( true, 4242, 500.5f );
        std::string file = ( root / "codec.zlr" ).string();
        made.push_back( file );
        if ( !rec.save( file ) ) return "cannot save " + file;
        ReplayPlayer pl;
        if ( !pl.load( file ) ) return "cannot load: " + pl.error();
        const ReplayHeader &g = pl.header();
        if ( g.m_mode != h.m_mode || g.m_mission != h.m_mission || g.m_difficulty != h.m_difficulty ||
             g.m_ngPlus != h.m_ngPlus || g.m_seed != h.m_seed || g.m_pilots != 2 || g.m_lives[1] != 2 ||
             g.m_score[1] != h.m_score[1] || g.m_lang != "en" || g.m_end != ReplayEndE::WON || g.m_endScore != 4242 ||
             g.m_time != 500.5f || g.m_steps != steps || loadoutText( g.m_load[0] ) != loadoutText( h.m_load[0] ) ||
             loadoutText( g.m_load[1] ) != loadoutText( h.m_load[1] ) || g.m_date != rec.header().m_date ||
             g.m_platform.empty() || g.m_platform != rec.header().m_platform )
            return "the header came back otherwise";
        if ( g.m_marks.size() != 2 || g.m_marks[0].m_step != 0 || g.m_marks[1].m_step != 4501 ||
             g.m_marks[1].m_kind != ReplayMarkE::RETRY )
            return "the marks came back otherwise";
        int marks = 0;
        for ( int s = 0; s < steps; ++s )
        {
            marks += (int)pl.marks().size();
            PlayerInput in[2];
            if ( !pl.next( in ) ) return "the body ends at step " + std::to_string( s );
            if ( !same( in[0], want[(size_t)s * 2] ) || !same( in[1], want[(size_t)s * 2 + 1] ) )
                return "step " + std::to_string( s ) + " came back otherwise";
        }
        PlayerInput in[2];
        if ( marks != 2 || pl.next( in ) || !pl.atEnd() ) return "the end came back otherwise";
        printf( "replay file: %d steps of two pilots in %zu bytes, read back the same\n", steps, rec.bytes() );
        // A damaged file is refused: cut short, one byte changed.
        std::vector<char> all;
        if ( FILE *f = fopen( file.c_str(), "rb" ) )
        {
            int c;
            while ( ( c = fgetc( f ) ) != EOF ) all.push_back( (char)c );
            fclose( f );
        }
        for ( int k = 0; k < 2; ++k )
        {
            std::vector<char> bad = all;
            if ( k == 0 ) bad.resize( bad.size() - 9 );
            else bad[bad.size() - 100] ^= 0x10;
            std::string name = ( root / ( k == 0 ? "cut.zlr" : "flip.zlr" ) ).string();
            made.push_back( name );
            if ( FILE *f = fopen( name.c_str(), "wb" ) )
            {
                fwrite( bad.data(), 1, bad.size(), f );
                fclose( f );
            }
            if ( pl.load( name ) ) return std::string( "a damaged file was taken: " ) + name;
        }
        // The folder: the newest two stay, and the best; what is no replay is left alone.
        for ( int k = 0; k < 6; ++k )
        {
            ReplayHeader one;
            one.m_mission = "e1_01";
            one.m_date = k == 5 ? "2025-12-31 23:59:59" : "2026-01-0" + std::to_string( k + 1 ) + " 12:00:00";
            ReplayRecorder w;
            w.begin( one );
            std::string name = ( list / ( k == 5 ? "best_old.zlr" : "r" + std::to_string( k ) + ".zlr" ) ).string();
            made.push_back( name );
            if ( !w.save( name ) ) return "cannot save " + name;
        }
        std::string junk = ( list / "junk.zlr" ).string();
        made.push_back( junk );
        if ( FILE *f = fopen( junk.c_str(), "wb" ) )
        {
            fputs( "not a replay\n", f );
            fclose( f );
        }
        std::vector<ReplayHeader> before = listReplays( list.string() );
        if ( before.size() != 6 || fs::path( before[0].m_path ).filename() != "r4.zlr" ||
             fs::path( before[5].m_path ).filename() != "best_old.zlr" )
            return "listReplays: wrong files or order";
        if ( pruneReplays( list.string(), 2 ) != 3 ) return "pruneReplays: wrong count";
        std::vector<ReplayHeader> after = listReplays( list.string() );
        if ( after.size() != 3 || fs::path( after[0].m_path ).filename() != "r4.zlr" ||
             fs::path( after[1].m_path ).filename() != "r3.zlr" ||
             fs::path( after[2].m_path ).filename() != "best_old.zlr" || !fs::exists( fs::path( junk ), e ) )
            return "pruneReplays: wrong files kept";
        return "";
    };
    std::string problem = test();
    // Only what the test made goes.
    for ( const std::string &m : made ) fs::remove( fs::path( m ), e );
    fs::remove( list, e );
    fs::remove( root, e );
    return problem;
}

}   // namespace zenith
