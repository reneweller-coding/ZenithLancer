/**
 * @file Audio.cpp
 * @brief The synthesised effects and the music.
 * @author Rene Weller
 */
#include "Audio.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <vector>

namespace zenith
{

namespace
{

constexpr int Rate = 44100;

/** @brief A sound being made. */
struct Synth
{
    std::vector<float> m_s;
    Rng                m_rng;

    explicit Synth( float seconds, uint32_t seed = 1 ) : m_s( (size_t)( seconds * Rate ), 0.0f ) { m_rng.seed( seed ); }

    size_t size() const { return m_s.size(); }
    float  t( size_t i ) const { return i / (float)Rate; }
    float  noise() { return m_rng.range( -1.0f, 1.0f ); }

    /** @brief A tone sweeping exponentially from f0 to f1, shaped by an attack/decay envelope. */
    void tone( float f0, float f1, float amp, float attack, float decay, int wave = 0, float start = 0.0f, float len = -1.0f )
    {
        double ph = 0;
        size_t a = (size_t)( start * Rate ), n = len < 0 ? size() : std::min( size(), a + (size_t)( len * Rate ) );
        float  dur = ( n - a ) / (float)Rate;
        for ( size_t i = a; i < n; ++i )
        {
            float tt = ( i - a ) / (float)Rate, u = tt / std::max( 1e-4f, dur );
            float f = f0 * std::pow( f1 / f0, u );
            ph += f / Rate;
            float p = (float)( ph - std::floor( ph ) );
            float v = wave == 0 ? std::sin( 2 * Pi * p )
                    : wave == 1 ? ( p < 0.5f ? 1.0f : -1.0f )
                    : wave == 2 ? 2 * p - 1
                                : 1 - 4 * std::fabs( p - 0.5f );
            float e = std::min( 1.0f, tt / std::max( 1e-4f, attack ) ) * std::exp( -tt / std::max( 1e-4f, decay ) );
            m_s[i] += v * e * amp;
        }
    }

    /** @brief Noise through a lowpass whose cutoff falls from c0 to c1 (Hz). */
    void noiseLp( float c0, float c1, float amp, float attack, float decay, float start = 0.0f, bool high = false )
    {
        float  y = 0, x1 = 0;
        size_t a = (size_t)( start * Rate );
        for ( size_t i = a; i < size(); ++i )
        {
            float tt = ( i - a ) / (float)Rate, u = tt / std::max( 1e-4f, t( size() ) - start );
            float c = c0 * std::pow( c1 / c0, u );
            float k = 1.0f - std::exp( -2 * Pi * c / Rate );
            float x = noise();
            y += ( x - y ) * k;
            float v = high ? x - y : y;
            ( void )x1;
            float e = std::min( 1.0f, tt / std::max( 1e-4f, attack ) ) * std::exp( -tt / std::max( 1e-4f, decay ) );
            m_s[i] += v * e * amp;
        }
    }

