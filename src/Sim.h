/**
 * @file Sim.h
 * @brief The game itself, without pictures or sound: the field, the pilots, the enemies and their bullets, the
 *        mission's timeline. It steps 60 times a second and is the same on every machine for the same seed, so it
 *        runs headless too (the autopilot's balance runs, the tests). What the eyes and ears need it leaves as
 *        events (explosions, hits, shots) for the renderer and the sound.
 * @author Rene Weller
 */
#pragma once

#include "Data.h"

#include <string>
#include <vector>

namespace zenith
{

/** @brief How the game is played. */
enum class ModeE : int
{
    CAMPAIGN,   ///< the full game: workshop, money, log crystals, branches
    ARCADE,     ///< lives, power-ups from the enemies, no workshop
    BOSSRUSH,   ///< the bosses one after another
    PREVIEW     ///< the workshop's test range: one pilot, a target
};

/** @brief What a pilot does in a step (from the pad, the keys, the mouse or the autopilot). */
struct PlayerInput
{
    float   m_x = 0.0f, m_y = 0.0f;      ///< the stick: -1..1 (y down)
    bool    m_fire = false;              ///< the trigger held
    bool    m_focus = false;             ///< slow, precise flight
    bool    m_mode = false;              ///< pressed: the rear gun's other mode
    bool    m_special = false;           ///< pressed: the special
    bool    m_mouse = false;             ///< fly to m_target instead
    Vector2 m_target = { 0, 0 };         ///< where the mouse points (field units)
};

/** @brief Sim's messages to the renderer and the sound. */
enum class EvE : int
{
    EXPLODE,     ///< m_a: 0 small .. 3 huge; m_color
    HIT,         ///< a bullet hit an enemy
    SPARK,       ///< a bullet hit armour that does not care
    SHOT,        ///< a pilot's gun fired (m_name: its sound)
    ENEMYSHOT,   ///< an enemy fired
    PLAYERHIT,   ///< the pilot was hit (m_a: the damage, m_b: 1 if the shield took it all)
    PLAYERDIE,   ///< the pilot is gone
    PICKUP,      ///< something taken (m_name: what)
    TEXT,        ///< a radio line (m_name: the story key)
    WARN,        ///< a boss comes
    BOSSDIE,     ///< a boss is down
    MUSIC,       ///< change the music (m_name)
    LASER,       ///< an enemy laser starts (warning first)
    CHAIN,       ///< a lightning arc from m_pos to m_to
    NOVA,        ///< a special went off (m_name: its kind)
    SHAKE,       ///< the screen shakes (m_a: strength)
    MODE,        ///< the rear gun changed its mode (m_a: 1 or 2)
    THEME,       ///< the ground changes (m_name: the theme)
    EMPTY        ///< the special has no charges left
};

/** @brief One message. */
struct SimEvent
{
    EvE         m_type;
    Vector2     m_pos = { 0, 0 };
    Vector2     m_to = { 0, 0 };
    float       m_a = 0.0f, m_b = 0.0f;
    Color       m_color = WHITE;
    std::string m_name;
    bool        m_ground = false;
};

/** @brief How an enemy moves (from its path line). */
struct PathState
{
    int                  m_kind = 0;      ///< which path
    float                m_speed = 160.0f;
    float                m_amp = 80.0f, m_freq = 0.5f;
    float                m_stop = 160.0f, m_wait = 3.0f;
    float                m_dir = 0.0f, m_turn = 90.0f;
    float                m_cx = 360.0f, m_cy = 300.0f, m_r = 120.0f;
    float                m_dur = 4.0f, m_accel = 0.0f;
    int                  m_side = 1;      ///< 1 from the left, -1 from the right
    float                m_exit = 0.0f;   ///< leave direction for hover (0 down, 180 up)
    std::vector<Vector2> m_pts;           ///< spline points
    float                m_phase = 0.0f;
    bool                 m_drive = false; ///< a ground enemy that also drives (dir, speed given)
};

/** @brief An enemy on the field. */
struct Enemy
{
    const EnemyDef    *m_def = nullptr;
    int                m_id = 0;
    Vector2            m_pos = { 0, 0 };
    Vector2            m_vel = { 0, 0 };
    Vector2            m_base = { 0, 0 };   ///< where its path is measured from
    float              m_t = 0.0f;          ///< seconds alive
    float              m_hp = 1.0f, m_hpMax = 1.0f;
    float              m_face = 0.0f;       ///< heading in degrees (0 = down the screen)
    float              m_head = 0.0f;       ///< the turning head's heading
    float              m_bank = 0.0f;       ///< leaning into a turn (-1..1)
    float              m_flash = 0.0f;      ///< the hit's white, fading
    int                m_parent = -1;       ///< index of the enemy it is part of
    Vector2            m_off = { 0, 0 };    ///< where on the parent
    bool               m_ground = false;
    bool               m_dead = false;      ///< destroyed (counts as a kill)
    bool               m_gone = false;      ///< left the field
    bool               m_entered = false;   ///< has been on the field
    PathState          m_path;
    std::vector<float> m_fireT;             ///< each fire line's countdown
    std::vector<int>   m_fireN;             ///< volleys fired
    std::vector<int>   m_burstLeft;
    float              m_spin = 0.0f;       ///< ring/spiral turning
    float              m_hpScale = 1.0f;
    std::vector<DropDef> m_extraDrops;      ///< drops the wave added
    int                m_wave = -1;         ///< which wave (all of a wave killed: a bonus)
    float              m_lift = 0.0f;       ///< parts: drawn this far above the parent's plane
    std::string        m_faceAs;            ///< the wave's own facing (instead of the enemy's)
};

/** @brief A bullet (the pilot's or an enemy's). */
struct Bullet
{
    Vector2     m_pos = { 0, 0 };
    Vector2     m_vel = { 0, 0 };
    Vector2     m_origin = { 0, 0 };
    float       m_radius = 4.0f;
    float       m_damage = 1.0f;
    float       m_age = 0.0f, m_life = 3.0f;
    float       m_accel = 0.0f;
    float       m_home = 0.0f;       ///< degrees a second towards a target
    float       m_amp = 0.0f, m_freq = 0.0f;
    float       m_explode = 0.0f;    ///< blast radius on impact
    float       m_drag = 0.0f;
    float       m_spin = 0.0f;       ///< orbiting shots: degrees a second about the ship
    int         m_pierce = 0;        ///< enemies it passes through
    int         m_bounce = 0;        ///< walls it comes back from
    int         m_split = 0;         ///< splits into this many on impact
    int         m_sprite = 0;        ///< how it looks (Render's table)
    Color       m_color = WHITE;
    bool        m_player = false;
    int         m_owner = 0;         ///< which pilot
    int         m_lastHit = -1;      ///< the enemy it hit last (piercing shots hit each once)
    bool        m_dead = false;
    bool        m_mine = false;
    float       m_slow = 0.0f;       ///< flame: slows to a stop
    float       m_length = 0.0f;     ///< drawn this long along its way
    float       m_phase = 0.0f;      ///< weaving shots: where in their swing they start (radians)
};

/** @brief A pilot's beam this step (the renderer draws it from its muzzle up the field). */
struct BeamShot
{
    Vector2 m_from = { 0, 0 };
    float   m_width = 10.0f;
    Color   m_color = WHITE;
};

/** @brief An enemy laser beam. */
struct Laser
{
    int     m_owner = -1;            ///< the enemy
    Vector2 m_off = { 0, 0 };        ///< the muzzle on it
    float   m_dir = 0.0f;            ///< degrees
    float   m_sweep = 0.0f;          ///< degrees a second it turns
    bool    m_aim = false;
    float   m_warn = 0.8f, m_dur = 1.0f, m_width = 14.0f;
    float   m_t = 0.0f;
    float   m_damage = 30.0f;        ///< a second
    Color   m_color = { 255, 60, 80, 255 };
    bool    m_dead = false;
    Vector2 m_from = { 0, 0 };       ///< where it starts this step (for the renderer)
};

/** @brief What lies on the field to be taken. */
enum class PickE : int
{
    CREDITS, POWER, SHIELD, ARMOR, WEAPON, REAR, SIDE, LIFE, SPECIAL, CUBE, KEY, PLAN
};

/** @brief A pickup. */
struct Pickup
{
    PickE       m_kind = PickE::CREDITS;
    Vector2     m_pos = { 0, 0 };
    Vector2     m_vel = { 0, 0 };
    int         m_value = 0;
    std::string m_item;
    float       m_t = 0.0f;
    bool        m_dead = false;
};

/** @brief A sidekick in flight. */
struct Sidekick
{
    const SidekickDef *m_def = nullptr;
    int                m_side = -1;      ///< -1 left, 1 right
    Vector2            m_pos = { 0, 0 };
    float              m_t = 0.0f;       ///< fire countdown
    int                m_ammo = 0;
    float              m_reload = 0.0f;
    float              m_charge = 0.0f;
    int                m_volley = 0;
};

/** @brief A pilot on the field. */
struct Player
{
    bool                m_active = false;
    Loadout             m_load;
    const ShipDef      *m_ship = nullptr;
    const WeaponDef    *m_front = nullptr;
    const WeaponDef    *m_rear = nullptr;
    const GeneratorDef *m_gen = nullptr;
    const ShieldDef    *m_shieldDef = nullptr;
    const SpecialDef   *m_special = nullptr;
    Sidekick            m_side[2];
    Vector2             m_pos = { FieldW * 0.5f, FieldH - 120.0f };
    Vector2             m_vel = { 0, 0 };
    float               m_armor = 20.0f, m_armorMax = 20.0f;
    float               m_shield = 0.0f;
    float               m_energy = 0.0f, m_energyMax = 1.0f;
    float               m_shieldWait = 0.0f;
    float               m_frontT = 0.0f, m_rearT = 0.0f;
    int                 m_frontVolley = 0, m_rearVolley = 0;
    int                 m_rearMode = 1;
    int                 m_charges = 0;
    float               m_invuln = 0.0f;     ///< seconds without harm (after a respawn)
    float               m_bank = 0.0f;
    bool                m_alive = true;
    float               m_dead = 0.0f;       ///< seconds since death
    int                 m_lives = 0;         ///< arcade
    long long           m_score = 0;
    int                 m_credits = 0;       ///< money taken in this mission
    bool                m_firing = false;
    bool                m_beam = false;      ///< a beam gun burns this step
    float               m_beamWidth = 0.0f;
    Color               m_beamColor = WHITE;
    std::vector<BeamShot> m_beams;           ///< every beam burning this step (guns, rear guns, sidekicks)
    float               m_barrier = 0.0f;    ///< special: seconds of the barrier
    float               m_drones = 0.0f;     ///< special: seconds of the drone swarm
    float               m_magnet = 0.0f;
    int                 m_hits = 0;          ///< hits taken this mission
    float               m_damageTaken = 0.0f;
};

/** @brief What a mission came to. */
struct Stats
{
    int                      m_spawned = 0;     ///< enemies that could be killed
    int                      m_kills = 0;
    int                      m_credits = 0;     ///< money taken
    int                      m_bossKills = 0;
    std::vector<std::string> m_cubes;           ///< log crystals found
    std::vector<std::string> m_keys;            ///< secret keys found
    std::vector<std::string> m_plans;           ///< plans found (new goods for the workshop)
    float                    m_time = 0.0f;     ///< seconds flown
    float                    m_damage = 0.0f;   ///< damage taken
    int                      m_deaths = 0;
};

/** @brief The field and all on it. */
class World
{
public:
    /**
     * @brief Sets a mission up.
     * @param[in] cat         the catalog
     * @param[in] level       the mission (nullptr for the test range)
     * @param[in] mode        how it is played
     * @param[in] difficulty  the difficulty's index
     * @param[in] seed        the random seed
     */
    void begin( const Catalog *cat, const LevelDef *level, ModeE mode, int difficulty, uint32_t seed );

