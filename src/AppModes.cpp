/**
 * @file AppModes.cpp
 * @brief The ways to play beside the campaign and the arcade: the campaign for two (the second pilot joining and
 *        leaving), the practice flights (any mission flown before, whole or straight to its boss, with the gear of the
 *        episode or of a save slot), the survival flight (the day's or a free one), the replays, the achievements and
 *        the statistics, the mods screen; and the play log every mission writes a line to.
 * @author Rene Weller
 */
#include "App.h"

#include "Files.h"
#include "Lang.h"
#include "Survival.h"
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
    if ( m_school ) return schoolLoadout( m_cat );
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
    m_feats.add( "practice" );
    checkFeats();
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
    // The flight ahead was not recorded: this flight is not replayed.
    m_rec.clear();
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
    if ( !m_opt.m_test.empty() || m_opt.m_bench || !m_world.m_level || m_school || !m_opt.m_video.empty() ) return;
    std::ofstream f( userDir() + "playlog.txt", std::ios::app );
    if ( !f ) return;
    char   stamp[32];
    time_t now = time( nullptr );
    strftime( stamp, sizeof( stamp ), "%Y-%m-%d %H:%M", localtime( &now ) );
    const Stats &st = m_world.m_stats;
    const char  *mode = m_practice ? "practice" : m_survival ? ( m_weekly ? "weekly" : m_daily ? "daily" : "survival" )
                      : m_mode == ModeE::ARCADE ? "arcade" : m_mode == ModeE::BOSSRUSH ? "bossrush" : "campaign";
    const Loadout &o = m_world.m_players[0].m_load;
    f << stamp << " v=" << ZENITH_VERSION << " mode=" << mode << " mission=" << m_world.m_level->m_name
      << " diff=" << m_cat.difficulty( m_world.m_diffIndex ).m_name << " plus=" << m_world.m_plus << " pilots=" << m_players
      << " result=" << result << " time=" << (int)st.m_time << " deaths=" << st.m_deaths << " damage=" << (int)st.m_damage
      << " kills=" << st.m_kills << "/" << st.m_spawned << " credits=" << st.m_credits << " bosstries=" << m_bossTries
      << " chain=" << st.m_bestChain << " grazes=" << st.m_grazes << " rules=" << m_world.m_rules
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
    if ( m_enter )
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

// ---------------------------------------------------------------------------------------------------- survival

const char *const RuleTitles[RuleCount] = { "Doppelfeuer", "Glaskanone", "Eile", "Ohne Schild",
                                             "Ein Leben",   "Gepanzert",  "Hast", "Nur Heck" };
const char *const RuleTexts[RuleCount] = {
    "Die Gegner feuern doppelt so oft.",
    "Deine Waffen und die Treffer der Gegner zählen doppelt.",
    "Gegner und Kugeln sind ein Drittel schneller.",
    "Kein Schild.",
    "Keine Leben in Reserve.",
    "Die Gegner halten 60 % mehr aus und bringen die Hälfte mehr Punkte.",
    "Die Kette bricht schon nach einer Sekunde.",
    "Die Frontkanone schweigt: Heckwaffe und Seitenschiffe kämpfen.",
};

std::string ruleNames( int rules )
{
    std::string out;
    for ( int i = 0; i < RuleCount; ++i )
        if ( rules & ( 1 << i ) ) out += ( out.empty() ? "" : "  ·  " ) + std::string( T( RuleTitles[i] ) );
    return out;
}

void App::startSurvival()
{
    // The endless flight under arcade rules: three lives, no continues, the waves from the seed. The weekly flight's
    // rules may take the lives in reserve or the front gun (then the rear gun fights at the front gun's power).
    m_cat.m_levels["survival"] = survival( m_cat, m_survivalSeed );
    m_carry[0] = m_cat.m_arcadeShips[(size_t)m_pilot[0]].m_loadout;
    if ( m_rules & (int)RuleE::REAR )
    {
        if ( m_carry[0].m_rear.empty() || !m_cat.weapon( m_carry[0].m_rear ) ) m_carry[0].m_rear = "tailgun";
        m_carry[0].m_rearLv = std::max( m_carry[0].m_rearLv, m_carry[0].m_frontLv );
    }
    m_lives[0] = m_rules & (int)RuleE::ONELIFE ? 1 : 3;
    m_score[0] = m_score[1] = 0;
    m_continues = 0;
    m_players = 1;
    m_episode = 1;
    m_run = { "survival" };
    m_runIdx = 0;
    startMission( "survival" );
}

