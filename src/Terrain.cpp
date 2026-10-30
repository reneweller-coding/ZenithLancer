/**
 * @file Terrain.cpp
 * @brief The ground and the starfield.
 * @author Rene Weller
 */
#include "Terrain.h"

#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace zenith
{

namespace
{

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

Color numColor( const Line &l, size_t i, Color def ) { return parseColor( l.arg( i ), def ); }

/** @brief Adds a flat-shaded triangle to vertex arrays. */
void addTri( std::vector<float> &pos, std::vector<float> &nrm, std::vector<unsigned char> &col, Vector3 a, Vector3 b,
             Vector3 c, Color ca, Color cb, Color cc )
{
    Vector3 n = Vector3Normalize( Vector3CrossProduct( Vector3Subtract( b, a ), Vector3Subtract( c, a ) ) );
    if ( n.y < 0 )
    {
        n = Vector3Negate( n );
        std::swap( b, c );
        std::swap( cb, cc );
    }
    const Vector3 v[3] = { a, b, c };
    const Color   k[3] = { ca, cb, cc };
    for ( int i = 0; i < 3; ++i )
    {
        pos.insert( pos.end(), { v[i].x, v[i].y, v[i].z } );
        nrm.insert( nrm.end(), { n.x, n.y, n.z } );
        col.insert( col.end(), { k[i].r, k[i].g, k[i].b, k[i].a } );
    }
}

/** @brief A box with its normals facing out. */
void addBox( std::vector<float> &pos, std::vector<float> &nrm, std::vector<unsigned char> &col, Vector3 c, Vector3 h,
             Color top, Color side, float taper = 0.0f )
{
    Vector3 v[8] = { { c.x - h.x, c.y - h.y, c.z - h.z }, { c.x + h.x, c.y - h.y, c.z - h.z },
                     { c.x + h.x, c.y - h.y, c.z + h.z }, { c.x - h.x, c.y - h.y, c.z + h.z },
                     { c.x - h.x * ( 1 - taper ), c.y + h.y, c.z - h.z * ( 1 - taper ) },
                     { c.x + h.x * ( 1 - taper ), c.y + h.y, c.z - h.z * ( 1 - taper ) },
                     { c.x + h.x * ( 1 - taper ), c.y + h.y, c.z + h.z * ( 1 - taper ) },
                     { c.x - h.x * ( 1 - taper ), c.y + h.y, c.z + h.z * ( 1 - taper ) } };
    int f[5][4] = { { 4, 7, 6, 5 }, { 0, 1, 5, 4 }, { 1, 2, 6, 5 }, { 2, 3, 7, 6 }, { 3, 0, 4, 7 } };
    for ( int i = 0; i < 5; ++i )
    {
        Color   k = i == 0 ? top : side;
        Vector3 a = v[f[i][0]], b = v[f[i][1]], cc = v[f[i][2]], d = v[f[i][3]];
        Vector3 n = Vector3Normalize( Vector3CrossProduct( Vector3Subtract( b, a ), Vector3Subtract( cc, a ) ) );
        Vector3 mid = Vector3Scale( Vector3Add( Vector3Add( a, b ), Vector3Add( cc, d ) ), 0.25f );
        if ( Vector3DotProduct( n, Vector3Subtract( mid, c ) ) < 0 )
        {
            n = Vector3Negate( n );
            std::swap( b, d );
        }
        for ( const Vector3 &q : { a, b, cc, a, cc, d } )
        {
            pos.insert( pos.end(), { q.x, q.y, q.z } );
            nrm.insert( nrm.end(), { n.x, n.y, n.z } );
            col.insert( col.end(), { k.r, k.g, k.b, k.a } );
        }
    }
}

Mesh toMesh( std::vector<float> &pos, std::vector<float> &nrm, std::vector<unsigned char> &col )
{
    Mesh m = {};
    m.vertexCount = (int)( pos.size() / 3 );
    m.triangleCount = m.vertexCount / 3;
    if ( m.vertexCount == 0 ) return m;
    m.vertices = (float *)MemAlloc( (unsigned)( pos.size() * sizeof( float ) ) );
    m.normals = (float *)MemAlloc( (unsigned)( nrm.size() * sizeof( float ) ) );
    m.colors = (unsigned char *)MemAlloc( (unsigned)col.size() );
    memcpy( m.vertices, pos.data(), pos.size() * sizeof( float ) );
    memcpy( m.normals, nrm.data(), nrm.size() * sizeof( float ) );
    memcpy( m.colors, col.data(), col.size() );
    UploadMesh( &m, false );
    return m;
}

}   // namespace

void Terrain::setup( const Catalog &cat, const std::string &theme, uint32_t seed, float halfW )
{
    unload();
    m_name = theme;
    m_seed = seed;
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
    m_glowAmount = 0.0f;
    m_city = m_trees = m_craters = m_plates = m_rocks = m_ridge = 0.0f;
    m_propColor = { 120, 120, 130, 255 };
    m_treeColor = { 40, 90, 50, 255 };
    m_cloudColor = { 255, 255, 255, 255 };
    m_clouds = 0.0f;
    m_nebulaA = { 60, 30, 120, 255 };
    m_nebulaB = { 20, 80, 140, 255 };
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
        else if ( k == "sun" ) m_look.m_sunDir = Vector3Normalize( { l.argNum( 0, -0.35f ), l.argNum( 1, -0.85f ), l.argNum( 2, -0.4f ) } );
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
        }
    }
    if ( m_look.m_space ) buildSpace();
    if ( m_clouds > 0 || m_look.m_space )
    {
        // A soft noise picture for the clouds (and the nebula in space).
        const int W = 256;
        Image     img = GenImageColor( W, W, BLANK );
        Color    *px = (Color *)img.data;
        for ( int y = 0; y < W; ++y )
            for ( int x = 0; x < W; ++x )
            {
                // Tileable: the noise sampled on a torus.
                float u = x / (float)W * 2 * Pi, v = y / (float)W * 2 * Pi;
                float n = fbm( 3 + std::cos( u ) * 1.6f, 3 + std::sin( u ) * 1.6f + std::cos( v ) * 1.6f, seed + 9, 5 ) +
                          0.5f * fbm( std::sin( v ) * 2.0f + 11, std::cos( v ) * 2.0f + std::sin( u ), seed + 3, 4 );
                float a = clampf( ( n + 0.05f ) * 1.8f, 0.0f, 1.0f );
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
}

void Terrain::unload()
{
    for ( auto &[k, m] : m_chunks ) UnloadMesh( m );
    m_chunks.clear();
    for ( Mesh &m : m_stars ) UnloadMesh( m );
    m_stars.clear();
    m_starDepth.clear();
    if ( m_hasCloudTex ) UnloadTexture( m_cloudTex );
    m_hasCloudTex = false;
}

float Terrain::groundHeight( float x, float g ) const
{
    float s = m_scale;
    float n = fbm( x * s, g * s, m_seed, 5 );
    if ( m_ridge > 0 ) n = lerpf( n, 0.6f - std::fabs( n ) * 1.6f, m_ridge );
    float h = n * m_relief;
    if ( m_plates > 0 )
    {
        // Station plating: flat decks at a few levels, with trenches between the plates.
        float q = std::floor( ( n * 0.5f + 0.5f ) * 4.0f ) / 4.0f;
        h = ( q - 0.5f ) * m_relief * 0.9f;
        float px = std::fmod( std::fabs( x ) + 4000.0f, 160.0f ), pz = std::fmod( std::fabs( g ) + 4000.0f, 160.0f );
        if ( px < 10 || pz < 10 ) h -= m_relief * 0.25f;
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
    if ( m_water > -1e8f ) h = std::max( h, m_water * m_relief );
    return h;
}

Color Terrain::groundColor( float x, float g, float h, float slope ) const
{
    float t = clampf( h / std::max( 1.0f, m_relief ) * 0.5f + 0.5f, 0.0f, 1.0f );
    Color c = t < 0.33f ? mixColor( m_low, m_mid, t / 0.33f ) : t < 0.7f ? mixColor( m_mid, m_high, ( t - 0.33f ) / 0.37f )
                                                                          : mixColor( m_high, m_peak, ( t - 0.7f ) / 0.3f );
    float n = fbm( x * 0.03f, g * 0.03f, m_seed + 5, 2 ) * 0.12f;
    c = scaleColor( c, 1.0f + n - slope * 0.25f );
    c.a = 0;
    if ( m_water > -1e8f && h <= m_water * m_relief + 0.01f )
    {
        c = m_waterColor;
        float w = fbm( x * 0.02f, g * 0.02f, m_seed + 31, 2 );
        c = scaleColor( c, 1.0f + w * 0.2f );
        c.a = (unsigned char)( clampf( m_waterGlow * ( 0.7f + w ), 0.0f, 1.0f ) * 255 );
    }
    else if ( m_glowAmount > 0 )
    {
        // Glowing veins: lava cracks, lattice lines.
        float v = std::fabs( fbm( x * 0.006f, g * 0.006f, m_seed + 50, 2 ) );
        bool  trench = false;
        if ( m_plates > 0 )
        {
            // Station plating: the trenches between the plates carry the light strips.
            float px = std::fmod( std::fabs( x ) + 4000.0f, 160.0f ), pz = std::fmod( std::fabs( g ) + 4000.0f, 160.0f );
            trench = ( px < 10 || pz < 10 ) && hashf( (int)( x / 160 ), (int)( g / 160 ), m_seed + 51 ) < m_glowAmount;
        }
        if ( trench || ( m_plates <= 0 && v < 0.012f * m_glowAmount ) )
        {
            c = m_glow;
            c.a = 200;
        }
    }
    return c;
}

Mesh Terrain::buildChunk( int k )
{
    std::vector<float>         pos, nrm;
    std::vector<unsigned char> col;
    const float                cell = 22.0f;
    int                        nx = (int)( m_halfW * 2 / cell ) + 1, nz = (int)( ChunkLen / cell ) + 1;
    float                      cz = ChunkLen / ( nz - 1 );
    float                      x0 = FieldW * 0.5f - m_halfW;
    Rng                        r;
    r.seed( m_seed * 31u + (uint32_t)k * 7919u );
    if ( !m_look.m_space )
    {
        std::vector<float> hs( (size_t)( nx + 1 ) * ( nz + 1 ) );
        for ( int j = 0; j <= nz; ++j )
            for ( int i = 0; i <= nx; ++i )
            {
                float x = x0 + i * cell, g = k * ChunkLen + j * cz;
                hs[(size_t)j * ( nx + 1 ) + i] = groundHeight( x, g );
            }
        auto H = [&]( int i, int j ) { return hs[(size_t)j * ( nx + 1 ) + i]; };
        for ( int j = 0; j < nz; ++j )
            for ( int i = 0; i < nx; ++i )
            {
                float   xa = x0 + i * cell, xb = xa + cell;
                float   ga = j * cz, gb = ga + cz;
                Vector3 a = { xa, H( i, j ), -ga }, b = { xb, H( i + 1, j ), -ga }, c = { xb, H( i + 1, j + 1 ), -gb },
                        d = { xa, H( i, j + 1 ), -gb };
                float   slope = clampf( ( std::fabs( a.y - c.y ) + std::fabs( b.y - d.y ) ) / ( cell * 2 ), 0, 1 );
                float   gm = k * ChunkLen + ( ga + gb ) * 0.5f, xm = ( xa + xb ) * 0.5f;
                Color   c1 = groundColor( xm - cell * 0.2f, gm, ( a.y + b.y + c.y ) / 3, slope );
                Color   c2 = groundColor( xm + cell * 0.2f, gm + cz * 0.2f, ( a.y + c.y + d.y ) / 3, slope );
                addTri( pos, nrm, col, a, b, c, c1, c1, c1 );
                addTri( pos, nrm, col, a, c, d, c2, c2, c2 );
            }
        // What stands on it.
        int cellsX = (int)( m_halfW * 2 / 60.0f ), cellsZ = (int)( ChunkLen / 60.0f );
        for ( int j = 0; j < cellsZ; ++j )
            for ( int i = 0; i < cellsX; ++i )
            {
                float x = x0 + ( i + 0.5f ) * 60.0f + r.range( -14, 14 ), gl = ( j + 0.5f ) * 60.0f + r.range( -14, 14 );
                float g = k * ChunkLen + gl;
                float h = groundHeight( x, g );
                bool  wet = m_water > -1e8f && h <= m_water * m_relief + 0.5f;
                float u = r.unit();
                // Cities cluster: their density follows a slow noise.
                float cityHere = m_city * clampf( fbm( x * 0.0025f, g * 0.0025f, m_seed + 400, 2 ) * 2.5f + 0.4f, 0, 1.4f );
                if ( !wet && u < cityHere )
                {
                    float   bw = r.range( 12, 26 ), bd = r.range( 12, 26 ), bh = r.range( 10, 60 ) * ( 0.6f + cityHere );
                    Color   side = scaleColor( m_propColor, r.range( 0.7f, 1.1f ) );
                    side.a = 0;
                    Color   top = scaleColor( side, 1.15f );
                    top.a = 0;
                    addBox( pos, nrm, col, { x, h + bh * 0.5f, -gl }, { bw * 0.5f, bh * 0.5f, bd * 0.5f }, top, side, r.range( 0, 0.25f ) );
                    if ( r.chance( 0.6f ) )
                    {
                        Color lamp = r.chance( 0.5f ) ? Color{ 255, 210, 140, 230 } : Color{ 140, 220, 255, 230 };
                        addBox( pos, nrm, col, { x, h + bh + 1.5f, -gl }, { 2.5f, 1.5f, 2.5f }, lamp, lamp );
                    }
                }
                else if ( !wet && r.unit() < m_trees )
                {
                    int n = 1 + r.below( 4 );
                    for ( int t = 0; t < n; ++t )
                    {
                        float tx = x + r.range( -20, 20 ), tz = gl + r.range( -20, 20 );
                        float th = groundHeight( tx, k * ChunkLen + tz );
                        float s = r.range( 7, 15 );
                        Color tc = scaleColor( m_treeColor, r.range( 0.75f, 1.2f ) );
                        tc.a = 0;
                        addBox( pos, nrm, col, { tx, th + s * 0.9f, -tz }, { s * 0.6f, s * 0.9f, s * 0.6f }, tc, scaleColor( tc, 0.8f ), 0.85f );
                    }
                }
                else if ( m_plates > 0 && r.unit() < 0.08f )
                {
                    // Station hardware: vents, antenna blocks, a light.
                    Color side = scaleColor( m_propColor, r.range( 0.8f, 1.1f ) );
                    side.a = 0;
                    addBox( pos, nrm, col, { x, h + 6, -gl }, { r.range( 6, 16 ), 6, r.range( 6, 16 ) }, side, side );
                    Color lamp = { 120, 220, 255, 255 };
                    if ( r.chance( 0.5f ) ) addBox( pos, nrm, col, { x, h + 13, -gl }, { 1.6f, 1.2f, 1.6f }, lamp, lamp );
                }
            }
    }
    else if ( m_rocks > 0 )
    {
        // Drifting rocks at many depths.
        int n = (int)( m_rocks * 30 );
        for ( int i = 0; i < n; ++i )
        {
            float   x = x0 + r.range( 0, m_halfW * 2 ), gl = r.range( 0, ChunkLen ), y = r.range( -900, 120 );
            float   s = r.range( 8, 40 );
            Color   c = scaleColor( m_mid, r.range( 0.6f, 1.2f ) );
            c.a = 0;
            // A crumpled box makes a fine rock from above.
            addBox( pos, nrm, col, { x, y, -gl }, { s, s * 0.7f, s * r.range( 0.7f, 1.3f ) }, scaleColor( c, 1.1f ), c, r.range( 0.2f, 0.6f ) );
        }
    }
    return toMesh( pos, nrm, col );
}

void Terrain::buildSpace()
{
    const float depths[3] = { 600.0f, 1800.0f, 4200.0f };
    for ( int layer = 0; layer < 3; ++layer )
    {
        std::vector<float>         pos, nrm;
        std::vector<unsigned char> col;
        Rng                        r;
        r.seed( m_seed + 1000u * layer );
        float depth = depths[layer];
        float scale = ( CamHeight + depth ) / CamHeight;
        float halfW = m_halfW * GroundFactor * scale + 200, len = 4000.0f;
        int   n = layer == 0 ? 180 : layer == 1 ? 420 : 900;
        for ( int i = 0; i < n; ++i )
        {
            float   x = FieldW * 0.5f + r.range( -halfW, halfW ), z = r.range( 0, len );
            float   s = r.range( 0.8f, 2.4f ) * scale * ( layer == 0 ? 1.4f : 1.0f );
            float   b = r.range( 0.35f, 1.0f );
            Color   c = r.chance( 0.15f ) ? Color{ 255, 200, 160, 255 } : r.chance( 0.2f ) ? Color{ 170, 200, 255, 255 } : Color{ 240, 240, 255, 255 };
            c = scaleColor( c, b );
            c.a = 255;
            Vector3 a = { x - s, 0, -z - s }, bb = { x + s, 0, -z - s }, cc = { x + s, 0, -z + s }, d = { x - s, 0, -z + s };
            addTri( pos, nrm, col, a, bb, cc, c, c, c );
            addTri( pos, nrm, col, a, cc, d, c, c, c );
        }
        m_stars.push_back( toMesh( pos, nrm, col ) );
        m_starDepth.push_back( depth );
    }
}

float Terrain::heightAt( float x, float z, double scroll ) const
{
    if ( m_look.m_space ) return -GroundDepth;
    float g = FieldH * 0.5f + (float)scroll - z;
    return -GroundDepth + groundHeight( x, g );
}

void Terrain::draw( Material &mat, double scroll, float time )
{
    (void)time;
    if ( m_look.m_space )
        for ( size_t l = 0; l < m_stars.size(); ++l )
        {
            float len = 4000.0f;
            float off = std::fmod( (float)scroll, len );
            for ( int rep = -1; rep <= 1; ++rep )
            {
                Matrix m = MatrixTranslate( 0, -m_starDepth[l], FieldH * 0.5f + off + rep * len - len * 0.5f + 2000.0f );
                DrawMesh( m_stars[l], mat, m );
            }
        }
    // The chunks in view (built when first needed, dropped when passed).
    float view = FieldH * 0.5f / GroundFactor;
    int   k0 = (int)std::floor( ( (float)scroll - view - 300.0f ) / ChunkLen );
    int   k1 = (int)std::floor( ( (float)scroll + view + 500.0f ) / ChunkLen );
    if ( !m_look.m_space || m_rocks > 0 )
    {
        for ( int k = k0; k <= k1; ++k )
        {
            auto it = m_chunks.find( k );
            if ( it == m_chunks.end() ) it = m_chunks.emplace( k, buildChunk( k ) ).first;
            if ( it->second.vertexCount == 0 ) continue;
            Matrix m = MatrixTranslate( 0, -GroundDepth, FieldH * 0.5f + (float)scroll - k * ChunkLen );
            DrawMesh( it->second, mat, m );
        }
        for ( auto it = m_chunks.begin(); it != m_chunks.end(); )
        {
            if ( it->first < k0 - 1 )
            {
                UnloadMesh( it->second );
                it = m_chunks.erase( it );
            }
            else
                ++it;
        }
    }
}

void Terrain::drawOver( double scroll, float time )
{
    if ( !m_hasCloudTex ) return;
    // Layers of cloud (or nebula) as large textured quads that scroll with the ground at their own depth.
    struct Layer
    {
        float m_depth, m_scale, m_alpha, m_drift;
        Color m_color;
    };
    std::vector<Layer> layers;
    if ( m_look.m_space )
    {
        layers.push_back( { 3000.0f, 2600.0f, 0.55f, 0.0f, m_nebulaA } );
        layers.push_back( { 2200.0f, 1900.0f, 0.35f, 0.0f, m_nebulaB } );
    }
    if ( m_clouds > 0 )
    {
        layers.push_back( { GroundDepth * 0.55f, 1100.0f, 0.55f * m_clouds, 9.0f, m_cloudColor } );
        layers.push_back( { GroundDepth * 0.25f, 1500.0f, 0.35f * m_clouds, 14.0f, m_cloudColor } );
    }
    rlDisableDepthMask();
    BeginBlendMode( BLEND_ALPHA );
    for ( const Layer &L : layers )
    {
        float y = -L.m_depth;
        float s = ( CamHeight + L.m_depth ) / CamHeight;
        float hw = m_halfW * GroundFactor * s * 1.2f, hh = FieldH * 0.5f * s * 1.3f;
        float v0 = ( -(float)scroll - time * L.m_drift ) / L.m_scale;
        float cx = FieldW * 0.5f, cz = FieldH * 0.5f;
        rlSetTexture( m_cloudTex.id );
        rlBegin( RL_QUADS );
        rlColor4ub( L.m_color.r, L.m_color.g, L.m_color.b, (unsigned char)( clampf( L.m_alpha, 0, 1 ) * 255 ) );
        float u0 = ( cx - hw ) / L.m_scale, u1 = ( cx + hw ) / L.m_scale;
        float t0 = v0 + ( cz - hh ) / L.m_scale, t1 = v0 + ( cz + hh ) / L.m_scale;
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
        rlSetTexture( 0 );
    }
    EndBlendMode();
    rlEnableDepthMask();
}

}   // namespace zenith
