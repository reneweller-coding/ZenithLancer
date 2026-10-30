/**
 * @file Data.h
 * @brief Everything the game is made of, read from the text files in data/: models (how a ship is built from parts),
 *        enemies, the workshop's goods (front and rear guns, sidekicks, hulls, generators, shields, specials), the
 *        missions and episodes, the stories, the difficulties and the arcade mode. docs/DATA.md describes the files.
 *
 *        Every file is lines of words. A line whose first word names a block ("enemy dart") starts that block; the
 *        lines after it belong to it. "key=value" words are named values, other words are in order; "quoted text"
 *        is one word; "#" starts a comment; a line starting with ">" is a line of prose for the block. A number may
 *        be a range "a..b": a gun's shot line then grows from a at its first power level to b at its last.
 * @author Rene Weller
 */
#pragma once

#include "Util.h"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace zenith
{

/** @brief A number that may grow over a range of power levels: "4" or "4..9". */
struct Range
{
    float m_a = 0.0f;   ///< at the first level
    float m_b = 0.0f;   ///< at the last

    /** @return the value at t in [0, 1] */
    float at( float t ) const { return m_a + ( m_b - m_a ) * t; }
};

/** @brief One line of a data file. */
struct Line
{
    std::string                                  m_key;    ///< its first word
    std::vector<std::string>                     m_args;   ///< the words after it without a "="
    std::unordered_map<std::string, std::string> m_kv;     ///< the "key=value" words
    std::string                                  m_file;   ///< where it stands (for messages)
    int                                          m_line = 0;

    /** @return whether a named value is there */
    bool has( const std::string &k ) const { return m_kv.count( k ) > 0; }
    /** @return a named value as text */
    std::string str( const std::string &k, const std::string &def = "" ) const;
    /** @return a named value as a number (a range gives its first value) */
    float num( const std::string &k, float def = 0.0f ) const;
    /** @return a named value as a whole number */
    int integer( const std::string &k, int def = 0 ) const { return (int)std::lround( num( k, (float)def ) ); }
    /** @return a named value as a range ("a..b"; a plain number is a range of one value) */
    Range range( const std::string &k, float def = 0.0f ) const;
    /** @return a named value as a colour */
    Color color( const std::string &k, Color def = WHITE ) const;
    /** @return the i-th word after the key ("" if missing) */
    std::string arg( size_t i, const std::string &def = "" ) const { return i < m_args.size() ? m_args[i] : def; }
    /** @return the i-th word as a number */
    float argNum( size_t i, float def = 0.0f ) const;
    /** @return "file:line" */
    std::string where() const { return m_file + ":" + std::to_string( m_line ); }
};

/** @brief A block of lines: "enemy dart" and what follows it. */
struct Block
{
    std::string              m_type;    ///< its first word (enemy, model, level ...)
    std::string              m_name;    ///< its second word
    Line                     m_head;    ///< the first line itself (named values on it count)
    std::vector<Line>        m_lines;   ///< the lines after it
    std::vector<std::string> m_prose;   ///< its ">" lines
};

/**
 * @brief Reads a data file into blocks.
 * @param[in]  path    the file
 * @param[in]  types   the words that start a block
 * @param[out] out     the blocks (appended)
 * @param[out] errors  messages (appended)
 * @return false if the file could not be read
 */
bool readBlocks( const std::string &path, const std::vector<std::string> &types, std::vector<Block> &out,
                 std::vector<std::string> &errors );

/** @brief How a model is built: its parts (docs/DATA.md, "Models"). The Forge turns it into meshes. */
struct ModelDef
{
    std::string       m_name;
    float             m_scale = 1.0f;   ///< all of it larger or smaller
    std::vector<Line> m_parts;          ///< "part <kind> key=value ..."
    std::string       m_where;          ///< file:line
};

/** @brief What an enemy is. */
enum class EnemyKindE : int
{
    AIR,        ///< flies on the field
    GROUND,     ///< stands on the ground below and moves with it
    BOSS,       ///< a boss: the mission waits for it, a bar shows its strength
    PART,       ///< a part of another enemy (a boss's gun, a carrier's turret)
    OBSTACLE    ///< a rock, a girder: in the way, maybe breakable
};

/** @brief One way an enemy shoots (docs/DATA.md, "Fire patterns"). */
struct FireDef
{
    std::string m_pattern;          ///< aimed, fan, ring, spiral, down, laser, missile, mine, spawn, wall, rain
    float       m_every = 1.5f;     ///< seconds between volleys
    float       m_delay = 0.6f;     ///< before the first
    float       m_speed = 200.0f;   ///< bullet speed (field units a second)
    int         m_n = 1;            ///< bullets a volley
    float       m_spread = 0.0f;    ///< degrees between them (fan, aimed)
    float       m_dir = 0.0f;       ///< direction (degrees, 0 = down) where not aimed
    std::string m_bullet = "orb";   ///< orb, needle, big, ring, missile, mine, shard, wave
    Color       m_color = { 255, 90, 60, 255 };
    int         m_burst = 1;        ///< volleys in quick succession
    float       m_gap = 0.1f;       ///< seconds between them
    float       m_rot = 0.0f;       ///< degrees each volley turns (ring, spiral)
    float       m_accel = 0.0f;     ///< bullet speed change a second
    float       m_from = 1.0f;      ///< only at this share of strength or below (boss phases)
    float       m_until = 0.0f;     ///< only above this share
    float       m_x = 0.0f;         ///< the muzzle across the enemy (field units, in its frame)
    float       m_z = 0.0f;         ///< and along it (positive: forwards)
    bool        m_mirror = false;   ///< also from -x
    int         m_cap = 0;          ///< volleys at most (0: no end)
    std::string m_spawn;            ///< spawn: the enemy launched
    float       m_turn = 0.0f;      ///< missile: degrees a second it turns towards the player
    float       m_dur = 1.0f;       ///< laser: how long it burns
    float       m_warn = 0.8f;      ///< laser: the warning line before
    float       m_width = 14.0f;    ///< laser: its width
    float       m_size = 0.0f;      ///< bullet radius (0: by kind)
    float       m_damage = 0.0f;    ///< damage a bullet (0: by kind)
    float       m_life = 8.0f;      ///< seconds a bullet lives
    bool        m_onlyDiff = false; ///< present only from m_minDiff
    int         m_minDiff = 0;      ///< the lowest difficulty that fires this
    int         m_parts = -1;       ///< 0: only when all its parts are destroyed, 1: only while one lives
    int         m_maxDiff = 99;     ///< the highest difficulty that fires this (diff=A-B)
    int         m_aim = -1;         ///< lasers: 1 aimed, 0 along dir, -1 as dir/rot say (aimed when both are 0)
    Line        m_line;             ///< the line itself (spawn: its path values)
    std::string m_where;
};

/** @brief Something an enemy may leave behind. */
struct DropDef
{
    std::string m_what;           ///< credits, power, shield, armor, weapon, rear, side, life, special, cube, key, plan
    float       m_chance = 1.0f;  ///< 0..1
    int         m_value = 0;      ///< credits' worth
    std::string m_item;           ///< which gun, cube, key or plan
    std::string m_mode;           ///< campaign, arcade or "" (both)
};

/** @brief A part fixed to an enemy. */
struct PartRef
{
    std::string m_enemy;          ///< the part's own enemy entry (kind part)
    float       m_x = 0.0f;       ///< where on the parent (its frame, in the parent's model units: scaled with it)
    float       m_z = 0.0f;
    float       m_y = 0.0f;       ///< how far above the parent's plane it is drawn (tall hulls)
};

/** @brief An enemy. */
struct EnemyDef
{
    std::string          m_name;
    std::string          m_title;                    ///< a boss's name on its bar
    std::string          m_titleEn;                  ///< ... in English
    std::string          m_model;
    EnemyKindE           m_kind = EnemyKindE::AIR;
    float                m_hp = 10.0f;
    float                m_radius = 16.0f;
    int                  m_score = 50;
    float                m_speed = 160.0f;
    Line                 m_path;                     ///< its usual way (a wave may name another)
    std::string          m_face = "move";            ///< move, down, up, player, spin, fixed
    std::vector<FireDef> m_fire;
    std::vector<DropDef> m_drops;
    std::string          m_explode = "small";        ///< small, medium, large, huge
    float                m_contact = 20.0f;          ///< damage on ramming the player
    std::vector<PartRef> m_parts;
    bool                 m_shielded = false;         ///< cannot be hurt while its parts live
    bool                 m_aimHead = false;          ///< the model's "head" group turns to the player
    float                m_scale = 1.0f;             ///< the model's size
    bool                 m_solid = true;             ///< player bullets stop on it (an obstacle may let them pass)
    bool                 m_invulnerable = false;     ///< cannot be destroyed (obstacles)
    float                m_life = 40.0f;             ///< seconds it may stay on the field (bosses: always)
    std::string          m_where;
};

/** @brief One line of shots of a gun or sidekick; its numbers may be ranges over its levels. */
struct ShotDef
{
    std::string m_kind;     ///< row, fan, side, back, homing, wave, beam, bounce, chain, bomb, flame, orbit, ripple, burst
    int         m_from = 1; ///< the first power level it fires at
    int         m_to = 11;  ///< the last
    int         m_mode = 0; ///< rear guns: 1 or 2 (0 both)
    Line        m_p;        ///< its values
};

/** @brief A gun of the workshop (front or rear). */
struct WeaponDef
{
    std::string          m_name;
    std::string          m_title;
    std::string          m_desc;
    std::string          m_titleEn, m_descEn;        ///< in English
    std::string          m_slot = "front";   ///< front or rear
    int                  m_price = 500;
    Range                m_rate = { 6, 6 };   ///< volleys a second over the levels
    Range                m_energy = { 5, 5 }; ///< generator energy a volley
    std::string          m_sound = "pulse";   ///< its sound: pulse, laser, missile, plasma, flame, beam, spread, rail
    std::string          m_mode1, m_mode2;    ///< rear guns: the names of their two modes
    std::string          m_mode1En, m_mode2En; ///< ... in English
    int                  m_modes = 1;
    std::vector<ShotDef> m_shots;
    int                  m_tier = 1;          ///< which episode's workshop offers it first
    bool                 m_hidden = false;    ///< only after its plan is found
    std::string          m_where;
};

/** @brief A sidekick (an option flying beside the ship). */
struct SidekickDef
{
    std::string          m_name;
    std::string          m_title;
    std::string          m_desc;
    std::string          m_titleEn, m_descEn;        ///< in English
    int                  m_price = 500;
    std::string          m_model;
    std::string          m_place = "fixed";    ///< fixed, orbit, trail, front
    std::string          m_aim = "forward";    ///< forward, nearest
    float                m_rate = 3.0f;        ///< volleys a second
    int                  m_ammo = 0;           ///< shots it carries (0: endless)
    float                m_reload = 0.0f;      ///< seconds for one shot to come back
    float                m_charge = 0.0f;      ///< seconds to charge (it fires once charged, with the trigger)
    std::vector<ShotDef> m_shots;              ///< levels do not apply: level 1 is used
    int                  m_tier = 1;
    bool                 m_hidden = false;
    std::string          m_where;
};

/** @brief A hull. */
struct ShipDef
{
    std::string m_name;
    std::string m_title;
    std::string m_desc;
    std::string m_titleEn, m_descEn;   ///< in English
    int         m_price = 1000;
    std::string m_model;
    float       m_armor = 20.0f;
    float       m_speed = 330.0f;     ///< field units a second
    float       m_accel = 2600.0f;
    float       m_radius = 9.0f;      ///< the hit circle
    float       m_genBonus = 0.0f;    ///< more generator power (0.1 = 10 %)
    int         m_tier = 1;
    bool        m_hidden = false;
    std::string m_where;
};

/** @brief A generator: the energy the guns and the shield draw on. */
struct GeneratorDef
{
    std::string m_name, m_title, m_desc, m_where;
    std::string m_titleEn, m_descEn;   ///< in English
    int         m_price = 1000;
    float       m_power = 30.0f;      ///< energy a second
    float       m_capacity = 60.0f;   ///< what it can hold
    int         m_tier = 1;
    bool        m_hidden = false;
};

/** @brief A shield. */
struct ShieldDef
{
    std::string m_name, m_title, m_desc, m_where;
    std::string m_titleEn, m_descEn;   ///< in English
    int         m_price = 1000;
    float       m_max = 10.0f;      ///< points it holds
    float       m_regen = 2.0f;     ///< points a second it grows back
    float       m_cost = 3.0f;      ///< energy a point
    float       m_delay = 1.0f;     ///< seconds after a hit before it grows again
    int         m_tier = 1;
    bool        m_hidden = false;
};

/** @brief A special (a charge-limited power on its own button). */
struct SpecialDef
{
    std::string m_name, m_title, m_desc, m_where;
    std::string m_titleEn, m_descEn;   ///< in English
    int         m_price = 1000;
    std::string m_kind = "nova";   ///< nova, barrier, drones, timeslow, lance, repair, magnet
    int         m_charges = 2;     ///< charges a mission
    Line        m_p;               ///< its values
    int         m_tier = 1;
    bool        m_hidden = false;
};

/** @brief One thing that happens in a mission at a time. */
struct LevelEvent
{
    float       m_t = 0.0f;         ///< seconds after the start (the timeline stands while a boss lives)
    std::string m_type;             ///< wave, ground, boss, pickup, text, scroll, music, warn, end, clear
    Line        m_p;                ///< its values
    int         m_minDiff = 0;      ///< only from this difficulty on
    int         m_maxDiff = 99;     ///< only up to this one
};

/** @brief A mission. */
struct LevelDef
{
    std::string              m_name;
    std::string              m_title;
    std::string              m_titleEn;        ///< in English
    std::string              m_file;
    int                      m_episode = 1;
    std::string              m_theme = "space";
    std::string              m_music;
    std::string              m_briefing;      ///< the story entry shown before
    std::string              m_debrief;       ///< ... and after
    float                    m_scroll = 60.0f;  ///< the ground's speed (world units a second)
    float                    m_length = 180.0f; ///< seconds (after the last event at the latest)
    std::vector<std::string> m_next;            ///< the missions that may follow (campaign)
    std::string              m_secret;          ///< a secret mission ...
    std::string              m_secretKey;       ///< ... open when this key was found here
    bool                     m_isSecret = false;
    int                      m_arcade = 0;      ///< place in the episode's arcade run (0: not in it)
    float                    m_mapX = 0.5f;     ///< where it lies on the episode's map (0..1)
    float                    m_mapY = 0.5f;
    std::vector<LevelEvent>  m_events;          ///< sorted by time
    uint32_t                 m_seed = 1;        ///< the ground's look
    std::string              m_where;
};

/** @brief What a pilot flies with. */
struct Loadout
{
    std::string m_ship = "lancer";
    std::string m_front = "pulse";
    int         m_frontLv = 1;
    std::string m_rear;
    int         m_rearLv = 1;
    std::string m_sideL, m_sideR;
    std::string m_gen = "cell";
    std::string m_shield = "veil";
    std::string m_special;
};

/** @brief An episode. */
struct EpisodeDef
{
    int                      m_id = 1;
    std::string              m_title;
    std::string              m_subtitle;
    std::string              m_titleEn, m_subtitleEn;   ///< in English
    std::string              m_first;          ///< its first mission
    std::string              m_intro;          ///< story entries
    std::string              m_ending;
    int                      m_credits = 0;    ///< money when it is started fresh
    Loadout                  m_loadout;        ///< ... and the gear
    std::vector<std::string> m_arcade;         ///< the arcade run's missions (in order)
    std::string              m_where;
};

/** @brief A text of the story: a log crystal, a briefing, a radio call, an ending. */
struct StoryDef
{
    std::string              m_type;    ///< cube, briefing, radio, intro, ending, bio
    std::string              m_name;
    std::string              m_title;
    std::string              m_from;    ///< who speaks or wrote it
    std::string              m_portrait;
    std::vector<std::string> m_text;
};

/** @brief A difficulty. */
struct DifficultyDef
{
    std::string m_name;
    std::string m_title;
    std::string m_desc;
    std::string m_titleEn, m_descEn;   ///< in English
    float       m_hp = 1.0f;        ///< enemy strength
    float       m_fire = 1.0f;      ///< how often they shoot
    float       m_speed = 1.0f;     ///< how fast their bullets are
    float       m_damage = 1.0f;    ///< what a hit costs the player
    float       m_credits = 1.0f;   ///< score and money
    bool        m_revenge = false;  ///< enemies leave a bullet when they die
    bool        m_hidden = false;   ///< unlocked by beating the one before it
};

/** @brief An arcade pilot: a hull with its gear. */
struct ArcadeShip
{
    std::string m_name;
    std::string m_title;
    std::string m_desc;
    std::string m_titleEn, m_descEn;   ///< in English
    Loadout     m_loadout;
};

/** @brief All of it. */
struct Catalog
{
    std::string                         m_dir;           ///< the data folder
    std::map<std::string, ModelDef>     m_models;
    std::map<std::string, EnemyDef>     m_enemies;
    std::map<std::string, WeaponDef>    m_weapons;       ///< front and rear guns
    std::map<std::string, SidekickDef>  m_sidekicks;
    std::map<std::string, ShipDef>      m_ships;
    std::map<std::string, GeneratorDef> m_generators;
    std::map<std::string, ShieldDef>    m_shields;
    std::map<std::string, SpecialDef>   m_specials;
    std::map<std::string, LevelDef>     m_levels;
    std::map<int, EpisodeDef>           m_episodes;
    std::map<std::string, StoryDef>     m_story;
    std::map<std::string, StoryDef>     m_storyEn;       ///< data/story/en: the same entries in English
    std::vector<DifficultyDef>          m_difficulties;
    std::vector<ArcadeShip>             m_arcadeShips;
    std::vector<Block>                  m_themes;        ///< the grounds' looks (Terrain reads them)
    std::vector<std::string>            m_order;         ///< the goods in the order of their files
    std::string                         m_hash;          ///< a checksum of the files read (16 hex digits; replays carry it)
    std::vector<std::string>            m_errors;
    std::vector<std::string>            m_warnings;

    /**
     * @brief Reads everything in a data folder (data/ and its subfolders), then the mods' data folders over it (an
     *        entry of a name the game has replaces the game's; new names add to it).
     * @param[in] dir   the folder
     * @param[in] mods  the mods' data folders, in order
     * @return false if there were errors (m_errors)
     */
    bool load( const std::string &dir, const std::vector<std::string> &mods = {} );

    /** @brief Checks what refers to what (models, enemies, missions, stories); adds to m_errors and m_warnings. */
    void check();

    /** @return an enemy (nullptr if unknown) */
    const EnemyDef *enemy( const std::string &n ) const;
    /** @return a gun */
    const WeaponDef *weapon( const std::string &n ) const;
    /** @return a sidekick */
    const SidekickDef *sidekick( const std::string &n ) const;
    /** @return a hull */
    const ShipDef *ship( const std::string &n ) const;
    /** @return a generator */
    const GeneratorDef *generator( const std::string &n ) const;
    /** @return a shield */
    const ShieldDef *shield( const std::string &n ) const;
    /** @return a special */
    const SpecialDef *special( const std::string &n ) const;
    /** @return a mission */
    const LevelDef *level( const std::string &n ) const;
    /** @return a story entry (in English where there is one and the game speaks English) */
    const StoryDef *story( const std::string &n ) const;
    /** @return a model */
    const ModelDef *model( const std::string &n ) const;
    /** @return a difficulty (clamped) */
    const DifficultyDef &difficulty( int i ) const;
    /** @return a ground's look */
    const Block *theme( const std::string &n ) const;
};

/** @return the price of raising a gun from level lv to lv + 1 */
int upgradePrice( const WeaponDef &w, int lv );

/** @return what a gun at level lv has cost in all (itself and its upgrades) */
int weaponValue( const WeaponDef &w, int lv );

/**
 * @brief A gun's damage a second at a level if every shot hits (the workshop's and the balance's estimate).
 * @param[in] w     the gun
 * @param[in] lv    its level 1..11
 * @param[in] mode  rear guns: 1 or 2
 * @return damage a second
 */
float weaponDps( const WeaponDef &w, int lv, int mode = 1 );

/**
 * @brief Reads a loadout written as "ship=lancer,front=pulse:5,rear=tailgun:3,left=pip,right=spark,gen=dynamo,
 *        shield=ward,special=nova_s" (what is not named stays as it was).
 * @param[in,out] o     the loadout
 * @param[in]     text  the text
 */
void parseLoadout( Loadout &o, const std::string &text );

}   // namespace zenith
