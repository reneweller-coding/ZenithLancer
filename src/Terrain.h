/**
 * @file Terrain.h
 * @brief The world under the field: a ground scrolling by (relief, water or lava, cities, forests, plating, craters)
 *        or, out in space, layers of stars, nebulae, a distant planet and drifting rocks. Its look comes from a theme
 *        block in data/themes.txt (docs/DATA.md, "Themes"). The ground lies GroundDepth below the field; it is a
 *        smooth heightfield in chunks whose surface (rock, sand, ice, canopy, hull plating, lattice, plasma, water,
 *        lava) is painted by a shader of its own, built for the theme's style when the theme is set up. Chunks are
 *        built on a worker thread ahead of the view, so scrolling never waits for them.
 * @author Rene Weller
 */
#pragma once

#include "Data.h"

#include <raylib.h>

#include <future>
#include <map>
#include <string>
#include <vector>

namespace zenith
{

/** @brief The light and air of a theme. */
struct Look
{
    Color   m_sky = { 8, 10, 18, 255 };          ///< what is behind everything
    Color   m_fog = { 20, 24, 40, 255 };
    float   m_fogDensity = 0.0012f;
    Vector3 m_sunDir = { -0.35f, -0.85f, -0.4f }; ///< the light's direction (pointing from the sun)
    Color   m_sunColor = { 255, 244, 228, 255 };
    float   m_sunPower = 1.25f;
    Color   m_ambUp = { 80, 90, 120, 255 };
    Color   m_ambDown = { 30, 26, 30, 255 };
    bool    m_space = false;                       ///< no ground: stars
    float   m_exposure = 1.0f;
    float   m_bloom = 0.9f;
    float   m_groundLight = 0.55f;                ///< how bright the ground is lit (it must not outshine the ships)
};

/** @brief What the renderer hands the ground every frame: the lights it lights the ships with, the shadows. */
struct GroundFrame
{
    Vector3        m_camPos = { 0, 0, 0 };         ///< the camera (world)
    int            m_lightCount = 0;               ///< short-lived lights (explosions, muzzles, engines)
    const Vector4 *m_lightPos = nullptr;           ///< per light: world position and radius
    const Vector4 *m_lightCol = nullptr;           ///< per light: colour and power
    unsigned int   m_shadowTex = 0;                ///< what flies, cast along the sun onto a plane (0: none)
    Vector4        m_shadowRect = { 0, 0, 1, 1 };  ///< the world rectangle of that picture: x, z, width, depth
    float          m_shadowY = 0.0f;               ///< the plane's world height
};

/** @brief The ground or the starfield. */
class Terrain
{
public:
    /**
     * @brief Sets a theme up.
     * @param[in] cat    the catalog (themes)
     * @param[in] theme  the theme's name
     * @param[in] seed   the mission's seed (the same mission looks the same)
     * @param[in] halfW  half the width the camera sees at the field (world units)
     */
    void setup( const Catalog &cat, const std::string &theme, uint32_t seed, float halfW );

    /** @brief Lets go of the meshes, shaders and textures. */
    void unload();

    /**
     * @brief Hands over this frame's lights and shadows (before draw).
     * @param[in] f  the frame
     */
    void setFrame( const GroundFrame &f ) { m_frame = f; }

    /**
     * @brief Draws it (inside BeginMode3D). The ground, its props and the rocks use the ground's own shader with
     *        the lights from setFrame.
     * @param[in] mat     the ships' lit material (not needed by the ground any more; kept for the callers)
     * @param[in] scroll  how far the ground has come
     * @param[in] time    seconds (water, lava, plasma, blinking lights)
     */
    void draw( Material &mat, double scroll, float time );

    /**
     * @brief Draws what lies above the ground and below the field (clouds, smoke) with alpha.
     * @param[in] scroll  how far the ground has come
     * @param[in] time    seconds
     */
    void drawOver( double scroll, float time );

    /**
     * @brief The ground's height (world y) under a world point.
     * @param[in] x       world x
     * @param[in] z       world z
     * @param[in] scroll  how far the ground has come
     * @return world y
     */
    float heightAt( float x, float z, double scroll ) const;

    /** @return the light and air */
    const Look &look() const { return m_look; }

    /** @return the theme's name */
    const std::string &name() const { return m_name; }

    /** @return the vertices of the meshes drawn in the last frame (for measuring) */
    int drawnVertices() const { return m_drawnVerts; }

    /** @return the meshes drawn in the last frame (for measuring) */
    int drawnMeshes() const { return m_drawnMeshes; }

    /** @brief The geometry of one chunk as indexed parts of at most 65535 vertices (built off the main thread). */
    struct Geometry
    {
        /** @brief One part (one mesh). */
        struct Part
        {
            std::vector<float>          m_pos, m_nrm, m_uv, m_uv2, m_tan;
            std::vector<unsigned char>  m_col;
            std::vector<unsigned short> m_idx;
        };
        std::vector<Part> m_parts;   ///< never more than MaxParts
    };

