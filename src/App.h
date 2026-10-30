/**
 * @file App.h
 * @brief The program's screens and their flow: the title over a mission flown by the autopilot, the main menu,
 *        the campaign (save slots, difficulty, episodes, stories, the mission plan, the workshop, the missions,
 *        results), the arcade (pilots for one or two players, continues, names for the best scores), the boss
 *        rush, the options and the key bindings, the best scores, the log of crystals.
 * @author Rene Weller
 */
#pragma once

#include "Achieve.h"
#include "Audio.h"
#include "Data.h"
#include "Input.h"
#include "Render.h"
#include "Replay.h"
#include "Save.h"
#include "Sim.h"

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace zenith
{

/** @brief What the command line asked for. */
struct Options
{
    bool        m_mute = false;
    int         m_width = 0, m_height = 0;
    int         m_fullscreen = -1;      ///< -1 as the settings say
    std::string m_data;                 ///< the data folder ("" beside the program or in the source tree)
    std::string m_assets;
    std::string m_level;                ///< start this mission at once
    int         m_difficulty = 2;
    std::string m_shot;                 ///< write a picture of the mission and quit
    float       m_at = 10.0f;           ///< ... after this many seconds of play
    bool        m_auto = false;         ///< the autopilot flies
    std::string m_mode = "campaign";
    std::string m_gallery;              ///< a picture of every model
    std::string m_shop;                 ///< a picture of the workshop
    std::string m_loadout;              ///< --loadout: the gear for --level (Data.h parseLoadout)
    std::string m_theme;                ///< --theme: another ground for --level (to look at themes)
    std::string m_screen;               ///< --screen: --shot takes a picture of this screen instead
    std::string m_test;                 ///< --apptest: a script of presses and expectations (docs in main.cpp)
    bool        m_bench = false;        ///< --bench: fly the heavy missions and measure the frame times
    int         m_pilots = 1;           ///< --pilots 2: --level flies with two pilots
    bool        m_benchAll = false;     ///< --bench all: at each graphics setting
    std::string m_video;                ///< --video FILE: a replay (--replay) or a mission (--level) rendered to a video
    std::string m_replay;               ///< --replay FILE: the replay for --video
    int         m_videoFps = 60;        ///< --fps: the video's frames a second
    float       m_videoSeconds = 0.0f;  ///< --seconds: the video at most this long (0: the whole mission)
    bool        m_deckCheck = false;    ///< --deckcheck: every screen at 1280x800, the smallest text measured
};

/** @brief The screens. */
enum class ScreenE : int
{
    TITLE, MAIN, OPTIONS, CONTROLS, SCORES, CREDITS, LOGBOOK, SLOTS, DIFFICULTY, EPISODE, STORY, PLAN, SHOP, PLAY,
    RESULTS, FAILED, PILOTS, CONTINUE, NAME, PRACTICE, SURVIVAL, REPLAYS, STATS, MODS, ARCHIVE, CHALLENGE, EDITOR, QUIT
};

/** @brief A paint for the pilots' ships (the archive's third page). */
struct PaintDef
{
    const char *m_name;      ///< as the settings keep it ("" the hull's own colours)
    Color       m_color;
    float       m_amount;    ///< how much of the hull takes it
    const char *m_need;      ///< the achievement that opens it ("" open from the start)
    const char *m_title;     ///< its name (German; T() gives the English)
};

/** @return the paints, the hull's own first */
const std::vector<PaintDef> &paints();

/**
 * @brief The flight school's gear: the middle of episode 2, with a rear gun of two modes, sidekicks and a special.
 * @param[in] cat  the catalog
 * @return the gear
 */
Loadout schoolLoadout( const Catalog &cat );

/**
 * @brief The weekly flight's rules by name.
 * @param[in] rules  RuleE bits
 * @return their names, joined ("" for none)
 */
std::string ruleNames( int rules );

/** @brief An event of a mission in the wave editor. */
struct EditEvent
{
    float       m_t = 3.0f;    ///< seconds after the start
    int         m_kind = 0;    ///< 0 wave, 1 ground, 2 obstacle, 3 boss, 4 pickup
    std::string m_what;        ///< the enemy
    int         m_n = 5;       ///< how many (a pickup: which, 1..6)
    int         m_form = 2;    ///< the formation (AppEditor.cpp's list)
    int         m_x = 360;     ///< where across the field
    int         m_path = 1;    ///< the path (AppEditor.cpp's list)
};

/** @brief A mission in the wave editor. */
struct EditMission
{
    std::string            m_name;           ///< its file's name ("user_1")
    std::string            m_title;
    std::string            m_theme;
    std::string            m_music;
    float                  m_scroll = 60.0f;
    int                    m_gear = 1;       ///< the test flight's gear: an episode's middle gear
    std::vector<EditEvent> m_events;
};

/** @brief A point of the ghost's flight: where the best flight of the same seed was, and its score then. */
struct GhostPoint
{
    int16_t m_x = 0, m_y = 0;
    int32_t m_score = 0;
};

/**
 * @brief The data folders of mods by their names (<user>/mods/<name>/data, else beside the game).
 * @param[in] names  the mods switched on
 * @return the folders that exist, in order
 */
std::vector<std::string> modDataDirs( const std::vector<std::string> &names );

/**
 * @brief Saves what the screen shows now (raylib's TakeScreenshot drops the folder and writes beside the program).
 * @param[in] path  the PNG, its folder made if missing
 */
void saveScreen( const std::string &path );

/** @brief The program. */
class App
{
public:
    /**
     * @brief Opens the window, the sound, reads the data.
     * @param[in] o  the command line
     * @return false if it cannot run
     */
    bool init( const Options &o );

    /** @brief Runs until the player quits. */
    void run();

    /** @brief Closes everything. */
    void shutdown();

    /**
     * @brief Plays a script through the screens (hidden, silent, missions fast by the autopilot).
     * @return 0 when every expectation held
     */
    int appTest();

    /** @brief Renders a picture of one screen (--screen NAME --shot FILE) and quits. */
    void screenShot();

    /** @brief Renders a picture of every model to a file (and quits). */
    void gallery( const std::string &file );

    /**
     * @brief Renders a replay (--replay) or a mission flown by the autopilot (--level, --at seconds) frame by frame
     *        into a video through ffmpeg, with the mission's music under it.
     * @return 0 when the video was written
     */
    int video();

    /**
     * @brief Draws every screen at the window's size (1280x800 for the Steam Deck) and reports the smallest text
     *        drawn on each, in pixels.
     * @return 0 when no text is smaller than 9 pixels
     */
    int deckCheck();

private:
    // The flow.
    void go( ScreenE s );
    void frame( float dt );
    void drawBackdrop( float dt, bool dim );
    void stepDemo();
    void newDemo();
    void resize();

    // Menus.
    void title( float dt );
    void mainMenu( float dt );
    void options( float dt );
    void controls( float dt );
    void scores( float dt );
    void credits( float dt );
    void logbook( float dt );
    void slots( float dt );
    void difficulty( float dt );
    void episode( float dt );
    void story( float dt );
    void plan( float dt );
    void shop( float dt );
    void play( float dt );
    void results( float dt );
    void failed( float dt );
    void pilots( float dt );
    void continueScreen( float dt );
    void nameEntry( float dt );
    void practice( float dt );
    void survivalMenu( float dt );
    void replays( float dt );
    void stats( float dt );
    void mods( float dt );
    void archive( float dt );
    void challenge( float dt );
    void editor( float dt );
    void editorList();
    void editorOpen( const std::string &name );
    LevelDef editorLevel() const;
    bool editorSave();
    std::vector<std::string> editorEnemies( int kind ) const;

    // Helpers.
    int  menu( const std::vector<std::string> &items, float x, float y, float w, int &sel, float size = 40.0f,
               const std::vector<bool> *enabled = nullptr, bool center = false, const std::vector<bool> *dim = nullptr );
    void heading( const std::string &title, const std::string &sub = "" );
    void hints( const std::string &text );
    void queueStory( const std::string &key );
    void showStories( ScreenE after );
    void startMission( const std::string &level );
    void startBossRush();
    void endMission( bool won );
    void hud();
    void pauseMenu( float dt );
    void photoMode( float dt );
    void campaignNext( const LevelDef &lv );
    void openEpisode( int ep, bool fresh );
    bool available( const std::string &name, int tier, bool hidden ) const;
    std::string diffKey() const;
    void saveCampaign();
    void saveSettings();
    void applySettings();
    std::string fmt( long long v ) const;
    Loadout previewLoad() const;
    void previewSetup();
    void drawShipCard( const Loadout &o, Rectangle r );
    void join2();
    void leave2();
    void startPractice( bool boss );
    Loadout practiceLoad( const LevelDef &lv ) const;
    void toBoss();
    void logMission( const char *result );
    float menuSize( size_t items, float most ) const;
    void loadCatalog();
    const Texture2D *portrait( const std::string &from );
    int  quality() const;
    int  fpsLimit() const;
    void bench();
    void saveReplay( bool won );
    void startReplay( const ReplayHeader &h );
    void stopReplay();
    void startSurvival();
    void countMission( bool won );
    void checkFeats();
    void drawToasts( float dt );
    void applyPaints();
    void noteSeen();
    void loadArchive();
    void startSchool();
    void schoolStep( const PlayerInput &in );
    void drawSchool();
    std::string ghostPath() const;
    void loadGhost();
    void saveGhost( long long best );
    void ghostStep();
    int  medalOf( bool won ) const;
    std::string medalName( int medal ) const;

    Options          m_opt;
    Catalog          m_cat;
    Settings         m_set;
    Profile          m_prof;
    Campaign         m_camp;
    int              m_slot = 0;
    Input            m_in;
    Audio            m_audio;
    Renderer         m_view;
    Renderer         m_preview;
    World            m_world;
    World            m_demo;
    World            m_shopWorld;
    bool             m_demoReady = false;
    std::string      m_assets;
    ScreenE          m_screen = ScreenE::TITLE;
    ScreenE          m_after = ScreenE::MAIN;
    ScreenE          m_back = ScreenE::MAIN;
    int              m_sel = 0;
    int              m_sel2 = 0;
    float            m_t = 0.0f;              ///< seconds on this screen
    bool             m_enter = false;         ///< the screen's first frame (go() sets it, its first frame clears it)
    std::deque<std::string> m_stories;        ///< story entries to show
    float            m_storyT = 0.0f;
    bool             m_running = true;

    // How the game is played.
    ModeE            m_mode = ModeE::CAMPAIGN;
    int              m_players = 1;
    int              m_diff = 2;
    int              m_episode = 1;
    std::vector<std::string> m_run;            ///< arcade: the missions of the run
    size_t           m_runIdx = 0;
    int              m_pilot[2] = { 0, 1 };
    int              m_pilotStep = 0;
    long long        m_score[2] = { 0, 0 };
    int              m_continues = 0;
    std::string      m_level;                  ///< the mission flown
    Loadout          m_carry[2];               ///< arcade: the gear carried from mission to mission
    int              m_lives[2] = { 3, 3 };

    // The mission.
    float            m_acc = 0.0f;
    bool             m_paused = false;
    int              m_pauseSel = 0;
    std::string      m_radio;
    std::string      m_radioFrom;
    float            m_radioT = 0.0f;
    float            m_warnT = 0.0f;
    float            m_rumbleT = 0.0f;         ///< the pad's motors run this much longer
    float            m_themeT = 0.0f;          ///< a change of ground: the flash fading
    std::string      m_themeNext;              ///< ... and the ground it brings (set at the flash's height)
    float            m_rumbleLow = 0.0f, m_rumbleHigh = 0.0f;
    float            m_introT = 0.0f;
    float            m_outroT = -1.0f;
    bool             m_won = false;
    Stats            m_last;
    std::vector<std::string> m_newCubes;
    bool             m_secretFound = false;
    int              m_earned = 0;

    // The photo mode (from the pause): the field stands, the view pans and zooms, no HUD.
    bool             m_photo = false;
    Vector2          m_photoCenter = { 0, 0 };
    float            m_photoHalfH = 0.0f;
    Vector2          m_viewCenter = { 0, 0 };   ///< the view before the photo mode
    float            m_viewHalfH = 0.0f;
    float            m_photoMsgT = 0.0f;         ///< "saved" shown this much longer

    // The boss fight's checkpoint: the field as it was when the boss came (a lost fight starts again from there).
    World            m_bossSave;
    bool             m_hasBossSave = false;
    int              m_bossTries = 0;
    void retryBoss();

    // Ways to play beside the campaign.
    bool             m_practice = false;       ///< a practice flight: nothing is kept
    bool             m_practiceBoss = false;   ///< ... that starts at the boss
    int              m_practiceGear = 0;       ///< its gear: 0 the episode's, 1..3 a save slot's
    int              m_practiceStep = 0;       ///< 0 the list, 1 the chosen mission's choices
    std::vector<std::string> m_practiceList;
    bool             m_survival = false;       ///< the survival flight (arcade rules, endless waves)
    bool             m_daily = false;          ///< ... with the day's seed
    uint32_t         m_survivalSeed = 1;
    int              m_earned2 = 0;            ///< the second pilot's money of the mission

    // The workshop.
    int              m_shopPilot = 0;          ///< the pilot served (the campaign for two)
    int              m_tab = 0;
    int              m_item = 0;
    std::string      m_shopMsg;
    float            m_shopMsgT = 0.0f;
    std::string      m_previewKey;
    float            m_previewT = 0.0f;

    // The name entry and the options.
    std::string      m_name = "PILOT";
    int              m_nameCursor = 0;
    std::string      m_scoreKey;
    int              m_scoreTab = 0;
    int              m_bindWait = -1;          ///< rebinding: the action waiting (-1 none)
    bool             m_bindPad = false;
    std::vector<std::string> m_logList;

    // Who speaks: the portrait beside a text (data/lang/speakers.txt, assets/portraits), the voice speaking it.
    std::map<std::string, std::string> m_portraitOf;
    std::map<std::string, Texture2D>   m_portraits;
    std::string      m_voiceFor;               ///< the story entry whose voice was started
    float            m_autoBombT[2] = { 0, 0 };

    // Replays: every mission recorded (<user>/replays), any of them played back.
    ReplayRecorder   m_rec;
    std::string      m_recPath;
    ReplayPlayer     m_play;
    bool             m_replaying = false;
    bool             m_replayEnd = false;
    int              m_replaySpeed = 1;
    std::vector<ReplayHeader> m_replayList;

    // Achievements and the statistics.
    Feats            m_feats;
    AchievementList  m_achieve;
    std::deque<const AchievementDef *> m_toasts;
    float            m_toastT = 0.0f;
    bool             m_bossHit = false;        ///< the pilots were hit since the boss came
    int              m_statsTab = 0;

    // The archive: the enemies met, the music heard, the paints.
    int              m_archiveTab = 0;
    std::vector<std::string> m_codex;          ///< the enemies it lists, in their order
    std::vector<std::array<std::string, 3>> m_pieces;   ///< the music room: piece, German title, English title
    std::string      m_archivePiece;           ///< the music room's piece playing ("" the menu's own)
    std::string      m_codexShown;             ///< the enemy on the preview's field
    int              m_paintPilot = 0;         ///< the pilot whose paint is chosen

    // The flight school: lessons one after the other, nothing kept but its being finished.
    bool             m_school = false;
    int              m_lesson = 0;
    float            m_lessonT = 0.0f;         ///< seconds in this lesson
    float            m_lessonDone = -1.0f;     ///< seconds since it was done (-1: not yet)
    float            m_lessonMeter = 0.0f;     ///< what it counts (distance flown, seconds of focus ...)
    Vector2          m_lessonPos = { 0, 0 };

    // The weekly flight and the ghost of the best flight of the same seed (daily and weekly).
    bool             m_weekly = false;
    std::string      m_challenge;              ///< the challenge's code flown ("" none)
    // The wave editor.
    int              m_editStep = 0;           ///< 0 the list of missions, 1 one being edited
    std::vector<std::string> m_editFiles;      ///< the missions saved
    EditMission      m_edit;
    int              m_editField = 0;          ///< the value of the event being changed
    bool             m_editDirty = true;       ///< the preview's field must be made again
    bool             m_editTyping = false;     ///< the title is being typed
    bool             m_editing = false;        ///< a test flight from the editor (it comes back there)
    std::string      m_editMsg;
    int              m_challengeStep = 0;      ///< 0 the choice, 1 a new code, 2 a code typed in
    std::string      m_codeInput;              ///< the code being typed
    int              m_codeRules = 0;          ///< a new challenge: with house rules (1) or without (0)
    uint32_t         m_codeSeed = 1;
    int              m_rules = 0;              ///< the weekly flight's RuleE bits (0 otherwise)
    std::vector<GhostPoint> m_ghost;           ///< the best flight, a point every second step
    std::vector<GhostPoint> m_ghostRec;        ///< this flight's, the same way
    long long        m_ghostBest = 0;          ///< the best flight's score at its end
    int              m_ghostStep = 0;          ///< steps flown in this flight
    long long        m_ghostBase = 0;          ///< the pilot's score when the flight began (an arcade mission's)
    bool             m_ghostActive = false;    ///< this flight records a ghost (ghostPath() is not empty)
    int              m_medal = 0;              ///< the mission's medal (results screen)
    bool             m_medalNew = false;       ///< ... better than any before
};

}   // namespace zenith
