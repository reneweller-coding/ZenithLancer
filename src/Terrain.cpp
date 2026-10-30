/**
 * @file Terrain.cpp
 * @brief The ground and the starfield: the heightfield chunks and their props (built on a worker thread), the ground
 *        shader with one surface per style, the space sky and the clouds.
 * @author Rene Weller
 */
#include "Terrain.h"

#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace zenith
{

namespace
{

constexpr float Cell = 10.0f;                        ///< the grid step of the ground (world units)
constexpr int   Rows = (int)( ChunkLen / Cell );     ///< grid rows per chunk
constexpr float PlateSize = 160.0f;                  ///< station plating: the big plates (the shader knows it too)
constexpr float CityBlock = 60.0f;                   ///< city: a block with its street (the shader knows it too)
constexpr float StreetW = 10.0f;                     ///< city: the street's width (the shader knows it too)
constexpr float HexR = 38.0f;                        ///< lattice: the cells' size (the shader knows it too)

/** @brief What a vertex is made of (the ground shader's fragMat.x). */
enum MatE : int
{
    M_GROUND = 0, M_BUILDING = 1, M_METAL = 2, M_LAMP = 3, M_ROCK = 4, M_PLANT = 5, M_HALL = 6, M_HOT = 7
};

uint32_t hash2( int x, int y, uint32_t seed )
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
    h = ( h ^ ( h >> 13 ) ) * 1274126177u;
    return h ^ ( h >> 16 );
}

float hashf( int x, int y, uint32_t seed ) { return ( hash2( x, y, seed ) & 0xffffff ) / 16777215.0f; }

/** @brief Smooth value noise in [-1, 1]. */
float vnoise( float x, float y, uint32_t seed )
{
    int   ix = (int)std::floor( x ), iy = (int)std::floor( y );
    float fx = x - ix, fy = y - iy;
    float ux = fx * fx * ( 3 - 2 * fx ), uy = fy * fy * ( 3 - 2 * fy );
    float a = hashf( ix, iy, seed ), b = hashf( ix + 1, iy, seed ), c = hashf( ix, iy + 1, seed ),
          d = hashf( ix + 1, iy + 1, seed );
    return ( lerpf( lerpf( a, b, ux ), lerpf( c, d, ux ), uy ) ) * 2.0f - 1.0f;
}

float fbm( float x, float y, uint32_t seed, int oct )
{
    float s = 0, amp = 0.5f, f = 1;
    for ( int i = 0; i < oct; ++i )
    {
        s += vnoise( x * f, y * f, seed + i * 101 ) * amp;
        f *= 2.03f;
        amp *= 0.5f;
    }
    return s;
}

float smooth( float a, float b, float x )
{
    float t = clampf( ( x - a ) / ( b - a ), 0.0f, 1.0f );
    return t * t * ( 3 - 2 * t );
}

Color numColor( const Line &l, size_t i, Color def ) { return parseColor( l.arg( i ), def ); }

Color withAlpha( Color c, int a ) { return { c.r, c.g, c.b, (unsigned char)std::clamp( a, 0, 255 ) }; }

/** @brief Fills chunk geometry: vertices with their material, indexed triangles, a new part when one is full. */
class Builder
{
public:
    explicit Builder( Terrain::Geometry &geo ) : m_geo( geo )
    {
        if ( m_geo.m_parts.empty() ) m_geo.m_parts.emplace_back();
    }

    /** @return true when n more vertices fit (a new part is started when this one is full) */
    bool room( int n )
    {
        if ( count() + n <= 65535 ) return true;
        if ( (int)m_geo.m_parts.size() >= Terrain::MaxParts ) return false;
        m_geo.m_parts.emplace_back();
        return true;
    }

    int count() const { return (int)( m_geo.m_parts.back().m_pos.size() / 3 ); }

    int vert( Vector3 p, Vector3 n, Color c, float u, float v, float mat, float rnd, Vector4 t = { 0, 0, 0, 0 } )
    {
        Terrain::Geometry::Part &q = m_geo.m_parts.back();
        q.m_pos.insert( q.m_pos.end(), { p.x, p.y, p.z } );
        q.m_nrm.insert( q.m_nrm.end(), { n.x, n.y, n.z } );
        q.m_col.insert( q.m_col.end(), { c.r, c.g, c.b, c.a } );
        q.m_uv.insert( q.m_uv.end(), { u, v } );
        q.m_uv2.insert( q.m_uv2.end(), { mat, rnd } );
        q.m_tan.insert( q.m_tan.end(), { t.x, t.y, t.z, t.w } );
        return count() - 1;
    }

    void tri( int a, int b, int c )
    {
        Terrain::Geometry::Part &q = m_geo.m_parts.back();
        q.m_idx.insert( q.m_idx.end(), { (unsigned short)a, (unsigned short)b, (unsigned short)c } );
    }

    void quad( int a, int b, int c, int d )
    {
        tri( a, b, c );
        tri( a, c, d );
    }

    /**
     * @brief A box standing on its base (no bottom): flat faces, each with its own surface coordinates (walls: along
     *        the wall and up; the roof: from its middle) and the half size in the tangent (roof edges, windows).
     */
    void box( Vector3 c, Vector3 h, Color top, Color side, float mat, float rnd, float taper = 0.0f )
    {
        if ( !room( 20 ) ) return;
        float   tx = h.x * ( 1 - taper ), tz = h.z * ( 1 - taper );
        Vector4 tan = { tx, tz, h.y * 2, 0 };
        float   y0 = c.y - h.y, y1 = c.y + h.y;
        int     a = vert( { c.x - tx, y1, c.z - tz }, { 0, 1, 0 }, top, -tx, tz, mat, rnd, tan );
        int     b = vert( { c.x + tx, y1, c.z - tz }, { 0, 1, 0 }, top, tx, tz, mat, rnd, tan );
        int     cc = vert( { c.x + tx, y1, c.z + tz }, { 0, 1, 0 }, top, tx, -tz, mat, rnd, tan );
        int     d = vert( { c.x - tx, y1, c.z + tz }, { 0, 1, 0 }, top, -tx, -tz, mat, rnd, tan );
        quad( a, b, cc, d );
        // The walls, counter-clockwise seen from above: bottom corners p0 p1, top corners q0 q1.
        const Vector3 p[4] = { { c.x - h.x, y0, c.z + h.z }, { c.x + h.x, y0, c.z + h.z }, { c.x + h.x, y0, c.z - h.z },
                               { c.x - h.x, y0, c.z - h.z } };
        const Vector3 q[4] = { { c.x - tx, y1, c.z + tz }, { c.x + tx, y1, c.z + tz }, { c.x + tx, y1, c.z - tz },
                               { c.x - tx, y1, c.z - tz } };
        for ( int i = 0; i < 4; ++i )
        {
            int     j = ( i + 1 ) & 3;
            Vector3 n = Vector3Normalize( Vector3CrossProduct( Vector3Subtract( p[j], p[i] ),
                                                               Vector3Subtract( q[i], p[i] ) ) );
            Vector3 mid = Vector3Scale( Vector3Add( p[i], p[j] ), 0.5f );
            if ( Vector3DotProduct( n, Vector3Subtract( mid, { c.x, mid.y, c.z } ) ) < 0 ) n = Vector3Negate( n );
            float len = Vector3Distance( p[i], p[j] );
            int   v0 = vert( p[i], n, side, 0, 0, mat, rnd, tan ), v1 = vert( p[j], n, side, len, 0, mat, rnd, tan );
            int   v2 = vert( q[j], n, side, len, h.y * 2, mat, rnd, tan ), v3 = vert( q[i], n, side, 0, h.y * 2, mat, rnd, tan );
            quad( v0, v1, v2, v3 );
        }
    }

    /** @brief Sets the surface coordinates of the last n vertices (a lamp's blink: rate a second, phase). */
    void setUvLast( int n, float u, float v )
    {
        std::vector<float> &uv = m_geo.m_parts.back().m_uv;
        for ( size_t i = uv.size() - (size_t)std::min( n, count() ) * 2; i < uv.size(); i += 2 )
        {
            uv[i] = u;
            uv[i + 1] = v;
        }
    }

    /** @brief A small light: steady (rate 0), blinking (rate > 0 a second) or pulsing (rate < 0). */
    void lamp( Vector3 c, float s, Color col, float rate, float phase )
    {
        if ( !room( 20 ) ) return;
        box( c, { s, s * 0.7f, s }, col, col, M_LAMP, 0 );
        setUvLast( 20, rate, phase );
    }

    /**
     * @brief An upright (tapering) cylinder with smooth sides and a flat top of its own material; a lamp top gets
     *        topRate as its blink (see lamp).
     */
    void cylinder( Vector3 base, float r0, float r1, float hgt, int sides, Color side, Color top, float mat, float topMat,
                   float rnd, float angle = 0.0f, float topRate = 0.0f )
    {
        if ( !room( sides * 3 + 3 ) ) return;
        int   first = count();
        float slope = ( r0 - r1 ) / std::max( 0.01f, hgt );
        for ( int i = 0; i <= sides; ++i )
        {
            float   a = angle + i / (float)sides * 2 * Pi, cs = std::cos( a ), sn = std::sin( a );
            Vector3 n = Vector3Normalize( { cs, slope, sn } );
            // A lamp's surface coordinates are its blink, not a place.
            bool  lampSide = (int)mat == M_LAMP;
            float u = lampSide ? topRate : i / (float)sides * 2 * Pi * r0;
            vert( { base.x + cs * r0, base.y, base.z + sn * r0 }, n, side, u, lampSide ? rnd : 0, mat, rnd );
            vert( { base.x + cs * r1, base.y + hgt, base.z + sn * r1 }, n, side, u, lampSide ? rnd : hgt, mat, rnd );
        }
        for ( int i = 0; i < sides; ++i ) quad( first + i * 2, first + i * 2 + 2, first + i * 2 + 3,
                                                first + i * 2 + 1 );
        if ( r1 <= 0.01f ) return;
        int centre = vert( { base.x, base.y + hgt, base.z }, { 0, 1, 0 }, top, topRate, rnd, topMat, rnd );
        for ( int i = 0; i <= sides; ++i )
        {
            float a = angle + i / (float)sides * 2 * Pi;
            vert( { base.x + std::cos( a ) * r1, base.y + hgt, base.z + std::sin( a ) * r1 }, { 0, 1, 0 }, top, topRate,
                  rnd, topMat, rnd );
        }
        for ( int i = 0; i < sides; ++i ) tri( centre, centre + 1 + i, centre + 2 + i );
    }

    /** @brief A lying pipe from a to b (horizontal), smooth all round. */
    void pipe( Vector3 a, Vector3 b, float r, int sides, Color col, float rnd )
    {
        if ( !room( ( sides + 1 ) * 2 ) ) return;
        Vector3 ax = Vector3Normalize( Vector3Subtract( b, a ) );
        Vector3 up = { 0, 1, 0 }, side = Vector3Normalize( Vector3CrossProduct( ax, up ) );
        float   len = Vector3Distance( a, b );
        int     first = count();
        for ( int i = 0; i <= sides; ++i )
        {
            float   t = i / (float)sides * Pi;   // the upper half is all the camera sees
            Vector3 n = Vector3Add( Vector3Scale( side, std::cos( t ) ), Vector3Scale( up, std::sin( t ) ) );
            vert( Vector3Add( a, Vector3Scale( n, r ) ), n, col, 0, t * r, M_METAL, rnd );
            vert( Vector3Add( b, Vector3Scale( n, r ) ), n, col, len, t * r, M_METAL, rnd );
        }
        for ( int i = 0; i < sides; ++i ) quad( first + i * 2, first + i * 2 + 1, first + i * 2 + 3,
                                                first + i * 2 + 2 );
    }

    /** @brief A dome (half a squashed sphere). */
    void dome( Vector3 c, float r, float hgt, Color col, float mat, float rnd )
    {
        const int seg = 10, rings = 4;
        if ( !room( seg * rings + 1 ) ) return;
        int first = count();
        for ( int j = 0; j < rings; ++j )
        {
            float t = j / (float)rings * Pi * 0.5f, cr = std::cos( t ), sr = std::sin( t );
            for ( int i = 0; i < seg; ++i )
            {
                float   a = i / (float)seg * 2 * Pi;
                Vector3 n = Vector3Normalize( { std::cos( a ) * cr / r, sr / std::max( 0.1f, hgt ), std::sin( a ) * cr / r } );
                vert( { c.x + std::cos( a ) * cr * r, c.y + sr * hgt, c.z + std::sin( a ) * cr * r }, n, col, a * r,
                      sr * hgt, mat, rnd );
            }
        }
        int top = vert( { c.x, c.y + hgt, c.z }, { 0, 1, 0 }, col, 0, hgt, mat, rnd );
        for ( int j = 0; j + 1 < rings; ++j )
            for ( int i = 0; i < seg; ++i )
            {
                int i1 = ( i + 1 ) % seg;
                quad( first + j * seg + i, first + j * seg + i1, first + ( j + 1 ) * seg + i1,
                      first + ( j + 1 ) * seg + i );
            }
        for ( int i = 0; i < seg; ++i ) tri( first + ( rings - 1 ) * seg + i,
                                             first + ( rings - 1 ) * seg + ( i + 1 ) % seg, top );
    }

    /** @brief A rock: a lumpy sphere (subdivided icosahedron) that tumbles about its middle (in the tangent). */
    void rock( Vector3 c, float r, Color col, float spin, uint32_t seed, Rng &rng )
    {
        struct Ico
        {
            std::vector<Vector3> m_v;
            std::vector<int>     m_t;
        };
        static const Ico ico = []() {
            Ico   s;
            float t = ( 1.0f + std::sqrt( 5.0f ) ) * 0.5f;
            s.m_v = { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t }, { 0, 1, t },
                      { 0, -1, -t }, { 0, 1, -t }, { t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } };
            for ( Vector3 &v : s.m_v ) v = Vector3Normalize( v );
            std::vector<int> f = { 0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6,
                                   7, 1, 8, 3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10,
                                   8, 6, 7, 9, 8, 1 };
            // One subdivision: every edge's middle once.
            std::map<std::pair<int, int>, int> mids;
            auto mid = [&]( int a, int b ) {
                auto key = std::make_pair( std::min( a, b ), std::max( a, b ) );
                auto it = mids.find( key );
                if ( it != mids.end() ) return it->second;
                s.m_v.push_back( Vector3Normalize( Vector3Add( s.m_v[(size_t)a], s.m_v[(size_t)b] ) ) );
                return mids[key] = (int)s.m_v.size() - 1;
            };
            for ( size_t i = 0; i < f.size(); i += 3 )
            {
                int a = f[i], b = f[i + 1], cc = f[i + 2], ab = mid( a, b ), bc = mid( b, cc ), ca = mid( cc, a );
                s.m_t.insert( s.m_t.end(), { a, ab, ca, b, bc, ab, cc, ca, bc, ab, bc, ca } );
            }
            return s;
        }();
        int nv = (int)ico.m_v.size();
        if ( !room( nv ) ) return;
        Vector3 stretch = { rng.range( 0.8f, 1.3f ), rng.range( 0.6f, 0.9f ), rng.range( 0.8f, 1.3f ) };
        float   o = rng.range( 0, 100 );
        std::vector<Vector3> p( (size_t)nv ), n( (size_t)nv, { 0, 0, 0 } );
        for ( int i = 0; i < nv; ++i )
        {
            const Vector3 &v = ico.m_v[(size_t)i];
            float          k = 0.78f + 0.42f * fbm( v.x * 1.6f + o, v.y * 1.6f + v.z * 1.1f, seed, 3 ) +
                      0.12f * fbm( v.z * 4.0f + o, v.x * 4.0f - v.y * 2.0f, seed + 7, 2 );
            p[(size_t)i] = { v.x * k * stretch.x * r, v.y * k * stretch.y * r, v.z * k * stretch.z * r };
        }
        for ( size_t t = 0; t < ico.m_t.size(); t += 3 )
        {
            int     a = ico.m_t[t], b = ico.m_t[t + 1], cc = ico.m_t[t + 2];
            Vector3 fn = Vector3CrossProduct( Vector3Subtract( p[(size_t)b], p[(size_t)a] ),
                                              Vector3Subtract( p[(size_t)cc], p[(size_t)a] ) );
            for ( int q : { a, b, cc } ) n[(size_t)q] = Vector3Add( n[(size_t)q], fn );
        }
        int     first = count();
        Vector4 tan = { c.x, c.y, c.z, spin };
        for ( int i = 0; i < nv; ++i )
        {
            Vector3 nn = Vector3Normalize( n[(size_t)i] );
            if ( Vector3DotProduct( nn, p[(size_t)i] ) < 0 ) nn = Vector3Negate( nn );
            Color k = scaleColor( col, rng.range( 0.75f, 1.15f ) );
            vert( Vector3Add( c, p[(size_t)i] ), nn, withAlpha( k, 0 ), 0, 0, M_ROCK, rng.unit(), tan );
        }
        for ( size_t t = 0; t < ico.m_t.size(); t += 3 ) tri( first + ico.m_t[t], first + ico.m_t[t + 1],
                                                              first + ico.m_t[t + 2] );
    }

private:
    Terrain::Geometry &m_geo;
};

// ---------------------------------------------------------------------------------------------------------------
// Shaders. The ground shader is one text with a surface per style; the style and the fluid are #defines.
// ---------------------------------------------------------------------------------------------------------------

const char *GroundVs = R"(
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;
in vec4 vertexTangent;
uniform mat4 mvp;
uniform mat4 matModel;
uniform float time;
uniform float scrollG;
out vec3 fragPos;
out vec3 fragNormal;
out vec4 fragColor;
out vec2 fragUv;
out vec2 fragMat;
out vec4 fragTan;
out vec2 fragGround;
void main()
{
    vec3 p = vertexPosition;
    vec3 n = vertexNormal;
    if ( abs( vertexTexCoord2.x - 4.0 ) < 0.5 && vertexTangent.w != 0.0 )
    {
        // A rock tumbles about its middle (the tangent holds the middle and the spin).
        vec3 c = vertexTangent.xyz;
        float a = time * vertexTangent.w;
        vec3 k = normalize( vec3( sin( c.x * 0.013 ), 0.7, cos( c.z * 0.011 ) ) );
        float cs = cos( a ), sn = sin( a );
        vec3 q = p - c;
        q = q * cs + cross( k, q ) * sn + k * dot( k, q ) * ( 1.0 - cs );
        n = n * cs + cross( k, n ) * sn + k * dot( k, n ) * ( 1.0 - cs );
        p = c + q;
    }
    vec4 wp = matModel * vec4( p, 1.0 );
    fragPos = wp.xyz;
    fragNormal = n;   // chunks are only moved, never turned
    fragColor = vertexColor;
    fragUv = vertexTexCoord;
    fragMat = vertexTexCoord2;
    fragTan = vertexTangent;
    fragGround = vec2( wp.x, scrollG - wp.z );
    gl_Position = mvp * vec4( p, 1.0 );
}
)";

