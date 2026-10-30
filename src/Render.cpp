/**
 * @file Render.cpp
 * @brief The renderer.
 * @author Rene Weller
 */
#include "Render.h"

#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>

namespace zenith
{

Forge            Renderer::s_forge;
RenderTexture2D *Renderer::s_outer = nullptr;

namespace
{

constexpr int MaxLights = 16;   // the lit shaders' light arrays (Renderer::MaxGlowLights fills them)

const char *LitVs = R"(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 fragPos;
out vec3 fragNormal;
out vec4 fragColor;
void main()
{
    fragPos = ( matModel * vec4( vertexPosition, 1.0 ) ).xyz;
    fragNormal = normalize( ( matNormal * vec4( vertexNormal, 0.0 ) ).xyz );
    fragColor = vertexColor;
    gl_Position = mvp * vec4( vertexPosition, 1.0 );
}
)";

const char *LitFs = R"(#version 330
in vec3 fragPos;
in vec3 fragNormal;
in vec4 fragColor;
uniform vec3 sunDir;
uniform vec3 sunColor;
uniform vec3 ambUp;
uniform vec3 ambDown;
uniform vec3 camPos;
uniform int lightCount;
uniform vec4 lightPos[16];
uniform vec4 lightCol[16];
uniform float flash;
uniform vec4 tint;
uniform vec4 fogColor;
uniform float emitBoost;
uniform float time;
uniform float specular;
out vec4 finalColor;
void main()
{
    vec3 n = normalize( fragNormal );
    vec3 albedo = fragColor.rgb * tint.rgb;
    float em = fragColor.a;
    vec3 v = normalize( camPos - fragPos );
    vec3 l = -sunDir;
    float ndl = dot( n, l );
    // The light wraps a little round the hull: a softer terminator on the faceted shapes.
    float wrap = max( ( ndl + 0.25 ) / 1.25, 0.0 );
    vec3 h = normalize( l + v );
    float nh = max( dot( n, h ), 0.0 );
    float nv = max( dot( n, v ), 0.0 );
    // Metal: a tight highlight on a broad sheen; the sky mirrored in the hull, strongest at grazing angles.
    float spec = ( pow( nh, 64.0 ) * 1.5 + pow( nh, 12.0 ) * 0.2 ) * specular;
    float fres = 0.05 + 0.95 * pow( 1.0 - nv, 5.0 );
    vec3 r = reflect( -v, n );
    vec3 env = mix( ambDown * 0.6, ambUp * 1.4 + sunColor * 0.12, smoothstep( -0.4, 0.7, r.y ) );
    vec3 amb = mix( ambDown, ambUp, n.y * 0.5 + 0.5 );
    vec3 col = albedo * ( amb + sunColor * wrap * 0.92 ) + sunColor * spec * smoothstep( 0.0, 0.3, ndl );
    col += env * fres * specular * 0.8;
    for ( int i = 0; i < lightCount; ++i )
    {
        vec3 L = lightPos[i].xyz - fragPos;
        float d = length( L );
        float att = clamp( 1.0 - d / lightPos[i].w, 0.0, 1.0 );
        att *= att;
        vec3 Ld = L / max( d, 0.001 );
        float nl = max( dot( n, Ld ), 0.0 ) * 0.85 + 0.15;
        col += albedo * lightCol[i].rgb * lightCol[i].a * nl * att;
    }
    // A rim along the hull's edges, brighter on the side away from the sun (it separates ships from the ground).
    float rim = pow( 1.0 - nv, 3.0 );
    col += rim * ( ambUp * 0.3 + sunColor * 0.12 ) * ( 0.6 + 0.6 * max( dot( n.xz, -l.xz ), 0.0 ) );
    // What is only lit (not glowing) stays below the bloom: a soft shoulder from 0.75, at most about 0.95, so
    // white hulls keep their shape instead of blooming into a blot. Lamps, engines and crystals still glow.
    float peak = max( col.r, max( col.g, col.b ) );
    if ( peak > 0.75 ) col *= ( 0.75 + 0.2 * ( 1.0 - exp( -( peak - 0.75 ) * 3.0 ) ) ) / peak;
    col += fragColor.rgb * em * emitBoost * ( 0.9 + 0.1 * sin( time * 6.0 + fragPos.x * 0.05 ) );
    col = mix( col, vec3( 2.2, 2.2, 2.4 ), flash );
    float depth = max( 0.0, -fragPos.y - 40.0 );
    col = mix( col, fogColor.rgb, 1.0 - exp( -depth * fogColor.a ) );
    finalColor = vec4( col, 1.0 );
}
)";

// What flies, cast along the sun onto the ground's plane: white where a ship's shadow falls (the ground reads it).
const char *ShadowVs = R"(#version 330
in vec3 vertexPosition;
uniform mat4 matModel;
uniform vec3 sunDir;
uniform float planeY;
uniform vec4 rect;
void main()
{
    vec3 p = ( matModel * vec4( vertexPosition, 1.0 ) ).xyz;
    vec2 q = p.xz + sunDir.xz * ( ( planeY - p.y ) / sunDir.y );
    gl_Position = vec4( ( q - rect.xy ) / rect.zw * 2.0 - 1.0, 0.0, 1.0 );
}
)";

const char *ShadowFs = R"(#version 330
out vec4 finalColor;
void main()
{
    finalColor = vec4( 1.0 );
}
)";

const char *GlowFs = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform float boost;
out vec4 finalColor;
void main()
{
    vec4 t = texture( texture0, fragTexCoord );
    float a = t.a * fragColor.a;
    finalColor = vec4( t.rgb * fragColor.rgb * boost * a, a );
}
)";

const char *PrefilterFs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 texel;
uniform float threshold;
out vec4 finalColor;
void main()
{
    vec3 c = texture( texture0, fragTexCoord + texel * vec2( -1, -1 ) ).rgb + texture( texture0, fragTexCoord + texel * vec2( 1, -1 ) ).rgb
           + texture( texture0, fragTexCoord + texel * vec2( -1, 1 ) ).rgb + texture( texture0, fragTexCoord + texel * vec2( 1, 1 ) ).rgb;
    c *= 0.25;
    float b = max( c.r, max( c.g, c.b ) );
    float knee = 0.5;
    float soft = clamp( b - threshold + knee, 0.0, 2.0 * knee );
    soft = soft * soft / ( 4.0 * knee + 1e-4 );
    float w = max( soft, b - threshold ) / max( b, 1e-4 );
    finalColor = vec4( min( c * w, vec3( 40.0 ) ), 1.0 );
}
)";

const char *DownFs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 texel;
out vec4 finalColor;
void main()
{
    vec3 c = texture( texture0, fragTexCoord ).rgb * 4.0;
    c += texture( texture0, fragTexCoord + texel * vec2( -1, -1 ) ).rgb;
    c += texture( texture0, fragTexCoord + texel * vec2( 1, -1 ) ).rgb;
    c += texture( texture0, fragTexCoord + texel * vec2( -1, 1 ) ).rgb;
    c += texture( texture0, fragTexCoord + texel * vec2( 1, 1 ) ).rgb;
    finalColor = vec4( c / 8.0, 1.0 );
}
)";

const char *UpFs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 texel;
out vec4 finalColor;
void main()
{
    vec3 c = vec3( 0.0 );
    c += texture( texture0, fragTexCoord + texel * vec2( -1, 0 ) ).rgb * 2.0;
    c += texture( texture0, fragTexCoord + texel * vec2( 1, 0 ) ).rgb * 2.0;
    c += texture( texture0, fragTexCoord + texel * vec2( 0, -1 ) ).rgb * 2.0;
    c += texture( texture0, fragTexCoord + texel * vec2( 0, 1 ) ).rgb * 2.0;
    c += texture( texture0, fragTexCoord + texel * vec2( -1, -1 ) ).rgb;
    c += texture( texture0, fragTexCoord + texel * vec2( 1, -1 ) ).rgb;
    c += texture( texture0, fragTexCoord + texel * vec2( -1, 1 ) ).rgb;
    c += texture( texture0, fragTexCoord + texel * vec2( 1, 1 ) ).rgb;
    finalColor = vec4( c / 12.0 * 0.85, 1.0 );
}
)";

