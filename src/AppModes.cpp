/**
 * @file AppModes.cpp
 * @brief The ways to play beside the campaign and the arcade: the campaign for two (the second pilot joining and
 *        leaving), the practice flights (any mission flown before, whole or straight to its boss, with the gear of the
 *        episode or of a save slot), the mods screen; and the play log every mission writes a line to.
 * @author Rene Weller
 */
#include "App.h"

#include "Files.h"
#include "Lang.h"
#include "Ui.h"

#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace zenith
{

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------------------------------- for two

void App::join2()
{
    // The second pilot comes with the episode's gear and start money the first time, else with what they had.
    if ( !m_camp.m_has2 )
    {
        auto ep = m_cat.m_episodes.find( m_camp.m_episode );
        if ( ep != m_cat.m_episodes.end() )
        {
            m_camp.m_load2 = ep->second.m_loadout;
            m_camp.m_credits2 = ep->second.m_credits;
        }
        else
            m_camp.m_load2 = m_camp.m_load;
        m_camp.m_has2 = true;
    }
    m_camp.m_coop = true;
    m_players = 2;
    m_audio.play( SfxE::LIFE );
}

void App::leave2()
{
    m_camp.m_coop = false;
    m_players = 1;
    m_audio.play( SfxE::BACK );
}

// ---------------------------------------------------------------------------------------------------- practice

Loadout App::practiceLoad( const LevelDef &lv ) const
{
    if ( m_practiceGear >= 1 && m_practiceGear <= 3 && fs::exists( slotPath( m_practiceGear - 1 ) ) )
    {
        Campaign c;
        c.load( slotPath( m_practiceGear - 1 ) );
        return c.m_load;
    }
    return midLoadout( m_cat, lv.m_episode );
}

void App::startPractice( bool boss )
{
    m_practice = true;
    m_practiceBoss = boss;
    m_survival = m_daily = false;
    m_mode = ModeE::CAMPAIGN;
    startMission( m_level );
    if ( boss ) toBoss();
}

void App::toBoss()
{
    // The mission flown ahead by the autopilot (who cannot die) until its boss comes, then handed over whole.
    const LevelDef *lv = m_world.m_level;
    if ( !lv ) return;
    m_world.m_immortal = true;
    std::string theme;
    for ( int i = 0; i < 60 * 60 * 15 && m_world.boss() < 0 && !m_world.finished(); ++i )
    {
        PlayerInput in[2] = { autopilot( m_world, 0 ), {} };
        m_world.step( in );
        for ( const SimEvent &e : m_world.m_events )
            if ( e.m_type == EvE::THEME ) theme = e.m_name;
    }
    m_world.m_immortal = false;
    for ( int i = 0; i < 2; ++i )
    {
        Player &p = m_world.m_players[i];
        if ( !p.m_active ) continue;
        p.m_armor = p.m_armorMax;
        p.m_shield = p.m_shieldDef ? p.m_shieldDef->m_max : 0.0f;
        p.m_energy = p.m_energyMax;
        p.m_invuln = 2.0f;
        if ( p.m_special ) p.m_charges = std::max( p.m_charges, p.m_special->m_charges );
    }
    for ( Bullet &b : m_world.m_bullets )
        if ( !b.m_player ) b.m_dead = true;
    m_world.m_lasers.clear();
    m_world.m_stats = Stats();
    if ( !theme.empty() ) m_view.setTheme( m_cat, theme, lv->m_seed + 17u );
    m_view.reset();
    m_bossSave = m_world;
    m_hasBossSave = m_world.boss() >= 0;
    m_bossTries = 0;
    m_introT = 10.0f;
    bool own = false;
    for ( const LevelEvent &ev : lv->m_events )
        if ( ev.m_type == "music" ) own = true;
    if ( !own ) m_audio.music( lv->m_episode >= 1 && lv->m_episode <= 5 ? "e" + std::to_string( lv->m_episode ) + "_boss" : "boss" );
}

void App::practice( float dt )
{
    drawBackdrop( dt, true );
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    heading( "Übung", "Jeder schon geflogene Einsatz, ganz oder nur sein Boss. Nichts davon zählt." );
    float pw = std::min( W - 80 * k, 1400 * k );
    Rectangle r = { W * 0.5f - pw * 0.5f, H * 0.2f, pw, H * 0.68f };
    ui::panel( r );
    if ( m_practiceList.empty() )
    {
        ui::text( "Noch kein Einsatz geflogen.", W * 0.5f, r.y + 60 * k, 30, ui::Dim, ui::AlignE::CENTER );
        hints( "Esc / B: zurück" );
        if ( m_in.back() || m_in.accept() ) go( ScreenE::MAIN );
        return;
    }
    auto label = [&]( const std::string &n ) {
        const LevelDef *lv = m_cat.level( n );
        std::string     s = lv ? tf( "Ep. %d", lv->m_episode ) + "   " + L( lv->m_title, lv->m_titleEn ) : n;
        return m_prof.m_bosses.count( n ) ? s + "   ·  " + T( "Boss" ) : s;
    };
    if ( m_practiceStep == 0 )
    {
        // The list: the selected line stays in view.
        int rows = (int)( ( r.height - 60 * k ) / ( ui::height( 28 ) * 1.45f ) );
        int n = (int)m_practiceList.size();
        int first = std::clamp( m_sel - rows / 2, 0, std::max( 0, n - rows ) );
        std::vector<std::string> view;
        for ( int i = first; i < std::min( n, first + rows ); ++i ) view.push_back( label( m_practiceList[(size_t)i] ) );
        int sel = m_sel - first;
        int c = menu( view, r.x + 50 * k, r.y + 30 * k, pw * 0.55f, sel, 28 );
        m_sel = std::clamp( first + sel, 0, n - 1 );
        if ( const LevelDef *lv = m_cat.level( m_practiceList[(size_t)m_sel] ) )
        {
            const StoryDef *b = m_cat.story( lv->m_briefing );
            std::string     text = b && !b->m_text.empty() ? b->m_text[0] : "";
            float           x = r.x + pw * 0.62f, y = r.y + 40 * k;
            ui::text( L( lv->m_title, lv->m_titleEn ), x, y, 34, WHITE );
            for ( const std::string &l : ui::wrap( text, pw * 0.34f, 22 ) )
            {
                y += 30 * k;
                if ( y > r.y + r.height - 40 * k ) break;
                ui::text( l, x, y + 20 * k, 22, ui::Ink );
            }
        }
        hints( "Eingabe / A: wählen    Esc / B: zurück" );
        if ( c >= 0 )
        {
            m_level = m_practiceList[(size_t)( first + c )];
            m_practiceStep = 1;
            m_sel2 = 0;
        }
        else if ( m_in.back() )
            go( ScreenE::MAIN );
        return;
    }
    // The chosen mission: whole or the boss, the gear, the difficulty.
    const LevelDef *lv = m_cat.level( m_level );
    if ( !lv )
    {
        m_practiceStep = 0;
        return;
    }
    std::string gear = T( "Ausrüstung der Episode" );
    if ( m_practiceGear >= 1 && m_practiceGear <= 3 && fs::exists( slotPath( m_practiceGear - 1 ) ) )
    {
        Campaign c;
        c.load( slotPath( m_practiceGear - 1 ) );
        const ShipDef *s = m_cat.ship( c.m_load.m_ship );
        gear = tf( "Speicherplatz %d", m_practiceGear ) + " (" + ( s ? L( s->m_title, s->m_titleEn ) : c.m_load.m_ship ) + ")";
    }
    else
        m_practiceGear = 0;
    m_diff = std::clamp( m_diff, 0, std::min( m_prof.m_maxDiff, (int)m_cat.m_difficulties.size() - 1 ) );
    const DifficultyDef &d = m_cat.difficulty( m_diff );
    bool hasBoss = m_prof.m_bosses.count( m_level ) > 0;
    std::vector<std::string> items = { T( "Ganzer Einsatz" ), T( "Nur der Boss" ), std::string( T( "Ausrüstung" ) ) + ":  " + gear,
                                       std::string( T( "Schwierigkeit" ) ) + ":  " + L( d.m_title, d.m_titleEn ), T( "Zurück" ) };
    std::vector<bool> en = { true, hasBoss, true, true, true };
    ui::text( L( lv->m_title, lv->m_titleEn ), r.x + 50 * k, r.y + 30 * k, 40, WHITE );
    int c = menu( items, r.x + 50 * k, r.y + 110 * k, pw - 100 * k, m_sel2, 32, &en );
    drawShipCard( practiceLoad( *lv ), { r.x + pw * 0.55f, r.y + 110 * k, pw * 0.4f, r.height - 140 * k } );
    int dir = m_in.right() ? 1 : m_in.left() ? -1 : 0;
    if ( c == 2 || ( m_sel2 == 2 && dir ) )
    {
        // The next source that exists: the episode's gear, then the filled slots.
        for ( int i = 0; i < 4; ++i )
        {
            m_practiceGear = ( m_practiceGear + ( dir < 0 ? 3 : 1 ) ) % 4;
            if ( m_practiceGear == 0 || fs::exists( slotPath( m_practiceGear - 1 ) ) ) break;
        }
        m_audio.play( SfxE::MOVE );
    }
    if ( c == 3 || ( m_sel2 == 3 && dir ) )
    {
        int open = std::min( m_prof.m_maxDiff, (int)m_cat.m_difficulties.size() - 1 ) + 1;
        m_diff = ( m_diff + ( dir < 0 ? open - 1 : 1 ) ) % open;
        m_audio.play( SfxE::MOVE );
    }
    hints( "Eingabe / A: wählen    Links/Rechts: ändern    Esc / B: zurück" );
    if ( c == 0 || c == 1 ) startPractice( c == 1 );
    else if ( c == 4 || m_in.back() ) m_practiceStep = 0;
}

// ---------------------------------------------------------------------------------------------------- the play log

void App::logMission( const char *result )
{
    // One line a mission in <user>/playlog.txt (tools/playlog_report.py sums them up): what was flown, how it
    // ended, where and by what the pilots were shot down.
    if ( !m_opt.m_test.empty() || m_opt.m_bench || !m_world.m_level ) return;
    std::ofstream f( userDir() + "playlog.txt", std::ios::app );
    if ( !f ) return;
    char   stamp[32];
    time_t now = time( nullptr );
    strftime( stamp, sizeof( stamp ), "%Y-%m-%d %H:%M", localtime( &now ) );
    const Stats &st = m_world.m_stats;
    const char  *mode = m_practice ? "practice" : m_survival ? ( m_daily ? "daily" : "survival" )
                      : m_mode == ModeE::ARCADE ? "arcade" : m_mode == ModeE::BOSSRUSH ? "bossrush" : "campaign";
    const Loadout &o = m_world.m_players[0].m_load;
    f << stamp << " v=" << ZENITH_VERSION << " mode=" << mode << " mission=" << m_world.m_level->m_name
      << " diff=" << m_cat.difficulty( m_world.m_diffIndex ).m_name << " plus=" << m_world.m_plus << " pilots=" << m_players
      << " result=" << result << " time=" << (int)st.m_time << " deaths=" << st.m_deaths << " damage=" << (int)st.m_damage
      << " kills=" << st.m_kills << "/" << st.m_spawned << " credits=" << st.m_credits << " bosstries=" << m_bossTries
      << " speed=" << m_set.m_speed << " gear=" << o.m_ship << "," << o.m_front << ":" << o.m_frontLv << ","
      << ( o.m_rear.empty() ? "-" : o.m_rear ) << ":" << o.m_rearLv << "," << o.m_gen << "," << o.m_shield;
    if ( !st.m_deathNotes.empty() )
    {
        f << " deathsat=";
        for ( size_t i = 0; i < st.m_deathNotes.size(); ++i )
        {
            const DeathNote &d = st.m_deathNotes[i];
            f << ( i ? "|" : "" ) << (int)d.m_t << ":" << (int)d.m_pos.x << ":" << (int)d.m_pos.y << ":"
              << ( d.m_by.empty() ? "-" : d.m_by ) << ":" << d.m_how << ( d.m_boss ? ":boss" : "" );
        }
    }
    f << "\n";
}

// ---------------------------------------------------------------------------------------------------- mods

namespace
{

/** @brief A mod folder: its name and what its mod.txt says. */
struct ModInfo
{
    std::string m_name, m_dir, m_title, m_titleEn, m_author, m_desc, m_descEn;
};

std::vector<ModInfo> findMods()
{
    // <user>/mods/<name>/ first, then the mods beside the game (the example ships there).
    std::vector<ModInfo> out;
    for ( const std::string &root : { userDir() + "mods", std::string( GetApplicationDirectory() ) + "mods" } )
    {
        std::error_code e;
        if ( !fs::is_directory( root, e ) ) continue;
        for ( const auto &entry : fs::directory_iterator( root, e ) )
        {
            if ( !entry.is_directory() ) continue;
            ModInfo m;
            m.m_name = entry.path().filename().string();
            m.m_dir = entry.path().string();
            bool seen = false;
            for ( const ModInfo &o : out ) seen = seen || o.m_name == m.m_name;
            if ( seen || !fs::is_directory( entry.path() / "data", e ) ) continue;
            std::ifstream in( entry.path() / "mod.txt" );
            std::string   line;
            while ( std::getline( in, line ) )
            {
                if ( !line.empty() && line.back() == '\r' ) line.pop_back();
                size_t sp = line.find( ' ' );
                if ( sp == std::string::npos ) continue;
                std::string k = line.substr( 0, sp ), v = line.substr( sp + 1 );
                if ( k == "title" ) m.m_title = v;
                else if ( k == "title_en" ) m.m_titleEn = v;
                else if ( k == "author" ) m.m_author = v;
                else if ( k == "desc" ) m.m_desc = v;
                else if ( k == "desc_en" ) m.m_descEn = v;
            }
            if ( m.m_title.empty() ) m.m_title = m.m_name;
            out.push_back( m );
        }
    }
    return out;
}

}   // namespace

std::vector<std::string> modDataDirs( const std::vector<std::string> &names )
{
    std::vector<std::string> out;
    std::vector<ModInfo>     all = findMods();
    for ( const std::string &n : names )
        for ( const ModInfo &m : all )
            if ( m.m_name == n ) out.push_back( ( fs::path( m.m_dir ) / "data" ).string() );
    return out;
}

void App::mods( float dt )
{
    drawBackdrop( dt, true );
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    heading( "Mods", tf( "Eigene Daten in %s", ( userDir() + "mods" ).c_str() ) );
    static std::vector<ModInfo> list;
    static std::vector<std::string> before;
    if ( m_t < 0.01f )
    {
        list = findMods();
        before = m_set.m_mods;
    }
    float pw = std::min( W - 80 * k, 1400 * k );
    Rectangle r = { W * 0.5f - pw * 0.5f, H * 0.2f, pw, H * 0.68f };
    ui::panel( r );
    std::vector<std::string> items;
    for ( const ModInfo &m : list )
    {
        bool on = std::find( m_set.m_mods.begin(), m_set.m_mods.end(), m.m_name ) != m_set.m_mods.end();
        items.push_back( std::string( on ? "[x]  " : "[  ]  " ) + L( m.m_title, m.m_titleEn.empty() ? m.m_title : m.m_titleEn ) );
    }
    items.push_back( T( "Zurück" ) );
    int c = menu( items, r.x + 50 * k, r.y + 30 * k, pw * 0.5f, m_sel, 30 );
    if ( m_sel < (int)list.size() )
    {
        const ModInfo &m = list[(size_t)m_sel];
        float x = r.x + pw * 0.56f, y = r.y + 30 * k;
        if ( !m.m_author.empty() ) ui::text( m.m_author, x, y, 24, ui::Gold ), y += 40 * k;
        for ( const std::string &l : ui::wrap( L( m.m_desc, m.m_descEn.empty() ? m.m_desc : m.m_descEn ), pw * 0.4f, 24 ) )
        {
            ui::text( l, x, y, 24, ui::Ink );
            y += 32 * k;
        }
    }
    if ( list.empty() )
        ui::text( "Keine Mods gefunden. docs/MODDING.md erklärt, wie eine entsteht.", W * 0.5f, r.y + r.height - 60 * k, 24, ui::Dim, ui::AlignE::CENTER );
    if ( !m_cat.m_errors.empty() )
        ui::text( tf( "%d Fehler in den Daten (siehe --validate)", (int)m_cat.m_errors.size() ), W * 0.5f, r.y + r.height - 30 * k, 22, ui::Warn,
                  ui::AlignE::CENTER );
    hints( "Eingabe / A: an / aus    Esc / B: zurück" );
    if ( c >= 0 && c < (int)list.size() )
    {
        auto it = std::find( m_set.m_mods.begin(), m_set.m_mods.end(), list[(size_t)c].m_name );
        if ( it != m_set.m_mods.end() ) m_set.m_mods.erase( it );
        else m_set.m_mods.push_back( list[(size_t)c].m_name );
    }
    if ( c == (int)list.size() || m_in.back() )
    {
        // Changed: the data is read again, with the mods now switched on.
        if ( m_set.m_mods != before )
        {
            saveSettings();
            loadCatalog();
        }
        go( ScreenE::MAIN );
    }
}

float App::menuSize( size_t items, float most ) const
{
    // As large as wished, as small as needed for every line to fit two thirds of the screen.
    float one = ui::height( 1.0f ) * 1.45f;
    return std::max( 18.0f, std::min( most, GetScreenHeight() * 0.66f / std::max( 1.0f, items * one ) ) );
}

}   // namespace zenith

namespace zenith
{

// Filled once the survival, replay and achievement modules are in (their screens go back for now).
void App::survivalMenu( float dt ) { (void)dt; go( ScreenE::MAIN ); }
void App::replays( float dt ) { (void)dt; go( ScreenE::MAIN ); }
void App::stats( float dt ) { (void)dt; go( ScreenE::MAIN ); }

}   // namespace zenith
