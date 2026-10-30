/**
 * @file Render.h
 * @brief Draws a World: the ground or the stars, shadows, the ships (lit facets, glowing parts, hit flashes,
 *        turning turrets, leaning into turns), exhaust, bullets and beams as glowing sprites, explosions with
 *        sparks, smoke, debris, shock rings and their light, then bloom and the tone curve. It keeps its own
 *        particles and lights, fed from the World's events after every step.
 * @author Rene Weller
 */
#pragma once

#include "Forge.h"
#include "Sim.h"
#include "Terrain.h"

#include <raylib.h>

#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace zenith
{

/** @brief A bit of light or smoke (field coordinates). */
struct Particle
{
    Vector2 m_pos = { 0, 0 }, m_vel = { 0, 0 };
    float   m_age = 0.0f, m_life = 0.5f;
    float   m_size0 = 8.0f, m_size1 = 2.0f;
    Color   m_c0 = WHITE, m_c1 = { 255, 80, 20, 0 };
    int     m_tex = 0;          ///< the sprite (Renderer's table)
    bool    m_add = true;       ///< added light; else blended (smoke)
    float   m_drag = 0.0f;
    float   m_rot = 0.0f, m_spin = 0.0f;
    float   m_stretch = 0.0f;   ///< drawn longer along its way
    bool    m_ground = false;   ///< moves with the ground
    float   m_boost = 2.0f;     ///< brightness (for the bloom)
};

/** @brief A short-lived light (explosions, muzzles). */
struct GlowLight
{
    Vector3 m_pos = { 0, 0, 0 };
    Color   m_color = WHITE;
    float   m_radius = 200.0f;
    float   m_power = 2.0f;
    float   m_age = 0.0f, m_life = 0.3f;
};

/** @brief A piece of a wreck tumbling down (3D). */
struct Debris
{
    Vector3 m_pos = { 0, 0, 0 }, m_vel = { 0, 0, 0 }, m_axis = { 0, 1, 0 };
    float   m_rot = 0.0f, m_spin = 0.0f, m_age = 0.0f, m_life = 1.0f, m_size = 3.0f;
    Color   m_color = GRAY;
};

/** @brief The picture of a World. */
class Renderer
{
public:
    /**
     * @brief Makes the targets for a picture size (the shaders and sprites are shared and made once).
     * @param[in] w      width in pixels
     * @param[in] h      height
     * @param[in] scale  the share of that size actually drawn (the graphics setting; present() scales it up)
     */
    void init( int w, int h, float scale = 1.0f );

    /**
     * @brief Looks at a part of the field only (the workshop's test range): a field point in the middle of the
     *        picture and half the height shown, in field units. init() shows the whole field.
     * @param[in] center  the field point in the middle
     * @param[in] halfH   half the height shown
     */
    void setView( Vector2 center, float halfH );

    /** @brief Lets go of the targets (and, for the last renderer, the shared things). */
    void shutdown();

    /**
     * @brief Sets the look for a mission (its theme's ground and light).
     * @param[in] cat    the catalog
     * @param[in] theme  the theme
     * @param[in] seed   the mission's seed
     */
    void setTheme( const Catalog &cat, const std::string &theme, uint32_t seed );

    /**
     * @brief Takes the World's events of the last step (particles, lights, shakes).
     * @param[in] w  the field
     */
    void events( const World &w );

    /**
     * @brief Moves the particles, lights and debris on (once a frame).
     * @param[in] w   the field
     * @param[in] dt  seconds since the last frame
     */
    void update( const World &w, float dt );

    /**
     * @brief Draws the World into the HDR target and through the bloom into the output target.
     * @param[in] w     the field
     * @param[in] cat   the catalog
     */
    void draw( const World &w, const Catalog &cat );

    /** @return the finished picture (draw it flipped: render textures are upside down) */
    const RenderTexture2D &output() const { return m_out; }

    /**
     * @brief Draws the finished picture to the screen.
     * @param[in] dst  where
     */
    void present( Rectangle dst ) const;

    /** @return a field point in the output picture's pixels */
    Vector2 toPixels( Vector2 field ) const;

    /** @return the field point in the picture's middle (setView) */
    Vector2 viewCenter() const { return m_center; }

    /** @return half the height shown, in field units (setView) */
    float viewHalfH() const { return m_halfH; }

    /** @return pixels per field unit (in the size given to init) */
    float pixelScale() const { return m_scale * m_up; }

    /** @return the field's rectangle in the output picture's pixels */
    Rectangle fieldRect() const;

    /** @return the picture's size (as given to init) */
    int width() const { return (int)std::lround( m_w * m_up ); }
    int height() const { return (int)std::lround( m_h * m_up ); }

    /** @brief Clears particles and lights (a new mission). */
    void reset();

    float m_shakeAmount = 1.0f;   ///< the options' screen shake (0 off)
    float m_hurt = 0.0f;          ///< the red of a hit, fading
    bool  m_showHitbox = false;   ///< draw the pilot's hit circle
    float m_time = 0.0f;
    float m_fade = 0.0f;          ///< 0 clear .. 1 black (mission start and end)
    int   m_quality = 2;          ///< 0 performance, 1 balanced, 2 quality (bloom passes, particles; the size is init's)
    int   m_bulletStyle = 0;      ///< enemy bullets: 0 as made, 1 strong, 2 for red-green, 3 for blue-yellow colour blindness
    bool  m_softFlash = false;    ///< flashes, the hit's red and explosions' light dimmed
    Color m_paint[2] = { WHITE, WHITE };      ///< each pilot's paint (the hull and sidekicks in its colour)
    float m_paintAmount[2] = { 0.0f, 0.0f };  ///< ... how much of it (0: the hull's own colours)
    /** @brief Where the ghost (the best flight of the same seed) flies: drawn when m_ghostOn. */
    Vector2 m_ghost = { 0, 0 };
    bool    m_ghostOn = false;

    static Forge s_forge;         ///< the models, shared by every renderer
    /** @brief A picture being drawn around the renderer (pictures of screens): drawn into again after draw(). */
    static RenderTexture2D *s_outer;

private:
    static constexpr int MaxGlowLights = 16;   ///< the lights a frame hands the shaders

    Matrix modelMatrix( Vector3 at, float face, float bank, float scale, float pitch ) const;
    void   drawMeshes( const ModelMesh &m, const Matrix &rest, float face, float head, Material &mat );
    void   drawModel( const ModelMesh &m, Vector3 at, float face, float bank, float scale, float flash, float head,
                      Color tint, float pitch = 0.0f );
    void   drawShadows( const World &w, const Catalog &cat );
    void   drawSprites( const World &w, const Catalog &cat );
    void sprite( int tex, Vector2 field, float size, Color c, float rot = 0.0f, float stretch = 0.0f,
                 Vector2 dir = { 0, 1 } );
    void beam( Vector2 a, Vector2 b, float width, Color c );
    void explode( Vector2 at, int size, bool ground, Color tint );
    void post();
    Vector3 groundPoint( Vector2 field, double scroll ) const;
    void setLights( const World &w );

    int               m_w = 0, m_h = 0;     ///< the targets' size
    float             m_up = 1.0f;        ///< init's size over the targets'
    float             m_scale = 1.0f;     ///< pixels per field unit (in the targets)
    float             m_halfH = FieldH * 0.5f;
    Vector2           m_center = { FieldW * 0.5f, FieldH * 0.5f };   ///< the field point in the picture's middle
    RenderTexture2D   m_hdr = {};
    RenderTexture2D   m_out = {};
    RenderTexture2D   m_shadow = {};          ///< what flies, cast along the sun onto the ground's plane
    Vector4           m_shadowRect = { 0, 0, 1, 1 };
    Vector4           m_lightPos[MaxGlowLights] = {}, m_lightCol[MaxGlowLights] = {};
    int               m_lightN = 0;
    std::vector<std::pair<float, int>> m_lightOrder;   ///< scratch for picking the lights (kept: no allocations)
    std::vector<RenderTexture2D> m_bloom;
    Camera3D          m_cam = {};
    Terrain           m_terrain;
    std::vector<Particle>  m_parts;
    std::vector<GlowLight> m_lights;
    std::vector<Debris>    m_debris;
    float             m_shake = 0.0f;
    Vector2           m_shakeOff = { 0, 0 };
    Rng               m_rng;
    double            m_scroll = 0.0;
    float             m_groundV = 0.0f;
    Vector4           m_paintNow = { 0, 0, 0, 0 };   ///< the paint of the model drawn now (the shader's "paint")
};

}   // namespace zenith