const char *FinalFs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform sampler2D bloomTex;
uniform float exposure;
uniform float bloom;
uniform float hurt;
uniform float fade;
uniform vec4 field;
out vec4 finalColor;
vec3 aces( vec3 x )
{
    return clamp( ( x * ( 2.51 * x + 0.03 ) ) / ( x * ( 2.43 * x + 0.59 ) + 0.14 ), 0.0, 1.0 );
}
void main()
{
    vec2 uv = fragTexCoord;
    vec3 c = texture( texture0, uv ).rgb * exposure + texture( bloomTex, uv ).rgb * bloom;
    c = aces( c * 1.05 );
    // Outside the field the world goes on, dimmed and greyed.
    float inside = step( field.x, uv.x ) * step( uv.x, field.z );
    float edge = min( abs( uv.x - field.x ), abs( uv.x - field.z ) );
    float dim = mix( 0.38, 1.0, inside );
    vec3 grey = vec3( dot( c, vec3( 0.3, 0.55, 0.15 ) ) );
    c = mix( mix( grey, c, 0.45 ), c, inside ) * dim;
    c += vec3( 0.25, 0.45, 0.6 ) * smoothstep( 0.004, 0.0, edge ) * 0.6;
    vec2 q = uv - 0.5;
    float vig = 1.0 - dot( q, q ) * 0.55;
    c *= vig;
    float r = smoothstep( 0.25, 0.75, length( q ) ) * hurt;
    c = mix( c, vec3( 0.9, 0.05, 0.05 ), r * 0.6 );
    c *= 1.0 - fade;
    finalColor = vec4( c, 1.0 );
}
)";

/** @brief The shared things. */
struct Shared
{
    int       m_users = 0;
    Shader    m_lit = {}, m_glow = {}, m_prefilter = {}, m_down = {}, m_up = {}, m_final = {}, m_shadow = {};
    Material  m_mat = {}, m_shadowMat = {};
    int       m_locShSun = 0, m_locShPlane = 0, m_locShRect = 0;
    Texture2D m_tex[9] = {};
    int       m_locSun = 0, m_locSunCol = 0, m_locAmbUp = 0, m_locAmbDown = 0, m_locCam = 0, m_locCount = 0,
              m_locLightPos = 0, m_locLightCol = 0, m_locFlash = 0, m_locTint = 0, m_locFog = 0, m_locEmit = 0,
              m_locTime = 0, m_locBoost = 0, m_locSpec = 0;
};
Shared g;

enum TexE : int
{
    T_SOFT, T_CORE, T_RING, T_STREAK, T_SPARK, T_SMOKE, T_SHARD, T_HEX, T_FLARE
};

Texture2D makeTex( int size, float ( *f )( float x, float y, Rng &r ), uint32_t seed )
{
    Image  img = GenImageColor( size, size, BLANK );
    Color *px = (Color *)img.data;
    Rng    r;
    r.seed( seed );
    for ( int y = 0; y < size; ++y )
        for ( int x = 0; x < size; ++x )
        {
            float u = ( x + 0.5f ) / size * 2 - 1, v = ( y + 0.5f ) / size * 2 - 1;
            float a = clampf( f( u, v, r ), 0, 1 );
            px[y * size + x] = { 255, 255, 255, (unsigned char)( a * 255 ) };
        }
    Texture2D t = LoadTextureFromImage( img );
    GenTextureMipmaps( &t );
    SetTextureFilter( t, TEXTURE_FILTER_TRILINEAR );
    UnloadImage( img );
    return t;
}

float noise2( float x, float y )
{
    auto h = []( int i, int j ) {
        uint32_t k = (uint32_t)i * 374761393u + (uint32_t)j * 668265263u;
        k = ( k ^ ( k >> 13 ) ) * 1274126177u;
        return ( ( k ^ ( k >> 16 ) ) & 0xffff ) / 65535.0f;
    };
    int   ix = (int)std::floor( x ), iy = (int)std::floor( y );
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * ( 3 - 2 * fx );
    fy = fy * fy * ( 3 - 2 * fy );
    return lerpf( lerpf( h( ix, iy ), h( ix + 1, iy ), fx ), lerpf( h( ix, iy + 1 ), h( ix + 1, iy + 1 ), fx ), fy );
}

void makeShared()
{
    g.m_lit = LoadShaderFromMemory( LitVs, LitFs );
    g.m_glow = LoadShaderFromMemory( nullptr, GlowFs );
    g.m_prefilter = LoadShaderFromMemory( nullptr, PrefilterFs );
    g.m_down = LoadShaderFromMemory( nullptr, DownFs );
    g.m_up = LoadShaderFromMemory( nullptr, UpFs );
    g.m_final = LoadShaderFromMemory( nullptr, FinalFs );
    g.m_mat = LoadMaterialDefault();
    g.m_mat.shader = g.m_lit;
    g.m_shadow = LoadShaderFromMemory( ShadowVs, ShadowFs );
    g.m_shadowMat = LoadMaterialDefault();
    g.m_shadowMat.shader = g.m_shadow;
    g.m_locShSun = GetShaderLocation( g.m_shadow, "sunDir" );
    g.m_locShPlane = GetShaderLocation( g.m_shadow, "planeY" );
    g.m_locShRect = GetShaderLocation( g.m_shadow, "rect" );
    Shader &s = g.m_lit;
    g.m_locSun = GetShaderLocation( s, "sunDir" );
    g.m_locSunCol = GetShaderLocation( s, "sunColor" );
    g.m_locAmbUp = GetShaderLocation( s, "ambUp" );
    g.m_locAmbDown = GetShaderLocation( s, "ambDown" );
    g.m_locCam = GetShaderLocation( s, "camPos" );
    g.m_locCount = GetShaderLocation( s, "lightCount" );
    g.m_locLightPos = GetShaderLocation( s, "lightPos" );
    g.m_locLightCol = GetShaderLocation( s, "lightCol" );
    g.m_locFlash = GetShaderLocation( s, "flash" );
    g.m_locTint = GetShaderLocation( s, "tint" );
    g.m_locFog = GetShaderLocation( s, "fogColor" );
    g.m_locEmit = GetShaderLocation( s, "emitBoost" );
    g.m_locTime = GetShaderLocation( s, "time" );
    g.m_locSpec = GetShaderLocation( s, "specular" );
    g.m_locBoost = GetShaderLocation( g.m_glow, "boost" );
    g.m_tex[T_SOFT] = makeTex( 64, []( float x, float y, Rng & ) { return std::exp( -( x * x + y * y ) * 4.5f ); }, 1 );
    g.m_tex[T_CORE] = makeTex( 64, []( float x, float y, Rng & ) {
        float d = std::sqrt( x * x + y * y );
        return clampf( ( 0.62f - d ) * 9.0f, 0, 1 ) + std::exp( -d * d * 9 ) * 0.4f;
    }, 2 );
    g.m_tex[T_RING] = makeTex( 128, []( float x, float y, Rng & ) {
        float d = std::sqrt( x * x + y * y );
        return std::exp( -( d - 0.8f ) * ( d - 0.8f ) * 180.0f );
    }, 3 );
    g.m_tex[T_STREAK] = makeTex( 64, []( float x, float y, Rng & ) {
        return std::exp( -x * x * 2.2f - y * y * 22.0f );
    }, 4 );
    g.m_tex[T_SPARK] = makeTex( 64, []( float x, float y, Rng & ) {
        float d = std::sqrt( x * x + y * y );
        float star = std::exp( -std::fabs( x ) * 22 ) * std::exp( -std::fabs( y ) * 2.5f ) + std::exp( -std::fabs( y ) * 22 ) * std::exp( -std::fabs( x ) * 2.5f );
        return star + std::exp( -d * d * 18 );
    }, 5 );
    g.m_tex[T_SMOKE] = makeTex( 128, []( float x, float y, Rng & ) {
        float d = std::sqrt( x * x + y * y );
        float n = noise2( x * 3 + 7, y * 3 + 3 ) * 0.6f + noise2( x * 7 + 1, y * 7 + 9 ) * 0.4f;
        return clampf( ( 1.0f - d ) * 1.6f, 0, 1 ) * ( 0.45f + 0.75f * n );
    }, 6 );
    g.m_tex[T_SHARD] = makeTex( 64, []( float x, float y, Rng & ) {
        float d = std::fabs( x ) * 1.8f + std::fabs( y );
        return clampf( ( 0.9f - d ) * 6.0f, 0, 1 );
    }, 7 );
    g.m_tex[T_HEX] = makeTex( 128, []( float x, float y, Rng & ) {
        float a = std::atan2( y, x ), d = std::sqrt( x * x + y * y );
        float hexR = 0.85f * std::cos( Pi / 6 ) / std::cos( std::fmod( a + Pi * 2, Pi / 3 ) - Pi / 6 );
        float edge = std::exp( -( d - hexR ) * ( d - hexR ) * 400.0f );
        return edge + clampf( hexR - d, 0, 1 ) * 0.25f;
    }, 8 );
    g.m_tex[T_FLARE] = makeTex( 128, []( float x, float y, Rng & ) {
        float d = std::sqrt( x * x + y * y );
        float rays = std::exp( -std::fabs( y ) * 40 ) * ( 1 - std::fabs( x ) ) + std::exp( -std::fabs( x ) * 40 ) * ( 1 - std::fabs( y ) ) * 0.5f;
        return std::exp( -d * d * 30 ) + rays * 0.8f;
    }, 9 );
}