void App::survivalMenu( float dt )
{
    drawBackdrop( dt, true );
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    heading( "Überleben", "Welle um Welle, Boss um Boss, bis nichts mehr geht." );
    std::string day = today(), week = thisWeek();
    int         rules = weeklyRules( weekSeed( week ) );
    auto        best = m_prof.m_daily.find( day );
    auto        bestWeek = m_prof.m_weekly.find( week );
    std::vector<std::string> items = { tf( "Tagesflug vom %s", day.c_str() ), tf( "Wochenflug %s", week.c_str() ), T( "Freier Flug" ),
                                       T( "Zurück" ) };
    float pw = 1000 * k;
    ui::panel( { W * 0.5f - pw * 0.5f, H * 0.3f - 30 * k, pw, 470 * k } );
    int c = menu( items, W * 0.5f, H * 0.3f, pw - 80 * k, m_sel, 36, nullptr, true );
    std::string about = m_sel == 0 ? T( "Für alle am selben Tag dieselben Wellen, auf Normal. Dein bester Tagesflug zählt, und sein Geist fliegt mit." )
                      : m_sel == 1 ? std::string( T( "Eine Woche lang dieselben Wellen und zwei Sonderregeln:" ) ) + " " + ruleNames( rules ) + "."
                      : m_sel == 2 ? T( "Neue Wellen bei jedem Flug, die Schwierigkeit nach Wahl." ) : "";
    float ay = H * 0.3f + 250 * k;
    for ( const std::string &l : ui::wrap( about, pw - 100 * k, 24 ) )
    {
        ui::text( l, W * 0.5f, ay, 24, ui::Ink, ui::AlignE::CENTER );
        ay += 32 * k;
    }
    if ( m_sel == 1 )
        for ( int i = 0; i < RuleCount; ++i )
            if ( rules & ( 1 << i ) )
            {
                ui::text( std::string( T( RuleTitles[i] ) ) + ": " + T( RuleTexts[i] ), W * 0.5f, ay, 22, ui::Gold, ui::AlignE::CENTER );
                ay += 30 * k;
            }
    if ( m_sel == 0 && best != m_prof.m_daily.end() )
        ui::text( tf( "Heute bester Flug: %s Punkte", fmt( best->second ).c_str() ), W * 0.5f, ay, 24, ui::Gold, ui::AlignE::CENTER );
    if ( m_sel == 1 && bestWeek != m_prof.m_weekly.end() )
        ui::text( tf( "Diese Woche bester Flug: %s Punkte", fmt( bestWeek->second ).c_str() ), W * 0.5f, ay, 24, ui::Gold, ui::AlignE::CENTER );
    hints( "Eingabe / A: wählen    Esc / B: zurück" );
    if ( c == 3 || m_in.back() )
    {
        m_survival = m_daily = m_weekly = false;
        m_rules = 0;
        go( ScreenE::MAIN );
        return;
    }
    if ( c < 0 ) return;
    m_mode = ModeE::ARCADE;
    m_players = 1;
    m_survival = true;
    m_daily = c == 0;
    m_weekly = c == 1;
    m_rules = m_weekly ? rules : 0;
    m_survivalSeed = m_daily ? dailySeed() : m_weekly ? weekSeed( week ) : (uint32_t)( GetTime() * 1000.0 ) ^ 0x51ab;
    if ( m_daily || m_weekly )
    {
        // The day's and the week's flights are the same for everyone: Normal.
        m_diff = std::min( 2, (int)m_cat.m_difficulties.size() - 1 );
        m_pilotStep = 0;
        go( ScreenE::PILOTS );
    }
    else
        go( ScreenE::DIFFICULTY );
}

// ---------------------------------------------------------------------------------------------------- replays

void App::saveReplay( bool won )
{
    if ( m_replaying || !m_rec.active() || m_recPath.empty() || m_opt.m_bench ) return;
    long long score = 0;
    for ( const Player &p : m_world.m_players )
        if ( p.m_active ) score += p.m_score;
    m_rec.finish( won, score, m_world.m_clock );
    m_rec.save( m_recPath );
    pruneReplays( userDir() + "replays", 40 );
}

