/**
 * @file Replay.h
 * @brief Recorded missions. The World is the same for the same seed, set-up and input, so a replay keeps only the
 *        set-up (a readable "key value" header) and the pilots' input of every step (a compact binary body: what
 *        changed from the step before, runs of equal steps counted). Marks note what the app did between two steps
 *        that the input does not tell: a boss fight fought again from its checkpoint, the arcade's continue.
 *
 *        The live game rounds every input with quantize() before it steps the World with it, so the World sees
 *        the very same input when it is played back.
 * @author Rene Weller
 */
#pragma once

#include "Sim.h"

#include <cstdint>
#include <string>
#include <vector>

namespace zenith
{

constexpr int   ReplayFormat = 1;       ///< the file format's version (in its first line)
constexpr char  ReplayExt[] = ".zlr";   ///< a replay file's ending
constexpr float ReplayGrid = 8.0f;      ///< the mouse target's steps in a field unit

/** @brief What the app did to the World between two steps. */
enum class ReplayMarkE : int
{
    RETRY,      ///< the boss's checkpoint was restored (restoreBoss)
    CONTINUE    ///< the arcade's continue was taken (continueArcade)
};

/** @brief A mark: the app did it after this many steps, before the next one. */
struct ReplayMark
{
    int         m_step = 0;                   ///< steps flown before it (0: before the first)
    ReplayMarkE m_kind = ReplayMarkE::RETRY;  ///< what
};

/** @brief How a recorded mission ended. */
enum class ReplayEndE : int
{
    OPEN,   ///< not told (the recorder was never finished)
    WON,    ///< the mission was won
    LOST    ///< lost or given up
};

/** @brief A replay's header: the mission's set-up, its result and its marks. */
struct ReplayHeader
{
    int                     m_format = ReplayFormat;   ///< the file's format version
    std::string             m_version;                 ///< the game's version that recorded it (ZENITH_VERSION)
    std::string             m_platform;                ///< "windows", "linux", "macos" (begin fills it; maths differ)
    ModeE                   m_mode = ModeE::CAMPAIGN;  ///< how it was played
    std::string             m_mission;                 ///< the mission ("bossrush" for the boss rush)
    int                     m_difficulty = 2;          ///< the difficulty's index (World::begin)
    int                     m_ngPlus = 0;              ///< the New Game+ level (0: none)
    uint32_t                m_seed = 0;                ///< the World's seed (World::begin)
    int                     m_pilots = 1;              ///< 1 or 2
    Loadout                 m_load[2];                 ///< each pilot's gear (World::addPlayer)
    int                     m_lives[2] = { 0, 0 };     ///< each pilot's lives as World::addPlayer takes them
    long long               m_score[2] = { 0, 0 };     ///< each pilot's score at the start
    std::string             m_date;                    ///< local time "YYYY-MM-DD hh:mm:ss" (begin fills it if empty)
    std::string             m_lang;                    ///< the language it was played in ("de", "en")
    ReplayEndE              m_end = ReplayEndE::OPEN;  ///< the result
    long long               m_endScore = 0;            ///< the score at the end (both pilots)
    float                   m_time = 0.0f;             ///< seconds flown (the World's clock at the end)
    int                     m_steps = 0;               ///< steps recorded
    std::vector<ReplayMark> m_marks;                   ///< in the order of their steps
    std::string             m_path;                    ///< the file it was read from ("" if none)
};

/**
 * @brief Rounds an input to what a replay stores: the stick to steps of 1/127, the mouse target to steps of
 *        1/ReplayGrid field unit (within a field's size beyond each edge), the buttons as they are. What the World
 *        does not read is cleared: the stick while the mouse steers, the target while it does not.
 * @param[in] in  a pilot's input
 * @return the input as it is stored and played back
 */
PlayerInput quantize( const PlayerInput &in );

/**
 * @brief A mode's name in a replay.
 * @param[in] mode  the mode
 * @return "campaign", "arcade", "bossrush" or "preview"
 */
const char *modeName( ModeE mode );

/**
 * @brief The mode of a name that modeName gives.
 * @param[in] name  the name
 * @return the mode (the campaign for an unknown name)
 */
ModeE modeOfName( const std::string &name );

/**
 * @brief Sets a World up as a header says, as App::startMission does: World::begin with its mission, mode,
 *        difficulty and seed, then each pilot with its gear, lives and score. The boss rush's mission must be in
 *        the catalog (cat.m_levels["bossrush"] = bossRush( cat ), as App::startBossRush does).
 * @param[out] w     the World
 * @param[in]  cat   the catalog (it must outlive the World)
 * @param[in]  head  the set-up
 * @return whether the mission was found
 */
bool startWorld( World &w, const Catalog &cat, const ReplayHeader &head );

/**
 * @brief The boss checkpoint restored, as App::retryBoss does: the World as it was when the boss came, the pilots
 *        alive with full armour, shield and energy, the enemies' bullets and lasers gone.
 * @param[in,out] w     the World
 * @param[in]     save  the World copied right after the step that gave EvE::BOSSSTART
 */
void restoreBoss( World &w, const World &save );

/**
 * @brief The arcade's continue, as App::continueScreen does: each pilot with three lives and no score, the World
 *        no longer failed.
 * @param[in,out] w  the World
 */
void continueArcade( World &w );

/** @brief Records a mission: the header, then each step's input, then the result. */
class ReplayRecorder
{
public:
    /**
     * @brief Starts a recording (anything recorded before is dropped).
     * @param[in] head  the set-up (the platform is filled in, the date if empty; result, steps and marks start
     *                  empty)
     */
    void begin( const ReplayHeader &head );