RenderTexture2D hdrTarget( int w, int h, bool depth )
{
    RenderTexture2D t = {};
    t.id = rlLoadFramebuffer();
    rlEnableFramebuffer( t.id );
    t.texture.id = rlLoadTexture( nullptr, w, h, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1 );
    t.texture.width = w;
    t.texture.height = h;
    t.texture.format = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
    t.texture.mipmaps = 1;
    rlFramebufferAttach( t.id, t.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0 );
    if ( depth )
    {
        t.depth.id = rlLoadTextureDepth( w, h, true );
        t.depth.width = w;
        t.depth.height = h;
        rlFramebufferAttach( t.id, t.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0 );
    }
    if ( !rlFramebufferComplete( t.id ) ) TraceLog( LOG_ERROR, "GFX: HDR target %dx%d incomplete", w, h );
    rlDisableFramebuffer();
    rlTextureParameters( t.texture.id, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_LINEAR );
    rlTextureParameters( t.texture.id, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR );
    rlTextureParameters( t.texture.id, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP );
    rlTextureParameters( t.texture.id, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP );
    return t;
}

void fullscreen( Shader s, Texture2D src, int w, int h )
{
    BeginShaderMode( s );
    DrawTexturePro( src, { 0, 0, (float)src.width, (float)-src.height }, { 0, 0, (float)w, (float)h }, { 0, 0 }, 0, WHITE );
    EndShaderMode();
}

Vector3 v3( Color c, float s = 1.0f ) { return { c.r / 255.0f * s, c.g / 255.0f * s, c.b / 255.0f * s }; }

}   // namespace


void Renderer::init( int w, int h, float scale )
{
    if ( g.m_users++ == 0 ) makeShared();
    // Drawn smaller when the graphics setting asks (the Steam Deck), and scaled up on present().
    scale = clampf( scale, 0.4f, 1.0f );
    m_w = std::max( 64, (int)std::lround( w * scale ) );
    m_h = std::max( 64, (int)std::lround( h * scale ) );
    m_up = w / (float)m_w;
    w = m_w;
    h = m_h;
    m_hdr = hdrTarget( w, h, true );
    m_out = LoadRenderTexture( w, h );
    SetTextureFilter( m_out.texture, TEXTURE_FILTER_BILINEAR );
    // The shadows need little detail: the ground softens them anyway.
    m_shadow = LoadRenderTexture( 512, 512 );
    SetTextureFilter( m_shadow.texture, TEXTURE_FILTER_BILINEAR );
    SetTextureWrap( m_shadow.texture, TEXTURE_WRAP_CLAMP );
    m_lightOrder.reserve( 64 );
    int bw = w / 2, bh = h / 2;
    for ( int i = 0; i < ( m_quality == 0 ? 4 : 6 ) && bw >= 4 && bh >= 4; ++i )
    {
        m_bloom.push_back( hdrTarget( bw, bh, false ) );
        bw /= 2;
        bh /= 2;
    }
    float aspect = w / (float)h;
    m_halfH = std::max( FieldH * 0.5f, FieldW * 0.5f / aspect ) * 1.0f;
    m_center = { FieldW * 0.5f, FieldH * 0.5f };
    m_scale = h / ( 2.0f * m_halfH );
    m_cam.position = { FieldW * 0.5f, CamHeight, FieldH * 0.5f };
    m_cam.target = { FieldW * 0.5f, 0, FieldH * 0.5f };
    m_cam.up = { 0, 0, -1 };
    m_cam.fovy = 2.0f * std::atan( m_halfH / CamHeight ) / Deg;
    m_cam.projection = CAMERA_PERSPECTIVE;
    m_rng.seed( 99 );
}

void Renderer::shutdown()
{
    if ( m_w == 0 ) return;
    m_terrain.unload();
    UnloadRenderTexture( m_out );
    UnloadRenderTexture( m_shadow );
    rlUnloadFramebuffer( m_hdr.id );
    rlUnloadTexture( m_hdr.texture.id );
    if ( m_hdr.depth.id ) rlUnloadTexture( m_hdr.depth.id );
    for ( RenderTexture2D &t : m_bloom )
    {
        rlUnloadFramebuffer( t.id );
        rlUnloadTexture( t.texture.id );
    }
    m_bloom.clear();
    m_w = 0;
    if ( --g.m_users == 0 )
    {
        s_forge.clear();
        UnloadShader( g.m_lit );
        UnloadShader( g.m_glow );
        UnloadShader( g.m_prefilter );
        UnloadShader( g.m_down );
        UnloadShader( g.m_up );
        UnloadShader( g.m_final );
        UnloadShader( g.m_shadow );
        MemFree( g.m_mat.maps );
        MemFree( g.m_shadowMat.maps );
        g.m_mat = g.m_shadowMat = {};
        for ( Texture2D &t : g.m_tex ) UnloadTexture( t );
    }
}

void Renderer::setTheme( const Catalog &cat, const std::string &theme, uint32_t seed )
{
    float aspect = m_w / (float)m_h;
    m_terrain.setup( cat, theme, seed, m_halfH * aspect );
}

void Renderer::reset()
{
    m_parts.clear();
    m_lights.clear();
    m_debris.clear();
    m_shake = 0;
    m_hurt = 0;
}

Rectangle Renderer::fieldRect() const
{
    Vector2 a = toPixels( { 0, 0 } ), b = toPixels( { FieldW, FieldH } );
    return { a.x * m_up, a.y * m_up, ( b.x - a.x ) * m_up, ( b.y - a.y ) * m_up };
}

void Renderer::setView( Vector2 center, float halfH )
{
    m_center = center;
    m_halfH = halfH;
    m_scale = m_h / ( 2.0f * m_halfH );
    m_cam.fovy = 2.0f * std::atan( m_halfH / CamHeight ) / Deg;
}

Vector2 Renderer::toPixels( Vector2 f ) const
{
    return { m_w * 0.5f + ( f.x - m_center.x ) * m_scale, m_h * 0.5f + ( f.y - m_center.y ) * m_scale };
}

Vector3 Renderer::groundPoint( Vector2 f, double scroll ) const
{
    float x = FieldW * 0.5f + ( f.x - FieldW * 0.5f ) / GroundFactor;
    float z = FieldH * 0.5f + ( f.y - FieldH * 0.5f ) / GroundFactor;
    return { x, m_terrain.heightAt( x, z, scroll ), z };
}