// Noise and hashes shared by the ground and the sky.
const char *NoiseGlsl = R"(
uniform sampler2D texture0;   // four value noises (r g b a), one lattice step = 16 texels, repeating every 16 steps
float vn( vec2 p ) { return texture( texture0, p * 0.0625 ).r; }
vec4 vn4( vec2 p ) { return texture( texture0, p * 0.0625 ); }
// Each octave turned against the last, so no grid shows through.
float fbm3( vec2 p )
{
    const mat2 r = mat2( 0.8, -0.6, 0.6, 0.8 );
    vec2 p2 = r * p * 2.03 + 17.3;
    return vn( p ) * 0.5 + vn( p2 ) * 0.3 + vn( r * p2 * 2.01 + 41.7 ) * 0.2;
}
// A soft glowing thread along the middle of a noise (bright core, a halo, its width varying on its way).
float glowThread( float v, float sharp )
{
    float r = 1.0 - abs( v * 2.0 - 1.0 );
    return pow( r, sharp ) + pow( r, sharp * 0.2 ) * 0.12;
}
uvec2 pcg( uvec2 v )
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;
    v ^= v >> 16u;
    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;
    v ^= v >> 16u;
    return v;
}
vec2 h22( vec2 c ) { return vec2( pcg( uvec2( ivec2( floor( c ) ) + 65536 ) ) ) * ( 1.0 / 4294967296.0 ); }
float h12( vec2 c ) { return h22( c ).x; }
)";

const char *GroundHeadFs = R"(
in vec3 fragPos;
in vec3 fragNormal;
in vec4 fragColor;
in vec2 fragUv;
in vec2 fragMat;
in vec4 fragTan;
in vec2 fragGround;
uniform sampler2D texture1;   // the shadows of what flies
uniform sampler2D texture2;   // the clouds (alpha)
uniform vec3 sunDir;
uniform vec3 sunColor;
uniform vec3 ambUp;
uniform vec3 ambDown;
uniform vec3 camPos;
uniform int lightCount;
uniform vec4 lightPos[16];
uniform vec4 lightCol[16];
uniform vec4 fogColor;
uniform float time;
uniform float groundLight;
uniform float relief;
uniform float waterH;
uniform vec3 band0;
uniform vec3 band1;
uniform vec3 band2;
uniform vec3 band3;
uniform vec3 waterCol;
uniform float waterGlow;
uniform vec3 glowCol;
uniform vec3 glowCol2;
uniform float glowAmount;
uniform vec3 treeCol;
uniform vec4 shadowRect;
uniform float shadowY;
uniform float shadowOn;
uniform vec4 cloudParam;
uniform float cloudY;
uniform float windows;
uniform float waves;
uniform float detail;
uniform float paint;
uniform vec3 mistCol;
uniform vec2 mist;
out vec4 finalColor;

struct Surf
{
    vec3 n;
    vec3 alb;
    vec3 emit;
    float spec;
    float gloss;
    float ao;
};

// The nearest of jittered points on a unit grid: the distance to it, to the cell's border, its cell, the way to it.
float cF1;
float cEdge;
vec2 cId;
vec2 cRel;
void cells( vec2 p, float jit )
{
    vec2 ip = floor( p ), fp = p - ip;
    vec2 mg = vec2( 0.0 ), mr = vec2( 0.0 );
    float md = 8.0;
    for ( int j = -1; j <= 1; ++j )
        for ( int i = -1; i <= 1; ++i )
        {
            vec2 g = vec2( float( i ), float( j ) );
            vec2 r = g + 0.5 + ( h22( ip + g ) - 0.5 ) * jit - fp;
            float d = dot( r, r );
            if ( d < md ) { md = d; mr = r; mg = g; }
        }
    float me = 8.0;
    for ( int j = -1; j <= 1; ++j )
        for ( int i = -1; i <= 1; ++i )
        {
            vec2 g = mg + vec2( float( i ), float( j ) );
            vec2 r = g + 0.5 + ( h22( ip + g ) - 0.5 ) * jit - fp;
            vec2 dd = r - mr;
            if ( dot( dd, dd ) > 1e-5 ) me = min( me, dot( 0.5 * ( mr + r ), normalize( dd ) ) );
        }
    cF1 = sqrt( md );
    cEdge = me;
    cId = ip + mg;
    cRel = mr;
}

// World units per pixel (set first thing in main: derivatives are only sure outside branches).
float g_upp;

// A line along d = 0 where d is a distance in world units, w its half width: never thinner than a pixel (fainter
// instead), smooth edges. No derivatives, so it may be used anywhere.
float wline( float d, float w )
{
    float wp = w / g_upp;
    float ww = max( wp, 0.6 );
    return ( 1.0 - smoothstep( ww - 0.5, ww + 0.5, abs( d ) / g_upp ) ) * min( 1.0, wp / 0.6 );
}

// The same along the zero line of any smooth value v (a noise): measured in pixels by its derivative.
float fline( float v, float w )
{
    float px = abs( v ) / max( fwidth( v ), 1e-5 );
    float wp = w / g_upp;
    float ww = max( wp, 0.6 );
    return ( 1.0 - smoothstep( ww - 0.5, ww + 0.5, px ) ) * min( 1.0, wp / 0.6 );
}

vec3 bands( float h, float jit )
{
    float t = clamp( h / max( relief, 1.0 ) * 0.5 + 0.5 + jit, 0.0, 1.0 );
    vec3 c = mix( band0, band1, smoothstep( 0.0, 0.38, t ) );
    c = mix( c, band2, smoothstep( 0.3, 0.72, t ) );
    return mix( c, band3, smoothstep( 0.68, 1.0, t ) );
}

// Small relief from noise: p in noise steps.
vec3 bump( vec3 n, vec2 p, float amt )
{
    float e = 0.3;
    float h0 = fbm3( p ), hx = fbm3( p + vec2( e, 0.0 ) ), hz = fbm3( p + vec2( 0.0, e ) );
    return normalize( n + vec3( -( hx - h0 ), 0.0, hz - h0 ) / e * amt );
}

vec3 lavaRamp( float t )
{
    vec3 deep = waterCol * vec3( 0.4, 0.1, 0.08 );
    vec3 hot = mix( waterCol, vec3( 1.0, 0.8, 0.45 ), 0.5 );
    return t < 0.5 ? mix( deep, waterCol, t * 2.0 ) : mix( waterCol, hot, t * 2.0 - 1.0 );
}

// Round crowns packed close (forests, polyps): k how much of it is canopy.
void canopy( inout Surf s, vec2 gp, float size, vec3 col, float k )
{
    cells( gp / size, 0.85 );
    float rad = 0.62 + 0.3 * h12( cId + 3.0 );
    float r = cF1 / rad;
    vec2 o = -cRel / rad;
    float dome = sqrt( max( 0.0, 1.0 - min( r * r, 1.0 ) ) );
    vec3 n = normalize( vec3( o.x, dome * 1.2 + 0.2, -o.y ) );
    vec2 q = gp / ( size * 0.28 ) + cId * 3.7;
    float l0 = vn( q ), lx = vn( q + vec2( 0.3, 0.0 ) ), lz = vn( q + vec2( 0.0, 0.3 ) );
    n = normalize( n + vec3( -( lx - l0 ), 0.0, lz - l0 ) * 2.5 );
    float hue = h12( cId + 11.0 );
    vec3 c = col * ( 0.72 + 0.5 * h12( cId + 17.0 ) );
    c = mix( c, c * vec3( 1.3, 1.15, 0.5 ), smoothstep( 0.8, 1.0, hue ) );
    c = mix( c, c * vec3( 0.65, 0.85, 1.05 ), smoothstep( 0.22, 0.0, hue ) );
    c *= 0.82 + 0.36 * l0;
    float inside = 1.0 - smoothstep( 0.8, 1.0, r );
    s.n = normalize( mix( s.n, n, k * inside ) );
    s.alb = mix( s.alb, mix( col * 0.22, c, inside ), k );
    s.ao = mix( s.ao, mix( 0.4, 1.0, inside * ( 0.55 + 0.45 * dome ) ), k );
    s.spec = mix( s.spec, 0.05, k );
}
)";

const char *GroundStylesFs = R"(
#if STYLE == S_ROCK
void land( inout Surf s, vec2 gp, float h, float raw, vec4 m )
{
    float jit = ( fbm3( gp / 55.0 ) - 0.5 ) * 0.3;
    s.n = bump( s.n, gp / 7.0, 0.5 * detail );
    float slope = 1.0 - s.n.y;
    vec3 c = bands( h, jit ) * ( 0.78 + 0.44 * fbm3( gp / 19.0 + 5.0 ) );
    cells( gp / 24.0, 0.9 );
    float crack = wline( cEdge * 24.0, 0.45 );
    c *= 1.0 - 0.5 * crack;
    c *= 1.0 - 0.3 * smoothstep( 0.15, 0.6, slope );
    c = mix( c, vec3( 0.3, 0.29, 0.28 ) * ( 0.8 + 0.4 * vn( gp / 3.0 ) ), m.b * 0.45 );
    // Ground far above the relief (cave walls) catches more light: the walls read against the floor.
    c = mix( c, band3 * 1.6, smoothstep( relief * 0.9, relief * 3.0, h ) );
    s.alb = c;
    s.ao = mix( 0.7, 1.0, smoothstep( -relief * 0.7, relief * 0.5, h ) );
#if FLUID == F_LAVA
    float hot = 1.0 - smoothstep( 0.0, 20.0, raw - waterH );
    s.alb *= 1.0 - 0.5 * hot;
    s.emit += lavaRamp( 0.25 + 0.4 * hot ) * crack * hot * hot * waterGlow * 1.2;
    s.emit += waterCol * 0.04 * hot * waterGlow;
#endif
    if ( glowAmount > 0.0 )
    {
        // Glowing veins: threads along a slow noise, only in some places, fading in and out along their way.
        vec2 vq = gp / 150.0 + 9.0;
        vq += vec2( fbm3( vq * 2.0 ), fbm3( vq * 2.0 + 5.0 ) ) * 0.4;
        float gate = smoothstep( 0.45, 0.62, vn( gp / 380.0 + 3.0 ) + glowAmount * 0.25 ) * smoothstep( 0.3, 0.6, vn( gp / 45.0 ) );
        float pulse = 0.75 + 0.25 * sin( time * 1.5 + gp.y * 0.02 );
        s.emit += glowCol * glowThread( fbm3( vq ), 110.0 ) * gate * pulse * 0.9;
    }
    if ( paint > 0.0 )
    {
        // Caves: patches of glowing moss and lichen on the floor, the light the crystals and fungi give.
        float moss = smoothstep( 0.58, 0.8, fbm3( gp / 70.0 + 13.0 ) ) * ( 0.6 + 0.4 * vn( gp / 4.0 ) );
        cells( gp / 7.0, 0.9 );
        float speck = ( 1.0 - smoothstep( 0.05, 0.16, cF1 ) ) * step( h12( cId + 3.0 ), 0.5 );
        s.emit += mix( glowCol, glowCol2, vn( gp / 120.0 ) ) * ( moss * 0.09 + moss * speck * 0.35 ) * paint;
        s.alb = mix( s.alb, s.alb * 1.6 + glowCol * 0.05, moss * paint );
    }
}
#endif

#if STYLE == S_SAND
void land( inout Surf s, vec2 gp, float h, float raw, vec4 m )
{
    float jit = ( fbm3( gp / 70.0 ) - 0.5 ) * 0.22;
    vec3 c = bands( h, jit );
    // Ripples across the wind on the flat sand.
    vec2 wd = vec2( 0.55, 0.83 );
    float w = dot( gp, wd ) * 0.8 + fbm3( gp / 25.0 ) * 9.0;
    float flatK = 1.0 - smoothstep( 0.1, 0.35, 1.0 - s.n.y );
    s.n = normalize( s.n + vec3( wd.x, 0.0, -wd.y ) * cos( w ) * 0.16 * flatK * detail );
    c *= 0.95 + 0.05 * sin( w );
    c *= 0.86 + 0.26 * fbm3( gp / 33.0 + 2.0 );
    // Rock of the mesas and canyon walls: strata.
    float slope = 1.0 - s.n.y;
    float rockK = max( m.b, smoothstep( 0.22, 0.45, slope ) );
    float strata = 0.5 + 0.5 * sin( h * 0.42 + fbm3( gp / 40.0 ) * 4.0 );
    vec3 rock = mix( band1 * vec3( 0.8, 0.58, 0.46 ), band2 * vec3( 0.95, 0.8, 0.66 ), strata );
    rock *= 0.78 + 0.32 * vn( gp / 4.0 );
    c = mix( c, rock, rockK * 0.85 );
    // Dry river beds: pale salt in the lowest ground.
    c = mix( c, band3 * 0.95, ( 1.0 - smoothstep( -relief * 0.75, -relief * 0.55, h ) ) * 0.5 );
    // Scrub: small dark tufts.
    cells( gp / 11.0, 0.9 );
    float tuft = ( 1.0 - smoothstep( 0.07, 0.15, cF1 ) ) * step( h12( cId + 5.0 ), 0.25 ) * ( 1.0 - rockK );
    c = mix( c, vec3( 0.16, 0.15, 0.08 ), tuft * 0.75 );
    s.alb = c;
    s.ao = mix( 0.72, 1.0, smoothstep( -relief * 0.6, relief * 0.4, h ) );
}
#endif

#if STYLE == S_ISLAND
void land( inout Surf s, vec2 gp, float h, float raw, vec4 m )
{
    float above = raw - waterH;
    float jit = ( fbm3( gp / 60.0 ) - 0.5 ) * 0.25;
    s.n = bump( s.n, gp / 9.0, 0.4 * detail );
    float slope = 1.0 - s.n.y;
    s.alb = bands( h, jit ) * ( 0.85 + 0.3 * fbm3( gp / 17.0 ) );
    s.ao = mix( 0.75, 1.0, smoothstep( -relief * 0.3, relief * 0.6, h ) );
    if ( m.r > 0.05 ) canopy( s, gp, 7.0, treeCol, smoothstep( 0.05, 0.4, m.r ) );
    s.alb = mix( s.alb, band3 * 0.8 * ( 0.8 + 0.4 * vn( gp / 3.0 ) ), smoothstep( 0.3, 0.6, slope ) );
    float beach = 1.0 - smoothstep( 1.0, 4.0 + 3.0 * vn( gp / 15.0 ), above );
    s.alb = mix( s.alb, band0 * ( 0.75 + 0.25 * smoothstep( 0.0, 1.2, above ) ), beach );
    s.n = normalize( mix( s.n, vec3( 0.0, 1.0, 0.0 ), beach * 0.5 ) );
}
#endif

#if STYLE == S_ICE
void land( inout Surf s, vec2 gp, float h, float raw, vec4 m )
{
    s.n = bump( s.n, gp / 12.0, 0.3 * detail );
    float slope = 1.0 - s.n.y;
    // Wind-swept snow: streaks along the wind.
    float streak = vn( vec2( gp.x / 5.0, gp.y / 40.0 ) + fbm3( gp / 50.0 ) * 2.0 );
    vec3 snow = mix( band2, band3, 0.45 + 0.5 * fbm3( gp / 45.0 ) ) * ( 0.92 + 0.1 * streak );
    // Blue ice where the snow is blown off and on steep faces, cracked.
    float iceK = max( smoothstep( 0.5, 0.75, 1.0 - m.b + ( fbm3( gp / 30.0 ) - 0.5 ) * 0.5 ),
                      smoothstep( 0.22, 0.45, slope ) );
    vec3 ice = mix( band0, band1, 0.35 + 0.45 * vn( gp / 20.0 ) );
    cells( gp / 30.0, 0.9 );
    ice *= 1.0 - 0.4 * wline( cEdge * 30.0, 0.3 );
    vec3 c = mix( snow, ice, iceK );
    s.spec = mix( 0.06, 0.45, iceK );
    s.gloss = mix( 10.0, 60.0, iceK );
    // Crevasses: long dark cuts, in fields.
    float field = smoothstep( 0.48, 0.62, vn( gp / 340.0 + 7.0 ) );
    float cv = vn( vec2( gp.x / 90.0, gp.y / 420.0 ) + vec2( fbm3( gp / 300.0 ) * 0.6, 0.0 ) ) - 0.5;
    field *= smoothstep( 0.42, 0.55, vn( gp / 45.0 + 2.0 ) );   // broken into lengths, not endless lines
    float crev = fline( cv, 1.4 ) * field;
    float lip = fline( cv - 0.012, 0.6 ) * field;
    c = mix( c, band3, lip * 0.35 );
    c = mix( c, vec3( 0.025, 0.07, 0.13 ), crev );
    s.alb = c;
    s.ao = mix( 0.75, 1.0, smoothstep( -relief * 0.5, relief * 0.5, h ) ) * ( 1.0 - 0.5 * crev );
}
#endif