    /**
     * @brief Puts a pilot on the field.
     * @param[in] i      0 or 1
     * @param[in] load   the gear
     * @param[in] lives  arcade lives (0 in the campaign)
     */
    void addPlayer( int i, const Loadout &load, int lives );

    /** @brief Fits a pilot's gear anew (arcade power-ups) keeping armour and position. */
    void refit( int i );

    /**
     * @brief One step (1/60 s).
     * @param[in] in  the two pilots' input
     */
    void step( const PlayerInput in[2] );

    /**
     * @brief Puts an enemy on the field (missions, the test range, the spawn pattern).
     * @param[in] def     what
     * @param[in] pos     where
     * @param[in] path    its path line (nullptr: the enemy's own)
     * @param[in] parent  the enemy it belongs to (-1 none)
     * @return its index
     */
    int spawn( const EnemyDef *def, Vector2 pos, const Line *path, int parent = -1 );

    /** @return whether the mission is over (won) */
    bool finished() const { return m_finished; }
    /** @return whether all pilots are out (lost) */
    bool failed() const { return m_failed; }
    /** @return the boss on the field (-1 none) */
    int boss() const;

    const Catalog       *m_cat = nullptr;
    const LevelDef      *m_level = nullptr;
    ModeE                m_mode = ModeE::CAMPAIGN;
    int                  m_diffIndex = 2;
    DifficultyDef        m_diff;
    Rng                  m_rng;
    float                m_clock = 0.0f;       ///< seconds since the start
    float                m_time = 0.0f;        ///< the timeline (stands while a boss holds it)
    size_t               m_next = 0;           ///< the next event
    bool                 m_hold = false;       ///< a boss holds the timeline
    float                m_scroll = 60.0f;     ///< the ground's speed now
    float                m_scrollGoal = 60.0f;
    double               m_scrollPos = 0.0;    ///< how far the ground has come (world units)
    float                m_slow = 1.0f;        ///< the special's slowed time (1: none)
    float                m_slowT = 0.0f;
    std::vector<Enemy>   m_enemies;
    std::vector<Bullet>  m_bullets;
    std::vector<Laser>   m_lasers;
    std::vector<Pickup>  m_pickups;
    Player               m_players[2];
    std::vector<SimEvent> m_events;            ///< this step's messages (cleared at the next step)
    Stats                m_stats;
    std::string          m_music;
    std::string          m_bossTitle;
    float                m_bossShown = 0.0f;   ///< the boss bar's fill (0..1)
    float                m_endT = -1.0f;       ///< counting down to the end
    bool                 m_finished = false;
    bool                 m_failed = false;
    int                  m_nextId = 1;
    int                  m_waves = 0;
    std::vector<int>     m_waveLeft;           ///< enemies of each wave still alive and unkilled
    std::vector<int>     m_waveKilled;
    std::vector<int>     m_waveSize;
    int                  m_powerKills = 0;     ///< arcade: kills since the last power-up
    bool                 m_noTimeline = false; ///< the test range: no events
    bool                 m_immortal = false;   ///< balance runs: the pilot cannot die (damage is still counted)
    bool                 m_holdLast = false;   ///< the boss that held the timeline is gone: the end follows