void Renderer::explode( Vector2 at, int size, bool ground, Color tint )
{
    float s = (float)( size + 1 );
    auto  P = [&]() {
        Particle p;
        p.m_pos = at;
        p.m_ground = ground;
        return p;
    };
    Particle f = P();
    f.m_tex = T_SOFT;
    f.m_life = 0.14f + 0.04f * s;
    f.m_size0 = 70 * s;
    f.m_size1 = 20 * s;
    f.m_c0 = { 255, 245, 220, 255 };
    f.m_c1 = { 255, 160, 60, 0 };
    f.m_boost = 4.0f;
    m_parts.push_back( f );
    int nFire = 5 + 5 * size;
    for ( int i = 0; i < nFire; ++i )
    {
        Particle p = P();
        p.m_tex = T_SMOKE;
        float a = m_rng.range( 0, 360 ), v = m_rng.range( 20, 110 ) * std::sqrt( s );
        p.m_vel = mul( dirOf( a ), v );
        p.m_pos = add( at, mul( dirOf( a ), m_rng.range( 0, 8 * s ) ) );
        p.m_life = m_rng.range( 0.3f, 0.6f ) + 0.1f * s;
        p.m_size0 = m_rng.range( 12, 22 ) * std::sqrt( s );
        p.m_size1 = m_rng.range( 30, 55 ) * std::pow( s, 0.8f );
        p.m_c0 = { 255, 230, 160, 255 };
        p.m_c1 = { 200, 50, 20, 0 };
        if ( tint.a > 0 && ( tint.r != 255 || tint.g != 255 || tint.b != 255 ) ) p.m_c1 = { tint.r, tint.g, tint.b, 0 };
        p.m_drag = 2.5f;
        p.m_rot = m_rng.range( 0, 360 );
        p.m_spin = m_rng.range( -90, 90 );
        p.m_boost = 2.6f;
        m_parts.push_back( p );
    }
    int nSpark = 6 + 8 * size;
    for ( int i = 0; i < nSpark; ++i )
    {
        Particle p = P();
        p.m_tex = T_STREAK;
        float a = m_rng.range( 0, 360 );
        p.m_vel = mul( dirOf( a ), m_rng.range( 200, 520 ) * ( 0.8f + 0.2f * s ) );
        p.m_life = m_rng.range( 0.25f, 0.55f );
        p.m_size0 = m_rng.range( 3, 6 );
        p.m_size1 = 1;
        p.m_c0 = { 255, 240, 190, 255 };
        p.m_c1 = { 255, 120, 30, 0 };
        p.m_drag = 3.0f;
        p.m_stretch = 3.0f;
        p.m_boost = 3.5f;
        m_parts.push_back( p );
    }
    int nSmoke = 2 + 3 * size;
    for ( int i = 0; i < nSmoke; ++i )
    {
        Particle p = P();
        p.m_tex = T_SMOKE;
        p.m_add = false;
        float a = m_rng.range( 0, 360 );
        p.m_vel = mul( dirOf( a ), m_rng.range( 10, 50 ) );
        p.m_life = m_rng.range( 0.9f, 1.8f ) + 0.2f * s;
        p.m_size0 = 18 * std::sqrt( s );
        p.m_size1 = m_rng.range( 50, 80 ) * std::sqrt( s );
        // Smoke lit from inside at first (warm), then grey and thinning.
        p.m_c0 = { 92, 72, 58, 175 };
        p.m_c1 = { 44, 44, 48, 0 };
        p.m_drag = 1.2f;
        p.m_rot = m_rng.range( 0, 360 );
        p.m_spin = m_rng.range( -30, 30 );
        p.m_boost = 1.0f;
        m_parts.push_back( p );
    }
    // Embers: a few small glowing bits that drift out slowly and die down (smaller than any bullet).
    int nEmber = 3 + 4 * size;
    for ( int i = 0; i < nEmber; ++i )
    {
        Particle p = P();
        p.m_tex = T_SOFT;
        p.m_vel = mul( dirOf( m_rng.range( 0, 360 ) ), m_rng.range( 40, 140 ) * std::sqrt( s ) );
        p.m_life = m_rng.range( 0.5f, 1.1f );
        p.m_size0 = m_rng.range( 2.5f, 4.0f );
        p.m_size1 = 1.0f;
        p.m_c0 = { 255, 190, 110, 255 };
        p.m_c1 = { 200, 50, 10, 0 };
        p.m_drag = 2.2f;
        p.m_boost = 2.4f;
        m_parts.push_back( p );
    }
    if ( size >= 1 )
    {
        Particle r = P();
        r.m_tex = T_RING;
        r.m_life = 0.35f + 0.08f * s;
        r.m_size0 = 12 * s;
        r.m_size1 = 120 * s;
        r.m_c0 = { 255, 230, 200, 200 };
        r.m_c1 = { 255, 140, 80, 0 };
        r.m_boost = 2.0f;
        m_parts.push_back( r );
    }
    GlowLight L;
    L.m_pos = ground ? groundPoint( at, m_scroll ) : Vector3{ at.x, 30.0f, at.y };
    L.m_pos.y += 30.0f;
    L.m_color = { 255, 170, 90, 255 };
    L.m_radius = 160 * s;
    L.m_power = m_softFlash ? 1.1f : 3.0f;
    L.m_life = 0.3f + 0.12f * s;
    m_lights.push_back( L );
    if ( !ground )
        for ( int i = 0; i < 2 + 3 * size; ++i )
        {
            Debris d;
            d.m_pos = { at.x, 0, at.y };
            float a = m_rng.range( 0, 360 );
            Vector2 v = mul( dirOf( a ), m_rng.range( 60, 220 ) );
            d.m_vel = { v.x, m_rng.range( -260, 60 ), v.y };
            d.m_axis = Vector3Normalize( { m_rng.range( -1, 1 ), m_rng.range( -1, 1 ), m_rng.range( -1, 1 ) } );
            d.m_spin = m_rng.range( 200, 700 );
            d.m_life = m_rng.range( 0.6f, 1.2f );
            d.m_size = m_rng.range( 1.5f, 4.0f ) * std::sqrt( s );
            d.m_color = { 90, 90, 100, 255 };
            m_debris.push_back( d );
        }
}

void Renderer::events( const World &w )
{
    for ( const SimEvent &e : w.m_events )
    {
        switch ( e.m_type )
        {
        case EvE::EXPLODE: explode( e.m_pos, (int)e.m_a, e.m_ground, e.m_color ); break;
        case EvE::HIT:
        case EvE::SPARK:
        {
            for ( int i = 0; i < ( e.m_type == EvE::HIT ? 3 : 2 ); ++i )
            {
                Particle p;
                p.m_pos = e.m_pos;
                p.m_tex = T_STREAK;
                p.m_vel = mul( dirOf( m_rng.range( 90, 270 ) ), m_rng.range( 120, 320 ) );
                p.m_life = m_rng.range( 0.1f, 0.22f );
                p.m_size0 = 4;
                p.m_size1 = 1;
                p.m_c0 = e.m_type == EvE::HIT ? Color{ 255, 230, 180, 255 } : Color{ 200, 220, 255, 255 };
                p.m_c1 = { 255, 120, 40, 0 };
                p.m_stretch = 2.5f;
                p.m_boost = 3.0f;
                m_parts.push_back( p );
            }
            break;
        }
        case EvE::PLAYERHIT:
        {
            if ( e.m_b < 0.5f ) m_hurt = std::min( m_softFlash ? 0.35f : 1.0f, m_hurt + ( 0.35f + e.m_a * 0.02f ) * ( m_softFlash ? 0.4f : 1.0f ) );
            Particle p;
            p.m_pos = e.m_pos;
            p.m_tex = e.m_b > 0.5f ? T_HEX : T_SOFT;
            p.m_life = 0.25f;
            p.m_size0 = 40;
            p.m_size1 = 70;
            p.m_c0 = e.m_b > 0.5f ? Color{ 120, 200, 255, 255 } : Color{ 255, 90, 60, 255 };
            p.m_c1 = { p.m_c0.r, p.m_c0.g, p.m_c0.b, 0 };
            p.m_boost = 2.5f;
            m_parts.push_back( p );
            break;
        }
        case EvE::SHAKE: m_shake = std::min( 1.6f, m_shake + e.m_a ); break;
        case EvE::PICKUP:
        {
            Particle p;
            p.m_pos = e.m_pos;
            p.m_tex = T_RING;
            p.m_life = 0.3f;
            p.m_size0 = 10;
            p.m_size1 = 50;
            p.m_c0 = e.m_name == "credits" ? Color{ 255, 220, 120, 255 } : Color{ 160, 255, 220, 255 };
            p.m_c1 = { p.m_c0.r, p.m_c0.g, p.m_c0.b, 0 };
            m_parts.push_back( p );
            break;
        }
        case EvE::CHAIN:
        {
            // A jagged arc of short-lived streak segments.
            Vector2 a = e.m_pos, b = e.m_to;
            int     n = 6;
            Vector2 prev = a;
            for ( int i = 1; i <= n; ++i )
            {
                Vector2 q = add( a, mul( sub( b, a ), i / (float)n ) );
                if ( i < n ) q = add( q, { m_rng.range( -10, 10 ), m_rng.range( -10, 10 ) } );
                Particle p;
                p.m_pos = mul( add( prev, q ), 0.5f );
                p.m_tex = T_STREAK;
                p.m_life = 0.12f;
                Vector2 d = sub( q, prev );
                p.m_vel = mul( norm( d ), 0.01f );
                p.m_size0 = p.m_size1 = 5;
                p.m_stretch = len( d ) / 5.0f;
                p.m_c0 = e.m_color;
                p.m_c1 = { e.m_color.r, e.m_color.g, e.m_color.b, 0 };
                p.m_boost = 4.0f;
                m_parts.push_back( p );
                prev = q;
            }
            break;
        }
        case EvE::NOVA:
        {
            Particle r;
            r.m_pos = e.m_pos;
            r.m_tex = T_RING;
            r.m_life = 0.6f;
            r.m_size0 = 20;
            r.m_size1 = e.m_a * 2.2f;
            r.m_c0 = e.m_color;
            r.m_c1 = { e.m_color.r, e.m_color.g, e.m_color.b, 0 };
            r.m_boost = 4.0f;
            m_parts.push_back( r );
            GlowLight L;
            L.m_pos = { e.m_pos.x, 60, e.m_pos.y };
            L.m_color = e.m_color;
            L.m_radius = 600;
            L.m_power = 3;
            L.m_life = 0.5f;
            m_lights.push_back( L );
            break;
        }
        case EvE::BOSSDIE:
            for ( int i = 0; i < 6; ++i )
                explode( add( e.m_pos, { m_rng.range( -60, 60 ), m_rng.range( -40, 40 ) } ), i == 0 ? 3 : 2, false, WHITE );
            m_shake = 1.6f;
            break;
        case EvE::SHOT:
            if ( e.m_a == 0 )
            {
                GlowLight L;
                L.m_pos = { e.m_pos.x, 20, e.m_pos.y - 20 };
                L.m_color = { 160, 220, 255, 255 };
                L.m_radius = 90;
                L.m_power = 1.2f;
                L.m_life = 0.06f;
                m_lights.push_back( L );
            }
            break;
        default: break;
        }
    }
    size_t most = m_quality == 0 ? 2500 : m_quality == 1 ? 4000 : 6000;
    if ( m_parts.size() > most ) m_parts.erase( m_parts.begin(), m_parts.begin() + (long)( m_parts.size() - most ) );
    if ( m_lights.size() > 40 ) m_lights.erase( m_lights.begin(), m_lights.begin() + (long)( m_lights.size() - 40 ) );
}