void App::startReplay( const ReplayHeader &h )
{
    if ( !m_play.load( h.m_path ) ) return;
    // The generated missions are made again from what the replay says.
    if ( h.m_mission == "bossrush" ) m_cat.m_levels["bossrush"] = bossRush( m_cat );
    if ( h.m_mission == "survival" ) m_cat.m_levels["survival"] = survival( m_cat, h.m_seed );
    const LevelDef *lv = m_cat.level( h.m_mission );
    if ( !lv || !startWorld( m_world, m_cat, h ) ) return;
    m_mode = h.m_mode;
    m_players = h.m_pilots;
    m_level = h.m_mission;
    m_survival = h.m_mission == "survival";
    m_practice = false;
    m_replaying = true;
    m_replayEnd = false;
    m_replaySpeed = 1;
    m_rec.clear();
    m_view.setTheme( m_cat, lv->m_theme, lv->m_seed );
    m_view.reset();
    m_audio.music( lv->m_music );
    m_acc = 0;
    m_paused = false;
    m_radio.clear();
    m_warnT = 0;
    m_hasBossSave = false;
    m_bossTries = 0;
    m_themeT = 0;
    m_themeNext.clear();
    m_introT = 0;
    m_outroT = -1;
    m_feats.add( "replays_watched" );
    checkFeats();
    go( ScreenE::PLAY );
}

void App::stopReplay()
{
    m_replaying = false;
    m_paused = false;
    m_survival = false;
    m_audio.voice( "", "" );
    newDemo();
    go( ScreenE::REPLAYS );
}

void App::replays( float dt )
{
    drawBackdrop( dt, true );
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    if ( m_enter ) m_replayList = listReplays( userDir() + "replays" );
    heading( "Wiederholungen", tf( "%d Aufnahmen", (int)m_replayList.size() ) );
    float pw = std::min( W - 80 * k, 1500 * k );
    Rectangle r = { W * 0.5f - pw * 0.5f, H * 0.2f, pw, H * 0.68f };
    ui::panel( r );
    if ( m_replayList.empty() )
    {
        ui::text( "Noch keine Aufnahme. Jeder Einsatz wird aufgenommen.", W * 0.5f, r.y + 60 * k, 30, ui::Dim, ui::AlignE::CENTER );
        hints( "Esc / B: zurück" );
        if ( m_in.back() || m_in.accept() ) go( ScreenE::MAIN );
        return;
    }
    auto title = [&]( const ReplayHeader &h ) {
        const LevelDef *lv = m_cat.level( h.m_mission );
        std::string     name = h.m_mission == "survival" ? std::string( T( "Überleben" ) ) : h.m_mission == "bossrush" ? std::string( T( "Bossjagd" ) )
                             : lv ? L( lv->m_title, lv->m_titleEn ) : h.m_mission;
        return h.m_date.substr( 0, 16 ) + "   " + name;
    };
    int rows = (int)( ( r.height - 60 * k ) / ( ui::height( 26 ) * 1.45f ) );
    int n = (int)m_replayList.size();
    int first = std::clamp( m_sel - rows / 2, 0, std::max( 0, n - rows ) );
    std::vector<std::string> view;
    for ( int i = first; i < std::min( n, first + rows ); ++i ) view.push_back( title( m_replayList[(size_t)i] ) );
    int sel = m_sel - first;
    int c = menu( view, r.x + 40 * k, r.y + 30 * k, pw * 0.55f, sel, 26 );
    m_sel = std::clamp( first + sel, 0, n - 1 );
    // The chosen one's facts.
    const ReplayHeader &h = m_replayList[(size_t)m_sel];
    float x = r.x + pw * 0.6f, y = r.y + 30 * k;
    auto  fact = [&]( const std::string &a, const std::string &b, Color col = ui::Ink ) {
        ui::text( a, x, y, 22, ui::Dim );
        ui::text( b, x + 220 * k, y, 24, col );
        y += 38 * k;
    };
    const char *modes[] = { "Kampagne", "Arcade", "Bossjagd", "Werkstatt" };
    fact( T( "Spielart" ), h.m_mission == "survival" ? std::string( T( "Überleben" ) ) : std::string( T( modes[std::clamp( (int)h.m_mode, 0, 3 )] ) ) +
                                                     ( h.m_pilots > 1 ? "  ·  " + std::string( T( "zu zweit" ) ) : std::string() ) );
    fact( T( "Schwierigkeit" ), L( m_cat.difficulty( h.m_difficulty ).m_title, m_cat.difficulty( h.m_difficulty ).m_titleEn ) +
                                    ( h.m_ngPlus > 0 ? tf( "  ·  NG+%d", h.m_ngPlus ) : std::string() ) );
    const ShipDef *ship = m_cat.ship( h.m_load[0].m_ship );
    fact( T( "Rumpf" ), ship ? L( ship->m_title, ship->m_titleEn ) : h.m_load[0].m_ship );
    int secs = (int)h.m_time;
    fact( T( "Flugzeit" ), std::to_string( secs / 60 ) + ":" + ( secs % 60 < 10 ? "0" : "" ) + std::to_string( secs % 60 ) );
    fact( T( "Punkte" ), fmt( h.m_endScore ), ui::Gold );
    fact( T( "Ergebnis" ), T( h.m_end == ReplayEndE::WON ? "geschafft" : h.m_end == ReplayEndE::LOST ? "verloren" : "abgebrochen" ),
          h.m_end == ReplayEndE::WON ? ui::Good : ui::Ink );
    bool old = h.m_format > ReplayFormat;
    bool other = h.m_version != ZENITH_VERSION;
    // Other data (a mod, a changed file) plays another game from the same input.
    bool data = !h.m_data.empty() && h.m_data != m_cat.m_hash;
    if ( old ) fact( "", T( "Aus einer neueren Fassung: nicht abspielbar." ), ui::Warn );
    else if ( other ) fact( "", tf( "Aufgenommen mit %s: kann abweichen.", h.m_version.c_str() ), ui::Warn );
    else if ( data ) fact( "", T( "Mit anderen Spieldaten aufgenommen (Mod?): kann abweichen." ), ui::Warn );
    hints( "Eingabe / A: abspielen    Esc / B: zurück" );
    if ( c >= 0 && !old ) startReplay( m_replayList[(size_t)( first + c )] );
    else if ( m_in.back() ) go( ScreenE::MAIN );
}