    /**
     * @brief Records a step's input; the World must be stepped with the same, quantized input.
     * @param[in] in  the two pilots' input of the step (quantized here again)
     */
    void push( const PlayerInput in[2] );

    /**
     * @brief Marks what the app did after the steps recorded so far; the mission goes on, so a result noted
     *        before is open again.
     * @param[in] kind  what
     */
    void mark( ReplayMarkE kind );

    /**
     * @brief Notes the result (again after a later mark; save writes the latest).
     * @param[in] won    whether the mission was won
     * @param[in] score  the score at the end (both pilots)
     * @param[in] time   seconds flown (the World's clock)
     */
    void finish( bool won, long long score, float time );

    /**
     * @brief Writes the replay (into a .tmp first, then in place; the folder is made if missing).
     * @param[in] path  the file
     * @return whether all of it was written
     */
    bool save( const std::string &path ) const;

    /** @brief Drops the recording. */
    void clear();

    /** @return whether a recording was begun and not cleared */
    bool active() const { return m_active; }

    /** @return the steps recorded */
    int steps() const { return m_head.m_steps; }

    /** @return the header as it stands */
    const ReplayHeader &header() const { return m_head; }

    /** @return the body's bytes so far */
    size_t bytes() const { return m_body.size() + ( m_run > 0 ? 1 : 0 ); }

private:
    ReplayHeader         m_head;
    std::vector<uint8_t> m_body;            ///< the encoded steps so far
    PlayerInput          m_last[2];         ///< the step before (quantized)
    int                  m_run = 0;         ///< steps equal to m_last not yet written
    bool                 m_active = false;
};

/** @brief Plays a replay back: its header, then each step's input. */
class ReplayPlayer
{
public:
    /**
     * @brief Reads a replay and checks it whole (magic, format, length, checksum, steps, marks).
     * @param[in] path  the file
     * @return whether it can be played (else error() tells why)
     */
    bool load( const std::string &path );

    /**
     * @brief The next step's input.
     * @param[out] in  the two pilots' input
     * @return false at the end (in is then the empty input)
     */
    bool next( PlayerInput in[2] );

    /**
     * @brief The marks due now: what the app must do to the World before the next step.
     * @return the marks, in their order (mostly none)
     */
    std::vector<ReplayMarkE> marks() const;

    /** @brief Back to the first step (the World must be set up anew with startWorld). */
    void rewind();

    /** @return whether every step has been given */
    bool atEnd() const { return m_pos >= m_head.m_steps; }

    /** @return the steps given so far */
    int position() const { return m_pos; }

    /** @return the header */
    const ReplayHeader &header() const { return m_head; }

    /** @return why load failed ("" if it did not) */
    const std::string &error() const { return m_error; }

private:
    ReplayHeader         m_head;
    std::vector<uint8_t> m_body;
    std::string          m_error;
    size_t               m_at = 0;          ///< the next byte of the body
    int                  m_pos = 0;         ///< steps given
    int                  m_run = 0;         ///< repeats of m_last still to give
    PlayerInput          m_last[2];
};

/**
 * @brief The replays in a folder (files ending in ReplayExt with a replay's first line; only their headers are
 *        read). Files of a newer format are listed too (m_format tells), the app cannot play them.
 * @param[in] dir  the folder
 * @return their headers with m_path set, the newest first (by date, then by name)
 */
std::vector<ReplayHeader> listReplays( const std::string &dir );

/**
 * @brief Deletes all but the newest replays in a folder; files whose name starts with "best_" are always kept and
 *        do not count.
 * @param[in] dir   the folder
 * @param[in] keep  how many of the others stay
 * @return how many were deleted
 */
int pruneReplays( const std::string &dir, int keep );

/**
 * @brief Checks the file itself (for --replaytest): random input of two pilots (stick, mouse, buttons, short and
 *        long runs, marks) saved, read back and compared step by step; then listReplays and pruneReplays on a few
 *        files. It writes into its own folder and removes it afterwards.
 * @param[in] dir  a folder of its own (made, then removed)
 * @return what went wrong ("" if nothing)
 */
std::string replaySelfTest( const std::string &dir );

}   // namespace zenith