void Renderer::update( const World &w, float dt )
{
    m_time += dt;
    m_scroll = w.m_scrollPos;
    m_groundV = w.m_scroll * GroundFactor;
    for ( Particle &p : m_parts )
    {
        p.m_age += dt;
        p.m_vel = mul( p.m_vel, std::max( 0.0f, 1.0f - p.m_drag * dt ) );
        p.m_pos = add( p.m_pos, mul( p.m_vel, dt ) );
        if ( p.m_ground ) p.m_pos.y += m_groundV * dt;
        p.m_rot += p.m_spin * dt;
    }
    m_parts.erase( std::remove_if( m_parts.begin(), m_parts.end(), []( const Particle &p ) { return p.m_age >= p.m_life; } ),
                   m_parts.end() );
    for ( GlowLight &l : m_lights ) l.m_age += dt;
    m_lights.erase( std::remove_if( m_lights.begin(), m_lights.end(), []( const GlowLight &l ) { return l.m_age >= l.m_life; } ),
                    m_lights.end() );
    for ( Debris &d : m_debris )
    {
        d.m_age += dt;
        d.m_vel.y -= 300.0f * dt;
        d.m_pos = Vector3Add( d.m_pos, Vector3Scale( d.m_vel, dt ) );
        d.m_rot += d.m_spin * dt;
    }
    m_debris.erase( std::remove_if( m_debris.begin(), m_debris.end(), []( const Debris &d ) { return d.m_age >= d.m_life; } ),
                    m_debris.end() );
    m_shake = std::max( 0.0f, m_shake - dt * 2.2f );
    float s = m_shake * m_shake * 9.0f * m_shakeAmount;
    m_shakeOff = { m_rng.range( -s, s ), m_rng.range( -s, s ) };
    m_hurt = std::max( 0.0f, m_hurt - dt * 1.6f );
    // Exhaust behind the pilots' ships.
    for ( const Player &p : w.m_players )
    {
        if ( !p.m_active || !p.m_alive || !p.m_ship ) continue;
        const ModelMesh &mm = s_forge.get( *w.m_cat, p.m_ship->m_model );
        for ( size_t i = 0; i < mm.m_engines.size(); ++i )
        {
            Particle q;
            q.m_pos = { p.m_pos.x + mm.m_engines[i].x, p.m_pos.y + mm.m_engines[i].z };
            q.m_vel = { m_rng.range( -15, 15 ), m_rng.range( 220, 320 ) };
            q.m_tex = T_SOFT;
            q.m_life = 0.12f;
            q.m_size0 = 7;
            q.m_size1 = 2;
            q.m_c0 = mm.m_engineColors[i];
            q.m_c1 = { mm.m_engineColors[i].r, (unsigned char)( mm.m_engineColors[i].g / 2 ), mm.m_engineColors[i].b, 0 };
            q.m_boost = 2.5f;
            m_parts.push_back( q );
        }
    }
}

void Renderer::setLights( const World &w )
{
    const Look &L = m_terrain.look();
    Shader     &s = g.m_lit;
    Vector3     sun = L.m_sunDir, sunCol = v3( L.m_sunColor, L.m_sunPower ), up = v3( L.m_ambUp ), down = v3( L.m_ambDown );
    SetShaderValue( s, g.m_locSun, &sun, SHADER_UNIFORM_VEC3 );
    SetShaderValue( s, g.m_locSunCol, &sunCol, SHADER_UNIFORM_VEC3 );
    SetShaderValue( s, g.m_locAmbUp, &up, SHADER_UNIFORM_VEC3 );
    SetShaderValue( s, g.m_locAmbDown, &down, SHADER_UNIFORM_VEC3 );
    SetShaderValue( s, g.m_locCam, &m_cam.position, SHADER_UNIFORM_VEC3 );
    Vector4 fog = { L.m_fog.r / 255.0f, L.m_fog.g / 255.0f, L.m_fog.b / 255.0f, L.m_space ? 0.0f : L.m_fogDensity };
    SetShaderValue( s, g.m_locFog, &fog, SHADER_UNIFORM_VEC4 );
    float emit = 3.2f;
    SetShaderValue( s, g.m_locEmit, &emit, SHADER_UNIFORM_FLOAT );
    SetShaderValue( s, g.m_locTime, &m_time, SHADER_UNIFORM_FLOAT );
    // The brightest lights of the moment: the pilots' engines first, then explosions and muzzles by strength.
    int n = 0;
    for ( const Player &p : w.m_players )
        if ( p.m_active && p.m_alive && n < MaxGlowLights )
        {
            m_lightPos[n] = { p.m_pos.x, 20.0f, p.m_pos.y + 20.0f, 110.0f };
            m_lightCol[n] = { 0.6f, 0.8f, 1.0f, 0.9f };
            ++n;
        }
    m_lightOrder.clear();
    for ( size_t i = 0; i < m_lights.size(); ++i )
    {
        const GlowLight &l = m_lights[i];
        m_lightOrder.push_back( { l.m_power * ( 1.0f - l.m_age / l.m_life ) * l.m_radius, (int)i } );
    }
    std::sort( m_lightOrder.begin(), m_lightOrder.end(),
               []( const auto &a, const auto &b ) { return a.first > b.first; } );
    for ( size_t i = 0; i < m_lightOrder.size() && n < MaxGlowLights; ++i, ++n )
    {
        const GlowLight &l = m_lights[(size_t)m_lightOrder[i].second];
        float            k = 1.0f - l.m_age / l.m_life;
        m_lightPos[n] = { l.m_pos.x, l.m_pos.y, l.m_pos.z, l.m_radius };
        m_lightCol[n] = { l.m_color.r / 255.0f, l.m_color.g / 255.0f, l.m_color.b / 255.0f, l.m_power * k };
    }
    m_lightN = n;
    SetShaderValue( s, g.m_locCount, &n, SHADER_UNIFORM_INT );
    if ( n > 0 )
    {
        SetShaderValueV( s, g.m_locLightPos, m_lightPos, SHADER_UNIFORM_VEC4, n );
        SetShaderValueV( s, g.m_locLightCol, m_lightCol, SHADER_UNIFORM_VEC4, n );
    }
    Vector4 tint = { 1, 1, 1, 1 };
    SetShaderValue( s, g.m_locTint, &tint, SHADER_UNIFORM_VEC4 );
    float flash = 0;
    SetShaderValue( s, g.m_locFlash, &flash, SHADER_UNIFORM_FLOAT );
}

Matrix Renderer::modelMatrix( Vector3 at, float face, float bank, float scale, float pitch ) const
{
    float yaw = ( 180.0f - face ) * Deg;
    return MatrixMultiply( MatrixMultiply( MatrixMultiply( MatrixScale( scale, scale, scale ), MatrixRotateZ( -bank * 0.6f ) ),
                                           MatrixRotateX( pitch ) ),
                           MatrixMultiply( MatrixRotateY( yaw ), MatrixTranslate( at.x, at.y, at.z ) ) );
}

void Renderer::drawMeshes( const ModelMesh &m, const Matrix &rest, float face, float head, Material &mat )
{
    if ( m.m_hasBase ) DrawMesh( m.m_base, mat, rest );
    if ( m.m_hasHead )
    {
        float  dy = ( face - head ) * Deg;
        Matrix hm = MatrixMultiply( MatrixMultiply( MatrixTranslate( -m.m_pivot.x, 0, -m.m_pivot.z ), MatrixRotateY( dy ) ),
                                    MatrixTranslate( m.m_pivot.x, 0, m.m_pivot.z ) );
        DrawMesh( m.m_head, mat, MatrixMultiply( hm, rest ) );
    }
    if ( m.m_hasSpin ) DrawMesh( m.m_spin, mat, MatrixMultiply( MatrixRotateY( m_time * m.m_spinSpeed * Deg ), rest ) );
}

void Renderer::drawModel( const ModelMesh &m, Vector3 at, float face, float bank, float scale, float flash, float head,
                          Color tint, float pitch )
{
    Shader &s = g.m_lit;
    float   f = clampf( flash, 0, 1 ) * 0.7f;
    SetShaderValue( s, g.m_locFlash, &f, SHADER_UNIFORM_FLOAT );
    Vector4 t = { tint.r / 255.0f, tint.g / 255.0f, tint.b / 255.0f, 1 };
    SetShaderValue( s, g.m_locTint, &t, SHADER_UNIFORM_VEC4 );
    drawMeshes( m, modelMatrix( at, face, bank, scale, pitch ), face, head, g.m_mat );
    f = 0;
    SetShaderValue( s, g.m_locFlash, &f, SHADER_UNIFORM_FLOAT );
}