#if STYLE == S_JUNGLE
void land( inout Surf s, vec2 gp, float h, float raw, vec4 m )
{
    float jit = ( fbm3( gp / 50.0 ) - 0.5 ) * 0.25;
    s.n = bump( s.n, gp / 6.0, 0.3 * detail );
    s.alb = bands( h, jit ) * ( 0.8 + 0.4 * fbm3( gp / 21.0 ) );
    s.ao = mix( 0.7, 1.0, smoothstep( -relief * 0.5, relief * 0.5, h ) );
    float k = smoothstep( 0.12, 0.45, m.r );
    if ( k > 0.0 )
    {
        canopy( s, gp, 15.0, treeCol, k );
        // Giants rising out of the canopy here and there.
        cells( gp / 37.0 + 5.0, 0.8 );
        float giant = step( h12( cId + 23.0 ), 0.35 ) * ( 1.0 - smoothstep( 0.32, 0.4, cF1 ) ) * k;
        if ( giant > 0.0 )
        {
            float r = cF1 / 0.4;
            vec2 o = -cRel / 0.4;
            vec3 n = normalize( vec3( o.x, sqrt( max( 0.0, 1.0 - r * r ) ) * 1.1 + 0.15, -o.y ) );
            vec2 q = gp / 4.0;
            n = normalize( n + vec3( vn( q ) - vn( q + vec2( 0.3, 0.0 ) ), 0.0, vn( q + vec2( 0.0, 0.3 ) ) - vn( q ) ) * 2.5 );
            s.n = normalize( mix( s.n, n, giant ) );
            s.alb = mix( s.alb, treeCol * vec3( 1.15, 1.1, 0.8 ) * ( 0.8 + 0.4 * vn( q ) ), giant );
            s.ao = mix( s.ao, 0.8 + 0.2 * ( 1.0 - r ), giant );
        }
    }
}
#endif

#if STYLE == S_PLATING || STYLE == S_FOUNDRY
void land( inout Surf s, vec2 gp, float h, float raw, vec4 m )
{
#if STYLE == S_PLATING
    vec2 ps = vec2( 40.0, 20.0 );
#else
    vec2 ps = vec2( 80.0, 40.0 );
#endif
    vec3 c = bands( h, 0.0 );
    vec2 pq = gp / ps;
    pq.x += mod( floor( pq.y ), 2.0 ) * 0.5;
    vec2 pid = floor( pq ), pf = fract( pq );
    c *= 0.86 + 0.28 * h12( pid + 11.0 );
    vec2 ed = min( pf, 1.0 - pf ) * ps;
    c *= 1.0 - 0.5 * wline( min( ed.x, ed.y ), 0.35 );
    c *= 0.82 + 0.3 * fbm3( gp / 23.0 );
    float slope = 1.0 - s.n.y;
#if STYLE == S_FOUNDRY
    float rust = smoothstep( 0.55, 0.8, fbm3( gp / 37.0 + 4.0 ) );
    c = mix( c, c * vec3( 1.35, 0.85, 0.6 ), rust * 0.6 );
    c *= 1.0 - 0.45 * smoothstep( 0.5, 0.85, fbm3( gp / 70.0 + 9.0 ) );
#endif
    // The trenches between the big plates and the light strips in them.
    vec2 tq = abs( mod( gp, 160.0 ) - 80.0 );
    tq = 80.0 - tq;
    float tr = min( tq.x, tq.y );
#if STYLE == S_PLATING
    // Hazard stripes along the lip of some plates' sides.
    vec2 side = tq.x < tq.y ? vec2( floor( gp.x / 160.0 + 0.5 ), floor( gp.y / 160.0 ) )
                            : vec2( floor( gp.x / 160.0 ), floor( gp.y / 160.0 + 0.5 ) + 500.0 );
    float hz = step( 6.5, tr ) * step( tr, 9.5 ) * step( h12( side + 77.0 ), 0.3 );
    vec3 stripe = mix( vec3( 0.42, 0.33, 0.07 ), vec3( 0.05 ), step( 0.5, fract( ( gp.x + gp.y ) / 6.0 ) ) );
    c = mix( c, stripe, hz * 0.8 );
#endif
    float inTrench = 1.0 - smoothstep( 4.0, 5.5, tr );
    c *= 1.0 - 0.6 * inTrench;
    float strip = wline( tr, 0.65 ) * step( 0.5, m.a );
    float run = tq.x < tq.y ? gp.y : gp.x;
    float pulse = 0.6 + 0.4 * smoothstep( 0.86, 1.0, fract( run / 130.0 - time * 0.45 ) );
    s.emit += glowCol * strip * pulse * 1.5;
    // Painted rings on some plates (landing and target marks).
    if ( paint > 0.0 )
    {
        vec2 pc = floor( gp / 160.0 );
        float pr = length( mod( gp, 160.0 ) - 80.0 );
        float mark = step( h12( pc + 41.0 ), paint ) * ( wline( pr - 46.0, 1.4 ) + wline( pr - 30.0, 0.7 ) );
        c = mix( c, vec3( 0.55, 0.5, 0.36 ), clamp( mark, 0.0, 1.0 ) * 0.6 );
    }
    s.alb = c;
    s.spec = 0.2;
    s.gloss = 28.0;
    s.ao = 1.0 - 0.35 * inTrench;
}
#endif

#if STYLE == S_LATTICE
void land( inout Surf s, vec2 gp, float h, float raw, vec4 m )
{
    const float R = 38.0;
    vec2 p = gp / R;
    const vec2 sv = vec2( 1.0, 1.7320508 );
    vec4 hc = floor( vec4( p, p - vec2( 0.5, 1.0 ) ) / sv.xyxy ) + 0.5;
    vec4 hh = vec4( p - hc.xy * sv, p - ( hc.zw + 0.5 ) * sv );
    vec2 lp = dot( hh.xy, hh.xy ) < dot( hh.zw, hh.zw ) ? hh.xy : hh.zw;
    vec2 ctr = p - lp;
    vec2 al = abs( lp );
    float hd = max( dot( al, vec2( 0.5, 0.8660254 ) ), al.x );
    float edge = ( 0.5 - hd ) * R;
    float lv = h12( floor( ctr * 2.0 + 0.5 ) + 5.0 );
    vec3 c = mix( band0, band2, lv * 0.8 ) * ( 0.85 + 0.25 * vn( gp / 9.0 ) );
    // A bevelled rim round every cell, an inner ring.
    float rim = 1.0 - smoothstep( 1.2, 3.2, edge );
    vec2 outw = normalize( lp + 1e-4 );
    s.n = normalize( s.n + vec3( outw.x, 0.0, -outw.y ) * rim * 0.9 );
    c = mix( c, band3 * 0.85, rim * 0.45 );
    c *= 1.0 - 0.4 * wline( edge - R * 0.17, 0.35 );
    // Some cells are pits, dark and deep.
    float pit = step( 0.84, lv ) * smoothstep( 2.0, 5.0, edge );
    c *= 1.0 - 0.7 * pit;
    // Circuits along the edges: is this edge lit, and a pulse running along it.
    float ang = atan( lp.y, lp.x );
    float na = floor( ( ang + 0.5235988 ) / 1.0471976 ) * 1.0471976;
    vec2 em = ctr + 0.5 * vec2( cos( na ), sin( na ) );
    vec2 eh = h22( floor( em * 2.0 + 0.5 ) + 31.0 );
    float lit = step( eh.x, glowAmount * 0.7 );
    float wire = wline( edge, 0.55 ) * lit;
    float along = dot( lp, vec2( -sin( na ), cos( na ) ) );
    float pulse = smoothstep( 0.75, 1.0, sin( along * 12.0 - time * 2.6 + eh.y * 40.0 ) );
    vec3 wc = eh.y < 0.62 ? glowCol2 : glowCol;
    s.emit += wc * wire * ( 0.55 + 0.9 * pulse );
    // Fine circuitry on the cells' faces.
    vec2 fq = gp / 6.0;
    float trace = wline( abs( fract( fq.x + floor( fq.y ) * 0.37 ) - 0.5 ) * 6.0 - 2.4, 0.25 ) * step( 0.6, h12( floor( fq ) + 3.0 ) );
    c = mix( c, band3, trace * 0.25 * ( 1.0 - pit ) );
    s.alb = c;
    s.spec = 0.3;
    s.gloss = 36.0;
    s.ao = 1.0 - 0.3 * rim;
}
#endif

#if STYLE == S_CORONA
void land( inout Surf s, vec2 gp, float h, float raw, vec4 m )
{
    // The plasma is kept dark under the field: deep red with slowly boiling granules, a few thin filaments in the
    // active regions; never large bright areas (the bullets must read on it).
    float t = time;
    cells( gp / 34.0 + vec2( 0.0, t * 0.03 ), 0.95 );
    float gran = smoothstep( 0.03, 0.35, cEdge ) * ( 0.6 + 0.4 * sin( t * 0.5 + h12( cId ) * 6.28 ) );
    float act = fbm3( gp / 340.0 + vec2( t * 0.008, 0.0 ) );
    float spot = smoothstep( 0.2, 0.34, fbm3( gp / 240.0 + 3.0 ) );
    vec2 w = vec2( fbm3( gp / 170.0 + t * 0.02 ), fbm3( gp / 170.0 + 7.0 - t * 0.015 ) ) - 0.5;
    float fil = glowThread( fbm3( gp / 120.0 + w * 3.0 ), 70.0 ) * smoothstep( 0.45, 0.7, act );
    // Plumes: soft brighter clouds of plasma drifting over the granules.
    float plume = smoothstep( 0.55, 0.85, fbm3( gp / 200.0 + w * 2.0 + vec2( 0.0, t * 0.03 ) ) );
    vec3 e = band0 + band1 * gran * ( 0.3 + 0.5 * act );
    e += band2 * ( smoothstep( 0.6, 0.85, act ) * 0.2 + plume * 0.18 );
    e *= mix( 0.3, 1.0, spot );
    e += band3 * fil * 0.55 * spot;
    s.emit = e * glowAmount;
    s.alb = vec3( 0.0 );
    s.spec = 0.0;
}
#endif

#if STYLE == S_CITY
void land( inout Surf s, vec2 gp, float h, float raw, vec4 m )
{
    s.n = bump( s.n, gp / 10.0, 0.12 );
    vec3 c = bands( h, ( fbm3( gp / 60.0 ) - 0.5 ) * 0.2 ) * ( 0.85 + 0.3 * fbm3( gp / 13.0 ) );
    s.alb = c;
    if ( m.r > 0.05 ) canopy( s, gp, 6.0, treeCol, smoothstep( 0.05, 0.5, m.r ) );
    float built = smoothstep( 0.12, 0.25, m.g );
    vec2 bq = mod( gp, 60.0 );
    float sx = step( bq.x, 10.0 ), sz = step( bq.y, 10.0 );
    float street = max( sx, sz ) * built;
    float along = sx > 0.5 ? gp.y : gp.x;
    float across = sx > 0.5 ? bq.x : bq.y;
    vec3 asphalt = vec3( 0.085, 0.09, 0.1 ) * ( 0.9 + 0.2 * vn( gp / 2.0 ) );
    asphalt = mix( asphalt, vec3( 0.45, 0.43, 0.36 ),
                   wline( across - 5.0, 0.2 ) * step( 0.5, fract( along / 6.0 ) ) * 0.5 );
    asphalt = mix( asphalt, vec3( 0.22, 0.22, 0.24 ), wline( across - 0.5, 0.5 ) + wline( across - 9.5, 0.5 ) );
    s.alb = mix( s.alb, asphalt, street );
    s.n = normalize( mix( s.n, vec3( 0.0, 1.0, 0.0 ), street ) );
    // Street lights along the kerbs, and traffic.
    float l1 = 1.0 - smoothstep( 0.3, 1.1, length( vec2( min( abs( bq.x - 1.0 ), abs( bq.x - 9.0 ) ),
                                                         abs( mod( gp.y, 14.0 ) - 7.0 ) ) ) );
    float l2 = 1.0 - smoothstep( 0.3, 1.1, length( vec2( abs( mod( gp.x, 14.0 ) - 7.0 ),
                                                         min( abs( bq.y - 1.0 ), abs( bq.y - 9.0 ) ) ) ) );
    float lamps = max( l1 * sx, l2 * sz ) * built;
    float lane = across < 5.0 ? 1.0 : -1.0;
    float row = floor( ( sx > 0.5 ? gp.x : gp.y ) / 60.0 );
    float cpos = fract( along / 31.0 + time * 0.22 * lane + h12( vec2( row, lane ) ) );
    float car = ( 1.0 - smoothstep( 0.4, 0.9, abs( across - 5.0 - 2.0 * lane ) ) )
              * ( 1.0 - smoothstep( 0.0, 0.025, abs( cpos - 0.5 ) ) );
    s.emit += vec3( 1.0, 0.8, 0.5 ) * lamps * windows * 0.8 + vec3( 0.9, 0.85, 0.7 ) * car * street * windows * 0.7;
}
#endif

#if STYLE == S_HIVE
void land( inout Surf s, vec2 gp, float h, float raw, vec4 m )
{
    float jit = ( fbm3( gp / 50.0 ) - 0.5 ) * 0.25;
    vec3 c = bands( h, jit );
    // A membrane of domed cells with dark folds between them, wet.
    cells( gp / 18.0, 0.85 );
    float fold = 1.0 - smoothstep( 0.0, 0.14, cEdge );
    vec2 o = -cRel;
    s.n = normalize( s.n + vec3( o.x, 0.0, -o.y ) * 0.9 * ( 1.0 - fold ) );
    c *= mix( 1.0, 0.4, fold );
    c *= 0.82 + 0.3 * h12( cId );
    s.spec = 0.3;
    s.gloss = 40.0;
    s.alb = c;
    s.ao = mix( 0.7, 1.0, smoothstep( -relief * 0.6, relief * 0.4, h ) ) * ( 1.0 - 0.35 * fold );
    if ( m.r > 0.05 ) canopy( s, gp, 9.0, treeCol, smoothstep( 0.1, 0.5, m.r ) );
    // Veins: winding threads of light in some places, beats running along them.
    vec2 wq = gp / 110.0;
    wq += vec2( fbm3( wq * 1.7 + 4.0 ), fbm3( wq * 1.7 + 9.0 ) ) * 1.2;
    float vgate = smoothstep( 0.5, 0.68, vn( gp / 300.0 + 6.0 ) + glowAmount * 0.3 ) * smoothstep( 0.25, 0.55, vn( gp / 50.0 + 2.0 ) );
    float beat = 0.5 + 0.5 * sin( time * 1.8 - ( gp.x + gp.y ) * 0.03 + fbm3( wq * 3.0 ) * 6.0 );
    s.emit += glowCol * glowThread( fbm3( wq ), 90.0 ) * vgate * ( 0.3 + 0.7 * beat ) * 0.9;
}
#endif
)";

const char *GroundFluidFs = R"(
#if FLUID == F_WATER || FLUID == F_BIO
void fluid( inout Surf s, vec2 gp, float depth, float h, vec4 m )
{
    vec3 bed = s.alb;
    s.emit *= 1.0 - smoothstep( 0.0, 0.5, depth );   // street lights and veins do not shine through water
    float w = waves;
    // Small waves from two noises drifting apart, and a long swell along the wind when the sea runs high.
    vec2 q1 = gp / 42.0 + vec2( time * 0.04, time * 0.09 );
    vec2 q2 = gp / 15.0 + vec2( -time * 0.12, time * 0.05 );
    float e = 0.25;
    float a0 = vn( q1 ) + 0.5 * vn( q2 );
    float ax = vn( q1 + vec2( e, 0.0 ) ) + 0.5 * vn( q2 + vec2( e, 0.0 ) );
    float az = vn( q1 + vec2( 0.0, e ) ) + 0.5 * vn( q2 + vec2( 0.0, e ) );
    vec2 wd = vec2( 0.8, 0.6 );
    float sw = dot( gp, wd ) / 21.0 - time * 1.1 + vn( gp / 60.0 ) * 4.0;
    vec2 grad = vec2( ax - a0, az - a0 ) / e * w * 0.6 + wd * cos( sw ) * 0.12 * smoothstep( 0.6, 1.4, w );
    s.n = normalize( vec3( -grad.x, 1.0, grad.y ) );
    float dk = smoothstep( 0.0, 28.0, depth );
    vec3 c = mix( waterCol * 1.5 + vec3( 0.0, 0.035, 0.03 ), waterCol * 0.55, dk );
    c = mix( bed * 0.5, c, 0.7 + 0.3 * smoothstep( 0.0, 3.0, depth ) );
    s.spec = 0.35;
    s.gloss = 220.0;
    s.ao = 1.0;
    // Foam along the shore, in bands that come and go; calm water hardly foams.
    float calm = smoothstep( 0.15, 0.6, w );
    float fn = vn( gp / 9.0 + time * 0.2 );
    float shore = 1.0 - smoothstep( 0.0, ( 1.0 + fn * 2.0 ) * ( 0.5 + w ), depth );
    float band = smoothstep( 0.75, 1.0, sin( depth * 1.3 - time * 1.7 + fn * 3.0 ) ) * ( 1.0 - smoothstep( 1.5, 5.0, depth ) );
    float foam = max( shore, band * 0.3 ) * ( 0.5 + 0.5 * fn ) * mix( 0.3, 1.0, calm );
    // White caps on the swell's crests when the sea runs high.
    float capN = vn( gp / 13.0 + vec2( time * 0.3, -time * 0.25 ) );
    foam = max( foam,
                smoothstep( 0.82, 1.0, sin( sw ) ) * smoothstep( 0.45, 0.75, capN ) * smoothstep( 0.8, 1.5, w ) * 0.7 );
#if STYLE == S_CITY
    // Quays: a crisp stone edge round the city's water.
    c = mix( c, vec3( 0.26, 0.26, 0.28 ), 1.0 - smoothstep( 0.2, 0.6, depth ) );
    foam = 0.0;
#endif
#if STYLE == S_ICE
    // Floes and thin new ice in the leads.
    cells( gp / 10.0 + vec2( vn( gp / 8.0 ) * 0.6, 0.0 ), 0.9 );
    float floe = ( 1.0 - smoothstep( 0.3 - 0.2 * vn( gp / 3.0 ), 0.42, cF1 ) ) * step( h12( cId + 9.0 ), 0.4 )
               * smoothstep( 1.0, 3.0, depth );
    c = mix( c, vec3( 0.1, 0.13, 0.17 ), smoothstep( 0.0, 3.0, depth ) * 0.5 * vn( gp / 7.0 ) );
    c = mix( c, band2 * 0.7, floe );
    foam *= 1.0 - floe;
#endif
    c = mix( c, vec3( 0.72, 0.77, 0.8 ), foam );
    s.spec *= 1.0 - foam;
    s.alb = c;
#if FLUID == F_BIO
    // Glowing fluid: dim in the middle, a brighter rim at the shore, rings running out from it.
    float rim = 1.0 - smoothstep( 0.0, 3.0, depth );
    float ring = smoothstep( 0.8, 1.0, sin( depth * 0.9 - time * 1.2 ) ) * ( 1.0 - smoothstep( 0.0, 12.0, depth ) );
    float swirl = vn( gp / 25.0 + vec2( time * 0.1, -time * 0.07 ) );
    s.emit += waterCol * ( 0.06 + 0.1 * swirl + rim * 0.3 + ring * 0.15 ) * waterGlow
            * ( 0.85 + 0.15 * sin( time * 1.3 + gp.x * 0.01 ) );
    s.alb = waterCol * 0.05 + vec3( 0.03, 0.02, 0.03 ) + vec3( 0.25 ) * foam * 0.3;
#endif
}
#endif