// ---------------------------------------------------------------------------------------------------- achievements

void App::countMission( bool won )
{
    // What a mission adds to the achievements (practice flights and replays add nothing).
    if ( m_practice || m_replaying || !m_opt.m_test.empty() || !m_world.m_level ) return;
    const Stats &st = m_world.m_stats;
    m_feats.add( "kills", st.m_kills );
    m_feats.add( "bosses", st.m_bossKills );
    m_feats.add( "deaths", st.m_deaths );
    m_feats.add( "seconds", st.m_time );
    m_feats.max( "max_chain", st.m_bestChain );
    m_feats.add( "grazes", st.m_grazes );
    {
        int seen = 0;
        for ( const std::string &n : m_codex ) seen += m_prof.m_seen.count( n ) ? 1 : 0;
        m_feats.max( "seen", seen );
    }
    for ( const std::string &c : st.m_cubes ) m_feats.once( "cubes", c );
    for ( const std::string &p : st.m_plans ) m_feats.once( "plans", p );
    if ( won )
    {
        m_feats.add( "missions" );
        if ( st.m_damage <= 0.0f ) m_feats.add( "nodamage" );
        if ( st.m_spawned > 0 && st.m_kills >= st.m_spawned ) m_feats.add( "allkills" );
        if ( m_world.m_level->m_isSecret ) m_feats.once( "secrets", m_world.m_level->m_name );
    }
    if ( m_mode == ModeE::CAMPAIGN && won )
    {
        long long earned = 0;
        for ( const Player &p : m_world.m_players )
            if ( p.m_active ) earned += p.m_credits;
        m_feats.add( "credits", (double)earned );
        m_feats.max( "max_credits", (double)( m_camp.m_credits + earned ) );
        if ( m_camp.m_coop ) m_feats.add( "coop_missions" );
    }
    if ( m_survival )
    {
        m_feats.max( "survival_seconds", st.m_time );
        if ( m_daily ) m_feats.add( "survival_daily" );
        if ( m_weekly ) m_feats.add( "weekly" );
    }
    if ( m_mode == ModeE::BOSSRUSH && won )
    {
        m_feats.add( "bossrush_done" );
        if ( m_diff >= 2 ) m_feats.min( "bossrush_best", st.m_time );
    }
    checkFeats();
}