    static constexpr int MaxParts = 4;   ///< meshes a chunk may hold

private:
    /** @brief The surface the ground shader paints. */
    enum class StyleE : int
    {
        ROCK, SAND, ISLAND, ICE, JUNGLE, PLATING, LATTICE, CORONA, CITY, FOUNDRY, HIVE
    };
    /** @brief What fills the ground below the water level. */
    enum class FluidE : int
    {
        NONE, WATER, LAVA, BIO
    };
    /** @brief A chunk on the GPU. */
    struct Chunk
    {
        Mesh m_mesh[MaxParts] = {};
        int  m_parts = 0;
        int  m_verts = 0;
    };

    float    rawHeight( float x, float g ) const;
    float    groundHeight( float x, float g ) const;
    float    forest( float x, float g, float raw ) const;
    float    built( float x, float g ) const;
    float    mesa( float x, float g ) const;
    Geometry buildChunk( int k ) const;
    void     buildProps( Geometry &geo, int k, float x0, float x1 ) const;
    void     buildRocks( Geometry &geo, int k ) const;
    void     upload( int k, Geometry &&geo );
    void     ensureChunks( int kNeed0, int kNeed1, int kAhead );
    void     releaseChunks();
    void     makeShaders();
    void     setStaticUniforms();
    void     setFrameUniforms( float time );
    void     drawSky( float time );

    std::string           m_name;
    Look                  m_look;
    Block                 m_theme;
    uint32_t              m_seed = 1;
    float                 m_halfW = 1200.0f;   ///< half the ground's width a chunk covers
    float                 m_viewHalfW = 640.0f;   ///< half the view's width at the field
    float                 m_relief = 60.0f;
    float                 m_scale = 0.004f;
    float                 m_water = -1e9f;      ///< water / lava level (relative height), -1e9 none
    Color                 m_waterColor = { 30, 70, 110, 255 };
    float                 m_waterGlow = 0.0f;
    Color                 m_low = { 70, 60, 50, 255 }, m_mid = { 110, 96, 70, 255 }, m_high = { 150, 140, 120, 255 },
                          m_peak = { 220, 220, 225, 255 };
    Color                 m_glow = { 255, 120, 40, 255 };
    Color                 m_glow2 = { 64, 220, 255, 255 };   ///< a second glow colour (lattice circuits)
    float                 m_glowAmount = 0.0f;
    float                 m_city = 0.0f, m_trees = 0.0f, m_craters = 0.0f, m_plates = 0.0f, m_rocks = 0.0f;
    float                 m_ridge = 0.0f;
    float                 m_rivers = 0.0f, m_dunes = 0.0f, m_mesas = 0.0f, m_channels = 0.0f, m_paint = 0.0f;
    float                 m_leads = 0.0f;
    float                 m_riverWidth = 1.0f, m_riverCanyon = 0.0f;   ///< rivers: width factor, cliffs beside them
    float                 m_walls = 0.0f, m_wallWidth = 820.0f;        ///< a cave: walls' height, the corridor's width
    float                 m_crystals = 0.0f;                           ///< glowing crystals and fungi (caves)
    Color                 m_crystalA = { 64, 200, 224, 255 }, m_crystalB = { 112, 255, 144, 255 };
    float                 m_waves = 0.5f, m_windows = 0.0f, m_stars = 1.0f, m_lightning = 0.0f, m_detail = 1.0f;
    Color                 m_propColor = { 120, 120, 130, 255 };
    Color                 m_treeColor = { 40, 90, 50, 255 };
    Color                 m_cloudColor = { 255, 255, 255, 255 };
    float                 m_clouds = 0.0f;
    Color                 m_mistColor = { 200, 220, 200, 255 };
    float                 m_mist = 0.0f, m_mistTop = -0.2f;
    Color                 m_planetColor = { 180, 140, 100, 255 };
    float                 m_planet[4] = { 0, 0, 0, 0 };   ///< a distant disc: radius, x and y (0..1 of the view), rings
    Color                 m_nebulaA = { 60, 30, 120, 255 }, m_nebulaB = { 20, 80, 140, 255 };
    float                 m_nebula = 1.0f;
    StyleE                m_style = StyleE::ROCK;
    FluidE                m_fluid = FluidE::NONE;
    std::map<int, Chunk>  m_chunks;
    std::future<Geometry> m_pending;           ///< the chunk being built off the main thread
    int                   m_pendingK = 0;
    GroundFrame           m_frame;
    Shader                m_ground = {}, m_sky = {}, m_cloud = {};
    Material              m_groundMat = {};
    bool                  m_hasShaders = false;
    Texture2D             m_noiseTex = {};
    Texture2D             m_cloudTex = {};
    bool                  m_hasNoise = false, m_hasCloudTex = false;
    double                m_scroll = 0.0;
    int                   m_drawnVerts = 0, m_drawnMeshes = 0;
    int                   m_loc[48] = {};       ///< the ground shader's uniforms (Terrain.cpp, GroundLocE)
    int                   m_skyLoc[16] = {};    ///< the sky's (SkyLocE)
    int                   m_cloudLoc[16] = {};  ///< the clouds' (CloudLocE)
};

/** @brief The length of a ground chunk (world units). */
constexpr float ChunkLen = 200.0f;

}   // namespace zenith