#if FLUID == F_LAVA
void fluid( inout Surf s, vec2 gp, float depth, float h, vec4 m )
{
    s.spec = 0.08;
    s.gloss = 20.0;
    s.ao = 1.0;
#if STYLE == S_FOUNDRY
    // Molten metal running down a channel: streaks flowing along it, slag drifting on it, cooler at the edges.
    vec2 tq = 80.0 - abs( mod( gp, 160.0 ) - 80.0 );
    bool alongG = tq.x < tq.y;
    float along = alongG ? gp.y : gp.x;
    float across = alongG ? tq.x : tq.y;
    float flow = along * 0.07 - time * 1.4;
    float streak = vn( vec2( flow, across * 0.5 ) ) * 0.6 + vn( vec2( flow * 2.3, across * 1.2 + 7.0 ) ) * 0.4;
    float slag = smoothstep( 0.6, 0.74, vn( vec2( along * 0.045 - time * 0.6, across * 0.3 + 3.0 ) ) );
    float heat = smoothstep( 0.0, 6.0, depth );
    s.alb = vec3( 0.05, 0.04, 0.035 );
    s.n = vec3( 0.0, 1.0, 0.0 );
    s.emit = lavaRamp( clamp( 0.25 + 0.4 * heat + 0.3 * streak, 0.0, 1.0 ) ) * ( 0.3 + 0.5 * heat ) * ( 1.0 - 0.8 * slag ) * waterGlow;
#else
    // A crust of dark plates drifting on the melt, thin glowing seams between them, hotter away from the shore.
    vec2 q = gp + vec2( 0.0, time * 2.2 );
    q += ( vn4( q / 80.0 ).xy - 0.5 ) * 26.0;
    cells( q / 22.0, 0.85 );
    float heat = smoothstep( 3.0, 24.0, depth ) * ( 0.45 + 0.55 * vn( gp / 55.0 + time * 0.04 ) );
    float seam = wline( cEdge * 22.0, 0.35 + 1.1 * heat ) * ( 0.2 + 0.8 * smoothstep( 0.35, 0.65, vn( q / 40.0 + 3.0 ) ) );
    float pulse = 0.8 + 0.2 * sin( time * 1.3 + h12( cId ) * 6.28 );
    s.alb = vec3( 0.035, 0.026, 0.022 ) * ( 0.7 + 0.6 * h12( cId + 7.0 ) );
    s.n = normalize( vec3( cRel.x * 0.3, 1.0, -cRel.y * 0.3 ) );
    float glow = seam * ( 0.25 + 0.6 * heat ) * pulse + heat * heat * 0.05;
    s.emit = lavaRamp( clamp( 0.15 + glow * 0.7, 0.0, 1.0 ) ) * glow * waterGlow;
#endif
}
#endif
)";

const char *GroundMainFs = R"(
void building( inout Surf s )
{
    vec3 base = fragColor.rgb;
    s.spec = 0.18;
    s.gloss = 24.0;
    if ( s.n.y > 0.6 )
    {
        // The roof: a parapet round its edge, vents, grime.
        vec2 e = fragTan.xy - abs( fragUv );
        float edge = min( e.x, e.y );
        s.alb = base * ( 0.8 + 0.3 * vn( fragUv * 0.45 + fragMat.y * 50.0 ) );
        s.alb = mix( s.alb, base * 1.3, 1.0 - smoothstep( 0.6, 1.3, edge ) );
        s.alb *= 1.0 - 0.3 * ( 1.0 - smoothstep( 1.3, 2.2, edge ) ) * smoothstep( 0.6, 1.3, edge );
        // At night a few skylights are lit.
        vec2 sq = fragUv / vec2( 5.0, 4.0 );
        vec2 sf = fract( sq );
        float skyl = step( 2.5, edge ) * step( 0.25, sf.x ) * step( sf.x, 0.75 ) * step( 0.3, sf.y ) * step( sf.y, 0.7 );
        skyl *= step( h12( floor( sq ) + fragMat.y * 71.0 ), windows * 0.12 ) * step( 0.5, fract( fragMat.y * 7.0 ) );
        s.emit += vec3( 1.0, 0.8, 0.55 ) * skyl * 0.6;
    }
    else
    {
        // The walls: floors of windows, some lit at night.
        vec2 cell = vec2( 3.2, 4.0 );
        vec2 wi = floor( fragUv / cell ), wf = fract( fragUv / cell );
        float win = step( 0.2, wf.x ) * step( wf.x, 0.78 ) * step( 0.3, wf.y ) * step( wf.y, 0.82 ) * step( 1.0, wi.y );
        float lit = step( h12( wi + fragMat.y * 97.0 ), windows * 0.7 );
        vec3 wc = mix( vec3( 1.0, 0.74, 0.42 ), vec3( 0.62, 0.8, 1.0 ), step( 0.72, h12( wi * 1.7 + 3.0 ) ) );
        s.alb = mix( base, vec3( 0.05, 0.06, 0.08 ), win * 0.75 );
        s.emit += wc * win * lit * 0.75;
        s.spec = mix( s.spec, 0.7, win );
        s.gloss = mix( s.gloss, 80.0, win );
    }
}

void hall( inout Surf s )
{
    // A factory hall: a ribbed roof with glowing skylights along its length; lit gates in the walls.
    vec3 base = fragColor.rgb;
    s.spec = 0.25;
    s.gloss = 30.0;
    if ( s.n.y > 0.6 )
    {
        bool alongX = fragTan.x > fragTan.y;
        float across = alongX ? fragUv.y : fragUv.x;
        float along = alongX ? fragUv.x : fragUv.y;
        float halfAcross = alongX ? fragTan.y : fragTan.x;
        float halfAlong = alongX ? fragTan.x : fragTan.y;
        float rib = 0.5 + 0.5 * sin( across * 2.2 );
        s.alb = base * ( 0.75 + 0.3 * rib ) * ( 0.85 + 0.3 * vn( fragUv * 0.3 + fragMat.y * 30.0 ) );
        float sky = step( abs( mod( across + halfAcross, 14.0 ) - 7.0 ), 1.3 ) * step( abs( along ), halfAlong - 3.0 )
                  * step( abs( across ), halfAcross - 3.0 );
        float flick = 0.8 + 0.2 * sin( time * 3.0 + fragMat.y * 40.0 + along * 0.1 );
        s.emit += glowCol * sky * flick * 0.55;
    }
    else
    {
        float gate = step( abs( mod( fragUv.x, 30.0 ) - 15.0 ), 4.0 ) * step( fragUv.y, 9.0 ) * step( 0.4, fragMat.y );
        s.alb = mix( base * ( 0.8 + 0.2 * step( 0.5, fract( fragUv.x / 2.0 ) ) ), vec3( 0.04 ), gate );
        s.emit += glowCol * gate * 0.45;
    }
}

void lamp( inout Surf s )
{
    // A light; fragUv: blinks a second (0 steady), its phase.
    float b = 1.0;
    if ( fragUv.x > 0.0 ) b = step( fract( time * fragUv.x + fragUv.y ), 0.3 );
    else if ( fragUv.x < 0.0 ) b = 0.6 + 0.4 * sin( time * -fragUv.x + fragUv.y * 6.28 );
    s.alb = fragColor.rgb * 0.3;
    s.emit = fragColor.rgb * ( fragColor.a * 2.2 ) * b;
}

void hot( inout Surf s )
{
    float f = 0.8 + 0.2 * sin( time * 4.0 + fragMat.y * 30.0 ) * sin( time * 2.3 + fragMat.y * 11.0 );
    s.alb = vec3( 0.05 );
    s.emit = lavaRamp( 0.55 + 0.25 * f ) * f * 1.1;
}

void main()
{
    Surf s;
    s.n = normalize( fragNormal );
    s.alb = fragColor.rgb;
    s.emit = vec3( 0.0 );
    s.spec = 0.04;
    s.gloss = 16.0;
    s.ao = 1.0;
    g_upp = max( fwidth( fragGround.x ), 1e-4 );
    int mat = int( fragMat.x + 0.5 );
    float h = fragPos.y + GROUND_DEPTH;
    if ( mat == 0 )
    {
        float depth = waterH - fragUv.x;
#if FLUID != F_NONE
        if ( depth < 14.0 || FLUID != F_LAVA ) land( s, fragGround, h, fragUv.x, fragColor );
        if ( depth > 0.0 ) fluid( s, fragGround, depth, h, fragColor );
#else
        land( s, fragGround, h, fragUv.x, fragColor );
#endif
    }
    else if ( mat == 1 ) building( s );
    else if ( mat == 3 ) lamp( s );
    else if ( mat == 6 ) hall( s );
    else if ( mat == 7 ) hot( s );
    else
    {
        s.spec = mat == 2 ? 0.35 : 0.06;
        s.gloss = mat == 2 ? 40.0 : 14.0;
        s.emit = fragColor.rgb * fragColor.a * 2.0;
    }
    // Light: the sun (dimmed under what flies and under the clouds), the sky, the short lights of the battle.
    vec3 l = -sunDir;
    float ndl = max( dot( s.n, l ), 0.0 );
    float sun = 1.0;
    if ( shadowOn > 0.5 )
    {
        vec2 sq = fragPos.xz + sunDir.xz * ( ( shadowY - fragPos.y ) / sunDir.y );
        vec2 uv = ( sq - shadowRect.xy ) / shadowRect.zw;
        vec2 t = 1.5 / vec2( textureSize( texture1, 0 ) );
        float sh = texture( texture1, uv ).r * 0.4
                 + ( texture( texture1, uv + vec2( t.x, t.y ) ).r + texture( texture1, uv - vec2( t.x, t.y ) ).r
                     + texture( texture1, uv + vec2( t.x, -t.y ) ).r + texture( texture1, uv + vec2( -t.x, t.y ) ).r ) * 0.15;
        sh *= step( 0.0, uv.x ) * step( uv.x, 1.0 ) * step( 0.0, uv.y ) * step( uv.y, 1.0 );
        sun *= 1.0 - 0.72 * sh;
    }
    if ( cloudParam.w > 0.0 )
    {
        vec2 cq = fragPos.xz + sunDir.xz * ( ( cloudY - fragPos.y ) / sunDir.y );
        sun *= 1.0 - texture( texture2, cq / cloudParam.x + cloudParam.yz ).a * cloudParam.w;
    }
    vec3 amb = mix( ambDown, ambUp, s.n.y * 0.5 + 0.5 ) * s.ao;
    vec3 lit = amb + sunColor * ndl * sun;
    for ( int i = 0; i < lightCount; ++i )
    {
        vec3 L = lightPos[i].xyz - fragPos;
        float d = length( L );
        float att = clamp( 1.0 - d / lightPos[i].w, 0.0, 1.0 );
        att *= att;
        float nl = max( dot( s.n, L / max( d, 0.001 ) ), 0.0 ) * 0.85 + 0.15;
        lit += lightCol[i].rgb * lightCol[i].a * nl * att;
    }
    vec3 v = normalize( camPos - fragPos );
    vec3 hv = normalize( l + v );
    vec3 spec = sunColor * pow( max( dot( s.n, hv ), 0.0 ), s.gloss ) * s.spec * sun * smoothstep( 0.0, 0.25, ndl );
    float fres = pow( 1.0 - max( dot( s.n, v ), 0.0 ), 4.0 );
    vec3 col = ( s.alb * lit + spec + ambUp * fres * s.spec * 0.4 ) * groundLight + s.emit;
    // Mist lying in the low ground.
    if ( mist.x > 0.0 )
    {
        float mk = ( 1.0 - smoothstep( mist.y - 30.0, mist.y + 12.0, h ) )
                 * ( 0.5 + 0.5 * vn( fragGround / 70.0 + vec2( time * 0.04, time * 0.015 ) ) );
        mk *= mat == 0 && waterH - fragUv.x > 0.0 ? 0.4 : 1.0;   // thinner over water: rivers stay dark
        col = mix( col, mistCol * groundLight * 1.3, clamp( mk * mist.x, 0.0, 1.0 ) );
    }
    float depthF = max( 0.0, -fragPos.y - 40.0 );
    col = mix( col, fogColor.rgb, 1.0 - exp( -depthF * fogColor.a ) );
    finalColor = vec4( col, 1.0 );
}
)";

// The sky out in space: a nebula, four layers of stars, a far planet; all drawn on one quad far below.
const char *SkyVs = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec3 fragPos;
out vec2 fragTexCoord;
out vec4 fragColor;
void main()
{
    fragPos = vertexPosition;
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp * vec4( vertexPosition, 1.0 );
}
)";

const char *SkyFs = R"(
in vec3 fragPos;
uniform vec3 camPos;
uniform float scroll;
uniform float time;
uniform vec3 skyCol;
uniform vec3 nebA;
uniform vec3 nebB;
uniform float nebAmt;
uniform float starAmt;
uniform vec4 planet;
uniform vec3 planetCol;
uniform vec3 sunDir;
out vec4 finalColor;

// This pixel on a layer d below the field, in its own coordinates (field units on the screen, g up the screen).
vec2 layer( float d )
{
    vec2 w = ( fragPos.xz - camPos.xz ) / ( camPos.y - fragPos.y ) * camPos.y;
    return vec2( w.x, scroll * camPos.y / ( camPos.y + d ) - w.y );
}

vec3 starLayer( vec2 p, float cell, float dens, float size, float seed )
{
    vec2 c = floor( p / cell );
    vec4 h = vec4( h22( c + seed * 131.0 ), h22( c + seed * 71.0 + 17.0 ) );
    if ( h.z > dens ) return vec3( 0.0 );
    vec2 sp = ( c + 0.2 + 0.6 * h.xy ) * cell;
    float px = max( fwidth( p.x ), 1e-3 );
    float b = h.w * h.w;
    float r = size * ( 0.45 + 1.1 * b );
    float rr = max( r, px * 0.8 );
    float d = length( p - sp ) / rr;
    float k = exp( -d * d * 2.2 ) * ( r / rr ) * ( r / rr );
    float tw = 1.0 + 0.45 * sin( time * ( 1.3 + 2.7 * h.x ) + h.y * 40.0 ) * step( 0.6, h.w );
    vec3 col = mix( vec3( 1.0, 0.78, 0.6 ), vec3( 0.66, 0.8, 1.0 ), h.y );
    col = mix( col, vec3( 1.0 ), 0.4 );
    return col * k * ( 0.22 + 1.5 * b ) * tw;
}

void main()
{
    vec3 col = skyCol;
    // The nebula: a warped noise in two colours, dust lanes through it, bright cores.
    vec2 nq = layer( 2600.0 ) / 560.0;
    vec2 w = vec2( fbm3( nq * 0.9 + 3.1 ), fbm3( nq * 0.9 + 8.7 ) ) - 0.5;
    float n = fbm3( nq + w * 1.8 );
    float n2 = fbm3( nq * 1.9 - w * 1.2 + 5.0 );
    float dens = smoothstep( 0.36, 0.78, n );
    vec3 neb = mix( nebA, nebB, smoothstep( 0.3, 0.7, n2 ) );
    float dust = smoothstep( 0.5, 0.68, fbm3( nq * 2.7 + w * 2.5 + 11.0 ) );
    col += ( neb * dens * ( 1.0 - 0.7 * dust ) + neb * pow( dens, 4.0 ) * 0.3 ) * nebAmt;
    // A thin far haze of light along a band.
    float band = smoothstep( 0.35, 0.75, fbm3( nq * 0.5 + 21.0 ) );
    col += mix( nebA, nebB, 0.5 ) * band * 0.06 * nebAmt;
    // Stars: near layers move faster; the dust hides some.
    float dim = 1.0 - 0.75 * dust * dens;
    vec3 st = starLayer( layer( 9000.0 ), 9.0, 0.35 + 0.4 * band, 0.42, 1.0 ) * 0.55;
    st += starLayer( layer( 4200.0 ), 17.0, 0.6, 0.55, 2.0 ) * 0.8;
    st += starLayer( layer( 1800.0 ), 31.0, 0.55, 0.7, 3.0 );
    st += starLayer( layer( 600.0 ), 58.0, 0.45, 0.9, 4.0 ) * 1.1;
    col += st * starAmt * dim;
    // A planet very far below, lit from the side.
    if ( planet.x > 0.0 )
    {
        vec2 pp = layer( 60000.0 );
        vec2 d = ( pp - planet.yz ) / planet.x;
        float r = length( d );
        float px = fwidth( pp.x ) / planet.x;
        float disc = 1.0 - smoothstep( 1.0 - px, 1.0 + px, r );
        vec3 L = normalize( -sunDir );   // (for the planet: its own light, from the field's side)
        vec3 n = vec3( d.x, sqrt( max( 0.0, 1.0 - r * r ) ), -d.y );
        float ndl = dot( n, L );
        float lat = d.y * 0.94 + d.x * 0.34;
        float bandsP = fbm3( vec2( lat * 6.5, d.x * 0.25 + time * 0.002 ) + 3.0 );
        vec3 surf = planetCol * ( 0.5 + 0.9 * bandsP );
        vec3 pc = surf * ( 0.015 + 0.62 * max( ndl, 0.0 ) );
        pc += planetCol * pow( 1.0 - n.y, 2.5 ) * smoothstep( -0.2, 0.6, ndl ) * 0.45;
        col = mix( col * ( 1.0 - disc ), pc, disc );
        float side = dot( normalize( vec3( d.x, 0.0, -d.y ) + 1e-4 ), L );
        float halo = exp( -max( r - 1.0, 0.0 ) * 16.0 ) * ( 1.0 - disc ) * smoothstep( -0.3, 0.7, side );
        col += planetCol * halo * 0.3;
        if ( planet.w > 0.0 )
        {
            float ca = 0.94, sa = 0.34;
            vec2 e = vec2( d.x * ca + d.y * sa, ( -d.x * sa + d.y * ca ) / 0.24 );
            float re = length( e );
            float ring = smoothstep( 1.35, 1.42, re ) * ( 1.0 - smoothstep( 2.05, 2.15, re ) );
            ring *= 0.55 + 0.45 * vn( vec2( re * 30.0, 0.5 ) );
            ring *= 1.0 - 0.7 * smoothstep( 1.68, 1.72, re ) * ( 1.0 - smoothstep( 1.76, 1.8, re ) );
            if ( e.y > 0.0 ) ring *= 1.0 - disc;
            col = mix( col, planetCol * 0.5 * ( 0.35 + 0.65 * smoothstep( -0.5, 0.5, side ) ), ring * 0.5 );
        }
    }
    finalColor = vec4( col, 1.0 );
}
)";