void App::checkFeats()
{
    if ( !m_opt.m_test.empty() || !m_opt.m_video.empty() || m_opt.m_deckCheck ) return;
    for ( const AchievementDef *a : newlyUnlocked( m_feats, m_achieve, today() ) )
    {
        m_toasts.push_back( a );
        m_audio.play( SfxE::CUBE );
    }
    m_feats.save( featsPath() );
}

void App::drawToasts( float dt )
{
    // A new achievement: a card at the top right for four seconds.
    if ( m_toasts.empty() ) return;
    m_toastT += dt;
    const AchievementDef *a = m_toasts.front();
    float k = ui::k(), W = (float)GetScreenWidth();
    float in = std::min( 1.0f, std::min( m_toastT * 4.0f, ( 4.0f - m_toastT ) * 3.0f ) );
    Rectangle r = { W - 560 * k, 20 * k - ( 1.0f - in ) * 140 * k, 540 * k, 104 * k };
    DrawRectangleRec( r, { 8, 14, 26, 235 } );
    DrawRectangleLinesEx( r, 2 * k, ui::Gold );
    ui::text( "ERFOLG", r.x + 20 * k, r.y + 12 * k, 20, ui::Gold );
    ui::text( L( a->m_title, a->m_titleEn ), r.x + 20 * k, r.y + 38 * k, 28, WHITE );
    ui::text( L( a->m_desc, a->m_descEn ), r.x + 20 * k, r.y + 72 * k, 18, ui::Dim );
    if ( m_toastT > 4.0f )
    {
        m_toasts.pop_front();
        m_toastT = 0.0f;
    }
}

void App::stats( float dt )
{
    drawBackdrop( dt, true );
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    int done = 0;
    for ( const AchievementDef &a : m_achieve.m_achievements ) done += m_feats.unlocked( a.m_name ) ? 1 : 0;
    if ( m_in.left() || m_in.right() || m_in.prevTab() || m_in.nextTab() )
    {
        m_statsTab = 1 - m_statsTab;
        m_sel = 0;
        m_audio.play( SfxE::MOVE );
    }
    heading( m_statsTab == 0 ? "Erfolge" : "Statistik",
             m_statsTab == 0 ? tf( "%d von %d", done, (int)m_achieve.m_achievements.size() ) : std::string( T( "Über alle Spiele" ) ) );
    float pw = std::min( W - 80 * k, 1400 * k );
    Rectangle r = { W * 0.5f - pw * 0.5f, H * 0.2f, pw, H * 0.68f };
    ui::panel( r );
    int n = m_statsTab == 0 ? (int)m_achieve.m_achievements.size() : (int)m_achieve.m_stats.size();
    float rowH = ( m_statsTab == 0 ? 74 : 46 ) * k;
    int   rows = std::max( 1, (int)( ( r.height - 40 * k ) / rowH ) );
    if ( m_in.up() ) m_sel = std::max( 0, m_sel - 1 );
    if ( m_in.down() ) m_sel = std::min( std::max( 0, n - 1 ), m_sel + 1 );
    int first = std::clamp( m_sel - rows / 2, 0, std::max( 0, n - rows ) );
    for ( int i = first; i < std::min( n, first + rows ); ++i )
    {
        float y = r.y + 20 * k + ( i - first ) * rowH;
        if ( i == m_sel ) ui::highlight( { r.x + 20 * k, y - 4 * k, pw - 40 * k, rowH - 6 * k }, m_t );
        if ( m_statsTab == 0 )
        {
            const AchievementDef &a = m_achieve.m_achievements[(size_t)i];
            bool                  got = m_feats.unlocked( a.m_name );
            bool                  veiled = a.m_hidden && !got;
            ui::text( veiled ? "???" : L( a.m_title, a.m_titleEn ), r.x + 40 * k, y, 28, got ? ui::Gold : ui::Ink );
            ui::text( veiled ? std::string( T( "Ein verborgener Erfolg." ) ) : L( a.m_desc, a.m_descEn ), r.x + 40 * k, y + 34 * k, 20, ui::Dim );
            if ( got )
                ui::text( m_feats.m_unlocked.at( a.m_name ), r.x + pw - 40 * k, y + 4 * k, 22, ui::Gold, ui::AlignE::RIGHT );
            else if ( !veiled )
                ui::bar( { r.x + pw - 300 * k, y + 14 * k, 260 * k, 8 * k }, progress( m_feats, a ), { 120, 200, 255, 255 } );
        }
        else
        {
            const StatDef &s = m_achieve.m_stats[(size_t)i];
            ui::text( L( s.m_title, s.m_titleEn ), r.x + 40 * k, y, 28, ui::Ink );
            ui::text( statText( m_feats, s ), r.x + pw - 40 * k, y, 28, WHITE, ui::AlignE::RIGHT );
        }
    }
    hints( "Links/Rechts: Erfolge / Statistik    Esc / B: zurück" );
    if ( m_in.back() || m_in.accept() ) go( ScreenE::MAIN );
}

}   // namespace zenith