void Renderer::drawShadows( const World &w, const Catalog &cat )
{
    const Look &L = m_terrain.look();
    if ( L.m_space ) return;
    // The rectangle on the ground's plane that the field's shadows can fall into (the sun pushes them aside).
    Vector3 sd = L.m_sunDir;
    float   planeY = -GroundDepth;
    float   t = planeY / std::min( -0.2f, sd.y );
    Vector2 off = { sd.x * t, sd.z * t };
    float   x0 = std::min( -100.0f, -100.0f + off.x ), x1 = std::max( FieldW + 100.0f, FieldW + 100.0f + off.x );
    float   z0 = std::min( -100.0f, -100.0f + off.y ), z1 = std::max( FieldH + 100.0f, FieldH + 100.0f + off.y );
    m_shadowRect = { x0, z0, x1 - x0, z1 - z0 };
    BeginTextureMode( m_shadow );
    ClearBackground( BLACK );
    rlDisableDepthTest();
    rlDisableBackfaceCulling();
    Shader &s = g.m_shadow;
    SetShaderValue( s, g.m_locShSun, &sd, SHADER_UNIFORM_VEC3 );
    SetShaderValue( s, g.m_locShPlane, &planeY, SHADER_UNIFORM_FLOAT );
    SetShaderValue( s, g.m_locShRect, &m_shadowRect, SHADER_UNIFORM_VEC4 );
    for ( const Enemy &e : w.m_enemies )
    {
        if ( e.m_dead || e.m_gone || e.m_ground ) continue;
        const ModelMesh &mm = s_forge.get( cat, e.m_def->m_model );
        Matrix           at = modelMatrix( { e.m_pos.x, 0, e.m_pos.y }, e.m_face, e.m_bank, e.m_def->m_scale, 0 );
        drawMeshes( mm, at, e.m_face, e.m_head, g.m_shadowMat );
    }
    for ( const Player &p : w.m_players )
    {
        if ( !p.m_active || !p.m_alive || !p.m_ship ) continue;
        const ModelMesh &mm = s_forge.get( cat, p.m_ship->m_model );
        drawMeshes( mm, modelMatrix( { p.m_pos.x, 10, p.m_pos.y }, 180.0f, p.m_bank, 1.0f, 0 ), 180.0f, 180.0f,
                    g.m_shadowMat );
        for ( const Sidekick &k : p.m_side )
        {
            if ( !k.m_def ) continue;
            Matrix at = modelMatrix( { k.m_pos.x, 8, k.m_pos.y }, 180.0f, p.m_bank * 0.5f, 1.0f, 0 );
            drawMeshes( s_forge.get( cat, k.m_def->m_model ), at, 180.0f, 180.0f, g.m_shadowMat );
        }
    }
    EndTextureMode();
    rlEnableDepthTest();
}

void Renderer::sprite( int tex, Vector2 field, float size, Color c, float rot, float stretch, Vector2 dir )
{
    Vector2   p = toPixels( field );
    float     s = size * m_scale;
    float     w = s * ( 1.0f + stretch ), h = s;
    float     a = stretch > 0 ? std::atan2( dir.y, dir.x ) / Deg : rot;
    Texture2D t = g.m_tex[tex];
    DrawTexturePro( t, { 0, 0, (float)t.width, (float)t.height }, { p.x, p.y, w, h }, { w * 0.5f, h * 0.5f }, a, c );
}

void Renderer::beam( Vector2 a, Vector2 b, float width, Color c )
{
    Vector2 d = sub( b, a );
    float   l = len( d );
    if ( l < 1 ) return;
    Vector2   pa = toPixels( a );
    float     w = l * m_scale, h = width * m_scale;
    Texture2D t = g.m_tex[T_STREAK];
    // The streak sprite is soft at both ends; a beam wants its middle: draw its centre stretched.
    DrawTexturePro( t, { t.width * 0.25f, 0, t.width * 0.5f, (float)t.height }, { pa.x, pa.y, w, h }, { 0, h * 0.5f },
                    std::atan2( d.y, d.x ) / Deg, c );
}