// Clouds: a noise picture lit from the sun's side (denser towards the sun is darker), lightning inside.
const char *CloudFs = R"(#version 330
in vec3 fragPos;
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 sunUv;
uniform vec3 lightCol;
uniform vec3 shadeCol;
uniform vec4 flash;
out vec4 finalColor;
void main()
{
    float a = texture( texture0, fragTexCoord ).a;
    float a2 = texture( texture0, fragTexCoord + sunUv ).a;
    float lit = clamp( 0.7 - ( a2 - a ) * 3.0 + ( 1.0 - a ) * 0.25, 0.0, 1.0 );
    vec3 c = fragColor.rgb * mix( shadeCol, lightCol, lit );
    float fd = length( fragPos.xz - flash.xy ) / flash.z;
    c += vec3( 0.7, 0.78, 1.0 ) * flash.w * exp( -fd * fd * 2.0 ) * ( 0.3 + a );
    float alpha = smoothstep( 0.05, 0.85, a ) * fragColor.a;
    finalColor = vec4( c, alpha );
}
)";

enum GroundLocE : int
{
    G_TIME, G_SCROLLG, G_SUNDIR, G_SUNCOL, G_AMBUP, G_AMBDOWN, G_CAMPOS, G_LIGHTCOUNT, G_LIGHTPOS, G_LIGHTCOL, G_FOG,
    G_GROUNDLIGHT, G_RELIEF, G_WATERH, G_BAND0, G_BAND1, G_BAND2, G_BAND3, G_WATERCOL, G_WATERGLOW, G_GLOWCOL,
    G_GLOWCOL2, G_GLOWAMOUNT, G_TREECOL, G_SHADOWRECT, G_SHADOWY, G_SHADOWON, G_CLOUDPARAM, G_CLOUDY, G_WINDOWS,
    G_WAVES, G_DETAIL, G_PAINT, G_MISTCOL, G_MIST, G_COUNT
};
const char *GroundLocNames[G_COUNT] = { "time", "scrollG", "sunDir", "sunColor", "ambUp", "ambDown", "camPos",
                                        "lightCount", "lightPos", "lightCol", "fogColor", "groundLight", "relief",
                                        "waterH", "band0", "band1", "band2", "band3", "waterCol", "waterGlow",
                                        "glowCol", "glowCol2", "glowAmount", "treeCol", "shadowRect", "shadowY",
                                        "shadowOn", "cloudParam", "cloudY", "windows", "waves", "detail", "paint",
                                        "mistCol", "mist" };

enum SkyLocE : int
{
    K_CAMPOS, K_SCROLL, K_TIME, K_SKY, K_NEBA, K_NEBB, K_NEBAMT, K_STARAMT, K_PLANET, K_PLANETCOL, K_SUNDIR, K_COUNT
};
const char *SkyLocNames[K_COUNT] = { "camPos", "scroll", "time", "skyCol", "nebA", "nebB", "nebAmt", "starAmt",
                                     "planet", "planetCol", "sunDir" };

enum CloudLocE : int
{
    C_SUNUV, C_LIGHT, C_SHADE, C_FLASH, C_COUNT
};
const char *CloudLocNames[C_COUNT] = { "sunUv", "lightCol", "shadeCol", "flash" };

Vector3 v3( Color c, float k = 1.0f ) { return { c.r / 255.0f * k, c.g / 255.0f * k, c.b / 255.0f * k }; }

/** @brief The mesh of one part of a chunk (on the GPU; the CPU copies are let go). */
Mesh toMesh( Terrain::Geometry::Part &p )
{
    Mesh m = {};
    m.vertexCount = (int)( p.m_pos.size() / 3 );
    m.triangleCount = (int)( p.m_idx.size() / 3 );
    if ( m.vertexCount == 0 || m.triangleCount == 0 ) return Mesh{};
    auto copy = []( const auto &v ) {
        void *d = MemAlloc( (unsigned)( v.size() * sizeof( v[0] ) ) );
        memcpy( d, v.data(), v.size() * sizeof( v[0] ) );
        return d;
    };
    m.vertices = (float *)copy( p.m_pos );
    m.normals = (float *)copy( p.m_nrm );
    m.colors = (unsigned char *)copy( p.m_col );
    m.texcoords = (float *)copy( p.m_uv );
    m.texcoords2 = (float *)copy( p.m_uv2 );
    m.tangents = (float *)copy( p.m_tan );
    m.indices = (unsigned short *)copy( p.m_idx );
    UploadMesh( &m, false );
    // The GPU has it; the CPU copies would only take memory. The indices stay: DrawMesh draws indexed only when
    // it sees them.
    MemFree( m.vertices );
    MemFree( m.normals );
    MemFree( m.colors );
    MemFree( m.texcoords );
    MemFree( m.texcoords2 );
    MemFree( m.tangents );
    m.vertices = m.normals = m.texcoords = m.texcoords2 = m.tangents = nullptr;
    m.colors = nullptr;
    return m;
}

}   // namespace

// ---------------------------------------------------------------------------------------------------------------

void Terrain::setup( const Catalog &cat, const std::string &theme, uint32_t seed, float halfW )
{
    unload();
    m_name = theme;
    m_seed = seed;
    m_viewHalfW = halfW;
    m_halfW = halfW / GroundFactor + 120.0f;
    m_look = Look();
    // Every value back to its default: a theme names only what differs.
    m_relief = 60.0f;
    m_scale = 0.004f;
    m_water = -1e9f;
    m_waterColor = { 30, 70, 110, 255 };
    m_waterGlow = 0.0f;
    m_low = { 70, 60, 50, 255 };
    m_mid = { 110, 96, 70, 255 };
    m_high = { 150, 140, 120, 255 };
    m_peak = { 220, 220, 225, 255 };
    m_glow = { 255, 120, 40, 255 };
    m_glow2 = { 64, 220, 255, 255 };
    m_glowAmount = 0.0f;
    m_city = m_trees = m_craters = m_plates = m_rocks = m_ridge = 0.0f;
    m_rivers = m_dunes = m_mesas = m_channels = m_paint = m_leads = 0.0f;
    m_riverWidth = 1.0f;
    m_riverCanyon = 0.0f;
    m_walls = 0.0f;
    m_wallWidth = 820.0f;
    m_crystals = 0.0f;
    m_crystalA = { 64, 200, 224, 255 };
    m_crystalB = { 112, 255, 144, 255 };
    m_waves = 0.5f;
    m_windows = 0.0f;
    m_stars = 1.0f;
    m_lightning = 0.0f;
    m_detail = 1.0f;
    m_propColor = { 120, 120, 130, 255 };
    m_treeColor = { 40, 90, 50, 255 };
    m_cloudColor = { 255, 255, 255, 255 };
    m_clouds = 0.0f;
    m_mistColor = { 200, 220, 200, 255 };
    m_mist = 0.0f;
    m_mistTop = -0.2f;
    m_planetColor = { 180, 140, 100, 255 };
    m_planet[0] = m_planet[1] = m_planet[2] = m_planet[3] = 0.0f;
    m_nebulaA = { 60, 30, 120, 255 };
    m_nebulaB = { 20, 80, 140, 255 };
    m_nebula = 1.0f;
    std::string style, fluid;
    const Block *b = cat.theme( theme );
    if ( !b ) b = cat.theme( "space" );
    m_theme = b ? *b : Block();
    for ( const Line &l : m_theme.m_lines )
    {
        const std::string &k = l.m_key;
        if ( k == "sky" ) m_look.m_sky = numColor( l, 0, m_look.m_sky );
        else if ( k == "fog" )
        {
            m_look.m_fog = numColor( l, 0, m_look.m_fog );
            m_look.m_fogDensity = l.argNum( 1, m_look.m_fogDensity );
        }
        else if ( k == "sun" )
            m_look.m_sunDir = Vector3Normalize( { l.argNum( 0, -0.35f ), l.argNum( 1, -0.85f ), l.argNum( 2, -0.4f ) } );
        else if ( k == "suncolor" )
        {
            m_look.m_sunColor = numColor( l, 0, m_look.m_sunColor );
            m_look.m_sunPower = l.argNum( 1, m_look.m_sunPower );
        }
        else if ( k == "ambient" )
        {
            m_look.m_ambUp = numColor( l, 0, m_look.m_ambUp );
            m_look.m_ambDown = numColor( l, 1, m_look.m_ambDown );
        }
        else if ( k == "exposure" ) m_look.m_exposure = l.argNum( 0, 1 );
        else if ( k == "bloom" ) m_look.m_bloom = l.argNum( 0, 0.9f );
        else if ( k == "groundlight" ) m_look.m_groundLight = l.argNum( 0, 0.55f );
        else if ( k == "space" ) m_look.m_space = l.argNum( 0, 1 ) != 0;
        else if ( k == "relief" ) m_relief = l.argNum( 0, m_relief );
        else if ( k == "scale" ) m_scale = l.argNum( 0, m_scale );
        else if ( k == "ridge" ) m_ridge = l.argNum( 0, 1 );
        else if ( k == "ground" )
        {
            m_low = numColor( l, 0, m_low );
            m_mid = numColor( l, 1, m_mid );
            m_high = numColor( l, 2, m_high );
            m_peak = numColor( l, 3, m_peak );
        }
        else if ( k == "water" )
        {
            m_water = l.argNum( 0, 0 );
            m_waterColor = numColor( l, 1, m_waterColor );
            m_waterGlow = l.argNum( 2, 0 );
        }
        else if ( k == "glow" )
        {
            m_glow = numColor( l, 0, m_glow );
            m_glowAmount = l.argNum( 1, 0.5f );
            m_glow2 = numColor( l, 2, m_glow2 );
        }
        else if ( k == "city" ) m_city = l.argNum( 0, 0.3f );
        else if ( k == "trees" )
        {
            m_trees = l.argNum( 0, 0.3f );
            m_treeColor = numColor( l, 1, m_treeColor );
        }
        else if ( k == "craters" ) m_craters = l.argNum( 0, 0.3f );
        else if ( k == "plates" ) m_plates = l.argNum( 0, 1 );
        else if ( k == "rocks" ) m_rocks = l.argNum( 0, 0.3f );
        else if ( k == "props" ) m_propColor = numColor( l, 0, m_propColor );
        else if ( k == "clouds" )
        {
            m_cloudColor = numColor( l, 0, m_cloudColor );
            m_clouds = l.argNum( 1, 0.5f );
        }
        else if ( k == "nebula" )
        {
            m_nebulaA = numColor( l, 0, m_nebulaA );
            m_nebulaB = numColor( l, 1, m_nebulaB );
            m_nebula = l.argNum( 2, 1.0f );
        }
        else if ( k == "style" ) style = l.arg( 0 );
        else if ( k == "fluid" ) fluid = l.arg( 0 );
        else if ( k == "rivers" )
        {
            m_rivers = l.argNum( 0, 1 );
            m_riverWidth = l.argNum( 1, 1 );
            m_riverCanyon = l.argNum( 2, 0 );
        }
        else if ( k == "leads" ) m_leads = l.argNum( 0, 1 );
        else if ( k == "walls" )
        {
            m_walls = l.argNum( 0, 150 );
            m_wallWidth = l.argNum( 1, 820 );
        }
        else if ( k == "crystals" )
        {
            m_crystals = l.argNum( 0, 0.5f );
            m_crystalA = numColor( l, 1, m_crystalA );
            m_crystalB = numColor( l, 2, m_crystalB );
        }
        else if ( k == "dunes" ) m_dunes = l.argNum( 0, 1 );
        else if ( k == "mesas" ) m_mesas = l.argNum( 0, 1 );
        else if ( k == "channels" ) m_channels = l.argNum( 0, 0.5f );
        else if ( k == "paint" ) m_paint = l.argNum( 0, 0.3f );
        else if ( k == "waves" ) m_waves = l.argNum( 0, 0.5f );
        else if ( k == "windows" ) m_windows = l.argNum( 0, 0.5f );
        else if ( k == "stars" ) m_stars = l.argNum( 0, 1 );
        else if ( k == "lightning" ) m_lightning = l.argNum( 0, 0.5f );
        else if ( k == "detail" ) m_detail = l.argNum( 0, 1 );
        else if ( k == "mist" )
        {
            m_mistColor = numColor( l, 0, m_mistColor );
            m_mist = l.argNum( 1, 0.5f );
            m_mistTop = l.argNum( 2, -0.2f );
        }
        else if ( k == "planet" )
        {
            m_planetColor = numColor( l, 0, m_planetColor );
            m_planet[0] = l.argNum( 1, 200 );
            m_planet[1] = l.argNum( 2, 0.8f );
            m_planet[2] = l.argNum( 3, 0.25f );
            m_planet[3] = l.argNum( 4, 0 );
        }
    }
    // The surface: named, or guessed from what the theme has (themes of other files keep working).
    static const std::pair<const char *, StyleE> styles[] = {
        { "rock", StyleE::ROCK },       { "sand", StyleE::SAND },       { "island", StyleE::ISLAND },
        { "ice", StyleE::ICE },         { "jungle", StyleE::JUNGLE },   { "plating", StyleE::PLATING },
        { "lattice", StyleE::LATTICE }, { "corona", StyleE::CORONA },   { "city", StyleE::CITY },
        { "foundry", StyleE::FOUNDRY }, { "hive", StyleE::HIVE } };
    m_style = m_plates > 0 ? ( m_city > 0.2f ? StyleE::FOUNDRY : StyleE::PLATING )
              : m_trees >= 0.5f ? StyleE::JUNGLE
              : m_city >= 0.5f  ? StyleE::CITY
              : ( m_water > -1e8f && m_water >= 0 && m_waterGlow < 0.5f ) ? StyleE::ISLAND
                                                                       : StyleE::ROCK;
    for ( const auto &[n, s] : styles )
        if ( style == n ) m_style = s;
    m_fluid = m_water < -1e8f ? FluidE::NONE : m_waterGlow >= 0.5f ? FluidE::LAVA : FluidE::WATER;
    if ( fluid == "water" ) m_fluid = FluidE::WATER;
    else if ( fluid == "lava" ) m_fluid = FluidE::LAVA;
    else if ( fluid == "bio" ) m_fluid = FluidE::BIO;
    else if ( fluid == "none" ) m_fluid = FluidE::NONE;
    if ( m_style == StyleE::CORONA ) m_fluid = FluidE::NONE;   // the plasma is all surface
    if ( m_fluid == FluidE::NONE ) m_water = -1e9f;

    // The noise picture the shaders build everything from: four tileable value noises, one in each channel.
    {
        const int W = 256, P = 16;   // 16 lattice steps of 16 texels
        Image     img = GenImageColor( W, W, BLANK );
        Color    *px = (Color *)img.data;
        for ( int y = 0; y < W; ++y )
            for ( int x = 0; x < W; ++x )
            {
                float         fx = x / 16.0f, fy = y / 16.0f;
                int           ix = (int)fx, iy = (int)fy;
                float         ux = fx - ix, uy = fy - iy;
                ux = ux * ux * ux * ( ux * ( ux * 6 - 15 ) + 10 );
                uy = uy * uy * uy * ( uy * ( uy * 6 - 15 ) + 10 );
                unsigned char ch[4];
                for ( int c = 0; c < 4; ++c )
                {
                    uint32_t s = 911u + (uint32_t)c * 7717u;
                    float    a = hashf( ix % P, iy % P, s ), bb = hashf( ( ix + 1 ) % P, iy % P, s ),
                          cc = hashf( ix % P, ( iy + 1 ) % P, s ), d = hashf( ( ix + 1 ) % P, ( iy + 1 ) % P, s );
                    ch[c] = (unsigned char)( lerpf( lerpf( a, bb, ux ), lerpf( cc, d, ux ), uy ) * 255.0f );
                }
                px[y * W + x] = { ch[0], ch[1], ch[2], ch[3] };
            }
        m_noiseTex = LoadTextureFromImage( img );
        GenTextureMipmaps( &m_noiseTex );
        SetTextureFilter( m_noiseTex, TEXTURE_FILTER_TRILINEAR );
        SetTextureWrap( m_noiseTex, TEXTURE_WRAP_REPEAT );
        UnloadImage( img );
        m_hasNoise = true;
    }
    if ( m_clouds > 0 )
    {
        // A soft noise picture for the clouds.
        const int W = 256;
        Image     img = GenImageColor( W, W, BLANK );
        Color    *px = (Color *)img.data;
        // Tileable value noise: every octave's lattice wraps at the picture's edge.
        auto tile = [&]( float x, float y, int period, uint32_t s ) {
            int   ix = (int)std::floor( x ), iy = (int)std::floor( y );
            float fx = x - ix, fy = y - iy;
            fx = fx * fx * ( 3 - 2 * fx );
            fy = fy * fy * ( 3 - 2 * fy );
            auto  at = [&]( int a, int b ) { return hashf( ( a % period + period ) % period, ( b % period + period ) % period, s ); };
            return lerpf( lerpf( at( ix, iy ), at( ix + 1, iy ), fx ), lerpf( at( ix, iy + 1 ), at( ix + 1, iy + 1 ), fx ),
                          fy ) * 2 - 1;
        };
        for ( int y = 0; y < W; ++y )
            for ( int x = 0; x < W; ++x )
            {
                float n = 0, amp = 0.55f;
                for ( int o = 0; o < 5; ++o )
                {
                    int period = 4 << o;
                    n += tile( x * period / (float)W, y * period / (float)W, period, seed + 9 + (uint32_t)o * 31u ) * amp;
                    amp *= 0.5f;
                }
                float a = clampf( ( n + 0.02f ) * 2.0f, 0.0f, 1.0f );
                a = a * a * ( 3 - 2 * a );
                px[y * W + x] = { 255, 255, 255, (unsigned char)( a * 255 ) };
            }
        m_cloudTex = LoadTextureFromImage( img );
        GenTextureMipmaps( &m_cloudTex );
        SetTextureFilter( m_cloudTex, TEXTURE_FILTER_TRILINEAR );
        SetTextureWrap( m_cloudTex, TEXTURE_WRAP_REPEAT );
        UnloadImage( img );
        m_hasCloudTex = true;
    }
    makeShaders();
    setStaticUniforms();
}