    /** @brief Peak to 'peak', soft-clipped. */
    void finish( float peak )
    {
        float m = 1e-6f;
        for ( float v : m_s ) m = std::max( m, std::fabs( v ) );
        for ( size_t i = 0; i < size(); ++i )
        {
            float v = m_s[i] / m * 1.2f;
            v = std::tanh( v ) / std::tanh( 1.2f );
            // A short fade at the end so nothing clicks.
            float tail = std::min( 1.0f, ( size() - i ) / ( Rate * 0.004f ) );
            m_s[i] = v * peak * tail;
        }
    }
};

/** @brief Makes one effect. */
std::vector<float> make( SfxE e )
{
    switch ( e )
    {
    case SfxE::PULSE:
    {
        Synth s( 0.09f, 1 );
        s.tone( 1500, 520, 0.6f, 0.002f, 0.035f, 1 );
        s.tone( 760, 300, 0.5f, 0.001f, 0.05f, 0 );
        s.noiseLp( 6000, 2000, 0.25f, 0.001f, 0.01f );
        s.finish( 0.5f );
        return s.m_s;
    }
    case SfxE::LASER:
    {
        Synth s( 0.16f, 2 );
        s.tone( 2400, 280, 0.6f, 0.002f, 0.06f, 2 );
        s.tone( 1200, 200, 0.4f, 0.002f, 0.08f, 0 );
        s.finish( 0.45f );
        return s.m_s;
    }
    case SfxE::MISSILE:
    {
        Synth s( 0.32f, 3 );
        s.noiseLp( 900, 4000, 0.8f, 0.03f, 0.12f );
        s.tone( 180, 420, 0.3f, 0.02f, 0.12f, 2 );
        s.finish( 0.45f );
        return s.m_s;
    }
    case SfxE::PLASMA:
    {
        Synth s( 0.22f, 4 );
        s.tone( 420, 110, 0.7f, 0.003f, 0.08f, 3 );
        s.tone( 840, 220, 0.3f, 0.003f, 0.05f, 0 );
        s.noiseLp( 3000, 400, 0.25f, 0.002f, 0.05f );
        s.finish( 0.5f );
        return s.m_s;
    }
    case SfxE::FLAME:
    {
        Synth s( 0.14f, 5 );
        s.noiseLp( 2200, 800, 0.9f, 0.01f, 0.06f );
        s.finish( 0.35f );
        return s.m_s;
    }
    case SfxE::BEAM:
    {
        Synth s( 0.13f, 6 );
        s.tone( 110, 112, 0.5f, 0.01f, 0.2f, 2 );
        s.tone( 330, 335, 0.25f, 0.01f, 0.2f, 0 );
        s.noiseLp( 5000, 5000, 0.12f, 0.01f, 0.2f, 0.0f, true );
        s.finish( 0.3f );
        return s.m_s;
    }
    case SfxE::SPREAD:
    {
        Synth s( 0.08f, 7 );
        s.tone( 1100, 420, 0.5f, 0.001f, 0.03f, 1 );
        s.tone( 1160, 440, 0.5f, 0.001f, 0.03f, 1 );
        s.finish( 0.42f );
        return s.m_s;
    }
    case SfxE::RAIL:
    {
        Synth s( 0.45f, 8 );
        s.tone( 3200, 160, 0.8f, 0.001f, 0.12f, 2 );
        s.noiseLp( 9000, 300, 0.5f, 0.001f, 0.15f );
        s.finish( 0.55f );
        return s.m_s;
    }
    case SfxE::CHAIN:
    {
        Synth s( 0.16f, 9 );
        for ( size_t i = 0; i < s.size(); ++i )
            if ( s.m_rng.chance( 0.02f ) ) s.m_s[i] = s.noise() * 1.0f;
        s.noiseLp( 8000, 2000, 0.4f, 0.001f, 0.06f, 0.0f, true );
        s.finish( 0.4f );
        return s.m_s;
    }
    case SfxE::ENEMYSHOT:
    {
        Synth s( 0.09f, 10 );
        s.tone( 700, 380, 0.6f, 0.002f, 0.04f, 3 );
        s.finish( 0.25f );
        return s.m_s;
    }
    case SfxE::ENEMYLASER:
    {
        Synth s( 0.8f, 11 );
        s.tone( 180, 900, 0.5f, 0.3f, 0.5f, 2 );
        s.tone( 90, 450, 0.4f, 0.3f, 0.5f, 1 );
        s.finish( 0.35f );
        return s.m_s;
    }
    case SfxE::HIT:
    {
        Synth s( 0.06f, 12 );
        s.noiseLp( 7000, 3000, 0.7f, 0.0005f, 0.012f, 0.0f, true );
        s.tone( 2100, 1800, 0.3f, 0.0005f, 0.02f, 0 );
        s.finish( 0.3f );
        return s.m_s;
    }
    case SfxE::SPARK:
    {
        Synth s( 0.05f, 13 );
        s.tone( 3400, 3000, 0.5f, 0.0005f, 0.012f, 3 );
        s.noiseLp( 9000, 6000, 0.4f, 0.0005f, 0.008f, 0.0f, true );
        s.finish( 0.25f );
        return s.m_s;
    }
    case SfxE::BOOM0:
    case SfxE::BOOM1:
    case SfxE::BOOM2:
    case SfxE::BOOM3:
    {
        int   k = (int)e - (int)SfxE::BOOM0;
        float len = 0.45f + k * 0.55f;
        Synth s( len, 20 + k );
        s.noiseLp( 4500 - k * 800, 120, 1.0f, 0.002f, 0.12f + k * 0.18f );
        s.tone( 90 - k * 12, 28, 0.9f, 0.003f, 0.1f + k * 0.12f, 0 );
        if ( k >= 2 ) s.noiseLp( 600, 60, 0.6f, 0.05f, 0.5f + k * 0.2f, 0.08f );
        s.finish( 0.55f + k * 0.1f );
        return s.m_s;
    }
    case SfxE::CREDITS:
    {
        Synth s( 0.16f, 30 );
        s.tone( 1320, 1320, 0.5f, 0.001f, 0.05f, 0, 0.0f, 0.07f );
        s.tone( 1980, 1980, 0.5f, 0.001f, 0.07f, 0, 0.05f );
        s.finish( 0.3f );
        return s.m_s;
    }
    case SfxE::POWER:
    {
        Synth s( 0.42f, 31 );
        const float notes[] = { 523, 659, 784, 1046 };
        for ( int i = 0; i < 4; ++i ) s.tone( notes[i], notes[i], 0.5f, 0.002f, 0.12f, 3, i * 0.07f );
        s.finish( 0.45f );
        return s.m_s;
    }
    case SfxE::ITEM:
    {
        Synth s( 0.5f, 32 );
        s.tone( 784, 784, 0.4f, 0.01f, 0.3f, 0 );
        s.tone( 988, 988, 0.4f, 0.01f, 0.3f, 0, 0.04f );
        s.tone( 1175, 1175, 0.4f, 0.01f, 0.3f, 0, 0.08f );
        s.finish( 0.45f );
        return s.m_s;
    }
    case SfxE::CUBE:
    {
        Synth s( 1.0f, 33 );
        const float notes[] = { 1046, 1568, 2093, 2637 };
        for ( int i = 0; i < 4; ++i ) s.tone( notes[i], notes[i] * 1.003f, 0.35f, 0.005f, 0.35f, 0, i * 0.06f );
        s.finish( 0.45f );
        return s.m_s;
    }
    case SfxE::LIFE:
    {
        Synth s( 0.7f, 34 );
        const float notes[] = { 523, 659, 784, 1046, 784, 1046 };
        for ( int i = 0; i < 6; ++i ) s.tone( notes[i], notes[i], 0.5f, 0.003f, 0.1f, 1, i * 0.09f, 0.12f );
        s.finish( 0.45f );
        return s.m_s;
    }
    case SfxE::SHIELDHIT:
    {
        Synth s( 0.18f, 35 );
        s.tone( 1200, 500, 0.5f, 0.001f, 0.06f, 0 );
        s.tone( 1800, 900, 0.3f, 0.001f, 0.04f, 3 );
        s.noiseLp( 6000, 2000, 0.3f, 0.001f, 0.03f, 0.0f, true );
        s.finish( 0.45f );
        return s.m_s;
    }
    case SfxE::ARMORHIT:
    {
        Synth s( 0.3f, 36 );
        s.noiseLp( 2500, 200, 0.9f, 0.001f, 0.08f );
        s.tone( 140, 60, 0.8f, 0.001f, 0.08f, 0 );
        s.finish( 0.6f );
        return s.m_s;
    }
    case SfxE::DIE:
    {
        Synth s( 1.8f, 37 );
        s.noiseLp( 3500, 80, 1.0f, 0.002f, 0.6f );
        s.tone( 600, 40, 0.5f, 0.002f, 0.7f, 2 );
        s.tone( 70, 25, 0.8f, 0.003f, 0.6f, 0 );
        s.finish( 0.7f );
        return s.m_s;
    }
    case SfxE::SIREN:
    {
        Synth s( 1.8f, 38 );
        for ( int i = 0; i < 4; ++i )
        {
            s.tone( 660, 660, 0.4f, 0.02f, 0.4f, 2, i * 0.45f, 0.22f );
            s.tone( 440, 440, 0.4f, 0.02f, 0.4f, 2, i * 0.45f + 0.22f, 0.22f );
        }
        s.finish( 0.45f );
        return s.m_s;
    }
    case SfxE::NOVA:
    {
        Synth s( 1.4f, 39 );
        s.noiseLp( 200, 6000, 0.6f, 0.25f, 0.25f );
        s.noiseLp( 3000, 80, 1.0f, 0.002f, 0.4f, 0.3f );
        s.tone( 80, 30, 0.8f, 0.003f, 0.5f, 0, 0.3f );
        s.finish( 0.65f );
        return s.m_s;
    }
    case SfxE::MODE:
    {
        Synth s( 0.1f, 40 );
        s.tone( 900, 900, 0.5f, 0.001f, 0.01f, 1, 0.0f, 0.02f );
        s.tone( 1300, 1300, 0.5f, 0.001f, 0.01f, 1, 0.05f, 0.02f );
        s.finish( 0.35f );
        return s.m_s;
    }
    case SfxE::EMPTY:
    case SfxE::DENY:
    {
        Synth s( 0.18f, 41 );
        s.tone( 140, 120, 0.6f, 0.002f, 0.1f, 1 );
        s.tone( 147, 125, 0.5f, 0.002f, 0.1f, 1 );
        s.finish( 0.35f );
        return s.m_s;
    }
    case SfxE::MOVE:
    {
        Synth s( 0.035f, 42 );
        s.tone( 2200, 2000, 0.5f, 0.0005f, 0.01f, 3 );
        s.finish( 0.22f );
        return s.m_s;
    }
    case SfxE::OK:
    {
        Synth s( 0.12f, 43 );
        s.tone( 880, 880, 0.5f, 0.001f, 0.04f, 3, 0.0f, 0.05f );
        s.tone( 1320, 1320, 0.5f, 0.001f, 0.05f, 3, 0.04f );
        s.finish( 0.3f );
        return s.m_s;
    }
    case SfxE::BACK:
    {
        Synth s( 0.12f, 44 );
        s.tone( 1100, 1100, 0.5f, 0.001f, 0.04f, 3, 0.0f, 0.05f );
        s.tone( 740, 740, 0.5f, 0.001f, 0.05f, 3, 0.04f );
        s.finish( 0.3f );
        return s.m_s;
    }
    case SfxE::BUY:
    {
        Synth s( 0.35f, 45 );
        s.tone( 1568, 1568, 0.4f, 0.001f, 0.1f, 0 );
        s.tone( 2093, 2093, 0.4f, 0.001f, 0.15f, 0, 0.06f );
        s.noiseLp( 8000, 4000, 0.15f, 0.001f, 0.03f, 0.0f, true );
        s.finish( 0.4f );
        return s.m_s;
    }
    case SfxE::BLIP:
    {
        Synth s( 0.025f, 46 );
        s.tone( 1800, 1700, 0.4f, 0.0005f, 0.008f, 1 );
        s.finish( 0.12f );
        return s.m_s;
    }
    default: break;
    }
    return std::vector<float>( 100, 0.0f );
}

Wave toWave( const std::vector<float> &s )
{
    Wave w = {};
    w.frameCount = (unsigned)s.size();
    w.sampleRate = Rate;
    w.sampleSize = 16;
    w.channels = 1;
    short *d = (short *)MemAlloc( (unsigned)( s.size() * sizeof( short ) ) );
    for ( size_t i = 0; i < s.size(); ++i ) d[i] = (short)( clampf( s[i], -1, 1 ) * 32000 );
    w.data = d;
    return w;
}

}   // namespace

void Audio::init( bool on, const std::string &assets )
{
    m_on = on && IsAudioDeviceReady();
    m_assets = assets;
    if ( !m_on ) return;
    for ( int e = 0; e < (int)SfxE::COUNT; ++e )
    {
        Wave w = toWave( make( (SfxE)e ) );
        m_sounds[e][0] = LoadSoundFromWave( w );
        UnloadWave( w );
        for ( int v = 1; v < Voices; ++v ) m_sounds[e][v] = LoadSoundAlias( m_sounds[e][0] );
    }
}

void Audio::shutdown()
{
    if ( !m_on ) return;
    for ( int i = 0; i < 2; ++i ) drop( i );
    voice( "", "" );
    for ( int e = 0; e < (int)SfxE::COUNT; ++e )
    {
        for ( int v = 1; v < Voices; ++v ) UnloadSoundAlias( m_sounds[e][v] );
        UnloadSound( m_sounds[e][0] );
    }
    m_on = false;
}

void Audio::play( SfxE s, float x, float volume, float pitch )
{
    if ( !m_on ) return;
    int    e = (int)s;
    double now = GetTime();
    // The same effect at most every 30 ms: a hail of hits would only get louder, not better.
    if ( now - m_last[e] < 0.03 ) return;
    m_last[e] = now;
    Sound &snd = m_sounds[e][m_next[e]];
    m_next[e] = ( m_next[e] + 1 ) % Voices;
    float pan = x < 0 ? 0.5f : 0.5f - ( x / FieldW - 0.5f ) * 0.7f;
    SetSoundPan( snd, clampf( pan, 0.0f, 1.0f ) );
    SetSoundVolume( snd, clampf( volume * m_effectVolume, 0.0f, 1.0f ) );
    SetSoundPitch( snd, pitch );
    PlaySound( snd );
}

void Audio::events( const World &w )
{
    if ( !m_on ) return;
    for ( const SimEvent &e : w.m_events )
    {
        float x = e.m_pos.x;
        switch ( e.m_type )
        {
        case EvE::SHOT:
        {
            const std::string &n = e.m_name;
            SfxE  s = n == "laser" ? SfxE::LASER : n == "missile" ? SfxE::MISSILE : n == "plasma" ? SfxE::PLASMA
                    : n == "flame" ? SfxE::FLAME : n == "beam" ? SfxE::BEAM : n == "spread" ? SfxE::SPREAD
                    : n == "rail" ? SfxE::RAIL : n == "chain" ? SfxE::CHAIN : SfxE::PULSE;
            play( s, x, e.m_a == 0 ? 0.55f : 0.35f, 1.0f + ( e.m_b > 0.5f ? 0.08f : 0.0f ) );
            break;
        }
        case EvE::ENEMYSHOT: play( SfxE::ENEMYSHOT, x, 0.35f, 0.9f + ( (int)( x * 7 ) % 5 ) * 0.05f ); break;
        case EvE::LASER: play( SfxE::ENEMYLASER, x, 0.6f ); break;
        case EvE::HIT: play( SfxE::HIT, x, 0.4f ); break;
        case EvE::SPARK: play( SfxE::SPARK, x, 0.35f ); break;
        case EvE::EXPLODE:
        {
            int k = std::clamp( (int)e.m_a, 0, 3 );
            play( (SfxE)( (int)SfxE::BOOM0 + k ), x, 0.55f + 0.12f * k, 0.9f + ( (int)( x * 13 ) % 7 ) * 0.03f );
            break;
        }
        case EvE::PICKUP:
        {
            const std::string &n = e.m_name;
            play( n == "credits" ? SfxE::CREDITS : n == "power" ? SfxE::POWER : n == "cube" || n == "key" || n == "plan" ? SfxE::CUBE
                : n == "life" ? SfxE::LIFE : SfxE::ITEM,
                  x, 0.6f );
            break;
        }
        case EvE::PLAYERHIT: play( e.m_b > 0.5f ? SfxE::SHIELDHIT : SfxE::ARMORHIT, x, 0.7f ); break;
        case EvE::PLAYERDIE: play( SfxE::DIE, x, 0.9f ); break;
        case EvE::WARN: play( SfxE::SIREN, -1, 0.7f ); break;
        case EvE::NOVA: play( SfxE::NOVA, x, 0.9f ); break;
        case EvE::MODE: play( SfxE::MODE, x, 0.5f ); break;
        case EvE::EMPTY: play( SfxE::EMPTY, x, 0.5f ); break;
        case EvE::CHAIN: play( SfxE::CHAIN, x, 0.4f ); break;
        case EvE::BOSSDIE: play( SfxE::BOOM3, x, 1.0f, 0.8f ); break;
        case EvE::MUSIC: music( e.m_name ); break;
        default: break;
        }
    }
}

void Audio::drop( int i )
{
    if ( m_has[i] )
    {
        StopMusicStream( m_music[i] );
        UnloadMusicStream( m_music[i] );
        m_has[i] = false;
    }
    if ( m_hasLow[i] )
    {
        StopMusicStream( m_low[i] );
        UnloadMusicStream( m_low[i] );
        m_hasLow[i] = false;
    }
}

void Audio::music( const std::string &name )
{
    if ( !m_on || name == m_name ) return;
    m_name = name;
    int next = 1 - m_cur;
    drop( next );
    // A piece may come in two takes (<name>.ogg, <name>_2.ogg): one of them, by chance.
    std::string file = name;
    if ( !name.empty() && std::filesystem::exists( m_assets + "/music/" + name + "_2.ogg" ) && GetRandomValue( 0, 1 ) == 1 )
        file = name + "_2";
    if ( !name.empty() )
        for ( const char *ext : { ".ogg", ".mp3", ".wav", ".flac" } )
        {
            std::string path = m_assets + "/music/" + file + ext;
            if ( !std::filesystem::exists( path ) ) continue;
            m_music[next] = LoadMusicStream( path.c_str() );
            if ( IsMusicValid( m_music[next] ) )
            {
                m_has[next] = true;
                m_music[next].looping = true;
                m_gain[next] = 0.0f;
                SetMusicVolume( m_music[next], 0.0f );
                // The quiet layer is just as long: started in the same breath, the two stay in step.
                std::string low = m_assets + "/music/" + file + "_low" + ext;
                if ( std::filesystem::exists( low ) )
                {
                    m_low[next] = LoadMusicStream( low.c_str() );
                    m_hasLow[next] = IsMusicValid( m_low[next] );
                    if ( m_hasLow[next] )
                    {
                        m_low[next].looping = true;
                        SetMusicVolume( m_low[next], 0.0f );
                        PlayMusicStream( m_low[next] );
                    }
                }
                PlayMusicStream( m_music[next] );
            }
            break;
        }
    m_cur = next;
}

void Audio::update( float dt )
{
    if ( !m_on ) return;
    // A voice: the music steps back while it speaks.
    if ( m_hasVoice )
    {
        UpdateMusicStream( m_voice );
        SetMusicVolume( m_voice, m_voiceVolume );
        if ( !IsMusicStreamPlaying( m_voice ) ) voice( "", "" );
    }
    m_duck += clampf( ( m_hasVoice ? 0.5f : 1.0f ) - m_duck, -dt * 3.0f, dt * 1.0f );
    m_mix += clampf( m_mixGoal - m_mix, -dt * 0.35f, dt * 0.6f );
    for ( int i = 0; i < 2; ++i )
    {
        if ( !m_has[i] ) continue;
        float goal = i == m_cur ? 1.0f : 0.0f;
        m_gain[i] += clampf( goal - m_gain[i], -dt / 1.2f, dt / 1.2f );
        // The quiet layer is the same music through a low pass, in phase: linear gains keep the bass steady and
        // the fade opens the filter.
        float full = m_hasLow[i] ? m_mix : 1.0f, low = 1.0f - m_mix;
        SetMusicVolume( m_music[i], m_gain[i] * m_musicVolume * m_duck * full );
        UpdateMusicStream( m_music[i] );
        if ( m_hasLow[i] )
        {
            SetMusicVolume( m_low[i], m_gain[i] * m_musicVolume * m_duck * low );
            UpdateMusicStream( m_low[i] );
        }
        if ( i != m_cur && m_gain[i] <= 0.0f ) drop( i );
    }
}

bool Audio::voice( const std::string &key, const std::string &lang )
{
    if ( !m_on ) return false;
    if ( m_hasVoice )
    {
        StopMusicStream( m_voice );
        UnloadMusicStream( m_voice );
        m_hasVoice = false;
    }
    if ( key.empty() || m_voiceVolume <= 0.01f ) return false;
    std::string path = m_assets + "/voice/" + lang + "/" + key + ".ogg";
    if ( !std::filesystem::exists( path ) ) return false;
    m_voice = LoadMusicStream( path.c_str() );
    if ( !IsMusicValid( m_voice ) ) return false;
    m_voice.looping = false;
    SetMusicVolume( m_voice, m_voiceVolume );
    PlayMusicStream( m_voice );
    m_hasVoice = true;
    return true;
}

void Audio::dump( const std::string &dir )
{
    std::filesystem::create_directories( dir );
    const char *names[] = { "pulse",  "laser", "missile", "plasma", "flame", "beam",   "spread", "rail",  "chain",
                            "eshot",  "elaser", "hit",    "spark",  "boom0", "boom1",  "boom2",  "boom3", "credits",
                            "power",  "item",  "cube",    "life",   "shieldhit", "armorhit", "die", "siren", "nova",
                            "mode",   "empty", "move",    "ok",     "back",  "buy",    "deny",   "blip" };
    for ( int e = 0; e < (int)SfxE::COUNT; ++e )
    {
        Wave w = toWave( make( (SfxE)e ) );
        ExportWave( w, ( dir + "/" + names[e] + ".wav" ).c_str() );
        UnloadWave( w );
    }
}

}   // namespace zenith
