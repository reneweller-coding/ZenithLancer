/**
 * @file AppArchive.cpp
 * @brief The archive (the enemies met, the music heard, the pilots' paints), the flight school (lessons one after
 *        the other on a field of their own) and the ghost of the daily and weekly flights (the best flight of the
 *        same seed, flown beside the pilot as a pale ring).
 * @author Rene Weller
 */
#include "App.h"

#include "Files.h"
#include "Lang.h"
#include "Survival.h"
#include "Ui.h"

#include <raylib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace zenith
{

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------------------------------- paints

const std::vector<PaintDef> &paints()
{
    static const std::vector<PaintDef> list = {
        { "", WHITE, 0.0f, "", "Werkslack" },
        { "cobalt", { 70, 120, 255, 255 }, 0.92f, "", "Kobalt" },
        { "signal", { 255, 70, 60, 255 }, 0.92f, "", "Signalrot" },
        { "ember", { 255, 120, 40, 255 }, 0.92f, "episode1", "Glut" },
        { "tide", { 40, 210, 200, 255 }, 0.92f, "episode2", "Gezeiten" },
        { "ash", { 150, 152, 165, 255 }, 0.92f, "episode3", "Asche" },
        { "thorn", { 110, 210, 80, 255 }, 0.92f, "episode4", "Dorn" },
        { "noon", { 255, 205, 90, 255 }, 0.92f, "episode5", "Mittagsgold" },
        { "dusk", { 210, 120, 70, 255 }, 0.92f, "episode6", "Abendrot" },
        { "school", { 235, 240, 255, 255 }, 0.85f, "school", "Fluglehrerweiß" },
        { "night", { 45, 50, 70, 255 }, 0.92f, "kills_1000", "Nachtschwarz" },
        { "chain", { 255, 80, 210, 255 }, 0.92f, "chain_50", "Kettenmagenta" },
        { "graze", { 160, 230, 255, 255 }, 0.92f, "grazes_1000", "Haarlinie" },
        { "weekly", { 170, 110, 255, 255 }, 0.92f, "weekly_1", "Wochenviolett" },
        { "archive", { 120, 255, 200, 255 }, 0.92f, "seen_100", "Archivgrün" },
        { "zenith", { 255, 240, 205, 255 }, 0.92f, "campaign_zenith", "Zenitgold" },
        { "bronze", { 205, 127, 50, 255 }, 0.92f, "gold_10", "Bronzeglanz" },
        { "medal", { 255, 196, 40, 255 }, 0.92f, "gold_all", "Medaillengold" },
    };
    return list;
}

void App::applyPaints()
{
    // A paint not (or no longer) open falls back to the hull's own colours.
    for ( int i = 0; i < 2; ++i )
    {
        const PaintDef *p = &paints()[0];
        for ( const PaintDef &d : paints() )
            if ( m_set.m_paint[i] == d.m_name && ( !*d.m_need || m_feats.unlocked( d.m_need ) ) ) p = &d;
        m_view.m_paint[i] = m_preview.m_paint[i] = p->m_color;
        m_view.m_paintAmount[i] = m_preview.m_paintAmount[i] = p->m_amount;
    }
}

// ---------------------------------------------------------------------------------------------------- archive

void App::loadArchive()
{
    // The music room's pieces (data/lang/music.txt) and the enemies in the archive's order: by episode, fliers
    // before ground targets, obstacles and bosses, the weak before the strong.
    m_pieces.clear();
    std::ifstream in( m_cat.m_dir + "/lang/music.txt" );
    std::string   line;
    while ( std::getline( in, line ) )
    {
        if ( !line.empty() && line.back() == '\r' ) line.pop_back();
        size_t sp = line.find( ' ' ), bar = line.find( " | " );
        if ( line.empty() || line[0] == '#' || sp == std::string::npos || bar == std::string::npos || bar < sp ) continue;
        m_pieces.push_back( { line.substr( 0, sp ), line.substr( sp + 1, bar - sp - 1 ), line.substr( bar + 3 ) } );
    }
    m_codex.clear();
    for ( const auto &[n, e] : m_cat.m_enemies )
        if ( e.m_kind != EnemyKindE::PART && n != "target" && !e.m_title.empty() && !e.m_scenery ) m_codex.push_back( n );
    auto episode = []( const std::string &n ) {
        return n.size() > 2 && n[0] == 'e' && isdigit( (unsigned char)n[1] ) && n[2] == '_' ? n[1] - '0' : 0;
    };
    auto order = []( EnemyKindE k ) { return k == EnemyKindE::AIR ? 0 : k == EnemyKindE::GROUND ? 1 : k == EnemyKindE::OBSTACLE ? 2 : 3; };
    std::stable_sort( m_codex.begin(), m_codex.end(), [&]( const std::string &a, const std::string &b ) {
        const EnemyDef &x = m_cat.m_enemies.at( a ), &y = m_cat.m_enemies.at( b );
        if ( episode( a ) != episode( b ) ) return episode( a ) < episode( b );
        if ( order( x.m_kind ) != order( y.m_kind ) ) return order( x.m_kind ) < order( y.m_kind );
        return x.m_hp < y.m_hp;
    } );
}

void App::noteSeen()
{
    // Every enemy that came onto the field opens in the archive (a replay shows what was seen already).
    if ( m_replaying ) return;
    for ( const Enemy &e : m_world.m_enemies )
        if ( e.m_entered && e.m_def && !e.m_def->m_scenery ) m_prof.m_seen.insert( e.m_def->m_name );
}

namespace
{

/** @return what an enemy's guns do, in words */
std::string weaponsOf( const EnemyDef &d )
{
    static const std::pair<const char *, const char *> words[] = {
        { "aimed", "gezielte Schüsse" }, { "fan", "Fächer" },     { "ring", "Ringe" },       { "spiral", "Spiralen" },
        { "down", "Sperrfeuer" },        { "laser", "Laser" },    { "missile", "Raketen" },  { "mine", "Minen" },
        { "spawn", "Begleiter" },        { "wall", "Wände" },     { "rain", "Geschossregen" }
    };
    std::string out;
    for ( const auto &[key, word] : words )
        for ( const FireDef &f : d.m_fire )
            if ( f.m_pattern == key )
            {
                out += ( out.empty() ? "" : ", " ) + std::string( T( word ) );
                break;
            }
    return out.empty() ? std::string( T( "keine" ) ) : out;
}

}   // namespace

void App::archive( float dt )
{
    if ( m_enter )
    {
        // What the archive knows counts for the achievements.
        int seen = 0, heard = 0;
        for ( const std::string &n : m_codex ) seen += m_prof.m_seen.count( n ) ? 1 : 0;
        for ( const auto &p : m_pieces ) heard += m_prof.m_heard.count( p[0] ) ? 1 : 0;
        m_feats.max( "seen", seen );
        m_feats.max( "heard", heard );
        checkFeats();
        m_archivePiece.clear();
        m_codexShown.clear();
        m_previewKey.clear();
    }
    drawBackdrop( dt, true );
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    if ( m_in.prevTab() || m_in.nextTab() || m_in.left() || m_in.right() )
    {
        bool back = m_in.prevTab() || m_in.left();
        m_archiveTab = ( m_archiveTab + ( back ? 2 : 1 ) ) % 3;
        m_sel = 0;
        m_codexShown.clear();
        applyPaints();
        m_audio.play( SfxE::MOVE );
    }
    heading( "Archiv" );
    const char *tabs[] = { "Gegner", "Musik", "Lackierungen" };
    for ( int t = 0; t < 3; ++t )
        ui::text( tabs[t], W * 0.5f + ( t - 1 ) * 300 * k, 140 * k, 30, t == m_archiveTab ? ui::Accent : ui::Dim, ui::AlignE::CENTER );
    float     pw = std::min( W - 80 * k, 1500 * k );
    Rectangle r = { W * 0.5f - pw * 0.5f, H * 0.2f, pw, H * 0.68f };
    ui::panel( r );
    // A list that keeps the selected line in view; returns the line chosen (-1 none).
    auto list = [&]( const std::vector<std::string> &all, const std::vector<bool> &lit, float size ) {
        int rows = std::max( 1, (int)( ( r.height - 60 * k ) / ( ui::height( size ) * 1.45f ) ) );
        int n = (int)all.size();
        int first = std::clamp( m_sel - rows / 2, 0, std::max( 0, n - rows ) );
        std::vector<std::string> view( all.begin() + first, all.begin() + std::min( n, first + rows ) );
        int sel = m_sel - first;
        // Lines not open yet stay dim, but may be looked at.
        std::vector<bool> dim;
        for ( int i = first; i < std::min( n, first + rows ); ++i ) dim.push_back( !lit[(size_t)i] );
        int c = menu( view, r.x + 40 * k, r.y + 30 * k, pw * 0.42f, sel, size, nullptr, false, &dim );
        m_sel = std::clamp( first + sel, 0, std::max( 0, n - 1 ) );
        return c >= 0 ? first + c : -1;
    };
    float x = r.x + pw * 0.5f, y = r.y + 30 * k;
    if ( m_archiveTab == 0 )
    {
        // The enemies: the met ones by name, with their model turning on the preview's field.
        std::vector<std::string> names;
        std::vector<bool>        lit;
        int                      met = 0;
        for ( const std::string &n : m_codex )
        {
            const EnemyDef &d = m_cat.m_enemies.at( n );
            bool            seen = m_prof.m_seen.count( n ) > 0;
            met += seen ? 1 : 0;
            names.push_back( seen ? L( d.m_title, d.m_titleEn ) + ( d.m_kind == EnemyKindE::BOSS ? "  ·  Boss" : "" ) : "???" );
            lit.push_back( seen );
        }
        list( names, lit, 26 );
        ui::text( tf( "%d von %d", met, (int)m_codex.size() ), r.x + pw - 30 * k, r.y + r.height - 44 * k, 22, ui::Dim, ui::AlignE::RIGHT );
        if ( m_codex.empty() ) return;
        const std::string &name = m_codex[(size_t)m_sel];
        const EnemyDef    &d = m_cat.m_enemies.at( name );
        if ( !m_prof.m_seen.count( name ) )
            ui::text( "Noch nicht begegnet.", x + pw * 0.22f, r.y + r.height * 0.45f, 30, ui::Dim, ui::AlignE::CENTER );
        else
        {
            if ( m_codexShown != name )
            {
                // The enemy alone on a field of its own, its parts with it, seen from above as in a mission.
                m_codexShown = name;
                m_shopWorld.begin( &m_cat, nullptr, ModeE::PREVIEW, 2, 7 );
                Line still;
                still.m_kv["path"] = "still";
                m_shopWorld.spawn( &d, { FieldW * 0.5f, FieldH * 0.5f }, &still );
                for ( Enemy &e : m_shopWorld.m_enemies )
                {
                    e.m_entered = true;
                    e.m_ground = false;
                }
                const ModelMesh &mm = Renderer::s_forge.get( m_cat, d.m_model );
                float            reach = std::max( mm.m_radius * d.m_scale, d.m_radius * d.m_scale );
                for ( const Enemy &e : m_shopWorld.m_enemies )
                    reach = std::max( reach, std::sqrt( dist2( e.m_pos, { FieldW * 0.5f, FieldH * 0.5f } ) ) + e.m_def->m_radius * e.m_def->m_scale );
                m_preview.setTheme( m_cat, "gallery", 1 );
                m_preview.setView( { FieldW * 0.5f, FieldH * 0.5f }, std::max( 50.0f, reach * 1.25f ) );
                m_preview.reset();
            }
            // The enemy itself is the first on the field, its parts follow it.
            m_shopWorld.turn( 0, 25.0f * std::sin( m_t * 0.8f ) );
            m_preview.update( m_shopWorld, dt );
            m_preview.draw( m_shopWorld, m_cat );
            Rectangle box = { x, y, pw * 0.25f, r.height - 60 * k };
            float     s = std::min( box.width / m_preview.width(), box.height / m_preview.height() );
            m_preview.present( { box.x, box.y, m_preview.width() * s, m_preview.height() * s } );
            // Its facts beside it.
            float fx = x + pw * 0.27f, fy = y;
            auto  fact = [&]( const std::string &a, const std::string &b ) {
                ui::text( a, fx, fy, 20, ui::Dim );
                for ( const std::string &l : ui::wrap( b, pw * 0.2f, 24 ) )
                {
                    ui::text( l, fx, fy + 24 * k, 24, ui::Ink );
                    fy += 30 * k;
                }
                fy += 34 * k;
            };
            ui::text( L( d.m_title, d.m_titleEn ), fx, fy, 34, WHITE );
            fy += 56 * k;
            int         ep = name.size() > 2 && name[0] == 'e' && isdigit( (unsigned char)name[1] ) ? name[1] - '0' : 0;
            auto        epi = m_cat.m_episodes.find( ep );
            const char *kinds[] = { "Flieger", "Bodenziel", "Boss", "Bauteil", "Hindernis" };
            fact( T( "Herkunft" ), epi != m_cat.m_episodes.end() ? tf( "Episode %d", ep ) + "  ·  " + L( epi->second.m_title, epi->second.m_titleEn )
                                                               : std::string( T( "überall" ) ) );
            fact( T( "Art" ), T( kinds[std::clamp( (int)d.m_kind, 0, 4 )] ) );
            fact( T( "Panzerung" ), d.m_invulnerable ? std::string( T( "unzerstörbar" ) ) : fmt( (long long)std::lround( d.m_hp ) ) );
            fact( T( "Punkte" ), fmt( d.m_score ) );
            fact( T( "Waffen" ), weaponsOf( d ) );
            if ( !d.m_parts.empty() ) fact( T( "Bauteile" ), std::to_string( d.m_parts.size() ) + ( d.m_shielded ? "  ·  " + std::string( T( "schützen den Rumpf" ) ) : "" ) );
        }
        hints( "Links/Rechts: Seite    Esc / B: zurück" );
    }
    else if ( m_archiveTab == 1 )
    {
        // The music room: every piece once heard, played here as long as the room is open.
        m_audio.intensity( 1.0f );
        std::vector<std::string> names;
        std::vector<bool>        lit;
        for ( const auto &p : m_pieces )
        {
            bool heard = m_prof.m_heard.count( p[0] ) > 0;
            bool now = m_archivePiece == p[0] || m_archivePiece == p[0] + "_2";
            names.push_back( heard ? std::string( now ? "> " : "" ) + L( p[1], p[2] ) : "???" );
            lit.push_back( heard );
        }
        int c = list( names, lit, 26 );
        if ( c >= 0 && lit[(size_t)c] )
        {
            // Chosen again: its other take, where it has one.
            const std::string &cue = m_pieces[(size_t)c][0];
            bool two = fs::exists( m_assets + "/music/" + cue + "_2.ogg" );
            m_archivePiece = m_archivePiece == cue && two ? cue + "_2" : cue;
        }
        if ( !m_archivePiece.empty() )
        {
            std::string base = m_archivePiece.size() > 2 && m_archivePiece.compare( m_archivePiece.size() - 2, 2, "_2" ) == 0
                                   ? m_archivePiece.substr( 0, m_archivePiece.size() - 2 ) : m_archivePiece;
            for ( const auto &p : m_pieces )
                if ( p[0] == base )
                {
                    ui::text( "Es läuft", x + 40 * k, y, 22, ui::Dim );
                    ui::text( L( p[1], p[2] ), x + 40 * k, y + 30 * k, 40, WHITE );
                    bool two = fs::exists( m_assets + "/music/" + base + "_2.ogg" );
                    if ( two ) ui::text( tf( "Fassung %d von 2", base == m_archivePiece ? 1 : 2 ), x + 40 * k, y + 90 * k, 24, ui::Gold );
                }
        }
        int heard = 0;
        for ( const auto &p : m_pieces ) heard += m_prof.m_heard.count( p[0] ) ? 1 : 0;
        ui::text( tf( "%d von %d gehört", heard, (int)m_pieces.size() ), r.x + pw - 30 * k, r.y + r.height - 44 * k, 22, ui::Dim, ui::AlignE::RIGHT );
        hints( "Eingabe / A: abspielen, nochmals: andere Fassung    Links/Rechts: Seite    Esc / B: zurück" );
    }
    else
    {
        // The paints: chosen for the first or the second pilot (Tab / Y), shown on the pilot's hull.
        if ( m_in.other() )
        {
            m_paintPilot = 1 - m_paintPilot;
            m_codexShown.clear();
            m_audio.play( SfxE::MOVE );
        }
        std::vector<std::string> names;
        std::vector<bool>        lit;
        for ( const PaintDef &p : paints() )
        {
            bool open = !*p.m_need || m_feats.unlocked( p.m_need );
            std::string mark = m_set.m_paint[0] == p.m_name ? "  [1]" : "";
            if ( m_set.m_paint[1] == p.m_name ) mark += "  [2]";
            names.push_back( std::string( T( p.m_title ) ) + mark );
            lit.push_back( open );
        }
        int c = list( names, lit, 28 );
        const PaintDef &p = paints()[(size_t)m_sel];
        bool            open = !*p.m_need || m_feats.unlocked( p.m_need );
        if ( c >= 0 && open )
        {
            m_set.m_paint[m_paintPilot] = paints()[(size_t)c].m_name;
            saveSettings();
            m_audio.play( SfxE::BUY );
        }
        // The pilot's ship in the paint looked at.
        if ( m_codexShown != "paint" )
        {
            m_codexShown = "paint";
            m_shopWorld.begin( &m_cat, nullptr, ModeE::PREVIEW, 2, 7 );
            if ( !m_cat.m_arcadeShips.empty() )
                m_shopWorld.addPlayer( 0, m_cat.m_arcadeShips[(size_t)std::clamp( m_pilot[m_paintPilot], 0, (int)m_cat.m_arcadeShips.size() - 1 )].m_loadout, 0 );
            m_shopWorld.m_players[0].m_pos = { FieldW * 0.5f, FieldH * 0.5f };
            m_shopWorld.m_players[0].m_invuln = 0.0f;
            m_preview.setTheme( m_cat, "gallery", 1 );
            m_preview.setView( { FieldW * 0.5f, FieldH * 0.5f }, 46.0f );
            m_preview.reset();
        }
        m_shopWorld.m_players[0].m_bank = 0.6f * std::sin( m_t * 1.2f );
        m_preview.m_paint[0] = p.m_color;
        m_preview.m_paintAmount[0] = p.m_amount;
        m_preview.update( m_shopWorld, dt );
        m_preview.draw( m_shopWorld, m_cat );
        Rectangle box = { x + 40 * k, y, pw * 0.4f, r.height * 0.62f };
        float     s = std::min( box.width / m_preview.width(), box.height / m_preview.height() );
        m_preview.present( { box.x + ( box.width - m_preview.width() * s ) * 0.5f, box.y, m_preview.width() * s, m_preview.height() * s } );
        float ty = y + r.height * 0.64f;
        ui::text( tf( "Für Pilot %d", m_paintPilot + 1 ), x + 40 * k, ty, 26, m_paintPilot == 0 ? ui::Accent : ui::Gold );
        if ( !open )
            for ( const AchievementDef &a : m_achieve.m_achievements )
                if ( a.m_name == p.m_need )
                    ui::text( std::string( T( "Erfolg" ) ) + ": " + L( a.m_title, a.m_titleEn ), x + 40 * k, ty + 40 * k, 24, ui::Warn );
        hints( "Eingabe / A: lackieren    Tab / Y: Pilot 1 / 2    Links/Rechts: Seite    Esc / B: zurück" );
    }
    if ( m_in.back() )
    {
        m_archivePiece.clear();
        m_codexShown.clear();
        m_previewKey.clear();
        applyPaints();
        go( ScreenE::MAIN );
    }
}

// ---------------------------------------------------------------------------------------------------- flight school

Loadout schoolLoadout( const Catalog &cat )
{
    // The gear of the middle of episode 2, with a rear gun of two modes, two sidekicks and a special to learn on.
    Loadout          o = midLoadout( cat, 2 );
    const WeaponDef *rear = cat.weapon( o.m_rear );
    if ( !rear || rear->m_modes < 2 )
        for ( const auto &[n, w] : cat.m_weapons )
            if ( w.m_slot == "rear" && w.m_modes > 1 && !w.m_hidden )
            {
                o.m_rear = n;
                break;
            }
    o.m_rearLv = std::max( o.m_rearLv, 3 );
    for ( std::string *side : { &o.m_sideL, &o.m_sideR } )
        if ( side->empty() )
            for ( const auto &[n, s] : cat.m_sidekicks )
                if ( !s.m_hidden )
                {
                    *side = n;
                    break;
                }
    if ( o.m_special.empty() && !cat.m_specials.empty() ) o.m_special = cat.m_specials.begin()->first;
    return o;
}

namespace
{

constexpr int Lessons = 9;

/** @brief A lesson's title and text (German; T() gives the English); "%s" takes the key of its action. */
struct Lesson
{
    const char *m_title;
    const char *m_text;
    int         m_act;   ///< ActE of the "%s" (-1 none)
};

const Lesson LessonList[Lessons] = {
    { "Fliegen", "Bewege dein Schiff mit den Pfeiltasten, WASD oder dem linken Stick. Flieg ein paar Bahnen über das Feld.", -1 },
    { "Feuer", "Halte %s gedrückt und schieß die drei Zielscheiben ab.", (int)ActE::FIRE },
    { "Fokus", "Halte %s: Das Schiff fliegt langsam und genau, so passt es durch enge Lücken. Zwei Sekunden lang.", (int)ActE::FOCUS },
    { "Streifschuss", "Lass die Kugeln knapp an dir vorbeiziehen, ohne dich treffen zu lassen. Jeder Streifschuss bringt Punkte. Hier kann dir nichts geschehen.", -1 },
    { "Kette", "Schieß die Drohnen schnell nacheinander ab: Jeder Abschuss in zwei Sekunden verlängert die Kette und hebt den Multiplikator. Ein Treffer bricht sie.", -1 },
    { "Heckwaffe", "Deine Heckwaffe hat zwei Betriebsarten. Schalte mit %s zweimal um.", (int)ActE::MODE },
    { "Spezialwaffe", "%s löst die Spezialwaffe aus. Ihre Ladungen sind knapp, heb sie für Notfälle auf. Probier sie jetzt aus.", (int)ActE::SPECIAL },
    { "Seitenschiffe", "Die beiden Begleiter an deinen Seiten feuern von selbst mit. Manche haben Munition, andere laden sich auf.", -1 },
    { "Werkstatt", "Zwischen den Einsätzen kaufst du bei Quill bessere Waffen, Schilde und Generatoren. Gute Jagd, Pilotin!", -1 },
};

}   // namespace

void App::startSchool()
{
    // A field of its own without a timeline: the lessons put on it what they need.
    LevelDef lv;
    const LevelDef *first = m_cat.level( "e1_01" );
    lv.m_name = "school";
    lv.m_title = "Flugschule";
    lv.m_titleEn = "Flight School";
    lv.m_episode = 1;
    lv.m_theme = first ? first->m_theme : "range";
    lv.m_scroll = first ? first->m_scroll : 60.0f;
    lv.m_seed = 11;
    lv.m_music = "plan";
    lv.m_length = 1e6f;
    m_cat.m_levels["school"] = lv;
    m_school = true;
    m_practice = true;
    m_practiceGear = 0;
    m_survival = m_daily = m_weekly = false;
    m_mode = ModeE::CAMPAIGN;
    m_players = 1;
    startMission( "school" );
    m_rec.clear();
    m_world.m_noTimeline = true;
    m_world.m_immortal = true;
    m_lesson = -1;
    m_lessonDone = 1e3f;   // straight on to the first lesson
    schoolStep( PlayerInput() );
}

void App::schoolStep( const PlayerInput &in )
{
    Player &p = m_world.m_players[0];
    auto    clearField = [&]() {
        for ( Enemy &e : m_world.m_enemies ) e.m_gone = true;
        for ( Bullet &b : m_world.m_bullets )
            if ( !b.m_player ) b.m_dead = true;
    };
    auto spawn = [&]( const char *name, Vector2 at ) {
        const EnemyDef *d = m_cat.enemy( name );
        if ( !d ) return;
        Line still;
        still.m_kv["path"] = "still";
        int i = m_world.spawn( d, at, &still );
        if ( i >= 0 ) m_world.m_enemies[(size_t)i].m_entered = true;
    };
    auto alive = [&]() {
        int n = 0;
        for ( const Enemy &e : m_world.m_enemies ) n += !e.m_dead && !e.m_gone && !e.m_def->m_scenery ? 1 : 0;
        return n;
    };
    m_lessonT += Dt;
    if ( m_lessonDone >= 0.0f )
    {
        // Done: a breath of praise, then the next lesson (after the last, the end).
        m_lessonDone += Dt;
        if ( m_lessonDone < 1.6f ) return;
        if ( m_lesson + 1 >= Lessons )
        {
            m_prof.m_school = true;
            m_feats.max( "school", 1 );
            checkFeats();
            m_world.m_finished = true;
            return;
        }
        ++m_lesson;
        m_lessonT = 0.0f;
        m_lessonDone = -1.0f;
        m_lessonMeter = 0.0f;
        m_lessonPos = p.m_pos;
        clearField();
        if ( m_lesson == 1 )
            for ( float fx : { 0.3f, 0.5f, 0.7f } ) spawn( "target", { FieldW * fx, fx == 0.5f ? 220.0f : 300.0f } );
        if ( m_lesson == 3 ) m_lessonMeter = (float)p.m_grazes;
        if ( m_lesson == 4 ) p.m_bestChain = 0;
        if ( m_lesson == 6 && p.m_special ) p.m_charges = std::max( p.m_charges, 1 );
        return;
    }
    bool done = false;
    switch ( m_lesson )
    {
    case 0:
        m_lessonMeter += std::sqrt( dist2( p.m_pos, m_lessonPos ) );
        m_lessonPos = p.m_pos;
        done = m_lessonMeter > 1600.0f;
        break;
    case 1: done = alive() == 0; break;
    case 2:
        m_lessonMeter += in.m_focus ? Dt : 0.0f;
        done = m_lessonMeter >= 2.0f;
        break;
    case 3:
    {
        // Slow bullets from above, aimed a little beside the pilot: they graze when the pilot holds still.
        if ( (int)( m_lessonT * 60.0f ) % 30 == 0 )
        {
            float  side = ( (int)( m_lessonT * 2.0f ) % 2 ) ? 1.0f : -1.0f;
            Bullet b;
            b.m_pos = b.m_origin = { p.m_pos.x + side * 30.0f, 40.0f };
            b.m_vel = { 0.0f, 260.0f };
            b.m_radius = 6.0f;
            b.m_damage = 1.0f;
            b.m_life = 6.0f;
            b.m_sprite = 0;
            b.m_color = { 255, 90, 200, 255 };
            m_world.m_bullets.push_back( b );
        }
        done = p.m_grazes - (int)m_lessonMeter >= 6;
        break;
    }
    case 4:
        // A row of drones; the row comes again until a chain of six stands.
        if ( alive() == 0 )
            for ( int i = 0; i < 8; ++i ) spawn( "drone", { FieldW * ( 0.15f + 0.1f * i ), 200.0f + 40.0f * ( i % 2 ) } );
        done = p.m_bestChain >= 6;
        break;
    case 5:
        for ( const SimEvent &e : m_world.m_events ) m_lessonMeter += e.m_type == EvE::MODE ? 1.0f : 0.0f;
        done = m_lessonMeter >= 2.0f;
        break;
    case 6:
        for ( const SimEvent &e : m_world.m_events ) done = done || e.m_type == EvE::NOVA;
        if ( p.m_special && p.m_charges <= 0 && !done ) p.m_charges = 1;
        break;
    case 7: done = m_lessonT > 7.0f; break;
    default: done = m_lessonT > 8.0f; break;
    }
    if ( done )
    {
        m_lessonDone = 0.0f;
        m_audio.play( SfxE::CUBE );
    }
}

void App::drawSchool()
{
    if ( !m_school || m_lesson < 0 || m_lesson >= Lessons ) return;
    float       k = ui::k();
    Rectangle   f = m_view.fieldRect();
    const Lesson &l = LessonList[m_lesson];
    std::string text = T( l.m_text );
    if ( l.m_act >= 0 )
    {
        char buf[512];
        snprintf( buf, sizeof( buf ), text.c_str(), m_in.label( (ActE)l.m_act, m_in.padUsed() ).c_str() );
        text = buf;
    }
    float     bw = f.width - 40 * k;
    auto      lines = ui::wrap( text, bw - 40 * k, 26 );
    Rectangle r = { f.x + 20 * k, f.y + 20 * k, bw, ( 70 + 32 * (float)lines.size() ) * k };
    DrawRectangleRec( r, { 4, 10, 20, 210 } );
    DrawRectangleLinesEx( r, 2 * k, m_lessonDone >= 0 ? ui::Good : Color{ 90, 200, 255, 200 } );
    ui::text( tf( "Lektion %d von %d", m_lesson + 1, Lessons ), r.x + 20 * k, r.y + 12 * k, 20, ui::Dim );
    ui::text( l.m_title, r.x + 230 * k, r.y + 8 * k, 28, m_lessonDone >= 0 ? ui::Good : WHITE );
    for ( size_t i = 0; i < lines.size(); ++i ) ui::text( lines[i], r.x + 20 * k, r.y + 50 * k + i * 32 * k, 26, ui::Ink );
    if ( m_lessonDone >= 0 ) ui::text( "Gut gemacht!", f.x + f.width * 0.5f, f.y + f.height * 0.45f, 52, ui::Good, ui::AlignE::CENTER );
    // What the lesson counts.
    const Player &p = m_world.m_players[0];
    std::string   meter;
    if ( m_lesson == 0 ) meter = tf( "%d %%", std::min( 100, (int)( m_lessonMeter / 16.0f ) ) );
    else if ( m_lesson == 2 ) meter = tf( "%.1f s", std::min( 2.0f, m_lessonMeter ) );
    else if ( m_lesson == 3 ) meter = tf( "%d von 6", std::min( 6, p.m_grazes - (int)m_lessonMeter ) );
    else if ( m_lesson == 4 ) meter = tf( "Kette %d von 6", std::min( 6, p.m_bestChain ) );
    else if ( m_lesson == 5 ) meter = tf( "%d von 2", std::min( 2, (int)m_lessonMeter ) );
    if ( !meter.empty() ) ui::text( meter, r.x + r.width - 20 * k, r.y + 12 * k, 22, ui::Gold, ui::AlignE::RIGHT );
}

// ---------------------------------------------------------------------------------------------------- the ghost

std::string App::ghostPath() const
{
    // The daily and weekly flights (and the challenges) by their seed; an arcade mission flown alone by the mission
    // and the difficulty.
    if ( m_replaying || !m_world.m_level ) return "";
    if ( m_survival && !m_challenge.empty() ) return userDir() + "ghosts/challenge_" + m_challenge + ".bin";
    if ( m_survival && ( m_daily || m_weekly ) )
        return userDir() + "ghosts/" + ( m_weekly ? "weekly_" + thisWeek() : "daily_" + today() ) + ".bin";
    if ( m_mode == ModeE::ARCADE && !m_survival && m_players == 1 )
        return userDir() + "ghosts/arcade_" + m_world.m_level->m_name + "_" + m_cat.difficulty( m_world.m_diffIndex ).m_name + ".bin";
    return "";
}

void App::loadGhost()
{
    // "ZLG1", the flight's score at its end, the number of points, the points.
    m_ghost.clear();
    m_ghostRec.clear();
    m_ghostStep = 0;
    m_ghostBest = 0;
    m_ghostBase = m_world.m_players[0].m_score;
    m_view.m_ghostOn = false;
    std::string path = ghostPath();
    m_ghostActive = !path.empty();
    if ( path.empty() ) return;
    FILE *f = fopen( path.c_str(), "rb" );
    if ( !f ) return;
    char     magic[4] = {};
    uint32_t n = 0;
    if ( fread( magic, 1, 4, f ) == 4 && std::equal( magic, magic + 4, "ZLG1" ) && fread( &m_ghostBest, 8, 1, f ) == 1 &&
         fread( &n, 4, 1, f ) == 1 && n < 4000000u )
    {
        m_ghost.resize( n );
        if ( fread( m_ghost.data(), sizeof( GhostPoint ), n, f ) != n ) m_ghost.clear();
    }
    fclose( f );
}

void App::saveGhost( long long best )
{
    std::string path = ghostPath();
    if ( path.empty() || m_ghostRec.empty() ) return;
    std::error_code e;
    fs::create_directories( fs::path( path ).parent_path(), e );
    // The older daily and weekly ghosts go: only today's and this week's are flown against.
    if ( m_survival && m_challenge.empty() )
    {
        std::string kind = m_weekly ? "weekly_" : "daily_";
        for ( const auto &entry : fs::directory_iterator( fs::path( path ).parent_path(), e ) )
            if ( entry.path().filename().string().rfind( kind, 0 ) == 0 && entry.path() != fs::path( path ) ) fs::remove( entry.path(), e );
    }
    FILE *f = fopen( path.c_str(), "wb" );
    if ( !f ) return;
    uint32_t  n = (uint32_t)m_ghostRec.size();
    fwrite( "ZLG1", 1, 4, f );
    fwrite( &best, 8, 1, f );
    fwrite( &n, 4, 1, f );
    fwrite( m_ghostRec.data(), sizeof( GhostPoint ), n, f );
    fclose( f );
}

void App::ghostStep()
{
    // Every second step a point of this flight; the best flight's point of the same moment on the field.
    if ( !m_ghostActive || m_replaying ) return;
    const Player &p = m_world.m_players[0];
    if ( ( m_ghostStep & 1 ) == 0 )
    {
        GhostPoint g;
        g.m_x = p.m_alive ? (int16_t)std::lround( p.m_pos.x ) : (int16_t)-1;
        g.m_y = (int16_t)std::lround( p.m_pos.y );
        g.m_score = (int32_t)std::min( p.m_score - m_ghostBase, (long long)INT32_MAX );
        m_ghostRec.push_back( g );
    }
    ++m_ghostStep;
    size_t i = (size_t)( m_ghostStep / 2 );
    m_view.m_ghostOn = i < m_ghost.size() && m_ghost[i].m_x >= 0;
    if ( m_view.m_ghostOn ) m_view.m_ghost = { (float)m_ghost[i].m_x, (float)m_ghost[i].m_y };
}

}   // namespace zenith
