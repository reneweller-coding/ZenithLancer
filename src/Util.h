/**
 * @file Util.h
 * @brief Small things everybody needs: the playing field's size, the clock, a seeded random source, vectors on the
 *        field, colours from text.
 * @author Rene Weller
 */
#pragma once

#include <raylib.h>

#include <cmath>
#include <cstdint>
#include <string>

namespace zenith
{

constexpr float FieldW = 720.0f;            ///< the playing field's width in field units
constexpr float FieldH = 800.0f;            ///< its height
constexpr float Dt = 1.0f / 60.0f;          ///< one step of the game (60 a second, always)
constexpr float Pi = 3.14159265358979f;     ///< pi
constexpr float Deg = Pi / 180.0f;          ///< one degree in radians

// The camera looks straight down at the field from CamHeight; the ground lies GroundDepth below it, so the ground
// moves at GroundFactor of its own speed on the screen (and what stands on it looks that much smaller).
constexpr float CamHeight = 1600.0f;                                     ///< the camera above the field
constexpr float GroundDepth = 260.0f;                                    ///< the ground below the field
constexpr float GroundFactor = CamHeight / ( CamHeight + GroundDepth );   ///< the ground's scale on the field
constexpr float RiseTime = 1.4f;   ///< seconds a flier launched from the ground takes to rise to the field

/** @brief A small, fast random source (xorshift); the same seed gives the same game everywhere. */
struct Rng
{
    uint32_t m_state = 0x9e3779b9u;   ///< the state (never 0)

    /** @brief Starts over. @param[in] seed  any number */
    void seed( uint32_t seed ) { m_state = seed ? seed * 2654435761u + 1u : 0x9e3779b9u; }

    /** @return the next 32 bits */
    uint32_t next()
    {
        uint32_t x = m_state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return m_state = x;
    }

    /** @return a number in [0, 1) */
    float unit() { return ( next() >> 8 ) * ( 1.0f / 16777216.0f ); }

    /** @return a number in [a, b) */
    float range( float a, float b ) { return a + ( b - a ) * unit(); }

    /** @return a whole number in [0, n) */
    int below( int n ) { return n > 0 ? (int)( next() % (uint32_t)n ) : 0; }

    /** @return true with the chance p */
    bool chance( float p ) { return unit() < p; }
};

/** @return a + b */
inline Vector2 add( Vector2 a, Vector2 b ) { return { a.x + b.x, a.y + b.y }; }
/** @return a - b */
inline Vector2 sub( Vector2 a, Vector2 b ) { return { a.x - b.x, a.y - b.y }; }
/** @return a * s */
inline Vector2 mul( Vector2 a, float s ) { return { a.x * s, a.y * s }; }
/** @return the length of a */
inline float len( Vector2 a ) { return std::sqrt( a.x * a.x + a.y * a.y ); }
/** @return the squared distance of a and b */
inline float dist2( Vector2 a, Vector2 b ) { return ( a.x - b.x ) * ( a.x - b.x ) + ( a.y - b.y ) * ( a.y - b.y ); }
/** @return a of length 1 (or (0, 1) when a is 0) */
inline Vector2 norm( Vector2 a )
{
    float l = len( a );
    return l > 1e-6f ? Vector2{ a.x / l, a.y / l } : Vector2{ 0.0f, 1.0f };
}
/** @return the direction of an angle on the field: 0 degrees points down the screen, 90 to the left (clockwise) */
inline Vector2 dirOf( float deg ) { return { -std::sin( deg * Deg ), std::cos( deg * Deg ) }; }
/** @return the angle of a direction (the inverse of dirOf) */
inline float angleOf( Vector2 d ) { return std::atan2( -d.x, d.y ) / Deg; }
/** @return v held between lo and hi */
inline float clampf( float v, float lo, float hi ) { return v < lo ? lo : ( v > hi ? hi : v ); }
/** @return a mix of a and b */
inline float lerpf( float a, float b, float t ) { return a + ( b - a ) * t; }
/** @return an angle in degrees brought into (-180, 180] */
inline float wrapDeg( float a )
{
    while ( a > 180.0f ) a -= 360.0f;
    while ( a <= -180.0f ) a += 360.0f;
    return a;
}

/**
 * @brief Reads a colour: "#rgb", "#rrggbb", "#rrggbbaa" or a name of the palette (steel, crimson ...).
 * @param[in] text  the colour
 * @param[in] def   when it cannot be read
 * @return the colour
 */
Color parseColor( const std::string &text, Color def = WHITE );

/** @return a colour mixed of a and b */
inline Color mixColor( Color a, Color b, float t )
{
    return { (unsigned char)lerpf( a.r, b.r, t ), (unsigned char)lerpf( a.g, b.g, t ),
             (unsigned char)lerpf( a.b, b.b, t ), (unsigned char)lerpf( a.a, b.a, t ) };
}

/** @return a colour scaled in brightness (alpha kept) */
inline Color scaleColor( Color c, float s )
{
    return { (unsigned char)clampf( c.r * s, 0, 255 ), (unsigned char)clampf( c.g * s, 0, 255 ),
             (unsigned char)clampf( c.b * s, 0, 255 ), c.a };
}

}   // namespace zenith