void Terrain::makeShaders()
{
    char defs[512];
    snprintf( defs, sizeof( defs ),
              "#version 330\n#define S_ROCK 0\n#define S_SAND 1\n#define S_ISLAND 2\n#define S_ICE 3\n#define S_JUNGLE 4\n"
              "#define S_PLATING 5\n#define S_LATTICE 6\n#define S_CORONA 7\n#define S_CITY 8\n#define S_FOUNDRY 9\n"
              "#define S_HIVE 10\n#define F_NONE 0\n#define F_WATER 1\n#define F_LAVA 2\n#define F_BIO 3\n"
              "#define STYLE %d\n#define FLUID %d\n#define GROUND_DEPTH %.1f\n",
              (int)m_style, (int)m_fluid, GroundDepth );
    std::string vs = std::string( defs ) + GroundVs;
    std::string fs = std::string( defs ) + NoiseGlsl + GroundHeadFs + GroundStylesFs + GroundFluidFs + GroundMainFs;
    m_ground = LoadShaderFromMemory( vs.c_str(), fs.c_str() );
    for ( int i = 0; i < G_COUNT; ++i ) m_loc[i] = GetShaderLocation( m_ground, GroundLocNames[i] );
    m_groundMat = LoadMaterialDefault();
    m_groundMat.shader = m_ground;
    if ( m_look.m_space )
    {
        std::string sfs = std::string( "#version 330\n" ) + NoiseGlsl + SkyFs;
        m_sky = LoadShaderFromMemory( SkyVs, sfs.c_str() );
        for ( int i = 0; i < K_COUNT; ++i ) m_skyLoc[i] = GetShaderLocation( m_sky, SkyLocNames[i] );
    }
    if ( m_hasCloudTex )
    {
        m_cloud = LoadShaderFromMemory( SkyVs, CloudFs );
        for ( int i = 0; i < C_COUNT; ++i ) m_cloudLoc[i] = GetShaderLocation( m_cloud, CloudLocNames[i] );
    }
    m_hasShaders = true;
}

void Terrain::setStaticUniforms()
{
    Shader &s = m_ground;
    auto    f1 = [&]( int i, float v ) { SetShaderValue( s, m_loc[i], &v, SHADER_UNIFORM_FLOAT ); };
    auto    f3 = [&]( int i, Vector3 v ) { SetShaderValue( s, m_loc[i], &v, SHADER_UNIFORM_VEC3 ); };
    const Look &L = m_look;
    f3( G_SUNDIR, L.m_sunDir );
    f3( G_SUNCOL, v3( L.m_sunColor, L.m_sunPower ) );
    f3( G_AMBUP, v3( L.m_ambUp ) );
    f3( G_AMBDOWN, v3( L.m_ambDown ) );
    // Rocks in space fade into the sky with depth; on a ground the theme's fog does it.
    Vector4 fog = L.m_space ? Vector4{ L.m_sky.r / 255.0f, L.m_sky.g / 255.0f, L.m_sky.b / 255.0f, 0.0009f }
                            : Vector4{ L.m_fog.r / 255.0f, L.m_fog.g / 255.0f, L.m_fog.b / 255.0f, L.m_fogDensity };
    SetShaderValue( s, m_loc[G_FOG], &fog, SHADER_UNIFORM_VEC4 );
    f1( G_GROUNDLIGHT, L.m_groundLight );
    f1( G_RELIEF, m_relief );
    f1( G_WATERH, m_water > -1e8f ? m_water * m_relief : -1e6f );
    f3( G_BAND0, v3( m_low ) );
    f3( G_BAND1, v3( m_mid ) );
    f3( G_BAND2, v3( m_high ) );
    f3( G_BAND3, v3( m_peak ) );
    f3( G_WATERCOL, v3( m_waterColor ) );
    f1( G_WATERGLOW, m_waterGlow );
    f3( G_GLOWCOL, v3( m_glow ) );
    f3( G_GLOWCOL2, v3( m_glow2 ) );
    f1( G_GLOWAMOUNT, m_glowAmount );
    f3( G_TREECOL, v3( m_treeColor ) );
    f1( G_WINDOWS, m_windows );
    f1( G_WAVES, m_waves );
    f1( G_DETAIL, m_detail );
    f1( G_PAINT, m_style == StyleE::ROCK ? m_crystals : m_paint );   // rock: the caves' glowing moss
    f3( G_MISTCOL, v3( m_mistColor ) );
    Vector2 mist = { m_mist, m_mistTop * m_relief };
    SetShaderValue( s, m_loc[G_MIST], &mist, SHADER_UNIFORM_VEC2 );
    m_groundMat.maps[MATERIAL_MAP_ALBEDO].texture = m_noiseTex;
    m_groundMat.maps[MATERIAL_MAP_METALNESS].texture = m_noiseTex;
    m_groundMat.maps[MATERIAL_MAP_NORMAL].texture = m_hasCloudTex ? m_cloudTex : m_noiseTex;
    if ( m_look.m_space )
    {
        Shader &k = m_sky;
        auto    k3 = [&]( int i, Vector3 v ) { SetShaderValue( k, m_skyLoc[i], &v, SHADER_UNIFORM_VEC3 ); };
        k3( K_SKY, v3( L.m_sky ) );
        k3( K_NEBA, v3( m_nebulaA ) );
        k3( K_NEBB, v3( m_nebulaB ) );
        k3( K_PLANETCOL, v3( m_planetColor ) );
        // The planet is lit on the side towards the field, as if the sun stood behind the camera's shoulder.
        k3( K_SUNDIR, Vector3Negate( Vector3Normalize( { 0.5f - m_planet[1], 0.35f, 0.5f - m_planet[2] } ) ) );
        float nb = m_nebula, st = m_stars;
        SetShaderValue( k, m_skyLoc[K_NEBAMT], &nb, SHADER_UNIFORM_FLOAT );
        SetShaderValue( k, m_skyLoc[K_STARAMT], &st, SHADER_UNIFORM_FLOAT );
        // The planet in the coordinates of its layer (at the mission's start it stands where the theme says).
        Vector4 pl = { m_planet[0], ( m_planet[1] - 0.5f ) * 2.0f * m_viewHalfW, -( m_planet[2] - 0.5f ) * FieldH,
                       m_planet[3] };
        SetShaderValue( k, m_skyLoc[K_PLANET], &pl, SHADER_UNIFORM_VEC4 );
    }
}

void Terrain::setFrameUniforms( float time )
{
    Shader &s = m_ground;
    auto    f1 = [&]( int i, float v ) { SetShaderValue( s, m_loc[i], &v, SHADER_UNIFORM_FLOAT ); };
    f1( G_TIME, time );
    f1( G_SCROLLG, FieldH * 0.5f + (float)m_scroll );
    SetShaderValue( s, m_loc[G_CAMPOS], &m_frame.m_camPos, SHADER_UNIFORM_VEC3 );
    int n = std::min( m_frame.m_lightCount, 16 );
    SetShaderValue( s, m_loc[G_LIGHTCOUNT], &n, SHADER_UNIFORM_INT );
    if ( n > 0 && m_frame.m_lightPos && m_frame.m_lightCol )
    {
        SetShaderValueV( s, m_loc[G_LIGHTPOS], m_frame.m_lightPos, SHADER_UNIFORM_VEC4, n );
        SetShaderValueV( s, m_loc[G_LIGHTCOL], m_frame.m_lightCol, SHADER_UNIFORM_VEC4, n );
    }
    bool shadow = m_frame.m_shadowTex != 0 && !m_look.m_space;
    f1( G_SHADOWON, shadow ? 1.0f : 0.0f );
    SetShaderValue( s, m_loc[G_SHADOWRECT], &m_frame.m_shadowRect, SHADER_UNIFORM_VEC4 );
    f1( G_SHADOWY, m_frame.m_shadowY );
    m_groundMat.maps[MATERIAL_MAP_METALNESS].texture.id = shadow ? m_frame.m_shadowTex : m_noiseTex.id;
    // The lower cloud layer's shadow (drawOver draws it at the same place).
    Vector4 cp = { 1100.0f, 0.0f, 0.0f, 0.0f };
    if ( m_clouds > 0 )
    {
        cp.z = ( -(float)m_scroll - time * 9.0f ) / cp.x;
        cp.w = std::min( 0.6f, 0.75f * m_clouds );
    }
    SetShaderValue( s, m_loc[G_CLOUDPARAM], &cp, SHADER_UNIFORM_VEC4 );
    f1( G_CLOUDY, -GroundDepth * 0.55f );
}

void Terrain::unload()
{
    releaseChunks();
    if ( m_hasShaders )
    {
        UnloadShader( m_ground );
        if ( m_sky.id ) UnloadShader( m_sky );
        if ( m_cloud.id ) UnloadShader( m_cloud );
        MemFree( m_groundMat.maps );
        m_groundMat = {};
        m_sky = m_cloud = m_ground = {};
        m_hasShaders = false;
    }
    if ( m_hasNoise ) UnloadTexture( m_noiseTex );
    m_hasNoise = false;
    if ( m_hasCloudTex ) UnloadTexture( m_cloudTex );
    m_hasCloudTex = false;
}

void Terrain::releaseChunks()
{
    // A chunk still being built must be finished before its theme goes.
    if ( m_pending.valid() ) m_pending.get();
    for ( auto &[k, c] : m_chunks )
        for ( int i = 0; i < c.m_parts; ++i ) UnloadMesh( c.m_mesh[i] );
    m_chunks.clear();
}

// ---------------------------------------------------------------------------------------------------------------
// The shape of the ground.
// ---------------------------------------------------------------------------------------------------------------

float Terrain::mesa( float x, float g ) const
{
    if ( m_mesas <= 0 ) return 0.0f;
    float m = fbm( x * 0.0016f, g * 0.0016f, m_seed + 21, 3 );
    return smooth( 0.3f - 0.22f * m_mesas, 0.36f - 0.22f * m_mesas, m );
}

float Terrain::rawHeight( float x, float g ) const
{
    float s = m_scale;
    float n = fbm( x * s, g * s, m_seed, 5 );
    if ( m_ridge > 0 ) n = lerpf( n, 0.6f - std::fabs( n ) * 1.6f, m_ridge );
    float h = n * m_relief;
    float waterLv = m_water > -1e8f ? m_water * m_relief : -1e9f;
    if ( m_style == StyleE::SAND )
    {
        // Mesas: tables with flat tops and steep sides, a terrace or two on the way up.
        float mk = mesa( x, g );
        if ( mk > 0 )
        {
            float top = m_relief * ( 0.85f + 0.2f * fbm( x * 0.01f, g * 0.01f, m_seed + 22, 2 ) );
            float t = mk * mk * ( 3 - 2 * mk );
            float terr = std::floor( t * 3.0f ) / 3.0f;
            h = lerpf( h, top, lerpf( t, terr, 0.35f ) );
        }
        // Dunes: long crests across the wind, gentle on the windward side, steep in the lee.
        if ( m_dunes > 0 )
        {
            float w = ( x * 0.55f + g * 0.83f ) / 95.0f + fbm( x * 0.004f, g * 0.004f, m_seed + 23, 2 ) * 1.6f;
            float f = w - std::floor( w );
            float prof = f < 0.78f ? f / 0.78f : ( 1.0f - f ) / 0.22f;
            prof = prof * prof * ( 3 - 2 * prof );
            float amp = m_dunes * m_relief * 0.24f * ( 0.55f + 0.45f * fbm( x * 0.003f, g * 0.003f, m_seed + 24, 2 ) );
            h += ( prof - 0.5f ) * amp * ( 1.0f - mk );
        }
    }
    if ( m_plates > 0 )
    {
        // Plating: every big plate one flat deck at one of four levels (whole districts higher or lower), the
        // steps hidden in the trenches between the plates (centred on multiples of 160, below both sides).
        auto level = [&]( int ix, int iz ) {
            float c = fbm( ( ix + 0.5f ) * PlateSize * s, ( iz + 0.5f ) * PlateSize * s, m_seed, 3 ) * 0.5f + 0.5f;
            float q = std::floor( clampf( c + ( hashf( ix, iz, m_seed + 55 ) - 0.5f ) * 0.45f, 0.0f, 0.999f ) * 4.0f ) / 3.0f;
            return ( q - 0.5f ) * m_relief * 0.8f;
        };
        int   ix = (int)std::floor( x / PlateSize ), iz = (int)std::floor( g / PlateSize );
        float px = std::fabs( std::remainder( x, PlateSize ) ), pz = std::fabs( std::remainder( g, PlateSize ) );
        h = level( ix, iz );
        if ( px < 5.5f || pz < 5.5f )
        {
            int   jx = (int)std::lround( x / PlateSize ), jz = (int)std::lround( g / PlateSize );
            float lo = h;
            if ( px < 5.5f ) lo = std::min( { lo, level( jx - 1, iz ), level( jx, iz ) } );
            if ( pz < 5.5f ) lo = std::min( { lo, level( ix, jz - 1 ), level( ix, jz ) } );
            if ( px < 5.5f && pz < 5.5f )
                lo = std::min( { lo, level( jx - 1, jz - 1 ), level( jx, jz - 1 ), level( jx - 1, jz ), level( jx, jz ) } );
            h = lo - m_relief * 0.2f;
        }
        if ( m_channels > 0 && waterLv > -1e8f )
        {
            // Molten channels in some trenches: a whole plate's side at a time, three grid steps wide.
            int  cx = (int)std::lround( x / PlateSize ), cz = (int)std::lround( g / PlateSize );
            bool vert = px < 10.5f && hashf( cx, iz, m_seed + 70 ) < m_channels;
            bool hor = pz < 10.5f && hashf( ix, cz, m_seed + 71 ) < m_channels * 0.6f;
            if ( vert || hor ) h = std::min( h, waterLv - 6.0f );
        }
    }
    if ( m_style == StyleE::ICE && m_leads > 0 && waterLv > -1e8f )
    {
        // Leads: long cracks in the sea ice where the dark water shows, mostly across the way.
        float v = std::fabs( fbm( x * 0.0021f + 3.0f, g * 0.0009f, m_seed + 140, 3 ) );
        float w = 0.02f * m_leads;
        if ( v < w * 1.6f ) h = std::min( h, lerpf( waterLv - 5.0f, h, smooth( w * 0.5f, w * 1.6f, v ) ) );
    }
    if ( m_craters > 0 )
    {
        float cell = 180.0f;
        int   cx = (int)std::floor( x / cell ), cz = (int)std::floor( g / cell );
        for ( int dz = -1; dz <= 1; ++dz )
            for ( int dx = -1; dx <= 1; ++dx )
            {
                int ix = cx + dx, iz = cz + dz;
                if ( hashf( ix, iz, m_seed + 77 ) > m_craters ) continue;
                float ccx = ( ix + 0.2f + 0.6f * hashf( ix, iz, m_seed + 78 ) ) * cell;
                float ccz = ( iz + 0.2f + 0.6f * hashf( ix, iz, m_seed + 79 ) ) * cell;
                float r = cell * ( 0.18f + 0.3f * hashf( ix, iz, m_seed + 80 ) );
                float d = std::sqrt( ( x - ccx ) * ( x - ccx ) + ( g - ccz ) * ( g - ccz ) ) / r;
                if ( d < 1.0f ) h -= ( 1 - d * d ) * r * 0.35f;
                else if ( d < 1.35f ) h += ( 1.35f - d ) / 0.35f * r * 0.08f;
            }
    }
    if ( m_rivers > 0 )
    {
        // Rivers winding down the ground along the scroll (in the desert dry canyons): a bowl of a bed, flat
        // banks beside it, or with riverCanyon cliffs rising from the water. The nearest river shapes the ground.
        bool  dry = waterLv < -1e8f;
        int   nr = m_rivers > 1.0f ? 2 : 1;
        float tMin = 1e9f;
        for ( int i = 0; i < nr; ++i )
        {
            float cx = FieldW * 0.5f + ( nr == 1 ? 0.0f : ( i - 0.5f ) * 760.0f ) + ( hashf( i, 1, m_seed + 60 ) - 0.5f ) * 300.0f;
            float wander = m_riverCanyon > 0 ? 0.45f : 1.0f;   // a gorge stays under the field
            float xr = cx + wander * ( 240.0f * std::sin( g * 0.0021f + i * 2.1f + m_seed % 7 ) +
                                       200.0f * fbm( g * 0.0016f, i * 13.7f, m_seed + 61, 3 ) );
            float w = ( dry ? 26.0f : 15.0f ) * std::min( 1.5f, m_rivers ) * m_riverWidth *
                      ( 0.75f + 0.5f * ( fbm( g * 0.003f, i * 3.3f, m_seed + 62, 2 ) * 0.5f + 0.5f ) );
            tMin = std::min( tMin, std::fabs( x - xr ) / w );
        }
        float t = tMin;
        if ( m_riverCanyon > 0 && !dry )
        {
            // A gorge: the river at the bottom, cliffs up to a plateau that falls back further out.
            float top = h + m_riverCanyon * m_relief * 0.6f * ( 1.0f - smooth( 4.0f, 8.0f, t ) );
            if ( t < 1.0f ) h = lerpf( waterLv - 8.0f, waterLv + 1.0f, t * t );
            else if ( t < 8.0f ) h = lerpf( waterLv + 1.0f, top, smooth( 1.0f, 1.8f, t ) );
        }
        else if ( t < 3.0f )
        {
            float bank = dry ? h - m_relief * 0.7f : waterLv + 1.5f;
            float bed = dry ? bank - m_relief * 0.15f : waterLv - 7.0f;
            if ( t < 1.0f ) h = std::min( h, lerpf( bed, bank, dry ? t * t * t * t : t * t ) );
            else h = std::min( h, lerpf( bank, h, smooth( 1.0f, 3.0f, t ) ) );
        }
    }
    if ( m_walls > 0 )
    {
        // A cave: rock walls rise beside a winding corridor (the field flies down the corridor).
        float cx = FieldW * 0.5f + 110.0f * std::sin( g * 0.0017f + m_seed % 5 ) + 70.0f * fbm( g * 0.003f, 7.7f, m_seed + 150, 2 );
        float d = std::fabs( x - cx ) - m_wallWidth * 0.5f + 60.0f * fbm( x * 0.004f, g * 0.006f, m_seed + 152, 2 );
        if ( d > -40.0f )
            h += m_walls * smooth( -40.0f, 120.0f, d ) *
                 ( 0.75f + 0.45f * ( fbm( x * 0.012f, g * 0.012f, m_seed + 151, 3 ) * 0.5f + 0.5f ) );
    }
    return h;
}

