/**
 * @file Audio.h
 * @brief The sound: effects synthesised at the start (no sample files: the guns, hits, explosions, pickups, the
 *        boss siren, the menus), panned by where they happen on the field; and the music, streamed from
 *        assets/music/<name>.ogg (or .mp3, .wav) with a crossfade between pieces. Without an audio device (--mute)
 *        every call does nothing.
 * @author Rene Weller
 */
#pragma once

#include "Sim.h"

#include <raylib.h>

#include <string>

namespace zenith
{

/** @brief The sound effects. */
enum class SfxE : int
{
    PULSE, LASER, MISSILE, PLASMA, FLAME, BEAM, SPREAD, RAIL, CHAIN,
    ENEMYSHOT, ENEMYLASER, HIT, SPARK,
    BOOM0, BOOM1, BOOM2, BOOM3,
    CREDITS, POWER, ITEM, CUBE, LIFE,
    SHIELDHIT, ARMORHIT, DIE,
    SIREN, NOVA, MODE, EMPTY,
    MOVE, OK, BACK, BUY, DENY, BLIP,
    COUNT
};

/** @brief The sound. */
class Audio
{
public:
    /**
     * @brief Synthesises the effects (the device must be open).
     * @param[in] on      false: stay silent
     * @param[in] assets  the assets folder (music)
     */
    void init( bool on, const std::string &assets );

    /** @brief Lets go of everything. */
    void shutdown();

    /**
     * @brief Plays an effect.
     * @param[in] s       the effect
     * @param[in] x       where across the field (pans it), -1 the middle
     * @param[in] volume  0..1
     * @param[in] pitch   1 as made
     */
    void play( SfxE s, float x = -1.0f, float volume = 1.0f, float pitch = 1.0f );

    /**
     * @brief Plays what the World's last step made heard.
     * @param[in] w  the field
     */
    void events( const World &w );

    /**
     * @brief Changes the music (with a crossfade); "" fades it out.
     * @param[in] name  a file in assets/music without its ending
     */
    void music( const std::string &name );

    /** @brief Keeps the music running (every frame). */
    void update( float dt );

    /**
     * @brief Writes every effect as a WAV (to listen to them, to level them).
     * @param[in] dir  the folder
     */
    static void dump( const std::string &dir );

    float m_musicVolume = 0.7f;
    float m_effectVolume = 0.8f;
    bool  m_on = false;

private:
    static constexpr int Voices = 8;
    Sound       m_sounds[(int)SfxE::COUNT][Voices] = {};
    int         m_next[(int)SfxE::COUNT] = {};
    double      m_last[(int)SfxE::COUNT] = {};
    std::string m_assets;
    Music       m_music[2] = {};
    bool        m_has[2] = { false, false };
    float       m_gain[2] = { 0, 0 };
    int         m_cur = 0;
    std::string m_name;
};

}   // namespace zenith