    /** @brief An enemy waiting to enter (a column's later ships). */
    struct Pending
    {
        float                m_t = 0.0f;   ///< when (m_clock)
        const EnemyDef      *m_def = nullptr;
        Vector2              m_pos = { 0, 0 };
        Line                 m_path;
        int                  m_wave = -1;
        float                m_hp = 1.0f;
        std::vector<DropDef> m_drops;
    };
    std::vector<Pending>  m_pending;
    std::vector<std::pair<float, size_t>> m_free;   ///< events a boss does not hold: (clock, index)
    std::vector<char>     m_freeDone;              ///< ... marked so the timeline skips them
    std::vector<Bullet>   m_born;              ///< bullets made while others are moved (joined after the step)
    std::vector<DropDef>  m_waveDrop;          ///< what a wave leaves when all of it is killed
    std::vector<Vector2>  m_waveLast;          ///< where its last one died
    std::vector<int>      m_waveBonus;         ///< credits for all of it (-1: by its size)
    std::vector<char>     m_shut;              ///< enemies whose shield holds while their parts live (this step)

    /** @brief Marks the shielded bodies whose parts still live (m_shut). */
    void markShut();

private:
    void runTimeline();
    void runEvent( const LevelEvent &ev );
    void spawnWave( const Line &p, bool ground );
    void movePlayer( int i, const PlayerInput &in );
    void firePlayer( int i, const PlayerInput &in );
    void fireShots( int i, const std::vector<ShotDef> &shots, int level, int maxLevel, int mode, int volley,
                    Vector2 from, float baseDir, bool sidekick, const Sidekick *sk );
    void useSpecial( int i );
    void moveSidekicks( int i, const PlayerInput &in );
    void moveEnemies();
    void moveEnemy( Enemy &e );
    void fireEnemy( int idx );
    void enemyBullet( const Enemy &e, const FireDef &f, Vector2 at, float dir, float speed );
    void moveBullets();
    void moveLasers();
    void movePickups();
    void collide();
    void hitEnemy( int idx, float dmg, int owner, Vector2 at );
    void killEnemy( int idx, int owner );
    void hurtPlayer( int i, float dmg, Vector2 at );
    void drop( const Enemy &e, const DropDef &d );
    void dropAt( PickE k, Vector2 at, int value, const std::string &item );
    void take( int i, Pickup &p );
    void compact();
    void emit( EvE t, Vector2 at, float a = 0, float b = 0, const std::string &name = "", Color c = WHITE );
    int  nearestEnemy( Vector2 from, float range, int skip = -1 ) const;
    int  nearestPlayer( Vector2 from ) const;
    Vector2 worldPos( const Enemy &e, Vector2 off ) const;
};

/**
 * @brief The autopilot: flies a pilot about, keeps firing, dodges bullets and lasers, takes pickups.
 * @param[in] w  the field
 * @param[in] i  the pilot
 * @return its input
 */
PlayerInput autopilot( const World &w, int i );

/** @brief A mission flown by the autopilot. */
struct SimResult
{
    std::string m_level;
    bool        m_won = false;
    float       m_time = 0.0f;
    int         m_kills = 0, m_spawned = 0, m_credits = 0;
    float       m_damage = 0.0f;
    int         m_deaths = 0;
    float       m_bossTime = 0.0f;   ///< seconds the bosses held the timeline
    int         m_cubes = 0, m_keys = 0, m_plans = 0;   ///< what was picked up
};

/**
 * @brief Flies a mission headless with the autopilot (balance and tests).
 * @param[in] cat         the catalog
 * @param[in] level       the mission
 * @param[in] difficulty  the difficulty
 * @param[in] load        the gear
 * @param[in] seed        the seed
 * @param[in] immortal    the pilot cannot die (to measure a whole mission)
 * @return what happened
 */
SimResult simulate( const Catalog &cat, const LevelDef &level, int difficulty, const Loadout &load, uint32_t seed,
                    bool immortal );

/**
 * @brief The boss rush's mission: the great bosses (each episode's _05, _09 and _10 missions) one after another,
 *        each episode over its own ground.
 * @param[in] cat  the catalog
 * @return the mission (name "bossrush")
 */
LevelDef bossRush( const Catalog &cat );

/** @return the sprite index of a bullet kind name (orb, needle, big ...) */
int spriteOf( const std::string &name );

}   // namespace zenith