void Renderer::drawSprites( const World &w, const Catalog &cat )
{
    (void)cat;
    BeginShaderMode( g.m_glow );
    auto boost = [&]( float b ) { SetShaderValue( g.m_glow, g.m_locBoost, &b, SHADER_UNIFORM_FLOAT ); };
    // Smoke first, blended.
    BeginBlendMode( BLEND_ALPHA_PREMULTIPLY );
    boost( 1.0f );
    for ( const Particle &p : m_parts )
    {
        if ( p.m_add ) continue;
        float t = p.m_age / p.m_life;
        sprite( p.m_tex, p.m_pos, lerpf( p.m_size0, p.m_size1, t ), mixColor( p.m_c0, p.m_c1, t ), p.m_rot );
    }
    // A dark halo under every enemy bullet and laser: added light alone vanishes on a bright ground (the corona,
    // snow, sand); with the ground darkened round it the glow reads on every theme.
    for ( const Bullet &b : w.m_bullets )
    {
        if ( b.m_player ) continue;
        Vector2 dir = len( b.m_vel ) > 1 ? norm( b.m_vel ) : Vector2{ 0, -1 };
        bool    longShot = b.m_sprite == 1 || b.m_sprite == 10 || b.m_sprite == 18 || b.m_sprite == 9;
        // The aid "strong bullets": a wider, darker halo.
        bool strong = m_bulletStyle > 0;
        sprite( T_SOFT, b.m_pos, b.m_radius * ( strong ? 6.6f : 5.2f ), { 0, 0, 0, (unsigned char)( strong ? 235 : 165 ) }, 0,
                longShot ? 1.6f : 0.0f, dir );
    }
    for ( const Laser &l : w.m_lasers )
        if ( !l.m_dead && l.m_t >= l.m_warn )
        {
            Vector2 d = dirOf( l.m_dir );
            beam( l.m_from, add( l.m_from, mul( d, 1600.0f ) ), l.m_width * 3.4f, { 0, 0, 0, 120 } );
        }
    rlDrawRenderBatchActive();
    EndBlendMode();
    BeginBlendMode( BLEND_ADD_COLORS );
    // Engine glows of the enemies (cheaper than particles): a hot dot and a short flame.
    boost( 2.4f );
    for ( const Enemy &e : w.m_enemies )
    {
        if ( e.m_dead || e.m_gone || e.m_ground ) continue;
        const ModelMesh &mm = s_forge.get( *w.m_cat, e.m_def->m_model );
        if ( mm.m_engines.empty() ) continue;
        float   yaw = ( 180.0f - e.m_face ) * Deg, c = std::cos( yaw ), s = std::sin( yaw );
        Vector2 back = dirOf( e.m_face + 180.0f );
        for ( size_t i = 0; i < mm.m_engines.size(); ++i )
        {
            Vector3 n = Vector3Scale( mm.m_engines[i], e.m_def->m_scale );
            Vector2 at = { e.m_pos.x + c * n.x + s * n.z, e.m_pos.y - s * n.x + c * n.z };
            float   flick = 0.85f + 0.15f * std::sin( m_time * 40.0f + i + e.m_id );
            sprite( T_SOFT, at, 10.0f * flick * e.m_def->m_scale, mm.m_engineColors[i] );
            sprite( T_STREAK, add( at, mul( back, 7.0f * e.m_def->m_scale ) ), 9.0f * e.m_def->m_scale, mm.m_engineColors[i], 0, 1.2f, back );
        }
    }
    // Particles (added light).
    for ( const Particle &p : m_parts )
    {
        if ( !p.m_add ) continue;
        float t = p.m_age / p.m_life;
        boost( p.m_boost );
        Vector2 dir = len( p.m_vel ) > 1 ? norm( p.m_vel ) : Vector2{ 0, 1 };
        sprite( p.m_tex, p.m_pos, lerpf( p.m_size0, p.m_size1, t ), mixColor( p.m_c0, p.m_c1, t ), p.m_rot, p.m_stretch, dir );
    }
    // The pilots' beams and the enemies' lasers.
    // Each beam from its own muzzle (front guns, rear guns, sidekicks).
    for ( const Player &p : w.m_players )
        if ( p.m_active && p.m_alive )
            for ( const BeamShot &b : p.m_beams )
            {
                boost( 3.0f );
                Vector2 from = { b.m_from.x, b.m_from.y - 4 };
                float   wob = 1.0f + 0.15f * std::sin( m_time * 50.0f + from.x );
                beam( from, { from.x, -40 }, b.m_width * 2.2f * wob, b.m_color );
                boost( 5.0f );
                beam( from, { from.x, -40 }, b.m_width * 0.7f, WHITE );
                sprite( T_SOFT, from, b.m_width * 3.0f, b.m_color );
            }
    for ( const Laser &l : w.m_lasers )
    {
        if ( l.m_dead ) continue;
        Vector2 d = dirOf( l.m_dir );
        Vector2 end = add( l.m_from, mul( d, 1600.0f ) );
        if ( l.m_t < l.m_warn )
        {
            // The warning: a thin flickering line.
            boost( 1.6f );
            float k = 0.4f + 0.6f * ( 0.5f + 0.5f * std::sin( m_time * 40.0f ) );
            beam( l.m_from, end, 3.0f, { l.m_color.r, l.m_color.g, l.m_color.b, (unsigned char)( 200 * k ) } );
        }
        else
        {
            boost( 3.0f );
            float grow = std::min( 1.0f, ( l.m_t - l.m_warn ) * 10.0f ) * std::min( 1.0f, ( l.m_warn + l.m_dur - l.m_t ) * 6.0f );
            beam( l.m_from, end, l.m_width * 2.0f * grow, l.m_color );
            boost( 5.0f );
            beam( l.m_from, end, l.m_width * 0.6f * grow, WHITE );
            sprite( T_SOFT, l.m_from, l.m_width * 3.0f, l.m_color );
        }
    }
    // Pickups.
    for ( const Pickup &pk : w.m_pickups )
    {
        float  pulse = 0.8f + 0.2f * std::sin( m_time * 8.0f + pk.m_pos.x );
        Color  c = { 255, 210, 90, 255 };
        int    core = T_SHARD;
        float  size = 14.0f;
        switch ( pk.m_kind )
        {
        case PickE::CREDITS: c = { 255, 205, 80, 255 }; size = pk.m_value >= 400 ? 18.0f : 12.0f; break;
        case PickE::POWER: c = { 255, 90, 220, 255 }; core = T_CORE; size = 18; break;
        case PickE::SHIELD: c = { 80, 170, 255, 255 }; core = T_HEX; size = 22; break;
        case PickE::ARMOR: c = { 90, 255, 130, 255 }; core = T_CORE; size = 18; break;
        case PickE::WEAPON: c = { 255, 140, 40, 255 }; core = T_HEX; size = 22; break;
        case PickE::REAR: c = { 255, 180, 40, 255 }; core = T_HEX; size = 22; break;
        case PickE::SIDE: c = { 120, 255, 255, 255 }; core = T_HEX; size = 22; break;
        case PickE::LIFE: c = { 255, 80, 80, 255 }; core = T_FLARE; size = 26; break;
        case PickE::SPECIAL: c = { 200, 160, 255, 255 }; core = T_FLARE; size = 22; break;
        case PickE::CUBE: c = { 90, 255, 240, 255 }; core = T_HEX; size = 24; break;
        case PickE::KEY: c = { 255, 240, 140, 255 }; core = T_FLARE; size = 28; break;
        case PickE::PLAN: c = { 240, 240, 255, 255 }; core = T_HEX; size = 24; break;
        }
        boost( 1.6f );
        sprite( T_SOFT, pk.m_pos, size * 2.4f * pulse, c );
        boost( 2.6f );
        sprite( core, pk.m_pos, size, c, m_time * 120.0f );
    }
    // Bullets.
    for ( const Bullet &b : w.m_bullets )
    {
        Vector2 dir = len( b.m_vel ) > 1 ? norm( b.m_vel ) : Vector2{ 0, -1 };
        float   r = b.m_radius;
        Color   c = b.m_color;
        // Colour-blind aids: every enemy bullet in one colour that stands apart from the pilots' blue shots.
        if ( !b.m_player && m_bulletStyle == 2 ) c = { 255, 196, 40, 255 };
        else if ( !b.m_player && m_bulletStyle == 3 ) c = { 255, 50, 120, 255 };
        switch ( b.m_sprite )
        {
        case 0:   // orb
        case 7:
            boost( 1.8f );
            sprite( T_SOFT, b.m_pos, r * 4.4f, c );
            boost( 2.2f );
            sprite( T_CORE, b.m_pos, r * 2.0f, WHITE );
            break;
        case 1:   // needle
            boost( 2.0f );
            sprite( T_STREAK, b.m_pos, r * 3.0f, c, 0, 2.5f, dir );
            boost( 2.4f );
            sprite( T_STREAK, b.m_pos, r * 1.4f, WHITE, 0, 3.0f, dir );
            break;
        case 2:   // big
            boost( 1.6f );
            sprite( T_SOFT, b.m_pos, r * 4.0f, c );
            boost( 2.4f );
            sprite( T_CORE, b.m_pos, r * 2.1f, mixColor( c, WHITE, 0.6f ) );
            sprite( T_RING, b.m_pos, r * 2.6f, c, m_time * 200.0f );
            break;
        case 3:   // ring
            boost( 2.2f );
            sprite( T_RING, b.m_pos, r * 2.8f, c );
            sprite( T_CORE, b.m_pos, r * 1.0f, WHITE );
            break;
        case 4:   // missile
            boost( 2.0f );
            sprite( T_STREAK, b.m_pos, r * 2.2f, WHITE, 0, 1.5f, dir );
            boost( 2.8f );
            sprite( T_SOFT, sub( b.m_pos, mul( dir, r * 2.0f ) ), r * 3.0f, c );
            break;
        case 5:   // mine
        {
            float blink = std::fmod( b.m_age * 4.0f, 1.0f ) < 0.5f ? 1.0f : 0.4f;
            boost( 2.0f * blink );
            sprite( T_CORE, b.m_pos, r * 2.0f, c );
            sprite( T_RING, b.m_pos, r * 3.2f, c, m_time * 90.0f );
            break;
        }
        case 6:   // shard
            boost( 2.2f );
            sprite( T_SOFT, b.m_pos, r * 3.2f, c );
            sprite( T_SHARD, b.m_pos, r * 2.4f, WHITE, angleOf( dir ) );
            break;
        case 10:   // laser
        case 18:   // rail
            boost( 2.6f );
            sprite( T_STREAK, b.m_pos, r * 2.4f, c, 0, b.m_sprite == 18 ? 9.0f : 6.0f, dir );
            boost( 3.0f );
            sprite( T_STREAK, b.m_pos, r * 1.0f, WHITE, 0, b.m_sprite == 18 ? 20.0f : 12.0f, dir );
            break;
        case 11:   // plasma
        case 17:   // bomb
        case 21:   // orbit
            boost( 2.0f );
            sprite( T_SOFT, b.m_pos, r * 4.4f, c );
            boost( 2.8f );
            sprite( T_CORE, b.m_pos, r * 2.0f, mixColor( c, WHITE, 0.7f ) );
            break;
        case 12:   // rocket
            boost( 2.0f );
            sprite( T_STREAK, b.m_pos, r * 2.0f, { 230, 230, 240, 255 }, 0, 1.4f, dir );
            boost( 3.2f );
            sprite( T_SOFT, sub( b.m_pos, mul( dir, r * 2.5f ) ), r * 3.0f, c );
            break;
        case 13:   // flame
        {
            float t = b.m_age / b.m_life;
            boost( 2.2f * ( 1 - t ) );
            sprite( T_SMOKE, b.m_pos, r * ( 3.0f + 6.0f * t ), mixColor( { 255, 240, 180, 255 }, c, t ), b.m_age * 300.0f );
            break;
        }
        case 14:   // spark
        case 15:   // star
            boost( 2.6f );
            sprite( T_SPARK, b.m_pos, r * 4.0f, c, m_time * 400.0f );
            break;
        case 16:   // ripple
            boost( 2.2f );
            sprite( T_RING, b.m_pos, r * ( 3.0f + b.m_age * 8.0f ), c );
            break;
        case 19:   // petal
            boost( 2.4f );
            sprite( T_SHARD, b.m_pos, r * 3.0f, c, angleOf( dir ) + 90.0f );
            break;
        default:   // pulse, bolt, needle_p
            boost( 2.4f );
            sprite( T_STREAK, b.m_pos, r * 2.6f, c, 0, b.m_sprite == 9 ? 3.5f : 2.0f, dir );
            boost( 3.0f );
            sprite( T_STREAK, b.m_pos, r * 1.1f, WHITE, 0, b.m_sprite == 9 ? 4.0f : 2.5f, dir );
            break;
        }
    }
    // The pilots' shields when they take a hit, their barrier, the hit circle.
    for ( const Player &p : w.m_players )
    {
        if ( !p.m_active || !p.m_alive ) continue;
        if ( p.m_barrier > 0 )
        {
            boost( 2.0f );
            sprite( T_HEX, p.m_pos, 70.0f, { 140, 220, 255, 200 }, m_time * 60.0f );
        }
        if ( m_showHitbox )
        {
            boost( 3.0f );
            float r = p.m_ship ? p.m_ship->m_radius : 9.0f;
            sprite( T_CORE, p.m_pos, r * 2.0f, { 255, 255, 255, 220 } );
        }
    }
    rlDrawRenderBatchActive();
    EndBlendMode();
    EndShaderMode();
}

