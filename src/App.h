/**
 * @file App.h
 * @brief The program's screens and their flow: the title over a mission flown by the autopilot, the main menu,
 *        the campaign (save slots, difficulty, episodes, stories, the mission plan, the workshop, the missions,
 *        results), the arcade (pilots for one or two players, continues, names for the best scores), the boss
 *        rush, the options and the key bindings, the best scores, the log of crystals.
 * @author Rene Weller
 */
#pragma once

#include "Audio.h"
#include "Data.h"
#include "Input.h"
#include "Render.h"
#include "Save.h"
#include "Sim.h"

#include <deque>
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
};

/** @brief The screens. */
enum class ScreenE : int
{
    TITLE, MAIN, OPTIONS, CONTROLS, SCORES, CREDITS, LOGBOOK, SLOTS, DIFFICULTY, EPISODE, STORY, PLAN, SHOP, PLAY,
    RESULTS, FAILED, PILOTS, CONTINUE, NAME, QUIT
};

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

    /** @brief Renders a picture of every model to a file (and quits). */
    void gallery( const std::string &file );

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

    // Helpers.
    int  menu( const std::vector<std::string> &items, float x, float y, float w, int &sel, float size = 40.0f,
               const std::vector<bool> *enabled = nullptr, bool center = false );
    void heading( const std::string &title, const std::string &sub = "" );
    void hints( const std::string &text );
    void queueStory( const std::string &key );
    void showStories( ScreenE after );
    void startMission( const std::string &level );
    void startBossRush();
    void endMission( bool won );
    void hud();
    void pauseMenu( float dt );
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
    float            m_introT = 0.0f;
    float            m_outroT = -1.0f;
    bool             m_won = false;
    Stats            m_last;
    std::vector<std::string> m_newCubes;
    bool             m_secretFound = false;
    int              m_earned = 0;

    // The workshop.
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
};

}   // namespace zenith