float Terrain::forest( float x, float g, float raw ) const
{
    if ( m_trees <= 0 ) return 0.0f;
    // Dense (trees 0.75: nearly everywhere) down to patches (0.35: here and there).
    float n = fbm( x * 0.004f, g * 0.004f, m_seed + 90, 3 ) * 2.2f + ( m_trees - 0.5f ) * 2.0f + 0.5f;
    float k = clampf( n, 0.0f, 1.0f );
    if ( m_water > -1e8f ) k *= smooth( m_water * m_relief + 1.5f, m_water * m_relief + 5.0f, raw );
    return k;
}

float Terrain::built( float x, float g ) const
{
    if ( m_city <= 0 ) return 0.0f;
    return m_city * clampf( fbm( x * 0.0025f, g * 0.0025f, m_seed + 400, 2 ) * 2.5f + 0.4f, 0, 1.4f );
}

float Terrain::groundHeight( float x, float g ) const
{
    float raw = rawHeight( x, g );
    float h = m_water > -1e8f ? std::max( raw, m_water * m_relief ) : raw;
    // The jungle's canopy stands above the ground.
    if ( m_style == StyleE::JUNGLE ) h += forest( x, g, raw ) * 9.0f;
    return h;
}

float Terrain::heightAt( float x, float z, double scroll ) const
{
    if ( m_look.m_space ) return -GroundDepth;
    float g = FieldH * 0.5f + (float)scroll - z;
    return -GroundDepth + groundHeight( x, g );
}

// ---------------------------------------------------------------------------------------------------------------
// Chunks.
// ---------------------------------------------------------------------------------------------------------------

Terrain::Geometry Terrain::buildChunk( int k ) const
{
    Geometry geo;
    Builder  b( geo );
    float    x0 = std::floor( ( FieldW * 0.5f - m_halfW ) / Cell ) * Cell;
    int      nx = (int)std::ceil( m_halfW * 2 / Cell ) + 1;
    float    x1 = x0 + nx * Cell;
    if ( m_look.m_space )
    {
        if ( m_rocks > 0 ) buildRocks( geo, k );
        return geo;
    }
    // Heights on the grid with a ring round it (for the normals): raw (water decides by it) and as built.
    int                stride = nx + 3;
    std::vector<float> raw( (size_t)stride * ( Rows + 3 ) ), hc( raw.size() );
    for ( int j = -1; j <= Rows + 1; ++j )
        for ( int i = -1; i <= nx + 1; ++i )
        {
            float  x = x0 + i * Cell, g = k * ChunkLen + j * Cell;
            size_t at = (size_t)( j + 1 ) * stride + ( i + 1 );
            raw[at] = rawHeight( x, g );
            float h = m_water > -1e8f ? std::max( raw[at], m_water * m_relief ) : raw[at];
            if ( m_style == StyleE::JUNGLE ) h += forest( x, g, raw[at] ) * 9.0f;
            hc[at] = h;
        }
    auto  H = [&]( int i, int j ) { return hc[(size_t)( j + 1 ) * stride + ( i + 1 )]; };
    auto  R = [&]( int i, int j ) { return raw[(size_t)( j + 1 ) * stride + ( i + 1 )]; };
    float wl = m_water > -1e8f ? m_water * m_relief : -1e9f;
    int   first = b.count();
    for ( int j = 0; j <= Rows; ++j )
        for ( int i = 0; i <= nx; ++i )
        {
            float   x = x0 + i * Cell, g = k * ChunkLen + j * Cell;
            Vector3 n = Vector3Normalize( { -( H( i + 1, j ) - H( i - 1, j ) ), 2 * Cell, H( i, j + 1 ) - H( i, j - 1 ) } );
            float   r = R( i, j );
            // The masks the shader reads: r forest, g built-up, b the style's own, a light strips.
            float veg = forest( x, g, r );
            if ( m_style == StyleE::CITY )
                veg = r > wl + 1.0f ? smooth( 0.35f, 0.55f, fbm( x * 0.006f, g * 0.006f, m_seed + 91, 2 ) * 0.5f + 0.5f ) : 0;
            float bm = built( x, g );
            float own = 0.0f;
            if ( m_style == StyleE::SAND ) own = mesa( x, g );
            else if ( m_style == StyleE::ICE ) own = smooth( -0.3f, 0.4f,
                                                             fbm( x * 0.004f, g * 0.004f, m_seed + 33, 3 ) );
            else own = smooth( 0.1f, 0.5f, fbm( x * 0.008f, g * 0.008f, m_seed + 33, 2 ) );
            float strip = 0.0f;
            if ( m_plates > 0 && m_glowAmount > 0 )
            {
                // Which trench sides carry a light strip: one draw per plate side (the shader draws the strip).
                float px = std::fabs( std::remainder( x, PlateSize ) ), pz = std::fabs( std::remainder( g, PlateSize ) );
                int ix = (int)std::floor( x / PlateSize ), iz = (int)std::floor( g / PlateSize );
                int jx = (int)std::lround( x / PlateSize ), jz = (int)std::lround( g / PlateSize );
                if ( px < 1.0f && hashf( jx, iz, m_seed + 51 ) < m_glowAmount ) strip = 1;
                if ( pz < 1.0f && hashf( ix, jz, m_seed + 52 ) < m_glowAmount ) strip = 1;
            }
            Color c = { (unsigned char)( clampf( veg, 0, 1 ) * 255 ), (unsigned char)( clampf( bm, 0, 1 ) * 255 ),
                        (unsigned char)( clampf( own, 0, 1 ) * 255 ), (unsigned char)( strip * 255 ) };
            b.vert( { x, H( i, j ), -( j * Cell ) }, n, c, r, 0, M_GROUND, hashf( i, j + k * Rows, m_seed ) );
        }
    // Each square split along the diagonal that follows the ground best.
    for ( int j = 0; j < Rows; ++j )
        for ( int i = 0; i < nx; ++i )
        {
            int a = first + j * ( nx + 1 ) + i, bb = a + 1, d = a + nx + 1, c = d + 1;
            if ( std::fabs( H( i, j ) - H( i + 1, j + 1 ) ) <= std::fabs( H( i + 1, j ) - H( i, j + 1 ) ) )
            {
                b.tri( a, bb, c );
                b.tri( a, c, d );
            }
            else
            {
                b.tri( a, bb, d );
                b.tri( bb, c, d );
            }
        }
    buildProps( geo, k, x0, x1 );
    return geo;
}

void Terrain::buildProps( Geometry &geo, int k, float x0, float x1 ) const
{
    Builder     b( geo );
    Rng         r;
    float       g0 = k * ChunkLen;
    float       wl = m_water > -1e8f ? m_water * m_relief : -1e9f;
    auto        local = [&]( float g ) { return -( g - g0 ); };   // a ground coordinate in the chunk's frame
    const Color lampRed = { 255, 70, 40, 200 }, lampGreen = { 80, 255, 140, 200 };
    r.seed( m_seed * 31u + (uint32_t)k * 7919u + 5u );

    // Cities: blocks on the street grid (the shader paints the streets where the built-up mask is).
    if ( m_city > 0 && m_style != StyleE::FOUNDRY )
    {
        int bz0 = (int)std::ceil( g0 / CityBlock ), bz1 = (int)std::ceil( ( g0 + ChunkLen ) / CityBlock ) - 1;
        for ( int bz = bz0; bz <= bz1; ++bz )
            for ( int bx = (int)std::floor( x0 / CityBlock ); bx * CityBlock < x1; ++bx )
            {
                float lx0 = bx * CityBlock + StreetW, lz0 = bz * CityBlock + StreetW, lot = CityBlock - StreetW;
                float cx = lx0 + lot * 0.5f, cg = lz0 + lot * 0.5f;
                float d = built( cx, cg );
                Rng   q;
                q.seed( hash2( bx, bz, m_seed + 500 ) );
                if ( d < 0.18f || q.unit() > d * 1.25f ) continue;
                float lowest = 1e9f, highest = -1e9f;
                for ( float ox : { lx0, lx0 + lot } )
                    for ( float oz : { lz0, lz0 + lot } )
                    {
                        float hh = groundHeight( ox, oz );
                        lowest = std::min( lowest, hh );
                        highest = std::max( highest, rawHeight( ox, oz ) );
                    }
                bool wet = wl > -1e8f && std::min( lowest, highest ) <= wl + 1.0f;
                float base = lowest;
                if ( wet )
                {
                    // On the water only Thalassa's floating cities stand: a platform first.
                    if ( m_style != StyleE::ISLAND ) continue;
                    base = wl + 3.0f;
                    Color pc = withAlpha( scaleColor( m_propColor, 0.7f ), 0 );
                    b.box( { cx, wl + 1.5f, local( cg ) }, { lot * 0.5f + 4, 2.5f, lot * 0.5f + 4 }, pc,
                           scaleColor( pc, 0.8f ), M_METAL, q.unit() );
                    if ( q.chance( 0.5f ) ) b.lamp( { cx + lot * 0.5f + 3, wl + 4.5f, local( cg ) }, 1.0f, lampGreen,
                                                    0.5f, q.unit() );
                }
                int split = q.below( 3 );
                int nxb = split == 2 ? 2 : 1, nzb = split >= 1 ? 2 : 1;
                for ( int iz = 0; iz < nzb; ++iz )
                    for ( int ix = 0; ix < nxb; ++ix )
                    {
                        float w = lot / nxb, dd = lot / nzb;
                        float inset = q.range( 1.5f, 4.0f );
                        float bx0 = lx0 + ix * w + inset, bz0b = lz0 + iz * dd + inset;
                        float bw = w - inset * 2, bd = dd - inset * 2;
                        if ( bw < 6 || bd < 6 ) continue;
                        float tall = 6.0f + d * d * q.range( 10.0f, 55.0f ) * ( q.chance( 0.12f ) ? 1.8f : 1.0f );
                        Color side = withAlpha( scaleColor( m_propColor, q.range( 0.7f, 1.15f ) ), 0 );
                        Color tone = q.chance( 0.5f ) ? Color{ 150, 140, 120, 0 } : Color{ 110, 125, 145, 0 };
                        side = mixColor( side, tone, 0.2f );
                        Color top = scaleColor( side, 1.1f );
                        float rnd = q.unit();
                        b.box( { bx0 + bw * 0.5f, base - 3 + ( tall + 3 ) * 0.5f, local( bz0b + bd * 0.5f ) },
                               { bw * 0.5f, ( tall + 3 ) * 0.5f, bd * 0.5f },
                               top, side, M_BUILDING, rnd, q.chance( 0.2f ) ? q.range( 0.05f, 0.2f ) : 0.0f );
                        float roof = base + tall;
                        if ( q.chance( 0.55f ) )
                        {
                            float s = std::min( bw, bd ) * q.range( 0.15f, 0.3f );
                            Color mc = withAlpha( scaleColor( side, 0.8f ), 0 );
                            float ux = bx0 + bw * q.range( 0.3f, 0.7f ), uz = bz0b + bd * q.range( 0.3f, 0.7f );
                            b.box( { ux, roof + s * 0.4f, local( uz ) }, { s, s * 0.4f, s * 0.8f }, mc, mc, M_METAL, rnd );
                        }
                        if ( tall > 40 && q.chance( 0.7f ) )
                            b.lamp( { bx0 + bw * 0.5f, roof + 0.8f, local( bz0b + bd * 0.5f ) }, 0.9f, lampRed, 0.45f, q.unit() );
                    }
            }
    }
    // Per style: what stands on the ground.
    if ( m_style == StyleE::PLATING || m_style == StyleE::FOUNDRY )
    {
        int pz0 = (int)std::ceil( g0 / PlateSize ), pz1 = (int)std::ceil( ( g0 + ChunkLen ) / PlateSize ) - 1;
        for ( int pz = pz0; pz <= pz1; ++pz )
            for ( int px = (int)std::floor( x0 / PlateSize ); px * PlateSize < x1; ++px )
            {
                Rng q;
                q.seed( hash2( px, pz, m_seed + 600 ) );
                float cx = ( px + 0.5f ) * PlateSize, cg = ( pz + 0.5f ) * PlateSize;
                float h = groundHeight( cx, cg );
                float rnd = q.unit();
                Color metal = withAlpha( scaleColor( m_propColor, q.range( 0.8f, 1.1f ) ), 0 );
                if ( m_style == StyleE::FOUNDRY )
                {
                    if ( q.chance( 0.6f ) )
                    {
                        // A hall along one axis, chimneys and tanks beside it.
                        bool  alongX = q.chance( 0.5f );
                        float len = q.range( 70, 120 ), wid = q.range( 32, 48 ), hgt = q.range( 14, 24 );
                        Vector3 half = alongX ? Vector3{ len * 0.5f, hgt * 0.5f, wid * 0.5f }
                                              : Vector3{ wid * 0.5f, hgt * 0.5f, len * 0.5f };
                        float ox = alongX ? 0.0f : q.range( -20, 20 ), oz = alongX ? q.range( -20, 20 ) : 0.0f;
                        b.box( { cx + ox, h + hgt * 0.5f - 2, local( cg + oz ) }, { half.x, half.y + 2, half.z }, metal,
                               scaleColor( metal, 0.85f ), M_HALL, rnd );
                        int nCh = q.below( 4 );
                        for ( int c = 0; c < nCh; ++c )
                        {
                            float chx = cx + ( alongX ? q.range( -40, 40 ) : ( wid * 0.5f + 12 ) * ( q.chance( 0.5f ) ? 1 : -1 ) );
                            float chz = cg + ( alongX ? ( wid * 0.5f + 12 ) * ( q.chance( 0.5f ) ? 1 : -1 ) : q.range( -40, 40 ) );
                            float rr = q.range( 3.5f, 6.0f ), ch = q.range( 30, 60 );
                            Color sc = withAlpha( scaleColor( m_propColor, q.range( 0.6f, 0.85f ) ), 0 );
                            b.cylinder( { chx, h - 2, local( chz ) }, rr * 1.15f, rr, ch, 8, sc,
                                        withAlpha( m_glow, 255 ), M_METAL, M_HOT, q.unit() );
                        }
                    }
                    else if ( q.chance( 0.6f ) )
                    {
                        // A tank farm with a pipe run.
                        int n = 2 + q.below( 3 );
                        for ( int t = 0; t < n; ++t )
                        {
                            float tr = q.range( 9, 15 );
                            float tx = cx + ( t - ( n - 1 ) * 0.5f ) * ( tr * 2 + 6 ), tz = cg + q.range( -12, 12 );
                            Color tc = withAlpha( scaleColor( m_propColor, q.range( 0.85f, 1.2f ) ), 0 );
                            b.cylinder( { tx, h, local( tz ) }, tr, tr * 0.92f, q.range( 8, 14 ), 12, tc,
                                        scaleColor( tc, 1.1f ), M_METAL, M_METAL, q.unit() );
                        }
                        b.pipe( { cx - 70, h + 3, local( cg + 22 ) }, { cx + 70, h + 3, local( cg + 22 ) }, 2.2f, 4,
                                metal, rnd );
                    }
                }
                else
                {
                    float u = q.unit();
                    if ( u < 0.22f )
                    {
                        // A hangar: a dark opening framed by landing lights.
                        float hw = q.range( 26, 40 ), hd = q.range( 20, 34 );
                        Color dark = { 14, 16, 20, 0 };
                        b.box( { cx, h + 1.0f, local( cg ) }, { hw, 1.0f, hd }, dark,
                               withAlpha( scaleColor( m_propColor, 0.7f ), 0 ), M_METAL, rnd );
                        Color lc = withAlpha( m_glow, 200 );
                        float ph = q.unit();
                        for ( int s = -1; s <= 1; s += 2 )
                            for ( int i = 0; i < 5; ++i )
                            {
                                // A chase of lights towards the opening.
                                float lx = cx - hw + 4 + i * ( hw * 2 - 8 ) / 4.0f;
                                b.lamp( { lx, h + 2.3f, local( cg + s * ( hd + 2.5f ) ) }, 1.0f, lc, 0.7f,
                                        ph - i * 0.12f );
                            }
                    }
                    else if ( u < 0.42f )
                    {
                        // A radar dome on a plinth.
                        float dr = q.range( 9, 16 );
                        Color dc = withAlpha( scaleColor( m_propColor, 1.25f ), 0 );
                        b.box( { cx + q.range( -30, 30 ), h + 2, local( cg + q.range( -30, 30 ) ) },
                               { dr + 3, 2, dr + 3 }, metal, metal, M_METAL, rnd );
                        b.dome( { cx, h + 4, local( cg ) }, dr, dr * 0.8f, dc, M_METAL, rnd );
                    }
                    else if ( u < 0.6f )
                    {
                        // An antenna mast with a blinking light on top.
                        float mh = q.range( 22, 45 );
                        float mx = cx + q.range( -50, 50 ), mz = cg + q.range( -50, 50 );
                        b.box( { mx, h + mh * 0.5f, local( mz ) }, { 1.2f, mh * 0.5f, 1.2f }, metal, metal, M_METAL,
                               rnd );
                        b.box( { mx, h + 4, local( mz ) }, { 4, 4, 4 }, metal, scaleColor( metal, 0.8f ), M_METAL,
                               rnd );
                        b.lamp( { mx, h + mh + 0.8f, local( mz ) }, 1.0f, lampRed, 0.8f, q.unit() );
                    }
                    else
                    {
                        // Vents and blocks.
                        int n = 1 + q.below( 3 );
                        for ( int i = 0; i < n; ++i )
                        {
                            float vw = q.range( 6, 16 ), vd = q.range( 6, 16 ), vh = q.range( 3, 9 );
                            b.box( { cx + q.range( -50, 50 ), h + vh * 0.5f, local( cg + q.range( -50, 50 ) ) },
                                   { vw, vh * 0.5f, vd }, scaleColor( metal, 1.1f ), metal, M_METAL, q.unit() );
                        }
                    }
                    // Pipes along a trench side, beacons at the corners.
                    if ( q.chance( 0.45f ) )
                    {
                        float off = q.chance( 0.5f ) ? 12.0f : -12.0f;
                        if ( q.chance( 0.5f ) )
                            b.pipe( { px * PlateSize + 10, h + 2.4f, local( pz * PlateSize + 80 + off * 5.5f ) },
                                    { ( px + 1 ) * PlateSize - 10, h + 2.4f, local( pz * PlateSize + 80 + off * 5.5f ) }, 2.4f, 4,
                                    metal, rnd );
                        else
                            b.pipe( { px * PlateSize + 80 + off * 5.5f, h + 2.4f, local( pz * PlateSize + 10 ) },
                                    { px * PlateSize + 80 + off * 5.5f, h + 2.4f, local( ( pz + 1 ) * PlateSize - 10 ) }, 2.4f, 4,
                                    metal, rnd );
                    }
                    if ( q.chance( 0.5f ) )
                        b.lamp( { px * PlateSize + 12, h + 0.8f, local( pz * PlateSize + 12 ) }, 1.0f,
                                q.chance( 0.5f ) ? lampRed : lampGreen, 0.5f, q.unit() );
                }
            }
    }
    else if ( m_style == StyleE::LATTICE )
    {
        // Pillars of the megastructure on some cell middles, their tops lit.
        const float sx = HexR, sz = HexR * 1.7320508f;
        int         jz0 = (int)std::floor( g0 / sz ) - 1, jz1 = (int)std::ceil( ( g0 + ChunkLen ) / sz );
        for ( int jz = jz0; jz <= jz1; ++jz )
            for ( int half = 0; half < 2; ++half )
                for ( int ix = (int)std::floor( x0 / sx ) - 1; ix * sx < x1; ++ix )
                {
                    float cx = ( ix + 0.5f + half * 0.5f ) * sx, cg = ( jz + 0.5f ) * sz + half * sz * 0.5f;
                    if ( cg < g0 || cg >= g0 + ChunkLen ) continue;
                    if ( hashf( ix * 2 + half, jz, m_seed + 650 ) > 0.07f ) continue;
                    float h = groundHeight( cx, cg ), ph = 20 + 50 * hashf( ix, jz, m_seed + 651 );
                    Color pc = withAlpha( scaleColor( m_propColor, 1.2f ), 0 );
                    Color ring = withAlpha( hashf( ix, jz, m_seed + 652 ) < 0.6f ? m_glow2 : m_glow, 90 );
                    // A dark column with a thin lit band below its top (a lit cap would read as a bullet).
                    b.cylinder( { cx, h - 2, local( cg ) }, HexR * 0.36f, HexR * 0.28f, ph, 6, pc,
                                scaleColor( pc, 1.15f ), M_METAL, M_METAL, 0, Pi / 6 );
                    b.cylinder( { cx, h - 2 + ph - 3.0f, local( cg ) }, HexR * 0.34f, HexR * 0.34f, 2.0f, 6, ring, ring, M_LAMP, M_LAMP,
                                hashf( ix, jz, m_seed + 653 ), Pi / 6, -1.3f );
                }
    }
    else if ( m_style == StyleE::HIVE || ( m_trees > 0 && m_style == StyleE::ROCK ) )
    {
        // Polyps and spires, their tips glowing softly.
        int n = (int)( m_trees * 40 );
        for ( int i = 0; i < n; ++i )
        {
            float x = r.range( x0, x1 ), g = g0 + r.range( 0, ChunkLen );
            float rw = rawHeight( x, g );
            if ( forest( x, g, rw ) < 0.4f ) continue;
            float h = groundHeight( x, g ), s = r.range( 3, 7 ), th = s * r.range( 2.5f, 6.0f );
            Color tc = withAlpha( scaleColor( m_treeColor, r.range( 0.7f, 1.2f ) ), 0 );
            b.cylinder( { x, h - 1, local( g ) }, s, s * 0.25f, th, 6, tc, withAlpha( m_glow, 110 ), M_PLANT, M_LAMP, r.unit(), 0.0f,
                        -r.range( 1.0f, 2.2f ) );
        }
    }
    else if ( m_style == StyleE::ROCK && m_fluid == FluidE::LAVA )
    {
        // Basalt spires standing in the lava fields.
        int n = 3 + r.below( 4 );
        for ( int i = 0; i < n; ++i )
        {
            float x = r.range( x0, x1 ), g = g0 + r.range( 0, ChunkLen );
            float h = groundHeight( x, g ), s = r.range( 4, 9 );
            Color sc = withAlpha( scaleColor( m_low, r.range( 0.7f, 1.0f ) ), 0 );
            b.cylinder( { x, h - 3, local( g ) }, s, s * 0.35f, s * r.range( 2.0f, 4.5f ), 5, sc, sc, M_PLANT, M_PLANT,
                        r.unit(), r.range( 0, 6.28f ) );
        }
    }
    if ( m_crystals > 0 )
    {
        // Caves: clusters of glowing crystals and fungi on the floor, pulsing slowly (dim: they light the dark,
        // they must not look like pickups).
        int n = (int)( m_crystals * 34 );
        for ( int i = 0; i < n; ++i )
        {
            float x = r.range( x0, x1 ), g = g0 + r.range( 0, ChunkLen );
            float h = groundHeight( x, g );
            float ph = r.unit(), rate = -r.range( 0.5f, 1.3f );
            if ( r.chance( 0.5f ) )
            {
                int   cnt = 3 + r.below( 4 );
                Color cc = withAlpha( mixColor( m_crystalA, m_crystalB, r.range( 0.0f, 0.3f ) ), 70 );
                for ( int j = 0; j < cnt; ++j )
                {
                    float s = r.range( 1.6f, 3.2f );
                    float cx = x + r.range( -6, 6 ), cz = g + r.range( -6, 6 );
                    b.cylinder( { cx, h - 1, local( cz ) }, s, s * 0.15f, s * r.range( 3.0f, 6.0f ), 6, cc, cc,
                                M_LAMP, M_LAMP, ph, r.range( 0, 1.0f ), rate );
                }
            }
            else
            {
                int   cnt = 3 + r.below( 5 );
                Color fc = withAlpha( mixColor( m_crystalB, m_crystalA, r.range( 0.0f, 0.3f ) ), 55 );
                for ( int j = 0; j < cnt; ++j )
                {
                    float s = r.range( 1.5f, 3.5f );
                    b.dome( { x + r.range( -8, 8 ), h - 0.5f, local( g + r.range( -8, 8 ) ) }, s, s * 0.6f, fc, M_LAMP,
                            ph );
                    b.setUvLast( 41, rate, ph );
                }
            }
        }
    }
    if ( m_style == StyleE::SAND )
    {
        // Boulders.
        int n = 6 + r.below( 10 );
        for ( int i = 0; i < n; ++i )
        {
            float x = r.range( x0, x1 ), g = g0 + r.range( 0, ChunkLen );
            float h = groundHeight( x, g ), s = r.range( 2.5f, 6.0f );
            Color sc = withAlpha( scaleColor( m_mid, r.range( 0.55f, 0.8f ) ), 0 );
            b.dome( { x, h - 1, local( g ) }, s, s * 0.7f, sc, M_PLANT, r.unit() );
        }
    }
}