void Renderer::draw( const World &w, const Catalog &cat )
{
    const Look &L = m_terrain.look();
    m_cam.position = { m_center.x + m_shakeOff.x, CamHeight, m_center.y + m_shakeOff.y };
    m_cam.target = { m_center.x + m_shakeOff.x, 0, m_center.y + m_shakeOff.y };
    // What flies casts its shadow first, into a picture of its own that the ground reads.
    drawShadows( w, cat );
    BeginTextureMode( m_hdr );
    ClearBackground( L.m_sky );
    rlSetClipPlanes( 10.0, 12000.0 );   // before BeginMode3D: it builds the projection
    BeginMode3D( m_cam );
    rlDisableBackfaceCulling();
    setLights( w );
    // The ground has its own shader; it is lit by the same lights and darkened under what flies.
    GroundFrame gf;
    gf.m_camPos = m_cam.position;
    gf.m_lightCount = m_lightN;
    gf.m_lightPos = m_lightPos;
    gf.m_lightCol = m_lightCol;
    gf.m_shadowTex = L.m_space ? 0 : m_shadow.texture.id;
    gf.m_shadowRect = m_shadowRect;
    gf.m_shadowY = -GroundDepth;
    m_terrain.setFrame( gf );
    m_terrain.draw( g.m_mat, m_scroll, m_time );
    float spec = 0.4f;
    SetShaderValue( g.m_lit, g.m_locSpec, &spec, SHADER_UNIFORM_FLOAT );
    // On the ground.
    for ( const Enemy &e : w.m_enemies )
    {
        if ( e.m_dead || e.m_gone || !e.m_ground ) continue;
        const ModelMesh &mm = s_forge.get( cat, e.m_def->m_model );
        Vector3          at = groundPoint( e.m_pos, m_scroll );
        drawModel( mm, at, e.m_face, 0, e.m_def->m_scale, e.m_flash, e.m_head, WHITE );
    }
    m_terrain.drawOver( m_scroll, m_time );
    // What flies.
    for ( const Enemy &e : w.m_enemies )
    {
        if ( e.m_dead || e.m_gone || e.m_ground ) continue;
        const ModelMesh &mm = s_forge.get( cat, e.m_def->m_model );
        float            bob = e.m_def->m_kind == EnemyKindE::OBSTACLE ? 0.0f : std::sin( m_time * 2.0f + e.m_id ) * 2.0f;
        float            pitch = e.m_def->m_face == "spin" ? 0.0f : 0.0f;
        Color            tint = WHITE;
        if ( e.m_def->m_kind == EnemyKindE::BOSS && e.m_hp < e.m_hpMax * 0.3f )
        {
            float k = 0.5f + 0.5f * std::sin( m_time * 10.0f );
            tint = { 255, (unsigned char)( 200 + 55 * k ), (unsigned char)( 200 + 55 * k ), 255 };
        }
        drawModel( mm, { e.m_pos.x, bob + e.m_lift, e.m_pos.y }, e.m_face, e.m_bank, e.m_def->m_scale, e.m_flash, e.m_head, tint, pitch );
    }
    for ( const Player &p : w.m_players )
    {
        if ( !p.m_active || !p.m_alive || !p.m_ship ) continue;
        if ( p.m_invuln > 0 && std::fmod( p.m_invuln, 0.2f ) < 0.08f ) continue;
        const ModelMesh &mm = s_forge.get( cat, p.m_ship->m_model );
        drawModel( mm, { p.m_pos.x, 10, p.m_pos.y }, 180.0f, p.m_bank, 1.0f, 0, 180.0f, WHITE, -p.m_vel.y * 0.0006f );
        for ( const Sidekick &s : p.m_side )
            if ( s.m_def )
            {
                const ModelMesh &sm = s_forge.get( cat, s.m_def->m_model );
                float            charge = s.m_def->m_charge > 0 ? s.m_charge / s.m_def->m_charge : 0.0f;
                drawModel( sm, { s.m_pos.x, 8, s.m_pos.y }, 180.0f, p.m_bank * 0.5f, 1.0f, charge >= 1.0f ? 0.3f : 0.0f, 180.0f, WHITE );
            }
    }
    // Debris.
    for ( const Debris &d : m_debris )
    {
        Matrix m = MatrixMultiply( MatrixMultiply( MatrixScale( d.m_size, d.m_size, d.m_size ), MatrixRotate( d.m_axis, d.m_rot * Deg ) ),
                                   MatrixTranslate( d.m_pos.x, d.m_pos.y, d.m_pos.z ) );
        const Debris &piece = d;
        (void)piece;
        static ModelMesh cube;
        static bool      made = false;
        if ( !made )
        {
            ModelDef md;
            md.m_name = "_debris";
            Line b;
            b.m_key = "box";
            b.m_kv = { { "w", "1" }, { "h", "0.6" }, { "d", "1.4" }, { "color", "#5a5a64" } };
            md.m_parts.push_back( b );
            cube = Forge::build( md );
            made = true;
        }
        DrawMesh( cube.m_base, g.m_mat, m );
    }
    EndMode3D();
    // Light: sprites in the field's pixels.
    drawSprites( w, cat );
    EndTextureMode();
    post();
    // raylib keeps no stack of targets: go back to the one around us.
    if ( s_outer ) BeginTextureMode( *s_outer );
}

void Renderer::post()
{
    const Look &L = m_terrain.look();
    Texture2D   src = m_hdr.texture;
    float       threshold = 1.0f;
    Vector2     texel = { 1.0f / src.width, 1.0f / src.height };
    BeginTextureMode( m_bloom[0] );
    SetShaderValue( g.m_prefilter, GetShaderLocation( g.m_prefilter, "texel" ), &texel, SHADER_UNIFORM_VEC2 );
    SetShaderValue( g.m_prefilter, GetShaderLocation( g.m_prefilter, "threshold" ), &threshold, SHADER_UNIFORM_FLOAT );
    fullscreen( g.m_prefilter, src, m_bloom[0].texture.width, m_bloom[0].texture.height );
    EndTextureMode();
    for ( size_t i = 1; i < m_bloom.size(); ++i )
    {
        Texture2D from = m_bloom[i - 1].texture;
        texel = { 1.0f / from.width, 1.0f / from.height };
        BeginTextureMode( m_bloom[i] );
        SetShaderValue( g.m_down, GetShaderLocation( g.m_down, "texel" ), &texel, SHADER_UNIFORM_VEC2 );
        fullscreen( g.m_down, from, m_bloom[i].texture.width, m_bloom[i].texture.height );
        EndTextureMode();
    }
    for ( size_t i = m_bloom.size() - 1; i > 0; --i )
    {
        Texture2D from = m_bloom[i].texture;
        texel = { 1.0f / from.width, 1.0f / from.height };
        BeginTextureMode( m_bloom[i - 1] );
        BeginBlendMode( BLEND_ADDITIVE );
        SetShaderValue( g.m_up, GetShaderLocation( g.m_up, "texel" ), &texel, SHADER_UNIFORM_VEC2 );
        fullscreen( g.m_up, from, m_bloom[i - 1].texture.width, m_bloom[i - 1].texture.height );
        EndBlendMode();
        EndTextureMode();
    }
    BeginTextureMode( m_out );
    Shader &f = g.m_final;
    float   exposure = L.m_exposure, bloom = L.m_bloom * 0.55f;
    Vector2 fa = toPixels( { 0, 0 } ), fb = toPixels( { FieldW, FieldH } );
    Vector4 field = { fa.x / m_w, fa.y / m_h, fb.x / m_w, fb.y / m_h };
    SetShaderValue( f, GetShaderLocation( f, "exposure" ), &exposure, SHADER_UNIFORM_FLOAT );
    SetShaderValue( f, GetShaderLocation( f, "bloom" ), &bloom, SHADER_UNIFORM_FLOAT );
    SetShaderValue( f, GetShaderLocation( f, "hurt" ), &m_hurt, SHADER_UNIFORM_FLOAT );
    SetShaderValue( f, GetShaderLocation( f, "fade" ), &m_fade, SHADER_UNIFORM_FLOAT );
    SetShaderValue( f, GetShaderLocation( f, "field" ), &field, SHADER_UNIFORM_VEC4 );
    BeginShaderMode( f );
    SetShaderValueTexture( f, GetShaderLocation( f, "bloomTex" ), m_bloom[0].texture );
    DrawTexturePro( src, { 0, 0, (float)src.width, (float)-src.height }, { 0, 0, (float)m_w, (float)m_h }, { 0, 0 }, 0, WHITE );
    EndShaderMode();
    EndTextureMode();
}

void Renderer::present( Rectangle dst ) const
{
    DrawTexturePro( m_out.texture, { 0, 0, (float)m_out.texture.width, (float)-m_out.texture.height }, dst, { 0, 0 }, 0, WHITE );
}

}   // namespace zenith
