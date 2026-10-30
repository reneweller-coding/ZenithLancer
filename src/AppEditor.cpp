/**
 * @file AppEditor.cpp
 * @brief The wave editor (from the Mods screen): a mission of one's own put together from the game's enemies, waves,
 *        paths and pickups, its field shown at the moment chosen, flown as a test at once and saved as the mod
 *        "editor" (<user>/mods/editor: the mission as a data file, and the editor's own file to edit it again).
 * @author Rene Weller
 */
#include "App.h"

#include "Files.h"
#include "Lang.h"
#include "Ui.h"

#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace zenith
{

namespace fs = std::filesystem;

namespace
{

// What an event may be, its formations and paths (DATA.md).
const char *const Kinds[] = { "wave", "ground", "obstacle", "boss", "pickup" };
const char *const KindTitles[] = { "Welle", "Bodenziel", "Hindernis", "Boss", "Fundstück" };
const char *const Forms[] = { "single", "column", "line", "v", "arc", "random", "sides", "grid" };
const char *const FormTitles[] = { "einzeln", "Kolonne", "Reihe", "Keil", "Bogen", "verstreut", "von beiden Seiten", "Raster" };
const char *const Paths[] = { "straight", "dive", "sine", "zigzag", "swoop", "arc", "strafe", "uturn", "loop", "hover", "chase", "orbit" };
const char *const Pickups[] = { "credits", "power", "shield", "armor", "special", "life" };
const char *const PickupTitles[] = { "Kredite", "Energie", "Schild", "Panzerung", "Spezialladung", "Leben" };
constexpr int     KindCount = 5, FormCount = 8, PathCount = 12, PickupCount = 6;

std::string editorDir() { return userDir() + "mods/editor/"; }

int indexOf( const char *const *list, int n, const std::string &s )
{
    for ( int i = 0; i < n; ++i )
        if ( s == list[i] ) return i;
    return 0;
}

std::string clock( float t )
{
    int s = (int)std::lround( t );
    return std::to_string( s / 60 ) + ":" + ( s % 60 < 10 ? "0" : "" ) + std::to_string( s % 60 );
}

}   // namespace

std::vector<std::string> App::editorEnemies( int kind ) const
{
    // The enemies an event of a kind may bring (waves: fliers; the others their own kind).
    EnemyKindE want = kind == 1 ? EnemyKindE::GROUND : kind == 2 ? EnemyKindE::OBSTACLE : kind == 3 ? EnemyKindE::BOSS : EnemyKindE::AIR;
    std::vector<std::string> out;
    for ( const std::string &n : m_codex )
        if ( m_cat.m_enemies.at( n ).m_kind == want ) out.push_back( n );
    return out;
}

void App::editorList()
{
    // The missions saved before: the editor's own files in <user>/mods/editor/missions.
    m_editFiles.clear();
    std::error_code e;
    for ( const auto &entry : fs::directory_iterator( editorDir() + "missions", e ) )
        if ( entry.path().extension() == ".edit" ) m_editFiles.push_back( entry.path().stem().string() );
    std::sort( m_editFiles.begin(), m_editFiles.end() );
}

void App::editorOpen( const std::string &name )
{
    // A saved mission read back, or a new one with a first wave.
    m_edit = EditMission();
    m_edit.m_name = name;
    std::ifstream in( editorDir() + "missions/" + name + ".edit" );
    std::string   line;
    while ( std::getline( in, line ) )
    {
        if ( !line.empty() && line.back() == '\r' ) line.pop_back();
        std::istringstream ls( line );
        std::string        key;
        ls >> key;
        if ( key == "title" ) std::getline( ls >> std::ws, m_edit.m_title );
        else if ( key == "theme" ) ls >> m_edit.m_theme;
        else if ( key == "music" ) ls >> m_edit.m_music;
        else if ( key == "scroll" ) ls >> m_edit.m_scroll;
        else if ( key == "gear" ) ls >> m_edit.m_gear;
        else if ( key == "event" )
        {
            EditEvent ev;
            std::string kind, form, path;
            ls >> ev.m_t >> kind >> ev.m_what >> ev.m_n >> form >> ev.m_x >> path;
            ev.m_kind = indexOf( Kinds, KindCount, kind );
            ev.m_form = indexOf( Forms, FormCount, form );
            ev.m_path = indexOf( Paths, PathCount, path );
            m_edit.m_events.push_back( ev );
        }
    }
    if ( m_edit.m_title.empty() ) m_edit.m_title = tf( "Eigener Einsatz %s", name.substr( name.find( '_' ) + 1 ).c_str() );
    if ( m_edit.m_theme.empty() )
    {
        const LevelDef *first = m_cat.level( "e1_01" );
        m_edit.m_theme = first ? first->m_theme : m_cat.m_themes.empty() ? "space" : m_cat.m_themes.front().m_name;
    }
    if ( m_edit.m_music.empty() ) m_edit.m_music = "e1_a";
    if ( m_edit.m_events.empty() )
    {
        EditEvent ev;
        std::vector<std::string> air = editorEnemies( 0 );
        ev.m_what = air.empty() ? "" : air.front();
        m_edit.m_events.push_back( ev );
    }
    m_editField = 0;
    m_editDirty = true;
    m_editMsg.clear();
}

LevelDef App::editorLevel() const
{
    // The mission as the game reads one: the events sorted by time, a warning before each boss, the end after the
    // last event (a boss holds the timeline until it is down).
    LevelDef lv;
    lv.m_name = m_edit.m_name;
    lv.m_title = lv.m_titleEn = m_edit.m_title;
    lv.m_episode = std::clamp( m_edit.m_gear, 1, 5 );
    lv.m_theme = m_edit.m_theme;
    lv.m_music = m_edit.m_music;
    lv.m_scroll = m_edit.m_scroll;
    lv.m_seed = 4242;
    std::vector<EditEvent> evs = m_edit.m_events;
    std::stable_sort( evs.begin(), evs.end(), []( const EditEvent &a, const EditEvent &b ) { return a.m_t < b.m_t; } );
    float last = 0.0f;
    for ( const EditEvent &e : evs )
    {
        LevelEvent ev;
        ev.m_t = e.m_t;
        ev.m_type = Kinds[e.m_kind];
        ev.m_p.m_key = ev.m_type;
        if ( e.m_kind == 4 )
        {
            ev.m_p.m_args = { Pickups[std::clamp( e.m_n - 1, 0, PickupCount - 1 )] };
            ev.m_p.m_kv["x"] = std::to_string( e.m_x );
            ev.m_p.m_kv["value"] = "250";
        }
        else
        {
            ev.m_p.m_args = { e.m_what };
            ev.m_p.m_kv["x"] = std::to_string( e.m_x );
            if ( e.m_kind == 0 || e.m_kind == 2 )
            {
                ev.m_p.m_kv["n"] = std::to_string( e.m_n );
                ev.m_p.m_kv["form"] = Forms[e.m_form];
                ev.m_p.m_kv["spacing"] = "70";
                ev.m_p.m_kv["gap"] = "0.4";
                ev.m_p.m_kv["range"] = "560";
            }
            if ( e.m_kind == 0 ) ev.m_p.m_kv["path"] = Paths[e.m_path];
        }
        if ( e.m_kind == 3 )
        {
            LevelEvent warn;
            warn.m_t = std::max( 0.0f, e.m_t - 3.0f );
            warn.m_type = warn.m_p.m_key = "warn";
            lv.m_events.push_back( warn );
        }
        lv.m_events.push_back( ev );
        last = std::max( last, e.m_t );
    }
    LevelEvent end;
    end.m_t = last + 12.0f;
    end.m_type = end.m_p.m_key = "end";
    lv.m_events.push_back( end );
    std::stable_sort( lv.m_events.begin(), lv.m_events.end(), []( const LevelEvent &a, const LevelEvent &b ) { return a.m_t < b.m_t; } );
    lv.m_length = last + 15.0f;
    return lv;
}

bool App::editorSave()
{
    // The editor's own file (to edit it again) and the mission as a data file of the mod "editor", which is switched
    // on: the mission is in the game from the next start (practice lists it once it is flown).
    std::error_code e;
    fs::create_directories( editorDir() + "missions", e );
    fs::create_directories( editorDir() + "data/levels/editor", e );
    std::ofstream ed( editorDir() + "missions/" + m_edit.m_name + ".edit" );
    ed << "title " << m_edit.m_title << "\ntheme " << m_edit.m_theme << "\nmusic " << m_edit.m_music << "\nscroll "
       << m_edit.m_scroll << "\ngear " << m_edit.m_gear << "\n";
    for ( const EditEvent &ev : m_edit.m_events )
        ed << "event " << ev.m_t << " " << Kinds[ev.m_kind] << " " << ( ev.m_what.empty() ? "-" : ev.m_what ) << " " << ev.m_n << " "
           << Forms[ev.m_form] << " " << ev.m_x << " " << Paths[ev.m_path] << "\n";
    if ( !ed ) return false;
    LevelDef      lv = editorLevel();
    std::ofstream f( editorDir() + "data/levels/editor/" + m_edit.m_name + ".txt" );
    f << "# Made with Zenith Lancer's wave editor (" << m_edit.m_name << ".edit beside it is the editor's own file).\n\n";
    f << "level " << lv.m_name << "\n  title \"" << lv.m_title << "\"\n  episode " << lv.m_episode << "\n  theme " << lv.m_theme
      << "\n  music " << lv.m_music << "\n  scroll " << lv.m_scroll << "\n  length " << lv.m_length << "\n\n";
    for ( const LevelEvent &ev : lv.m_events )
    {
        f << "  at " << ev.m_t << " " << ev.m_type;
        for ( const std::string &a : ev.m_p.m_args ) f << " " << a;
        std::vector<std::pair<std::string, std::string>> kv( ev.m_p.m_kv.begin(), ev.m_p.m_kv.end() );
        std::sort( kv.begin(), kv.end() );
        for ( const auto &[k, v] : kv ) f << " " << k << "=" << v;
        f << "\n";
    }
    std::ofstream mod( editorDir() + "mod.txt" );
    mod << "title Eigene Einsätze\ntitle_en Own missions\nauthor Wellen-Editor\ndesc Mit dem Wellen-Editor gebaute Einsätze.\n"
           "desc_en Missions built with the wave editor.\n";
    if ( std::find( m_set.m_mods.begin(), m_set.m_mods.end(), "editor" ) == m_set.m_mods.end() )
    {
        m_set.m_mods.push_back( "editor" );
        saveSettings();
    }
    return (bool)f && (bool)mod;
}

void App::editor( float dt )
{
    drawBackdrop( dt, true );
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    if ( m_enter )
    {
        // Back from a test flight, or in from the Mods screen.
        editorList();
        m_editing = m_practice = false;
        m_editTyping = false;
    }
    float     pw = std::min( W - 60 * k, 1700 * k );
    Rectangle r = { W * 0.5f - pw * 0.5f, H * 0.17f, pw, H * 0.72f };
    if ( m_editStep == 0 )
    {
        // The missions saved before, or a new one.
        heading( "Wellen-Editor", "Eigene Einsätze aus den Gegnern und Wegen des Spiels" );
        ui::panel( r );
        std::vector<std::string> items = { T( "Neuer Einsatz" ) };
        for ( const std::string &n : m_editFiles ) items.push_back( n );
        items.push_back( T( "Zurück" ) );
        int c = menu( items, r.x + 60 * k, r.y + 40 * k, pw * 0.5f, m_sel, 32 );
        ui::text( tf( "Gespeichert in %s", editorDir().c_str() ), r.x + 60 * k, r.y + r.height - 50 * k, 20, ui::Dim );
        hints( "Eingabe / A: wählen    Esc / B: zurück" );
        if ( c == 0 )
        {
            int n = 1;
            while ( std::find( m_editFiles.begin(), m_editFiles.end(), "user_" + std::to_string( n ) ) != m_editFiles.end() ) ++n;
            editorOpen( "user_" + std::to_string( n ) );
            m_editStep = 1;
            m_sel = 0;
        }
        else if ( c > 0 && c <= (int)m_editFiles.size() )
        {
            editorOpen( m_editFiles[(size_t)c - 1] );
            m_editStep = 1;
            m_sel = 0;
        }
        else if ( c == (int)items.size() - 1 || m_in.back() )
            go( ScreenE::MODS );
        return;
    }
    heading( "Wellen-Editor", m_edit.m_title );
    // The rows: the mission's settings, its events in time, then what can be done.
    std::vector<int> order( m_edit.m_events.size() );
    for ( size_t i = 0; i < order.size(); ++i ) order[i] = (int)i;
    std::stable_sort( order.begin(), order.end(), [&]( int a, int b ) { return m_edit.m_events[(size_t)a].m_t < m_edit.m_events[(size_t)b].m_t; } );
    std::string musicTitle = m_edit.m_music;
    for ( const auto &p : m_pieces )
        if ( p[0] == m_edit.m_music ) musicTitle = L( p[1], p[2] );
    enum { TITLE, THEME, MUSIC, SCROLL, GEAR, FIRST };
    std::vector<std::string> items = { std::string( T( "Titel" ) ) + ":  " + m_edit.m_title + ( m_editTyping ? "_" : "" ),
                                       std::string( T( "Boden" ) ) + ":  " + m_edit.m_theme,
                                       std::string( T( "Musik" ) ) + ":  " + musicTitle,
                                       std::string( T( "Bodentempo" ) ) + ":  " + std::to_string( (int)m_edit.m_scroll ),
                                       std::string( T( "Ausrüstung" ) ) + ":  " + tf( "Episode %d", m_edit.m_gear ) };
    auto describe = [&]( const EditEvent &e ) {
        std::string s = clock( e.m_t ) + "   " + T( KindTitles[e.m_kind] ) + "   ";
        if ( e.m_kind == 4 ) return s + T( PickupTitles[std::clamp( e.m_n - 1, 0, PickupCount - 1 )] ) + tf( "   x %d", e.m_x );
        const EnemyDef *d = m_cat.enemy( e.m_what );
        s += d ? L( d->m_title, d->m_titleEn ) : e.m_what;
        if ( e.m_kind == 0 || e.m_kind == 2 ) s += tf( " ×%d   ", e.m_n ) + T( FormTitles[e.m_form] );
        if ( e.m_kind == 0 ) s += std::string( "   " ) + Paths[e.m_path];
        return s;
    };
    for ( int i : order ) items.push_back( describe( m_edit.m_events[(size_t)i] ) );
    int add = (int)items.size();
    items.insert( items.end(), { T( "+ Ereignis" ), T( "Probeflug" ), T( "Speichern" ), T( "Zurück" ) } );
    ui::panel( r );
    float lw = pw * 0.56f, size = 26, rowH = ui::height( size ) * 1.45f;
    int   rows = std::max( 4, (int)( ( r.height - 60 * k ) / rowH ) ), n = (int)items.size();
    int   first = std::clamp( m_sel - rows / 2, 0, std::max( 0, n - rows ) );
    std::vector<std::string> view( items.begin() + first, items.begin() + std::min( n, first + rows ) );
    int sel = m_sel - first, before = m_sel;
    int c = menu( view, r.x + 40 * k, r.y + 30 * k, lw - 40 * k, sel, size );
    m_sel = std::clamp( first + sel, 0, n - 1 );
    if ( c >= 0 ) c += first;
    bool typing = m_editTyping;
    if ( typing )
    {
        // Typing the title: the keys are letters now (W and S would move the list).
        m_sel = TITLE;
        c = -1;
    }
    if ( m_sel != before ) m_editField = 0, m_editDirty = true;
    int  d = m_in.right() ? 1 : m_in.left() ? -1 : 0;
    int  big = m_in.nextTab() ? 1 : m_in.prevTab() ? -1 : 0;
    bool changed = false;
    // The mission's settings.
    if ( typing )
    {
        for ( int ch = GetCharPressed(); ch > 0; ch = GetCharPressed() )
            if ( ch >= 32 && ch < 127 && m_edit.m_title.size() < 40 ) m_edit.m_title += (char)ch;
        if ( IsKeyPressed( KEY_BACKSPACE ) && !m_edit.m_title.empty() ) m_edit.m_title.pop_back();
        if ( IsKeyPressed( KEY_ENTER ) || IsKeyPressed( KEY_KP_ENTER ) || IsKeyPressed( KEY_ESCAPE ) ) m_editTyping = false;
    }
    else if ( c == TITLE )
    {
        m_editTyping = true;
        while ( GetCharPressed() > 0 ) {}
    }
    if ( m_sel == THEME && d && !m_cat.m_themes.empty() )
    {
        int i = 0, nt = (int)m_cat.m_themes.size();
        while ( i < nt && m_cat.m_themes[(size_t)i].m_name != m_edit.m_theme ) ++i;
        m_edit.m_theme = m_cat.m_themes[(size_t)( ( i + d + nt ) % nt )].m_name;
        changed = true;
    }
    if ( m_sel == MUSIC && d && !m_pieces.empty() )
    {
        int i = 0, np = (int)m_pieces.size();
        while ( i < np && m_pieces[(size_t)i][0] != m_edit.m_music ) ++i;
        m_edit.m_music = m_pieces[(size_t)( ( i + d + np ) % np )][0];
        changed = true;
    }
    if ( m_sel == SCROLL && d ) m_edit.m_scroll = clampf( m_edit.m_scroll + d * 10.0f, 0.0f, 200.0f ), changed = true;
    if ( m_sel == GEAR && d ) m_edit.m_gear = ( m_edit.m_gear - 1 + d + 5 ) % 5 + 1, changed = true;
    // An event: Tab / Y picks the value, left/right change it (Q/E or LB/RB in big steps).
    const char *fields[] = { "Zeit", "Art", "Was", "Anzahl", "Formation", "x", "Weg", "Löschen" };
    if ( m_sel >= FIRST && m_sel < add )
    {
        EditEvent &e = m_edit.m_events[(size_t)order[(size_t)( m_sel - FIRST )]];
        if ( m_in.other() ) m_editField = ( m_editField + 1 ) % 8, m_audio.play( SfxE::MOVE );
        int step = d ? d : big * 10;
        if ( step )
        {
            changed = true;
            switch ( m_editField )
            {
            case 0: e.m_t = clampf( e.m_t + step, 0.0f, 900.0f ); break;
            case 1:
            {
                e.m_kind = ( e.m_kind + ( step > 0 ? 1 : KindCount - 1 ) ) % KindCount;
                std::vector<std::string> list = editorEnemies( e.m_kind );
                e.m_what = list.empty() ? "" : list.front();
                e.m_n = e.m_kind == 4 ? 1 : e.m_kind == 3 || e.m_kind == 1 ? 1 : 5;
                break;
            }
            case 2:
                if ( e.m_kind == 4 ) e.m_n = ( e.m_n - 1 + ( step > 0 ? 1 : PickupCount - 1 ) ) % PickupCount + 1;
                else
                {
                    std::vector<std::string> list = editorEnemies( e.m_kind );
                    if ( !list.empty() )
                    {
                        int i = (int)( std::find( list.begin(), list.end(), e.m_what ) - list.begin() );
                        int nl = (int)list.size();
                        e.m_what = list[(size_t)( ( i + ( step > 0 ? 1 : nl - 1 ) + nl ) % nl )];
                    }
                }
                break;
            case 3: e.m_n = std::clamp( e.m_n + ( step > 0 ? 1 : -1 ), 1, 24 ); break;
            case 4: e.m_form = ( e.m_form + ( step > 0 ? 1 : FormCount - 1 ) ) % FormCount; break;
            case 5: e.m_x = std::clamp( e.m_x + step * 20, 40, 680 ); break;
            case 6: e.m_path = ( e.m_path + ( step > 0 ? 1 : PathCount - 1 ) ) % PathCount; break;
            default: changed = false; break;
            }
        }
        if ( m_editField == 7 && c == m_sel )
        {
            m_edit.m_events.erase( m_edit.m_events.begin() + order[(size_t)( m_sel - FIRST )] );
            m_sel = std::max( FIRST - 1, m_sel - 1 );
            m_editField = 0;
            changed = true;
        }
    }
    if ( c == add )
    {
        // A new wave a little after the last event.
        EditEvent e;
        for ( const EditEvent &o : m_edit.m_events ) e.m_t = std::max( e.m_t, o.m_t + 4.0f );
        std::vector<std::string> air = editorEnemies( 0 );
        e.m_what = air.empty() ? "" : air[(size_t)( m_edit.m_events.size() % std::max<size_t>( 1, air.size() ) )];
        m_edit.m_events.push_back( e );
        m_sel = FIRST + (int)m_edit.m_events.size() - 1;
        m_editField = 0;
        changed = true;
    }
    if ( changed ) m_editDirty = true;
    // The field at the chosen moment: the mission flown by the autopilot (unhurt) to two seconds after the event.
    float at = 3.0f;
    if ( m_sel >= FIRST && m_sel < add ) at = m_edit.m_events[(size_t)order[(size_t)( m_sel - FIRST )]].m_t + 2.0f;
    if ( m_editDirty )
    {
        m_editDirty = false;
        m_cat.m_levels["editor_preview"] = editorLevel();
        m_cat.m_levels["editor_preview"].m_name = "editor_preview";
        const LevelDef *lv = m_cat.level( "editor_preview" );
        m_shopWorld.begin( &m_cat, lv, ModeE::CAMPAIGN, 2, 7 );
        m_shopWorld.addPlayer( 0, midLoadout( m_cat, m_edit.m_gear ), 0 );
        m_shopWorld.m_immortal = true;
        for ( int i = 0; i < 60 * 600 && m_shopWorld.m_clock < at && !m_shopWorld.finished(); ++i )
        {
            PlayerInput in[2] = { autopilot( m_shopWorld, 0 ), {} };
            m_shopWorld.step( in );
        }
        m_preview.setTheme( m_cat, m_edit.m_theme, lv->m_seed );
        m_preview.setView( { FieldW * 0.5f, FieldH * 0.5f }, FieldH * 0.5f );
        m_preview.reset();
        m_previewKey.clear();
    }
    m_preview.update( m_shopWorld, dt );
    m_preview.draw( m_shopWorld, m_cat );
    Rectangle box = { r.x + lw + 20 * k, r.y + 20 * k, pw - lw - 40 * k, r.height * 0.62f };
    float     s = std::min( box.width / m_preview.width(), box.height / m_preview.height() );
    m_preview.present( { box.x + ( box.width - m_preview.width() * s ) * 0.5f, box.y, m_preview.width() * s, m_preview.height() * s } );
    float ty = box.y + box.height + 16 * k;
    ui::text( tf( "Bei %s", clock( at ).c_str() ), box.x, ty, 22, ui::Dim );
    if ( m_sel >= FIRST && m_sel < add )
    {
        // The event's values, the one being changed in gold.
        for ( int f = 0; f < 8; ++f )
            ui::text( T( fields[f] ), box.x + ( f % 4 ) * box.width * 0.25f, ty + 36 * k + ( f / 4 ) * 34 * k, 22, f == m_editField ? ui::Gold : ui::Ink );
        hints( "Tab / Y: Wert    Links/Rechts: ändern    Q/E, LB/RB: große Schritte    Eingabe auf Löschen: löschen    Esc / B: zurück" );
    }
    else
        hints( typing ? "Tippen: Titel    Eingabe: fertig" : m_sel == TITLE ? "Eingabe: Titel tippen    Esc / B: zurück"
                                                                     : "Links/Rechts: ändern    Eingabe / A: wählen    Esc / B: zurück" );
    if ( !m_editMsg.empty() ) ui::text( m_editMsg, box.x, ty + 110 * k, 22, ui::Good );
    if ( c == add + 1 )
    {
        // The test flight: saved, then flown as a practice flight that comes back here.
        editorSave();
        m_cat.m_levels[m_edit.m_name] = editorLevel();
        m_practice = true;
        m_practiceBoss = false;
        m_practiceGear = 0;
        m_editing = true;
        m_survival = m_daily = m_weekly = false;
        m_mode = ModeE::CAMPAIGN;
        m_players = 1;
        m_level = m_edit.m_name;
        startMission( m_level );
        return;
    }
    if ( c == add + 2 ) m_editMsg = editorSave() ? T( "Gespeichert (Mod \"editor\" ist an)." ) : T( "Speichern fehlgeschlagen." );
    if ( c == add + 3 || ( m_in.back() && !typing ) )
    {
        m_editStep = 0;
        m_sel = 0;
        editorList();
        m_previewKey.clear();
    }
}

}   // namespace zenith