void Terrain::buildRocks( Geometry &geo, int k ) const
{
    Builder b( geo );
    Rng     r;
    r.seed( m_seed * 31u + (uint32_t)k * 7919u );
    int n = (int)( m_rocks * 46 );
    for ( int i = 0; i < n; ++i )
    {
        // Deeper rocks are seen over a wider ground: spread them over what the view sees at their depth.
        float y = r.range( -900, 60 );
        float wide = m_viewHalfW * ( CamHeight + GroundDepth - y ) / CamHeight + 60;
        float x = FieldW * 0.5f + r.range( -wide, wide ), gl = r.range( 0, ChunkLen );
        float s = r.chance( 0.08f ) ? r.range( 40, 80 ) : r.range( 6, 26 );
        Color c = mixColor( m_mid, m_high, r.unit() );
        c = scaleColor( c, r.range( 0.55f, 1.0f ) );
        b.rock( { x, y, -gl }, s, c, r.range( -0.4f, 0.4f ) * ( 30.0f / s ), m_seed + (uint32_t)i, r );
    }
}

void Terrain::upload( int k, Geometry &&geo )
{
    Chunk c;
    for ( Geometry::Part &p : geo.m_parts )
    {
        if ( c.m_parts >= MaxParts ) break;
        Mesh m = toMesh( p );
        if ( m.vertexCount == 0 ) continue;
        c.m_verts += m.vertexCount;
        c.m_mesh[c.m_parts++] = m;
    }
    auto it = m_chunks.find( k );
    if ( it != m_chunks.end() )
    {
        for ( int i = 0; i < it->second.m_parts; ++i ) UnloadMesh( it->second.m_mesh[i] );
        it->second = c;
    }
    else
        m_chunks.emplace( k, c );
}

void Terrain::ensureChunks( int kNeed0, int kNeed1, int kAhead )
{
    // A chunk the worker finished goes up first.
    if ( m_pending.valid() && m_pending.wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready )
        upload( m_pendingK, m_pending.get() );
    // What is in view must be there now. Usually the worker built it long ago; when it did not (the first frame
    // of a theme), all that is missing is built at once, on as many threads as there are chunks.
    std::vector<std::pair<int, std::future<Geometry>>> now;
    for ( int k = kNeed0; k <= kNeed1; ++k )
    {
        if ( m_chunks.count( k ) ) continue;
        if ( m_pending.valid() && m_pendingK == k )
            upload( k, m_pending.get() );
        else
            now.emplace_back( k, std::async( std::launch::async, [this, k]() { return buildChunk( k ); } ) );
    }
    for ( auto &[k, f] : now ) upload( k, f.get() );
    // Then the next one ahead, on the worker.
    if ( !m_pending.valid() )
        for ( int k = kNeed1 + 1; k <= kAhead; ++k )
            if ( !m_chunks.count( k ) )
            {
                m_pendingK = k;
                m_pending = std::async( std::launch::async, [this, k]() { return buildChunk( k ); } );
                break;
            }
    // Chunks left behind go.
    for ( auto it = m_chunks.begin(); it != m_chunks.end(); )
    {
        if ( it->first < kNeed0 - 1 )
        {
            for ( int i = 0; i < it->second.m_parts; ++i ) UnloadMesh( it->second.m_mesh[i] );
            it = m_chunks.erase( it );
        }
        else
            ++it;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Drawing.
// ---------------------------------------------------------------------------------------------------------------

void Terrain::drawSky( float time )
{
    Shader &k = m_sky;
    float   sc = (float)m_scroll;
    SetShaderValue( k, m_skyLoc[K_CAMPOS], &m_frame.m_camPos, SHADER_UNIFORM_VEC3 );
    SetShaderValue( k, m_skyLoc[K_SCROLL], &sc, SHADER_UNIFORM_FLOAT );
    SetShaderValue( k, m_skyLoc[K_TIME], &time, SHADER_UNIFORM_FLOAT );
    // One quad far below that fills the view; the shader works out every layer from where its pixels lie.
    float y = -6000.0f, s = ( CamHeight - y ) / CamHeight;
    float hw = m_viewHalfW * s * 1.3f + 400, hh = FieldH * 0.5f * s * 1.3f + 400;
    float cx = FieldW * 0.5f, cz = FieldH * 0.5f;
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginShaderMode( k );
    rlSetTexture( m_noiseTex.id );
    rlBegin( RL_QUADS );
    rlColor4ub( 255, 255, 255, 255 );
    rlTexCoord2f( 0, 0 );
    rlVertex3f( cx - hw, y, cz - hh );
    rlTexCoord2f( 0, 1 );
    rlVertex3f( cx - hw, y, cz + hh );
    rlTexCoord2f( 1, 1 );
    rlVertex3f( cx + hw, y, cz + hh );
    rlTexCoord2f( 1, 0 );
    rlVertex3f( cx + hw, y, cz - hh );
    rlEnd();
    rlSetTexture( 0 );
    rlDrawRenderBatchActive();
    EndShaderMode();
    rlEnableDepthMask();
}

void Terrain::draw( Material &mat, double scroll, float time )
{
    (void)mat;
    m_scroll = scroll;
    m_drawnVerts = m_drawnMeshes = 0;
    if ( !m_hasShaders ) return;
    if ( m_look.m_space ) drawSky( time );
    if ( m_look.m_space && m_rocks <= 0 ) return;
    // The chunks in view: the ground's view reaches FieldH / 2 / GroundFactor either side (rocks deeper further).
    float view = FieldH * 0.5f / GroundFactor + 40.0f;
    if ( m_look.m_space ) view = FieldH * 0.5f * ( CamHeight + GroundDepth + 900.0f ) / CamHeight + 60.0f;
    int k0 = (int)std::floor( ( (float)scroll - view ) / ChunkLen );
    int k1 = (int)std::floor( ( (float)scroll + view ) / ChunkLen );
    ensureChunks( k0, k1, k1 + 2 );
    setFrameUniforms( time );
    for ( int k = k0; k <= k1; ++k )
    {
        auto it = m_chunks.find( k );
        if ( it == m_chunks.end() ) continue;
        Matrix m = MatrixTranslate( 0, -GroundDepth, FieldH * 0.5f + (float)scroll - k * ChunkLen );
        for ( int i = 0; i < it->second.m_parts; ++i )
        {
            DrawMesh( it->second.m_mesh[i], m_groundMat, m );
            m_drawnVerts += it->second.m_mesh[i].vertexCount;
            ++m_drawnMeshes;
        }
    }
}

void Terrain::drawOver( double scroll, float time )
{
    if ( !m_hasCloudTex || m_clouds <= 0 ) return;
    // Layers of cloud as large textured quads that scroll with the ground at their own depth.
    struct Layer
    {
        float m_depth, m_scale, m_alpha, m_drift;
    };
    const Layer layers[2] = { { GroundDepth * 0.55f, 1100.0f, std::min( 0.95f, 0.6f * m_clouds ), 9.0f },
                              { GroundDepth * 0.25f, 1500.0f, std::min( 0.8f, 0.38f * m_clouds ), 14.0f } };
    // Lightning: now and then a flash somewhere inside the clouds.
    Vector4 flash = { 0, 0, 1, 0 };
    if ( m_lightning > 0 )
    {
        float slot = std::floor( time / 0.9f );
        for ( int s = 0; s < 2; ++s )
        {
            int   sl = (int)slot - s;
            float t = time - sl * 0.9f - 0.9f * hashf( sl, 1, m_seed + 800 );
            if ( hashf( sl, 2, m_seed + 800 ) > m_lightning * 0.4f || t < 0 || t > 0.35f ) continue;
            float env = ( t < 0.06f ? 1.0f : t < 0.12f ? 0.3f : t < 0.18f ? 0.8f : 0.0f ) + std::max( 0.0f, 0.35f - t ) * 0.6f;
            flash = { FieldW * 0.5f + ( hashf( sl, 3, m_seed + 800 ) - 0.5f ) * 1400.0f,
                      FieldH * 0.5f + ( hashf( sl, 4, m_seed + 800 ) - 0.5f ) * 1000.0f, 260.0f, env * 0.9f };
        }
    }
    const Look &L = m_look;
    Vector3     light = Vector3Add( v3( L.m_sunColor, L.m_sunPower * 0.62f ), v3( L.m_ambUp, 0.4f ) );
    Vector3     shade = Vector3Add( v3( L.m_ambUp, 0.55f ), v3( L.m_ambDown, 0.4f ) );
    light = Vector3Scale( light, L.m_groundLight * 1.35f );
    shade = Vector3Scale( shade, L.m_groundLight * 1.35f );
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginShaderMode( m_cloud );
    SetShaderValue( m_cloud, m_cloudLoc[C_LIGHT], &light, SHADER_UNIFORM_VEC3 );
    SetShaderValue( m_cloud, m_cloudLoc[C_SHADE], &shade, SHADER_UNIFORM_VEC3 );
    SetShaderValue( m_cloud, m_cloudLoc[C_FLASH], &flash, SHADER_UNIFORM_VEC4 );
    BeginBlendMode( BLEND_ALPHA );
    for ( const Layer &Ly : layers )
    {
        Vector2 sunUv = { -L.m_sunDir.x * 18.0f / Ly.m_scale, -L.m_sunDir.z * 18.0f / Ly.m_scale };
        SetShaderValue( m_cloud, m_cloudLoc[C_SUNUV], &sunUv, SHADER_UNIFORM_VEC2 );
        float y = -Ly.m_depth;
        float s = ( CamHeight + Ly.m_depth ) / CamHeight;
        float hw = m_halfW * GroundFactor * s * 1.2f, hh = FieldH * 0.5f * s * 1.3f;
        float v0 = ( -(float)scroll - time * Ly.m_drift ) / Ly.m_scale;
        float cx = FieldW * 0.5f, cz = FieldH * 0.5f;
        rlSetTexture( m_cloudTex.id );
        rlBegin( RL_QUADS );
        rlColor4ub( m_cloudColor.r, m_cloudColor.g, m_cloudColor.b,
                    (unsigned char)( clampf( Ly.m_alpha, 0, 1 ) * 255 ) );
        float u0 = ( cx - hw ) / Ly.m_scale, u1 = ( cx + hw ) / Ly.m_scale;
        float t0 = v0 + ( cz - hh ) / Ly.m_scale, t1 = v0 + ( cz + hh ) / Ly.m_scale;
        rlNormal3f( 0, 1, 0 );
        rlTexCoord2f( u0, t0 );
        rlVertex3f( cx - hw, y, cz - hh );
        rlTexCoord2f( u0, t1 );
        rlVertex3f( cx - hw, y, cz + hh );
        rlTexCoord2f( u1, t1 );
        rlVertex3f( cx + hw, y, cz + hh );
        rlTexCoord2f( u1, t0 );
        rlVertex3f( cx + hw, y, cz - hh );
        rlEnd();
        rlDrawRenderBatchActive();
        rlSetTexture( 0 );
    }
    EndBlendMode();
    EndShaderMode();
    rlEnableDepthMask();
}

}   // namespace zenith