namespace zenith
{

// ---------------------------------------------------------------------------------------------------- benchmark

void App::bench()
{
    // Heavy stretches flown by the autopilot with the full picture, no frame limit: the opening of each episode's
    // last mission and its boss. The frame times go to the screen's console and to <user>/bench.txt.
    std::vector<std::string> names;
    for ( const char *n : { "e1_10", "e2_10", "e3_10", "e4_10", "e5_10" } )
        if ( m_cat.level( n ) ) names.push_back( n );
    std::vector<int> levels = m_opt.m_benchAll ? std::vector<int>{ 0, 1, 2 } : std::vector<int>{ quality() };
    const char      *qname[] = { "performance", "balanced", "quality" };
    std::string      report;
    char             line[256];
    snprintf( line, sizeof( line ), "Zenith Lancer %s bench, %dx%d%s\n", ZENITH_VERSION, GetScreenWidth(), GetScreenHeight(),
              getenv( "SteamDeck" ) ? " (Steam Deck)" : "" );
    report += line;
    SetTargetFPS( 0 );
    m_opt.m_auto = true;
    for ( int q : levels )
    {
        m_view.m_quality = q;
        resize();
        std::vector<float> all;
        for ( const std::string &n : names )
            for ( int part = 0; part < 2; ++part )
            {
                m_practice = true;
                m_practiceGear = 0;
                m_mode = ModeE::CAMPAIGN;
                m_level = n;
                startMission( n );
                if ( part == 1 ) toBoss();
                m_world.m_immortal = true;
                std::vector<float> times;
                double             start = GetTime();
                while ( GetTime() - start < 20.0 && !WindowShouldClose() && !m_world.finished() )
                {
                    m_in.update();
                    BeginDrawing();
                    ClearBackground( BLACK );
                    frame( std::min( GetFrameTime(), 0.1f ) );
                    EndDrawing();
                    if ( GetTime() - start > 1.0 ) times.push_back( GetFrameTime() );
                }
                if ( times.empty() ) continue;
                std::vector<float> sorted = times;
                std::sort( sorted.begin(), sorted.end() );
                double sum = 0;
                for ( float t : times ) sum += t;
                float avg = (float)( sum / times.size() ), p99 = sorted[(size_t)( sorted.size() * 0.99 )], worst = sorted.back();
                snprintf( line, sizeof( line ), "%-12s %-6s %-5s  %6.1f fps   1%% low %6.1f fps   worst %6.1f ms\n",
                          qname[q], n.c_str(), part ? "boss" : "start", 1.0f / avg, 1.0f / p99, worst * 1000.0f );
                report += line;
                printf( "%s", line );
                fflush( stdout );
                all.insert( all.end(), times.begin(), times.end() );
            }
        if ( !all.empty() )
        {
            std::sort( all.begin(), all.end() );
            double sum = 0;
            for ( float t : all ) sum += t;
            snprintf( line, sizeof( line ), "%-12s all           %6.1f fps   1%% low %6.1f fps\n", qname[q], all.size() / sum,
                      1.0f / all[(size_t)( all.size() * 0.99 )] );
            report += line;
            printf( "%s", line );
        }
    }
    m_practice = false;
    if ( FILE *f = fopen( ( userDir() + "bench.txt" ).c_str(), "w" ) )
    {
        fputs( report.c_str(), f );
        fclose( f );
    }
    printf( "written: %sbench.txt\n", userDir().c_str() );
}

}   // namespace zenith
