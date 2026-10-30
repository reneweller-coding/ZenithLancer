/**
 * @file Terrain.h
 * @brief The world under the field: a ground scrolling by (relief, water or lava, cities, forests, plating, craters)
 *        or, out in space, layers of stars, nebulae and drifting rocks. Its look comes from a theme block in
 *        data/themes.txt (docs/DATA.md, "Themes"). The ground lies GroundDepth below the field and is drawn with
 *        the same lit shader as the ships.
 * @author Rene Weller
 */
#pragma once

#include "Data.h"

#include <raylib.h>

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

    /** @brief Lets go of the meshes. */
    void unload();

    /**
     * @brief Draws it (inside BeginMode3D, with the lit shader bound to the given material).
     * @param[in] mat     the lit material
     * @param[in] scroll  how far the ground has come
     * @param[in] time    seconds (water, lava)
     */
    void draw( Material &mat, double scroll, float time );

    /**
     * @brief Draws what lies above the ground and below the field (clouds, drifting smoke) with alpha.
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

private:
    float groundHeight( float x, float g ) const;
    Color groundColor( float x, float g, float h, float slope ) const;
    Mesh  buildChunk( int k );
    void  buildSpace();

    std::string         m_name;
    Look                m_look;
    Block               m_theme;
    uint32_t            m_seed = 1;
    float               m_halfW = 1200.0f;
    float               m_relief = 60.0f;
    float               m_scale = 0.004f;
    float               m_water = -1e9f;      ///< water / lava level (relative height), -1e9 none
    Color               m_waterColor = { 30, 70, 110, 255 };
    float               m_waterGlow = 0.0f;
    Color               m_low = { 70, 60, 50, 255 }, m_mid = { 110, 96, 70, 255 }, m_high = { 150, 140, 120, 255 },
                        m_peak = { 220, 220, 225, 255 };
    Color               m_glow = { 255, 120, 40, 255 };
    float               m_glowAmount = 0.0f;
    float               m_city = 0.0f, m_trees = 0.0f, m_craters = 0.0f, m_plates = 0.0f, m_rocks = 0.0f;
    float               m_ridge = 0.0f;
    Color               m_propColor = { 120, 120, 130, 255 };
    Color               m_treeColor = { 40, 90, 50, 255 };
    Color               m_cloudColor = { 255, 255, 255, 255 };
    float               m_clouds = 0.0f;
    std::map<int, Mesh> m_chunks;
    std::vector<Mesh>   m_stars;               ///< star layers (space)
    std::vector<float>  m_starDepth;
    Texture2D           m_cloudTex = {};
    bool                m_hasCloudTex = false;
    Texture2D           m_nebulaTex = {};
    bool                m_hasNebula = false;
    Color               m_nebulaA = { 60, 30, 120, 255 }, m_nebulaB = { 20, 80, 140, 255 };
};

/** @brief The length of a ground chunk (world units). */
constexpr float ChunkLen = 200.0f;

}   // namespace zenith
