/**
 * @file App.cpp
 * @brief The program's flow and its menus (the mission and the workshop are in AppPlay.cpp and AppShop.cpp).
 * @author Rene Weller
 */
#include "App.h"

#include "Crash.h"
#include "Files.h"
#include "Ui.h"

#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>

namespace zenith
{

namespace fs = std::filesystem;

namespace
{

bool steamDeck()
{
    const char *d = getenv( "SteamDeck" );
    return d && atoi( d ) == 1;
}

std::string pick( const std::string &given, const std::string &name )
{
    if ( !given.empty() ) return given;
    std::string beside = std::string( GetApplicationDirectory() ) + name;
    std::error_code e;
    if ( fs::is_directory( beside, e ) ) return beside;
    return std::string( ZENITH_ROOT ) + "/" + name;
}

const int FpsSteps[] = { 30, 40, 60, 90, 120, 144, 0 };

}   // namespace

bool App::init( const Options &o )
{
    m_opt = o;
    std::string data = pick( o.m_data, "data" );
    m_assets = pick( o.m_assets, "assets" );
    m_cat.load( data );
    for ( const std::string &e : m_cat.m_errors ) TraceLog( LOG_WARNING, "DATA: %s", e.c_str() );
    m_set.load( userDir() + "settings.txt" );
    m_prof.load( userDir() + "profile.txt" );
    for ( const std::string &b : m_set.m_binds ) m_in.load( b );
    if ( m_prof.m_maxDiff < 0 ) m_prof.m_maxDiff = 0;
    int visible = 0;
    for ( const DifficultyDef &d : m_cat.m_difficulties )
        if ( !d.m_hidden ) ++visible;
    m_prof.m_maxDiff = std::max( m_prof.m_maxDiff, visible - 1 );
    bool deck = steamDeck();
    int  w = o.m_width > 0 ? o.m_width : m_set.m_width > 0 ? m_set.m_width : deck ? 1280 : 1600;
    int  h = o.m_height > 0 ? o.m_height : m_set.m_height > 0 ? m_set.m_height : deck ? 800 : 900;
    unsigned flags = FLAG_WINDOW_RESIZABLE;
    if ( m_set.m_vsync ) flags |= FLAG_VSYNC_HINT;
    if ( !o.m_shot.empty() || !o.m_gallery.empty() || !o.m_shop.empty() ) flags = FLAG_WINDOW_HIDDEN;
    SetConfigFlags( flags );
    SetTraceLogLevel( LOG_WARNING );
    InitWindow( w, h, "Zenith Lancer" );
    if ( !IsWindowReady() ) return false;
    SetExitKey( 0 );
    SetWindowMinSize( 640, 400 );
    bool full = o.m_fullscreen >= 0 ? o.m_fullscreen == 1 : ( m_set.m_fullscreen || ( deck && m_set.m_width == 0 ) );
    if ( full && o.m_shot.empty() ) ToggleBorderlessWindowed();
    SetTargetFPS( o.m_shot.empty() ? m_set.m_fps : 0 );
    if ( !o.m_mute ) InitAudioDevice();
    m_audio.init( !o.m_mute, m_assets );
    m_audio.m_musicVolume = m_set.m_music;
    m_audio.m_effectVolume = m_set.m_effects;
    ui::init( m_assets );
    ui::setTextSize( m_set.m_textSize * ( deck ? 1.12f : 1.0f ) );
    m_view.init( GetScreenWidth(), GetScreenHeight() );
    m_preview.init( 600, 660 );
    applySettings();
    crash::install( userDir() + "crashes/", "" );
    m_diff = o.m_difficulty;
    newDemo();
    if ( !o.m_level.empty() && m_cat.level( o.m_level ) )
    {
        m_mode = o.m_mode == "arcade" ? ModeE::ARCADE : ModeE::CAMPAIGN;
        const LevelDef *lv = m_cat.level( o.m_level );
        m_episode = lv->m_episode;
        auto ep = m_cat.m_episodes.find( m_episode );
        m_camp = Campaign();
        m_camp.m_difficulty = m_diff;
        m_camp.m_episode = m_episode;
        if ( ep != m_cat.m_episodes.end() ) m_camp.m_load = ep->second.m_loadout;
        if ( m_mode == ModeE::ARCADE && !m_cat.m_arcadeShips.empty() ) m_carry[0] = m_cat.m_arcadeShips[0].m_loadout;
        parseLoadout( m_camp.m_load, o.m_loadout );
        parseLoadout( m_carry[0], o.m_loadout );
        m_lives[0] = 3;
        startMission( o.m_level );
    }
    return true;
}

void App::shutdown()
{
    saveSettings();
    m_prof.save( userDir() + "profile.txt" );
    m_view.shutdown();
    m_preview.shutdown();
    ui::shutdown();
    m_audio.shutdown();
    if ( IsAudioDeviceReady() ) CloseAudioDevice();
    CloseWindow();
}

void App::applySettings()
{
    m_audio.m_musicVolume = m_set.m_music;
    m_audio.m_effectVolume = m_set.m_effects;
    m_view.m_shakeAmount = m_set.m_shake;
    m_view.m_showHitbox = m_set.m_hitbox;
    SetTargetFPS( m_set.m_fps );
    ui::setTextSize( m_set.m_textSize * ( steamDeck() ? 1.12f : 1.0f ) );
}

void App::saveSettings()
{
    m_set.m_binds = m_in.save();
    m_set.save( userDir() + "settings.txt" );
}

void App::resize()
{
    m_view.shutdown();
    m_view.init( GetScreenWidth(), GetScreenHeight() );
    if ( m_screen == ScreenE::PLAY && m_world.m_level )
        m_view.setTheme( m_cat, m_world.m_level->m_theme, m_world.m_level->m_seed );
    else if ( m_demoReady && m_demo.m_level )
        m_view.setTheme( m_cat, m_demo.m_level->m_theme, m_demo.m_level->m_seed );
}

void App::run()
{
    if ( !m_opt.m_shot.empty() )
    {
        // A picture of the mission after some seconds (the autopilot flies), then quit.
        int steps = (int)( m_opt.m_at / Dt );
        for ( int i = 0; i < steps && !m_world.finished(); ++i )
        {
            PlayerInput in[2] = { autopilot( m_world, 0 ), autopilot( m_world, 1 ) };
            m_world.step( in );
            m_view.events( m_world );
            if ( i % 2 == 0 ) m_view.update( m_world, Dt * 2 );
        }
        m_introT = 10;
        m_screen = ScreenE::PLAY;
        // Drawn into a picture of its own (a hidden window's screen cannot be read back reliably).
        RenderTexture2D rt = LoadRenderTexture( GetScreenWidth(), GetScreenHeight() );
        m_view.update( m_world, Dt );
        m_view.draw( m_world, m_cat );
        BeginDrawing();
        BeginTextureMode( rt );
        ClearBackground( BLACK );
        m_view.present( { 0, 0, (float)GetScreenWidth(), (float)GetScreenHeight() } );
        hud();
        EndTextureMode();
        EndDrawing();
        Image img = LoadImageFromTexture( rt.texture );
        ImageFlipVertical( &img );
        ImageFormat( &img, PIXELFORMAT_UNCOMPRESSED_R8G8B8 );
        ExportImage( img, m_opt.m_shot.c_str() );
        UnloadImage( img );
        UnloadRenderTexture( rt );
        return;
    }
    while ( m_running && !WindowShouldClose() )
    {
        float dt = std::min( GetFrameTime(), 0.1f );
        m_in.update();
        m_audio.update( dt );
        if ( IsWindowResized() ) resize();
        if ( IsKeyPressed( KEY_F11 ) || ( IsKeyPressed( KEY_ENTER ) && IsKeyDown( KEY_LEFT_ALT ) ) )
        {
            ToggleBorderlessWindowed();
            m_set.m_fullscreen = !m_set.m_fullscreen;
            resize();
        }
        if ( IsKeyPressed( KEY_F12 ) )
        {
            std::string dir = userDir() + "shots/";
            fs::create_directories( dir );
            TakeScreenshot( ( dir + "zenith_" + std::to_string( (long long)( GetTime() * 1000 ) ) + ".png" ).c_str() );
        }
        BeginDrawing();
        ClearBackground( BLACK );
        frame( dt );
        EndDrawing();
        m_t += dt;
        m_prof.m_seconds += dt;
    }
}

void App::go( ScreenE s )
{
    m_screen = s;
    m_sel = 0;
    m_sel2 = 0;
    m_t = 0;
    m_storyT = 0;
}

void App::frame( float dt )
{
    switch ( m_screen )
    {
    case ScreenE::TITLE: title( dt ); break;
    case ScreenE::MAIN: mainMenu( dt ); break;
    case ScreenE::OPTIONS: options( dt ); break;
    case ScreenE::CONTROLS: controls( dt ); break;
    case ScreenE::SCORES: scores( dt ); break;
    case ScreenE::CREDITS: credits( dt ); break;
    case ScreenE::LOGBOOK: logbook( dt ); break;
    case ScreenE::SLOTS: slots( dt ); break;
    case ScreenE::DIFFICULTY: difficulty( dt ); break;
    case ScreenE::EPISODE: episode( dt ); break;
    case ScreenE::STORY: story( dt ); break;
    case ScreenE::PLAN: plan( dt ); break;
    case ScreenE::SHOP: shop( dt ); break;
    case ScreenE::PLAY: play( dt ); break;
    case ScreenE::RESULTS: results( dt ); break;
    case ScreenE::FAILED: failed( dt ); break;
    case ScreenE::PILOTS: pilots( dt ); break;
    case ScreenE::CONTINUE: continueScreen( dt ); break;
    case ScreenE::NAME: nameEntry( dt ); break;
    case ScreenE::QUIT: m_running = false; break;
    }
}

// ---------------------------------------------------------------------------------------------------- backdrop

void App::newDemo()
{
    // A mission of the arcade runs, flown by the autopilot behind the menus.
    std::vector<const LevelDef *> pool;
    for ( const auto &[n, lv] : m_cat.m_levels )
        if ( lv.m_arcade > 0 && lv.m_episode <= std::max( 1, m_prof.m_episodes ) ) pool.push_back( &lv );
    if ( pool.empty() )
        for ( const auto &[n, lv] : m_cat.m_levels ) pool.push_back( &lv );
    if ( pool.empty() || m_cat.m_arcadeShips.empty() )
    {
        m_demoReady = false;
        return;
    }
    static Rng r;
    r.seed( (uint32_t)( GetTime() * 1000 ) + 17 );
    const LevelDef *lv = pool[(size_t)r.below( (int)pool.size() )];
    m_demo.begin( &m_cat, lv, ModeE::ARCADE, 2, r.next() );
    m_demo.m_immortal = true;
    Loadout o = m_cat.m_arcadeShips[(size_t)r.below( (int)m_cat.m_arcadeShips.size() )].m_loadout;
    o.m_frontLv = 6 + r.below( 5 );
    m_demo.addPlayer( 0, o, 0 );
    // Start a little in, where something happens.
    for ( int i = 0; i < 60 * 8; ++i )
    {
        PlayerInput in[2] = { autopilot( m_demo, 0 ), {} };
        m_demo.step( in );
    }
    m_view.setTheme( m_cat, lv->m_theme, lv->m_seed );
    m_view.reset();
    m_demoReady = true;
}

void App::stepDemo()
{
    if ( !m_demoReady ) return;
    PlayerInput in[2] = { autopilot( m_demo, 0 ), {} };
    m_demo.step( in );
    m_view.events( m_demo );
    if ( m_demo.finished() || m_demo.failed() || m_demo.m_clock > 200.0f ) newDemo();
}

void App::drawBackdrop( float dt, bool dim )
{
    if ( m_back == ScreenE::PLAY && ( m_screen == ScreenE::OPTIONS || m_screen == ScreenE::CONTROLS ) )
    {
        // Paused in a mission: the field stands still behind the options.
        m_view.update( m_world, 0.0f );
        m_view.draw( m_world, m_cat );
        m_view.present( { 0, 0, (float)GetScreenWidth(), (float)GetScreenHeight() } );
    }
    else if ( m_demoReady )
    {
        static float acc = 0;
        acc += dt;
        int n = 0;
        while ( acc >= Dt && n++ < 4 )
        {
            stepDemo();
            acc -= Dt;
        }
        if ( acc > Dt ) acc = 0;
        m_view.update( m_demo, dt );
        m_view.draw( m_demo, m_cat );
        m_view.present( { 0, 0, (float)GetScreenWidth(), (float)GetScreenHeight() } );
    }
    else
        DrawRectangleGradientV( 0, 0, GetScreenWidth(), GetScreenHeight(), { 6, 10, 24, 255 }, { 2, 2, 6, 255 } );
    if ( dim ) DrawRectangle( 0, 0, GetScreenWidth(), GetScreenHeight(), { 0, 0, 0, 150 } );
}

// ---------------------------------------------------------------------------------------------------- helpers

int App::menu( const std::vector<std::string> &items, float x, float y, float w, int &sel, float size,
               const std::vector<bool> *enabled, bool center )
{
    int n = (int)items.size();
    if ( n == 0 ) return -1;
    auto ok = [&]( int i ) { return !enabled || ( i < (int)enabled->size() && ( *enabled )[(size_t)i] ); };
    sel = std::clamp( sel, 0, n - 1 );
    if ( m_in.up() )
    {
        for ( int k = 0; k < n; ++k )
        {
            sel = ( sel + n - 1 ) % n;
            if ( ok( sel ) ) break;
        }
        m_audio.play( SfxE::MOVE );
    }
    if ( m_in.down() )
    {
        for ( int k = 0; k < n; ++k )
        {
            sel = ( sel + 1 ) % n;
            if ( ok( sel ) ) break;
        }
        m_audio.play( SfxE::MOVE );
    }
    float rowH = ui::height( size ) * 1.45f;
    int   chosen = -1;
    Vector2 mp = GetMousePosition();
    for ( int i = 0; i < n; ++i )
    {
        Rectangle r = { x - 16 * ui::k(), y + i * rowH - rowH * 0.12f, w, rowH * 0.95f };
        if ( center ) r.x = x - w * 0.5f;
        if ( CheckCollisionPointRec( mp, r ) && ok( i ) && m_in.mouseMoved() ) sel = i;
        if ( CheckCollisionPointRec( mp, r ) && ok( i ) && IsMouseButtonPressed( MOUSE_BUTTON_LEFT ) )
        {
            sel = i;
            chosen = i;
        }
        if ( i == sel ) ui::highlight( r, m_t );
        Color c = !ok( i ) ? Color{ 80, 90, 105, 255 } : i == sel ? WHITE : ui::Ink;
        ui::text( items[(size_t)i], center ? x : x, y + i * rowH, size, c, center ? ui::AlignE::CENTER : ui::AlignE::LEFT );
    }
    if ( m_in.accept() && ok( sel ) ) chosen = sel;
    if ( chosen >= 0 ) m_audio.play( SfxE::OK );
    return chosen;
}

void App::heading( const std::string &title, const std::string &sub )
{
    float k = ui::k();
    ui::text( title, GetScreenWidth() * 0.5f, 60 * k, 64, WHITE, ui::AlignE::CENTER );
    if ( !sub.empty() ) ui::text( sub, GetScreenWidth() * 0.5f, 140 * k, 28, ui::Accent, ui::AlignE::CENTER );
}

void App::hints( const std::string &t )
{
    float k = ui::k();
    DrawRectangle( 0, GetScreenHeight() - (int)( 54 * k ), GetScreenWidth(), (int)( 54 * k ), { 0, 0, 0, 160 } );
    ui::text( t, GetScreenWidth() * 0.5f, GetScreenHeight() - 44 * k, 24, ui::Dim, ui::AlignE::CENTER );
}

std::string App::fmt( long long v ) const
{
    std::string s = std::to_string( v < 0 ? -v : v ), out;
    for ( size_t i = 0; i < s.size(); ++i )
    {
        if ( i > 0 && ( s.size() - i ) % 3 == 0 ) out += '.';
        out += s[i];
    }
    return ( v < 0 ? "-" : "" ) + out;
}

std::string App::diffKey() const
{
    const char *m = m_mode == ModeE::ARCADE ? ( m_players > 1 ? "arcade2" : "arcade" ) : m_mode == ModeE::BOSSRUSH ? "bossrush" : "campaign";
    return std::string( m ) + "_" + m_cat.difficulty( m_diff ).m_name;
}

void App::queueStory( const std::string &key )
{
    if ( !key.empty() && m_cat.story( key ) ) m_stories.push_back( key );
}

void App::showStories( ScreenE after )
{
    m_after = after;
    if ( m_stories.empty() )
    {
        if ( after == ScreenE::PLAY && !m_level.empty() )
            startMission( m_level );
        else
            go( after );
        return;
    }
    go( ScreenE::STORY );
}

bool App::available( const std::string &name, int tier, bool hidden ) const
{
    if ( m_camp.m_owned.count( name ) || m_camp.m_plans.count( name ) ) return true;
    return !hidden && tier <= m_camp.m_episode;
}

void App::saveCampaign()
{
    if ( m_mode != ModeE::CAMPAIGN ) return;
    m_camp.save( slotPath( m_slot ) );
    m_prof.save( userDir() + "profile.txt" );
}

// ---------------------------------------------------------------------------------------------------- screens

void App::title( float dt )
{
    drawBackdrop( dt, false );
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    DrawRectangleGradientV( 0, (int)( H * 0.18f ), (int)W, (int)( H * 0.3f ), { 0, 0, 0, 0 }, { 0, 0, 0, 170 } );
    DrawRectangle( 0, (int)( H * 0.48f ), (int)W, (int)( H * 0.12f ), { 0, 0, 0, 170 } );
    DrawRectangleGradientV( 0, (int)( H * 0.6f ), (int)W, (int)( H * 0.1f ), { 0, 0, 0, 170 }, { 0, 0, 0, 0 } );
    float pulse = 0.5f + 0.5f * std::sin( m_t * 2.0f );
    ui::text( "ZENITH LANCER", W * 0.5f + 3 * k, H * 0.36f + 3 * k, 150, { 0, 40, 80, 200 }, ui::AlignE::CENTER );
    ui::text( "ZENITH LANCER", W * 0.5f, H * 0.36f, 150, { 235, 245, 255, 255 }, ui::AlignE::CENTER );
    ui::text( "Die Lanze des Zenits", W * 0.5f, H * 0.36f + 150 * k, 34, ui::Accent, ui::AlignE::CENTER );
    Color c = { 255, 220, 140, (unsigned char)( 120 + 135 * pulse ) };
    ui::text( m_in.padUsed() ? "Drücke A oder Start" : "Drücke Eingabe", W * 0.5f, H * 0.52f, 40, c, ui::AlignE::CENTER );
    ui::text( "v" ZENITH_VERSION, W - 20 * k, H - 40 * k, 22, ui::Dim, ui::AlignE::RIGHT );
    if ( m_in.accept() || m_in.pause() || IsMouseButtonPressed( MOUSE_BUTTON_LEFT ) )
    {
        m_audio.play( SfxE::OK );
        go( ScreenE::MAIN );
    }
}

void App::mainMenu( float dt )
{
    drawBackdrop( dt, true );
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    ui::text( "ZENITH LANCER", W * 0.5f, H * 0.1f, 96, WHITE, ui::AlignE::CENTER );
    std::vector<std::string> items = { "Kampagne", "Arcade", "Arcade für zwei", "Bossjagd", "Logbuch", "Bestenliste",
                                       "Optionen", "Mitwirkende", "Beenden" };
    std::vector<bool>        en = { true, true, true, m_prof.m_bossRush, true, true, true, true, true };
    float pw = 520 * k, ph = items.size() * ui::height( 40 ) * 1.45f + 60 * k;
    ui::panel( { W * 0.5f - pw * 0.5f, H * 0.3f - 30 * k, pw, ph } );
    int c = menu( items, W * 0.5f, H * 0.3f, pw - 40 * k, m_sel, 40, &en, true );
    hints( "Auswählen: Pfeile / Steuerkreuz    Bestätigen: Eingabe / A    Zurück: Esc / B" );
    if ( m_in.back() ) go( ScreenE::TITLE );
    switch ( c )
    {
    case 0:
        m_mode = ModeE::CAMPAIGN;
        m_players = 1;
        go( ScreenE::SLOTS );
        break;
    case 1:
    case 2:
        m_mode = ModeE::ARCADE;
        m_players = c == 1 ? 1 : 2;
        m_back = ScreenE::MAIN;
        go( ScreenE::DIFFICULTY );
        break;
    case 3:
        m_mode = ModeE::BOSSRUSH;
        m_players = 1;
        go( ScreenE::DIFFICULTY );
        break;
    case 4:
        m_logList.assign( m_prof.m_cubes.begin(), m_prof.m_cubes.end() );
        go( ScreenE::LOGBOOK );
        break;
    case 5: go( ScreenE::SCORES ); break;
    case 6:
        m_back = ScreenE::MAIN;
        go( ScreenE::OPTIONS );
        break;
    case 7: go( ScreenE::CREDITS ); break;
    case 8: go( ScreenE::QUIT ); break;
    default: break;
    }
}

void App::slots( float dt )
{
    drawBackdrop( dt, true );
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    heading( "Kampagne", "Wähle einen Speicherplatz" );
    std::vector<std::string> items;
    std::vector<Campaign>    saved( 3 );
    std::vector<bool>        filled( 3 );
    for ( int s = 0; s < 3; ++s )
    {
        filled[(size_t)s] = fs::exists( slotPath( s ) );
        if ( filled[(size_t)s] )
        {
            saved[(size_t)s].load( slotPath( s ) );
            const Campaign &c = saved[(size_t)s];
            auto            ep = m_cat.m_episodes.find( c.m_episode );
            items.push_back( "Platz " + std::to_string( s + 1 ) + ":  Episode " + std::to_string( c.m_episode ) + " " +
                             ( ep != m_cat.m_episodes.end() ? ep->second.m_title : "" ) + "  ·  " +
                             m_cat.difficulty( c.m_difficulty ).m_title + "  ·  " + fmt( c.m_credits ) + " Kr  ·  " + c.m_stamp );
        }
        else
            items.push_back( "Platz " + std::to_string( s + 1 ) + ":  leer" );
    }
    float pw = 1300 * k;
    ui::panel( { W * 0.5f - pw * 0.5f, H * 0.3f - 30 * k, pw, 330 * k } );
    if ( m_sel2 == 0 )
    {
        int c = menu( items, W * 0.5f - pw * 0.5f + 50 * k, H * 0.3f, pw - 80 * k, m_sel, 34 );
        if ( c >= 0 )
        {
            m_slot = c;
            if ( filled[(size_t)c] )
                m_sel2 = 1;
            else
            {
                m_back = ScreenE::SLOTS;
                go( ScreenE::DIFFICULTY );
            }
        }
        if ( m_in.back() ) go( ScreenE::MAIN );
    }
    else
    {
        static int sub = 0;
        ui::text( "Platz " + std::to_string( m_slot + 1 ), W * 0.5f, H * 0.62f, 34, ui::Gold, ui::AlignE::CENTER );
        int c = menu( { "Fortsetzen", "Neu beginnen (überschreibt)", "Zurück" }, W * 0.5f, H * 0.68f, 600 * k, sub, 34, nullptr, true );
        if ( c == 0 )
        {
            m_camp.load( slotPath( m_slot ) );
            m_diff = m_camp.m_difficulty;
            m_episode = m_camp.m_episode;
            go( ScreenE::PLAN );
        }
        else if ( c == 1 )
        {
            m_back = ScreenE::SLOTS;
            go( ScreenE::DIFFICULTY );
        }
        else if ( c == 2 || m_in.back() )
            m_sel2 = 0;
    }
    hints( "Eingabe / A: wählen    Esc / B: zurück" );
}

void App::difficulty( float dt )
{
    drawBackdrop( dt, true );
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    heading( "Schwierigkeit", m_mode == ModeE::ARCADE ? ( m_players > 1 ? "Arcade für zwei" : "Arcade" ) : m_mode == ModeE::BOSSRUSH ? "Bossjagd" : "Kampagne" );
    std::vector<std::string> items;
    std::vector<bool>        en;
    for ( size_t i = 0; i < m_cat.m_difficulties.size(); ++i )
    {
        const DifficultyDef &d = m_cat.m_difficulties[i];
        bool open = (int)i <= m_prof.m_maxDiff;
        items.push_back( open || !d.m_hidden ? d.m_title : "???" );
        en.push_back( open );
    }
    if ( m_t < 0.01f ) m_sel = std::min( 2, (int)items.size() - 1 );
    float pw = 1100 * k;
    ui::panel( { W * 0.5f - pw * 0.5f, H * 0.25f - 30 * k, pw, items.size() * ui::height( 38 ) * 1.45f + 200 * k } );
    int c = menu( items, W * 0.5f - pw * 0.5f + 60 * k, H * 0.25f, 400 * k, m_sel, 38, &en );
    const DifficultyDef &d = m_cat.difficulty( m_sel );
    std::vector<std::string> lines = ui::wrap( en[(size_t)m_sel] ? d.m_desc : "Noch verschlossen.", pw * 0.5f, 26 );
    for ( size_t i = 0; i < lines.size(); ++i ) ui::text( lines[i], W * 0.5f - pw * 0.5f + 520 * k, H * 0.25f + i * 36 * k, 26, ui::Ink );
    char buf[160];
    snprintf( buf, sizeof( buf ), "Gegner %d %%   Feuer %d %%   Treffer %d %%   Kredite %d %%", (int)( d.m_hp * 100 ), (int)( d.m_fire * 100 ),
              (int)( d.m_damage * 100 ), (int)( d.m_credits * 100 ) );
    ui::text( buf, W * 0.5f - pw * 0.5f + 520 * k, H * 0.25f + 6 * 36 * k, 22, ui::Dim );
    hints( "Eingabe / A: wählen    Esc / B: zurück" );
    if ( m_in.back() ) go( m_mode == ModeE::CAMPAIGN ? ScreenE::SLOTS : ScreenE::MAIN );
    if ( c >= 0 )
    {
        m_diff = c;
        if ( m_mode == ModeE::CAMPAIGN )
            go( ScreenE::EPISODE );
        else if ( m_mode == ModeE::BOSSRUSH )
        {
            m_pilotStep = 0;
            go( ScreenE::PILOTS );
        }
        else
        {
            m_pilotStep = 0;
            go( ScreenE::PILOTS );
        }
    }
}

void App::episode( float dt )
{
    drawBackdrop( dt, true );
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    heading( "Episode", m_mode == ModeE::CAMPAIGN ? "Wo beginnt deine Geschichte?" : "Wo beginnt der Lauf?" );
    std::vector<std::string> items;
    std::vector<bool>        en;
    std::vector<int>         ids;
    int open = m_mode == ModeE::CAMPAIGN ? m_prof.m_episodes : m_prof.m_arcadeEpisodes;
    for ( const auto &[id, ep] : m_cat.m_episodes )
    {
        ids.push_back( id );
        items.push_back( std::to_string( id ) + "  " + ( id <= open ? ep.m_title : "???" ) );
        en.push_back( id <= open );
    }
    float pw = 1100 * k;
    ui::panel( { W * 0.5f - pw * 0.5f, H * 0.25f - 30 * k, pw, 480 * k } );
    int c = menu( items, W * 0.5f - pw * 0.5f + 60 * k, H * 0.25f, 460 * k, m_sel, 40, &en );
    if ( !ids.empty() && en[(size_t)m_sel] )
    {
        const EpisodeDef &ep = m_cat.m_episodes.at( ids[(size_t)m_sel] );
        std::vector<std::string> lines = ui::wrap( ep.m_subtitle, pw * 0.42f, 26 );
        for ( size_t i = 0; i < lines.size(); ++i ) ui::text( lines[i], W * 0.5f - pw * 0.5f + 600 * k, H * 0.25f + i * 36 * k, 26, ui::Ink );
        if ( m_mode == ModeE::CAMPAIGN )
            ui::text( "Startkapital: " + fmt( ep.m_credits ) + " Kr", W * 0.5f - pw * 0.5f + 600 * k, H * 0.25f + 330 * k, 24, ui::Gold );
    }
    hints( "Eingabe / A: wählen    Esc / B: zurück" );
    if ( m_in.back() ) go( ScreenE::DIFFICULTY );
    if ( c >= 0 )
    {
        m_episode = ids[(size_t)c];
        if ( m_mode == ModeE::CAMPAIGN )
        {
            m_camp = Campaign();
            m_camp.m_difficulty = m_diff;
            openEpisode( m_episode, true );
        }
        else
        {
            m_run = m_cat.m_episodes.at( m_episode ).m_arcade;
            m_runIdx = 0;
            m_score[0] = m_score[1] = 0;
            m_continues = 0;
            for ( int i = 0; i < m_players; ++i )
            {
                m_carry[i] = m_cat.m_arcadeShips[(size_t)m_pilot[i]].m_loadout;
                m_lives[i] = 3;
            }
            if ( !m_run.empty() ) startMission( m_run[0] );
        }
    }
}

void App::openEpisode( int ep, bool fresh )
{
    auto it = m_cat.m_episodes.find( ep );
    if ( it == m_cat.m_episodes.end() ) return;
    const EpisodeDef &e = it->second;
    m_camp.m_episode = ep;
    m_episode = ep;
    if ( fresh )
    {
        m_camp.m_credits = e.m_credits;
        m_camp.m_load = e.m_loadout;
        m_camp.m_owned.insert( { e.m_loadout.m_ship, e.m_loadout.m_front, e.m_loadout.m_gen, e.m_loadout.m_shield } );
    }
    m_camp.m_choices = { e.m_first };
    m_stories.clear();
    queueStory( e.m_intro );
    saveCampaign();
    showStories( ScreenE::PLAN );
}

void App::story( float dt )
{
    drawBackdrop( dt, true );
    if ( m_stories.empty() )
    {
        showStories( m_after );
        return;
    }
    const StoryDef *s = m_cat.story( m_stories.front() );
    if ( !s )
    {
        m_stories.pop_front();
        return;
    }
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    m_storyT += dt;
    float pw = std::min( W - 80 * k, 1300 * k ), ph = H * 0.74f;
    Rectangle r = { W * 0.5f - pw * 0.5f, H * 0.1f, pw, ph };
    ui::panel( r, 0.86f, s->m_type == "cube" ? Color{ 90, 255, 230, 255 } : Color{ 90, 170, 230, 255 } );
    const char *kind = s->m_type == "cube" ? "LOGKRISTALL" : s->m_type == "briefing" ? "EINSATZBESPRECHUNG"
                     : s->m_type == "ending" ? "" : s->m_type == "intro" ? "" : s->m_type == "radio" ? "FUNK" : "";
    ui::text( kind, r.x + 40 * k, r.y + 26 * k, 22, s->m_type == "cube" ? Color{ 90, 255, 230, 255 } : ui::Accent );
    ui::text( s->m_title, r.x + 40 * k, r.y + 56 * k, 44, WHITE );
    if ( !s->m_from.empty() ) ui::text( s->m_from, r.x + pw - 40 * k, r.y + 66 * k, 26, ui::Gold, ui::AlignE::RIGHT );
    std::string all;
    for ( const std::string &l : s->m_text ) all += l.empty() ? "\n\n" : ( all.empty() || all.back() == '\n' ? "" : " " ) + l;
    // Typed out: 90 characters a second (accept shows all at once).
    size_t shown = (size_t)( m_storyT * 90.0f );
    bool   done = shown >= all.size();
    std::vector<std::string> lines = ui::wrap( done ? all : all.substr( 0, shown ), pw - 80 * k, 28 );
    float y = r.y + 130 * k, lh = ui::height( 28 ) * 1.35f;
    size_t maxLines = (size_t)( ( ph - 200 * k ) / lh );
    size_t first = lines.size() > maxLines ? lines.size() - maxLines : 0;
    for ( size_t i = first; i < lines.size(); ++i ) ui::text( lines[i], r.x + 40 * k, y + ( i - first ) * lh, 28, ui::Ink );
    if ( !done && (int)( m_storyT * 30 ) % 3 == 0 ) m_audio.play( SfxE::BLIP, -1, 0.4f );
    ui::text( std::to_string( m_stories.size() > 1 ? m_stories.size() - 1 : 0 ) + ( m_stories.size() > 2 ? " weitere" : m_stories.size() == 2 ? " weiterer" : "" ),
              r.x + pw - 40 * k, r.y + ph - 50 * k, 22, m_stories.size() > 1 ? ui::Dim : BLANK, ui::AlignE::RIGHT );
    hints( done ? "Eingabe / A: weiter    Esc / B: alle überspringen" : "Eingabe / A: ganz zeigen    Esc / B: alle überspringen" );
    if ( m_in.accept() || IsMouseButtonPressed( MOUSE_BUTTON_LEFT ) )
    {
        if ( !done )
            m_storyT = 1e6f;
        else
        {
            m_stories.pop_front();
            m_storyT = 0;
            m_audio.play( SfxE::OK );
        }
    }
    if ( m_in.back() )
    {
        m_stories.clear();
        m_audio.play( SfxE::BACK );
    }
}

void App::plan( float dt )
{
    drawBackdrop( dt, true );
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    auto  ep = m_cat.m_episodes.find( m_camp.m_episode );
    std::string epTitle = ep != m_cat.m_episodes.end() ? ep->second.m_title : "";
    heading( "Einsatzplanung", "Episode " + std::to_string( m_camp.m_episode ) + ": " + epTitle + "   ·   " +
                                   m_cat.difficulty( m_camp.m_difficulty ).m_title );
    // The map: the episode's missions as points, flown ones bright, open ones pulsing.
    Rectangle map = { W * 0.06f, H * 0.22f, W * 0.48f, H * 0.62f };
    ui::panel( map, 0.7f );
    for ( const auto &[n, lv] : m_cat.m_levels )
    {
        if ( lv.m_episode != m_camp.m_episode ) continue;
        bool    known = m_camp.m_done.count( n ) || std::find( m_camp.m_choices.begin(), m_camp.m_choices.end(), n ) != m_camp.m_choices.end();
        Vector2 a = { map.x + map.width * ( 0.06f + 0.88f * lv.m_mapX ), map.y + map.height * ( 0.08f + 0.84f * lv.m_mapY ) };
        if ( m_camp.m_done.count( n ) )
            for ( const std::string &nx : lv.m_next )
                if ( const LevelDef *b = m_cat.level( nx ) )
                {
                    bool    kb = m_camp.m_done.count( nx ) || std::find( m_camp.m_choices.begin(), m_camp.m_choices.end(), nx ) != m_camp.m_choices.end();
                    Vector2 bp = { map.x + map.width * ( 0.06f + 0.88f * b->m_mapX ), map.y + map.height * ( 0.08f + 0.84f * b->m_mapY ) };
                    if ( kb ) DrawLineEx( a, bp, 2 * k, { 90, 170, 230, 160 } );
                }
        if ( !known ) continue;
        bool  open = !m_camp.m_done.count( n );
        float r = ( open ? 10 + 3 * std::sin( m_t * 5 ) : 8 ) * k;
        DrawCircleV( a, r, open ? Color{ 255, 210, 100, 255 } : Color{ 110, 200, 255, 255 } );
        if ( lv.m_isSecret ) DrawCircleLinesV( a, r + 5 * k, ui::Good );
        ui::text( lv.m_title, a.x + 14 * k, a.y - 12 * k, 20, open ? ui::Gold : ui::Dim );
    }
    // The choices.
    std::vector<std::string> items;
    for ( const std::string &c : m_camp.m_choices )
    {
        const LevelDef *lv = m_cat.level( c );
        items.push_back( std::string( "Einsatz: " ) + ( lv ? lv->m_title : c ) + ( lv && lv->m_isSecret ? "  (geheim)" : "" ) );
    }
    size_t nMissions = items.size();
    items.push_back( "Werkstatt" );
    items.push_back( "Logbuch" );
    items.push_back( "Optionen" );
    items.push_back( "Speichern und Hauptmenü" );
    Rectangle side = { W * 0.57f, H * 0.22f, W * 0.37f, H * 0.62f };
    ui::panel( side, 0.8f );
    ui::text( "Kredite: " + fmt( m_camp.m_credits ), side.x + 30 * k, side.y + 24 * k, 32, ui::Gold );
    ui::text( "Einsätze: " + std::to_string( m_camp.m_missions ) + "    Logkristalle: " + std::to_string( m_camp.m_cubes.size() ),
              side.x + 30 * k, side.y + 66 * k, 22, ui::Dim );
    int c = menu( items, side.x + 40 * k, side.y + 120 * k, side.width - 60 * k, m_sel, 30 );
    if ( m_sel < (int)nMissions )
    {
        const LevelDef *lv = m_cat.level( m_camp.m_choices[(size_t)m_sel] );
        if ( lv )
        {
            const StoryDef *b = m_cat.story( lv->m_briefing );
            std::string     text = b && !b->m_text.empty() ? b->m_text[0] : "";
            std::vector<std::string> lines = ui::wrap( text, side.width - 60 * k, 22 );
            for ( size_t i = 0; i < lines.size() && i < 6; ++i )
                ui::text( lines[i], side.x + 30 * k, side.y + side.height - 200 * k + i * 30 * k, 22, ui::Ink );
        }
    }
    hints( "Eingabe / A: wählen    Esc / B: Hauptmenü" );
    if ( m_in.back() )
    {
        saveCampaign();
        newDemo();
        go( ScreenE::MAIN );
    }
    if ( c < 0 ) return;
    if ( c < (int)nMissions )
    {
        m_level = m_camp.m_choices[(size_t)c];
        m_stories.clear();
        if ( const LevelDef *lv = m_cat.level( m_level ) ) queueStory( lv->m_briefing );
        showStories( ScreenE::PLAY );
    }
    else if ( c == (int)nMissions )
    {
        m_tab = 0;
        m_item = 0;
        go( ScreenE::SHOP );
    }
    else if ( c == (int)nMissions + 1 )
    {
        m_logList.assign( m_camp.m_cubes.begin(), m_camp.m_cubes.end() );
        m_back = ScreenE::PLAN;
        go( ScreenE::LOGBOOK );
    }
    else if ( c == (int)nMissions + 2 )
    {
        m_back = ScreenE::PLAN;
        go( ScreenE::OPTIONS );
    }
    else
    {
        saveCampaign();
        newDemo();
        go( ScreenE::MAIN );
    }
}

void App::results( float dt )
{
    drawBackdrop( dt, true );
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    const LevelDef *lv = m_cat.level( m_level );
    heading( m_mode == ModeE::ARCADE ? "Abschnitt geschafft" : "Einsatz erfüllt", lv ? lv->m_title : "" );
    float pw = 900 * k;
    Rectangle r = { W * 0.5f - pw * 0.5f, H * 0.24f, pw, H * 0.56f };
    ui::panel( r );
    float y = r.y + 40 * k;
    auto row = [&]( const std::string &a, const std::string &b, Color c = ui::Ink ) {
        ui::text( a, r.x + 50 * k, y, 30, ui::Dim );
        ui::text( b, r.x + pw - 50 * k, y, 30, c, ui::AlignE::RIGHT );
        y += 48 * k;
    };
    int pct = m_last.m_spawned > 0 ? (int)std::lround( 100.0 * m_last.m_kills / m_last.m_spawned ) : 100;
    row( "Abschüsse", std::to_string( m_last.m_kills ) + " / " + std::to_string( m_last.m_spawned ) + "  (" + std::to_string( pct ) + " %)" );
    int secs = (int)m_last.m_time;
    row( "Flugzeit", std::to_string( secs / 60 ) + ":" + ( secs % 60 < 10 ? "0" : "" ) + std::to_string( secs % 60 ) );
    row( "Treffer eingesteckt", fmt( (long long)m_last.m_damage ) );
    if ( m_mode == ModeE::CAMPAIGN )
    {
        row( "Kredite verdient", "+" + fmt( m_earned ), ui::Gold );
        row( "Kredite gesamt", fmt( m_camp.m_credits ), ui::Gold );
        row( "Logkristalle", std::to_string( m_last.m_cubes.size() ) + ( m_newCubes.empty() ? "" : "  (" + std::to_string( m_newCubes.size() ) + " neu)" ), { 90, 255, 230, 255 } );
        if ( !m_last.m_plans.empty() ) row( "Baupläne gefunden", std::to_string( m_last.m_plans.size() ), ui::Good );
        if ( m_secretFound ) row( "Geheimer Kurs entdeckt!", "", ui::Good );
    }
    else
    {
        row( "Punkte", fmt( m_score[0] + ( m_players > 1 ? m_score[1] : 0 ) ), ui::Gold );
        if ( pct >= 100 ) row( "Alles abgeschossen!", "+10.000", ui::Good );
    }
    hints( "Eingabe / A: weiter" );
    if ( m_t > 0.6f && ( m_in.accept() || IsMouseButtonPressed( MOUSE_BUTTON_LEFT ) ) )
    {
        m_audio.play( SfxE::OK );
        if ( m_mode == ModeE::CAMPAIGN )
        {
            for ( const std::string &c : m_newCubes ) queueStory( c );
            if ( lv ) queueStory( lv->m_debrief );
            if ( lv ) campaignNext( *lv );
        }
        else
        {
            // Arcade: the next mission of the run, then the next episode's run.
            ++m_runIdx;
            if ( m_runIdx >= m_run.size() )
            {
                auto ep = m_cat.m_episodes.find( m_episode );
                if ( ep != m_cat.m_episodes.end() ) queueStory( ep->second.m_ending );
                m_prof.m_arcadeEpisodes = std::max( m_prof.m_arcadeEpisodes, m_episode + 1 );
                auto nx = m_cat.m_episodes.find( m_episode + 1 );
                if ( nx == m_cat.m_episodes.end() )
                {
                    m_scoreKey = diffKey();
                    m_name = "PILOT";
                    m_nameCursor = 0;
                    showStories( m_prof.place( m_scoreKey, m_score[0] + m_score[1] ) >= 0 ? ScreenE::NAME : ScreenE::SCORES );
                    return;
                }
                m_episode = nx->first;
                m_run = nx->second.m_arcade;
                m_runIdx = 0;
            }
            m_level = m_run[m_runIdx];
            showStories( ScreenE::PLAY );
        }
    }
}

void App::campaignNext( const LevelDef &lv )
{
    m_camp.m_choices.clear();
    for ( const std::string &n : lv.m_next )
        if ( !m_camp.m_done.count( n ) || m_cat.level( n ) == nullptr ) m_camp.m_choices.push_back( n );
    if ( m_camp.m_choices.empty() )
        for ( const std::string &n : lv.m_next ) m_camp.m_choices.push_back( n );   // a loop back: fly it again
    if ( !lv.m_secret.empty() && ( lv.m_secretKey.empty() || m_camp.m_keys.count( lv.m_secretKey ) ) && !m_camp.m_done.count( lv.m_secret ) )
        m_camp.m_choices.push_back( lv.m_secret );
    if ( m_camp.m_choices.empty() || lv.m_next.empty() )
    {
        // The episode is over.
        m_camp.m_choices.clear();
        auto ep = m_cat.m_episodes.find( m_camp.m_episode );
        if ( ep != m_cat.m_episodes.end() ) queueStory( ep->second.m_ending );
        m_prof.m_episodes = std::max( m_prof.m_episodes, m_camp.m_episode + 1 );
        m_prof.m_arcadeEpisodes = std::max( m_prof.m_arcadeEpisodes, m_camp.m_episode + 1 );
        auto nx = m_cat.m_episodes.find( m_camp.m_episode + 1 );
        if ( nx == m_cat.m_episodes.end() )
        {
            // The end of the campaign: the boss rush opens, and the next difficulty.
            m_prof.m_bossRush = true;
            if ( m_camp.m_difficulty >= m_prof.m_maxDiff ) m_prof.m_maxDiff = std::min( (int)m_cat.m_difficulties.size() - 1, m_prof.m_maxDiff + 1 );
            m_prof.save( userDir() + "profile.txt" );
            fs::remove( slotPath( m_slot ) );
            m_scoreKey = diffKey();
            m_name = "PILOT";
            m_nameCursor = 0;
            m_score[0] = m_camp.m_score;
            showStories( m_prof.place( m_scoreKey, m_camp.m_score ) >= 0 ? ScreenE::NAME : ScreenE::CREDITS );
            return;
        }
        m_camp.m_episode = nx->first;
        m_camp.m_choices = { nx->second.m_first };
        // The next episode's workshop adds a little start money to what was saved.
        m_camp.m_credits += nx->second.m_credits / 4;
        queueStory( nx->second.m_intro );
    }
    saveCampaign();
    showStories( ScreenE::PLAN );
}

void App::failed( float dt )
{
    drawBackdrop( dt, true );
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    heading( m_mode == ModeE::CAMPAIGN ? "Einsatz gescheitert" : "Spiel vorbei" );
    std::vector<std::string> items = m_mode == ModeE::CAMPAIGN
                                         ? std::vector<std::string>{ "Noch einmal versuchen", "Zur Einsatzplanung", "Hauptmenü" }
                                         : std::vector<std::string>{ "Hauptmenü" };
    ui::panel( { W * 0.5f - 400 * k, H * 0.35f - 30 * k, 800 * k, 260 * k } );
    if ( m_mode == ModeE::CAMPAIGN )
        ui::text( "Die Kredite dieses Einsatzes sind verloren.", W * 0.5f, H * 0.28f, 26, ui::Dim, ui::AlignE::CENTER );
    int c = menu( items, W * 0.5f, H * 0.35f, 700 * k, m_sel, 36, nullptr, true );
    if ( c < 0 ) return;
    if ( m_mode == ModeE::CAMPAIGN && c == 0 ) startMission( m_level );
    else if ( m_mode == ModeE::CAMPAIGN && c == 1 ) go( ScreenE::PLAN );
    else
    {
        newDemo();
        go( ScreenE::MAIN );
    }
}

void App::pilots( float dt )
{
    drawBackdrop( dt, true );
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    heading( m_players > 1 ? "Pilot " + std::to_string( m_pilotStep + 1 ) + " wählt" : "Wähle dein Schiff", m_mode == ModeE::BOSSRUSH ? "Bossjagd" : "Arcade" );
    std::vector<std::string> items;
    for ( const ArcadeShip &a : m_cat.m_arcadeShips ) items.push_back( a.m_title );
    if ( items.empty() )
    {
        go( ScreenE::MAIN );
        return;
    }
    float pw = 1300 * k;
    Rectangle r = { W * 0.5f - pw * 0.5f, H * 0.22f, pw, H * 0.62f };
    ui::panel( r );
    int c = menu( items, r.x + 50 * k, r.y + 40 * k, 420 * k, m_sel, 36 );
    const ArcadeShip &a = m_cat.m_arcadeShips[(size_t)m_sel];
    drawShipCard( a.m_loadout, { r.x + 520 * k, r.y + 30 * k, pw - 560 * k, r.height - 60 * k } );
    std::vector<std::string> lines = ui::wrap( a.m_desc, pw - 600 * k, 24 );
    for ( size_t i = 0; i < lines.size(); ++i ) ui::text( lines[i], r.x + 540 * k, r.y + r.height - 150 * k + i * 32 * k, 24, ui::Ink );
    hints( "Eingabe / A: wählen    Esc / B: zurück" );
    if ( m_in.back() )
    {
        if ( m_pilotStep > 0 ) --m_pilotStep;
        else go( ScreenE::DIFFICULTY );
    }
    if ( c < 0 ) return;
    m_pilot[m_pilotStep] = c;
    if ( m_pilotStep + 1 < m_players )
    {
        ++m_pilotStep;
        m_sel = ( c + 1 ) % (int)items.size();
        return;
    }
    if ( m_mode == ModeE::BOSSRUSH )
        startBossRush();
    else
        go( ScreenE::EPISODE );
}

void App::drawShipCard( const Loadout &o, Rectangle r )
{
    float k = ui::k();
    float y = r.y + 10 * k;
    auto  row = [&]( const std::string &a, const std::string &b ) {
        ui::text( a, r.x, y, 22, ui::Dim );
        ui::text( b, r.x + 200 * k, y, 26, ui::Ink );
        y += 38 * k;
    };
    const ShipDef   *s = m_cat.ship( o.m_ship );
    const WeaponDef *f = m_cat.weapon( o.m_front );
    const WeaponDef *re = o.m_rear.empty() ? nullptr : m_cat.weapon( o.m_rear );
    row( "Rumpf", s ? s->m_title : o.m_ship );
    if ( s )
    {
        row( "Panzerung", std::to_string( (int)s->m_armor ) );
        row( "Tempo", std::to_string( (int)s->m_speed ) );
    }
    row( "Front", ( f ? f->m_title : o.m_front ) + "  Stufe " + std::to_string( o.m_frontLv ) );
    row( "Heck", re ? re->m_title : "–" );
    const SidekickDef *l = o.m_sideL.empty() ? nullptr : m_cat.sidekick( o.m_sideL );
    const SidekickDef *rr = o.m_sideR.empty() ? nullptr : m_cat.sidekick( o.m_sideR );
    row( "Begleiter", ( l ? l->m_title : "–" ) + std::string( " / " ) + ( rr ? rr->m_title : "–" ) );
    const GeneratorDef *g = m_cat.generator( o.m_gen );
    const ShieldDef    *sh = m_cat.shield( o.m_shield );
    row( "Generator", g ? g->m_title : o.m_gen );
    row( "Schild", sh ? sh->m_title : o.m_shield );
    const SpecialDef *sp = o.m_special.empty() ? nullptr : m_cat.special( o.m_special );
    row( "Spezial", sp ? sp->m_title : "–" );
}

void App::continueScreen( float dt )
{
    // The field stands behind, darkened.
    m_view.update( m_world, dt );
    m_view.draw( m_world, m_cat );
    m_view.present( { 0, 0, (float)GetScreenWidth(), (float)GetScreenHeight() } );
    DrawRectangle( 0, 0, GetScreenWidth(), GetScreenHeight(), { 0, 0, 0, 170 } );
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    int   left = 10 - (int)m_t;
    ui::text( "WEITERMACHEN?", W * 0.5f, H * 0.32f, 80, WHITE, ui::AlignE::CENTER );
    ui::text( std::to_string( std::max( 0, left ) ), W * 0.5f, H * 0.45f, 140, ui::Gold, ui::AlignE::CENTER );
    ui::text( "Die Punkte beginnen wieder bei null.", W * 0.5f, H * 0.66f, 28, ui::Dim, ui::AlignE::CENTER );
    hints( "Eingabe / A: weiter    Esc / B: aufgeben" );
    (void)k;
    if ( m_in.accept() )
    {
        ++m_continues;
        for ( int i = 0; i < m_players; ++i )
        {
            Player &p = m_world.m_players[i];
            if ( !p.m_active ) continue;
            p.m_lives = 3;
            p.m_score = 0;
            p.m_dead = 5.0f;
        }
        m_score[0] = m_score[1] = 0;
        m_world.m_failed = false;
        m_audio.play( SfxE::LIFE );
        m_screen = ScreenE::PLAY;
        m_paused = false;
        return;
    }
    if ( left < 0 || m_in.back() )
    {
        m_scoreKey = diffKey();
        m_name = "PILOT";
        m_nameCursor = 0;
        long long total = m_score[0] + m_score[1];
        go( m_prof.place( m_scoreKey, total ) >= 0 ? ScreenE::NAME : ScreenE::FAILED );
    }
}

void App::nameEntry( float dt )
{
    drawBackdrop( dt, true );
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    long long total = m_mode == ModeE::CAMPAIGN ? m_camp.m_score : m_score[0] + m_score[1];
    heading( "Neuer Rekord!", fmt( total ) + " Punkte" );
    const std::string letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZÄÖÜ0123456789 -.";
    // Typed letters go straight in; the pad turns letters with up and down.
    int ch = GetCharPressed();
    while ( ch > 0 )
    {
        if ( ch >= 32 && ch < 127 && m_name.size() < 12 )
        {
            if ( m_nameCursor >= (int)m_name.size() ) m_name += (char)toupper( ch );
            else m_name[(size_t)m_nameCursor] = (char)toupper( ch );
            m_nameCursor = std::min( 11, m_nameCursor + 1 );
        }
        ch = GetCharPressed();
    }
    if ( IsKeyPressed( KEY_BACKSPACE ) && !m_name.empty() )
    {
        m_name.erase( m_name.begin() + std::min( (int)m_name.size() - 1, std::max( 0, m_nameCursor - ( m_nameCursor >= (int)m_name.size() ? 1 : 0 ) ) ) );
        m_nameCursor = std::max( 0, m_nameCursor - 1 );
    }
    if ( m_nameCursor >= (int)m_name.size() ) m_name.resize( (size_t)m_nameCursor + 1, ' ' );
    auto turn = [&]( int d ) {
        char   c = m_name[(size_t)m_nameCursor];
        size_t i = letters.find( c );
        i = i == std::string::npos ? 0 : ( i + letters.size() + d ) % letters.size();
        m_name[(size_t)m_nameCursor] = letters[i];
    };
    bool padUp = false, padDown = false;
    for ( int p = 0; p < 4; ++p )
        if ( IsGamepadAvailable( p ) )
        {
            padUp = padUp || IsGamepadButtonPressed( p, GAMEPAD_BUTTON_LEFT_FACE_UP );
            padDown = padDown || IsGamepadButtonPressed( p, GAMEPAD_BUTTON_LEFT_FACE_DOWN );
        }
    if ( padUp || IsKeyPressed( KEY_UP ) ) turn( 1 );
    if ( padDown || IsKeyPressed( KEY_DOWN ) ) turn( -1 );
    if ( m_in.left() ) m_nameCursor = std::max( 0, m_nameCursor - 1 );
    if ( m_in.right() ) m_nameCursor = std::min( 11, m_nameCursor + 1 );
    float cw = 54 * k;
    float x0 = W * 0.5f - 6 * cw;
    for ( int i = 0; i < 12; ++i )
    {
        Rectangle r = { x0 + i * cw, H * 0.42f, cw * 0.86f, 80 * k };
        DrawRectangleRec( r, i == m_nameCursor ? Color{ 40, 90, 140, 255 } : Color{ 16, 22, 36, 255 } );
        if ( i < (int)m_name.size() ) ui::text( std::string( 1, m_name[(size_t)i] ), r.x + r.width * 0.5f, r.y + 10 * k, 54, WHITE, ui::AlignE::CENTER );
    }
    hints( "Tippen oder Hoch/Runter: Buchstabe    Links/Rechts: Stelle    Eingabe / Start: fertig" );
    bool done = IsKeyPressed( KEY_ENTER ) || IsKeyPressed( KEY_KP_ENTER );
    for ( int p = 0; p < 4; ++p )
        if ( IsGamepadAvailable( p ) && ( IsGamepadButtonPressed( p, GAMEPAD_BUTTON_MIDDLE_RIGHT ) || IsGamepadButtonPressed( p, GAMEPAD_BUTTON_RIGHT_FACE_DOWN ) ) )
            done = true;
    if ( done )
    {
        std::string n = m_name;
        while ( !n.empty() && n.back() == ' ' ) n.pop_back();
        if ( n.empty() ) n = "PILOT";
        HighScore h;
        h.m_name = n;
        h.m_score = total;
        h.m_episode = m_mode == ModeE::CAMPAIGN ? m_camp.m_episode : m_episode;
        h.m_pilot = m_mode == ModeE::CAMPAIGN ? m_camp.m_load.m_ship : m_cat.m_arcadeShips.empty() ? "" : m_cat.m_arcadeShips[(size_t)m_pilot[0]].m_name;
        m_prof.add( m_scoreKey, h );
        m_prof.save( userDir() + "profile.txt" );
        m_audio.play( SfxE::BUY );
        go( ScreenE::SCORES );
    }
}

void App::scores( float dt )
{
    drawBackdrop( dt, true );
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    const char *modes[] = { "arcade", "arcade2", "campaign", "bossrush" };
    const char *names[] = { "Arcade", "Arcade für zwei", "Kampagne", "Bossjagd" };
    int nd = (int)m_cat.m_difficulties.size();
    int total = 4 * nd;
    if ( m_t < 0.01f && !m_scoreKey.empty() )
        for ( int i = 0; i < total; ++i )
            if ( std::string( modes[i / nd] ) + "_" + m_cat.m_difficulties[(size_t)( i % nd )].m_name == m_scoreKey ) m_scoreTab = i;
    if ( m_in.left() ) m_scoreTab = ( m_scoreTab + total - 1 ) % total;
    if ( m_in.right() ) m_scoreTab = ( m_scoreTab + 1 ) % total;
    int m = m_scoreTab / nd, d = m_scoreTab % nd;
    std::string key = std::string( modes[m] ) + "_" + m_cat.m_difficulties[(size_t)d].m_name;
    heading( "Bestenliste", std::string( "◀  " ) + names[m] + "  ·  " + m_cat.m_difficulties[(size_t)d].m_title + "  ▶" );
    float pw = 1100 * k;
    Rectangle r = { W * 0.5f - pw * 0.5f, H * 0.22f, pw, H * 0.64f };
    ui::panel( r );
    auto it = m_prof.m_scores.find( key );
    if ( it == m_prof.m_scores.end() || it->second.empty() )
        ui::text( "Noch kein Eintrag.", W * 0.5f, r.y + 60 * k, 30, ui::Dim, ui::AlignE::CENTER );
    else
        for ( size_t i = 0; i < it->second.size(); ++i )
        {
            const HighScore &h = it->second[i];
            float y = r.y + 40 * k + i * 56 * k;
            Color c = i == 0 ? ui::Gold : ui::Ink;
            ui::text( std::to_string( i + 1 ) + ".", r.x + 60 * k, y, 32, c );
            ui::text( h.m_name, r.x + 140 * k, y, 32, c );
            ui::text( "Ep. " + std::to_string( h.m_episode ), r.x + 600 * k, y, 24, ui::Dim );
            ui::text( fmt( h.m_score ), r.x + pw - 60 * k, y, 32, c, ui::AlignE::RIGHT );
        }
    hints( "Links/Rechts: Liste    Esc / B: zurück" );
    if ( m_in.back() || m_in.accept() )
    {
        m_scoreKey.clear();
        newDemo();
        go( ScreenE::MAIN );
    }
}

void App::credits( float dt )
{
    drawBackdrop( dt, true );
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    heading( "Zenith Lancer", "Mitwirkende" );
    const char *lines[] = { "Idee und Leitung: Rene Weller",
                            "Programm, Welt, Gegner, Waffen und Texte: Claude (Anthropic) mit Rene Weller",
                            "",
                            "Ein Shoot 'em up in der Tradition der großen Neunziger-Jahre-Shooter",
                            "mit Werkstatt, Kampagne und Arcade.",
                            "",
                            "Grafik: raylib (zlib-Lizenz)    Schrift: Barlow (SIL Open Font License)",
                            "Alle Schiffe, Gegner und Landschaften werden im Programm gebaut.",
                            "Alle Geräuscheffekte werden im Programm erzeugt.",
                            "",
                            "Danke fürs Spielen!" };
    float y = H * 0.25f;
    for ( const char *l : lines )
    {
        ui::text( l, W * 0.5f, y, 30, ui::Ink, ui::AlignE::CENTER );
        y += 46 * k;
    }
    hints( "Esc / B: zurück" );
    if ( m_in.back() || m_in.accept() )
    {
        newDemo();
        go( ScreenE::MAIN );
    }
}

void App::logbook( float dt )
{
    drawBackdrop( dt, true );
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    heading( "Logbuch", std::to_string( m_logList.size() ) + " Logkristalle" );
    std::vector<std::string> items;
    for ( const std::string &c : m_logList )
    {
        const StoryDef *s = m_cat.story( c );
        items.push_back( s ? s->m_title : c );
    }
    float pw = 1400 * k;
    Rectangle r = { W * 0.5f - pw * 0.5f, H * 0.2f, pw, H * 0.68f };
    ui::panel( r );
    if ( items.empty() )
        ui::text( "Noch keine Logkristalle gefunden.", W * 0.5f, r.y + 60 * k, 30, ui::Dim, ui::AlignE::CENTER );
    else
    {
        // A scrolling list: the selected line stays in view.
        int   rows = (int)( ( r.height - 60 * k ) / ( ui::height( 28 ) * 1.45f ) );
        int   first = std::clamp( m_sel - rows / 2, 0, std::max( 0, (int)items.size() - rows ) );
        std::vector<std::string> view( items.begin() + first, items.begin() + std::min( (int)items.size(), first + rows ) );
        int sel = m_sel - first;
        int c = menu( view, r.x + 50 * k, r.y + 30 * k, 560 * k, sel, 28 );
        m_sel = std::clamp( first + sel, 0, (int)items.size() - 1 );
        if ( m_in.up() && m_sel > 0 ) {}
        const StoryDef *s = m_cat.story( m_logList[(size_t)m_sel] );
        if ( s )
        {
            std::string all;
            for ( const std::string &l : s->m_text ) all += ( all.empty() ? "" : " " ) + l;
            std::vector<std::string> lines = ui::wrap( all, pw - 720 * k, 24 );
            ui::text( s->m_from, r.x + 660 * k, r.y + 30 * k, 24, ui::Gold );
            for ( size_t i = 0; i < lines.size() && i < 20; ++i ) ui::text( lines[i], r.x + 660 * k, r.y + 76 * k + i * 32 * k, 24, ui::Ink );
        }
        if ( c >= 0 )
        {
            m_stories.clear();
            queueStory( m_logList[(size_t)( first + c )] );
            showStories( ScreenE::LOGBOOK );
            return;
        }
    }
    hints( "Eingabe / A: lesen    Esc / B: zurück" );
    if ( m_in.back() ) go( m_back == ScreenE::PLAN ? ScreenE::PLAN : ScreenE::MAIN );
}

void App::options( float dt )
{
    drawBackdrop( dt, true );
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    heading( "Optionen" );
    auto pct = []( float v ) { return std::to_string( (int)std::lround( v * 100 ) ) + " %"; };
    std::string fps = m_set.m_fps == 0 ? "ohne Grenze" : std::to_string( m_set.m_fps );
    std::vector<std::string> items = {
        "Musik:  " + pct( m_set.m_music ),
        "Effekte:  " + pct( m_set.m_effects ),
        std::string( "Vollbild:  " ) + ( IsWindowState( FLAG_BORDERLESS_WINDOWED_MODE ) ? "an" : "aus" ),
        "Bildrate:  " + fps,
        std::string( "Bildschirmwackeln:  " ) + ( m_set.m_shake <= 0 ? "aus" : m_set.m_shake < 0.75f ? "schwach" : "voll" ),
        std::string( "Dauerfeuer (Feuer schaltet um):  " ) + ( m_set.m_autofire ? "an" : "aus" ),
        std::string( "Maussteuerung:  " ) + ( m_set.m_mouse ? "an" : "aus" ),
        std::string( "Trefferpunkt zeigen:  " ) + ( m_set.m_hitbox ? "an" : "aus" ),
        "Textgröße:  " + pct( m_set.m_textSize ),
        "Tasten und Knöpfe ...",
        "Zurück" };
    float pw = 1000 * k;
    ui::panel( { W * 0.5f - pw * 0.5f, H * 0.2f - 30 * k, pw, items.size() * ui::height( 34 ) * 1.45f + 60 * k } );
    int  c = menu( items, W * 0.5f - pw * 0.5f + 60 * k, H * 0.2f, pw - 100 * k, m_sel, 34 );
    int  d = m_in.right() ? 1 : m_in.left() ? -1 : 0;
    if ( c >= 0 && c <= 8 ) d = 1;
    if ( d != 0 )
    {
        m_audio.play( SfxE::MOVE );
        switch ( m_sel )
        {
        case 0: m_set.m_music = clampf( m_set.m_music + d * 0.1f, 0, 1 ); break;
        case 1: m_set.m_effects = clampf( m_set.m_effects + d * 0.1f, 0, 1 ); break;
        case 2:
            ToggleBorderlessWindowed();
            m_set.m_fullscreen = IsWindowState( FLAG_BORDERLESS_WINDOWED_MODE );
            resize();
            break;
        case 3:
        {
            int i = 0;
            while ( i < 6 && FpsSteps[i] != m_set.m_fps ) ++i;
            i = ( i + d + 7 ) % 7;
            m_set.m_fps = FpsSteps[i];
            break;
        }
        case 4: m_set.m_shake = m_set.m_shake <= 0 ? ( d > 0 ? 0.5f : 1.0f ) : m_set.m_shake < 0.75f ? ( d > 0 ? 1.0f : 0.0f ) : ( d > 0 ? 0.0f : 0.5f ); break;
        case 5: m_set.m_autofire = !m_set.m_autofire; break;
        case 6: m_set.m_mouse = !m_set.m_mouse; break;
        case 7: m_set.m_hitbox = !m_set.m_hitbox; break;
        case 8: m_set.m_textSize = clampf( m_set.m_textSize + d * 0.1f, 0.8f, 1.6f ); break;
        default: break;
        }
        applySettings();
        if ( m_sel == 0 && m_audio.m_on ) m_audio.play( SfxE::CREDITS );
        if ( m_sel == 1 ) m_audio.play( SfxE::HIT );
    }
    if ( c == 9 ) go( ScreenE::CONTROLS );
    hints( "Links/Rechts: ändern    Esc / B: zurück" );
    if ( c == 10 || m_in.back() )
    {
        saveSettings();
        if ( m_back == ScreenE::PLAY )
        {
            m_screen = ScreenE::PLAY;
            m_paused = true;
        }
        else
            go( m_back );
    }
}

void App::controls( float dt )
{
    drawBackdrop( dt, true );
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    heading( "Tasten und Knöpfe", "Eingabe: Taste ändern    X / Y-Knopf am Pad: Knopf ändern" );
    const char *names[] = { "Hoch", "Runter", "Links", "Rechts", "Feuer", "Langsam (präzise)", "Heckwaffe umschalten", "Spezialwaffe", "Pause" };
    float pw = 1300 * k;
    Rectangle r = { W * 0.5f - pw * 0.5f, H * 0.2f, pw, H * 0.66f };
    ui::panel( r );
    int n = (int)ActE::COUNT;
    if ( m_bindWait < 0 )
    {
        if ( m_in.up() ) m_sel = ( m_sel + n + 1 ) % ( n + 2 );
        if ( m_in.down() ) m_sel = ( m_sel + 1 ) % ( n + 2 );
    }
    float rowH = 50 * k;
    for ( int i = 0; i < n + 2; ++i )
    {
        float     y = r.y + 30 * k + i * rowH;
        Rectangle row = { r.x + 30 * k, y - 6 * k, pw - 60 * k, rowH - 6 * k };
        if ( i == m_sel ) ui::highlight( row, m_t );
        if ( i < n )
        {
            ui::text( names[i], r.x + 50 * k, y, 28, ui::Ink );
            bool waitK = m_bindWait == i && !m_bindPad, waitP = m_bindWait == i && m_bindPad;
            ui::text( waitK ? "Taste drücken ..." : m_in.label( (ActE)i, false ), r.x + 520 * k, y, 28, waitK ? ui::Gold : ui::Ink );
            ui::text( waitP ? "Knopf drücken ..." : m_in.label( (ActE)i, true ), r.x + 980 * k, y, 28, waitP ? ui::Gold : ui::Ink );
        }
        else
            ui::text( i == n ? "Alles zurücksetzen" : "Zurück", r.x + 50 * k, y, 28, ui::Ink );
    }
    if ( m_bindWait >= 0 )
    {
        if ( !m_bindPad )
        {
            int key = Input::capturedKey();
            if ( key ) { m_in.m_binds[m_bindWait].m_key = key; m_bindWait = -1; m_audio.play( SfxE::OK ); }
        }
        else
        {
            int b = m_in.capturedPad();
            if ( b ) { m_in.m_binds[m_bindWait].m_pad = b; m_bindWait = -1; m_audio.play( SfxE::OK ); }
        }
        if ( IsKeyPressed( KEY_ESCAPE ) ) m_bindWait = -1;
        return;
    }
    bool padChange = false;
    for ( int p = 0; p < 4; ++p )
        if ( IsGamepadAvailable( p ) && ( IsGamepadButtonPressed( p, GAMEPAD_BUTTON_RIGHT_FACE_LEFT ) || IsGamepadButtonPressed( p, GAMEPAD_BUTTON_RIGHT_FACE_UP ) ) )
            padChange = true;
    hints( "Eingabe: Taste    X/Y: Knopf    Esc / B: zurück" );
    if ( m_sel < n && ( IsKeyPressed( KEY_ENTER ) || padChange ) )
    {
        m_bindWait = m_sel;
        m_bindPad = padChange;
        return;
    }
    if ( m_sel == n && m_in.accept() ) m_in.reset();
    if ( ( m_sel == n + 1 && m_in.accept() ) || m_in.back() )
    {
        saveSettings();
        go( ScreenE::OPTIONS );
    }
}

void App::gallery( const std::string &file )
{
    // Every model on a sheet, lit as in the game.
    std::vector<std::string> names;
    for ( const auto &[n, m] : m_cat.m_models ) names.push_back( n );
    int    cols = 8, rows = std::max( 1, ( (int)names.size() + cols - 1 ) / cols );
    int    cw = 220, ch = 220;
    RenderTexture2D sheet = LoadRenderTexture( cols * cw, rows * ch );
    Material mat = LoadMaterialDefault();
    BeginTextureMode( sheet );
    ClearBackground( { 20, 24, 34, 255 } );
    EndTextureMode();
    Renderer r;
    r.init( 400, 400 );
    World w;
    w.begin( &m_cat, nullptr, ModeE::PREVIEW, 2, 1 );
    r.setTheme( m_cat, "gallery", 1 );
    for ( size_t i = 0; i < names.size(); ++i )
    {
        w.m_enemies.clear();
        EnemyDef *d = new EnemyDef();
        d->m_name = names[i];
        d->m_model = names[i];
        d->m_face = "fixed";
        const ModelMesh &mm = Renderer::s_forge.get( m_cat, names[i] );
        d->m_scale = clampf( 150.0f / std::max( 10.0f, mm.m_radius * 2.0f ), 0.2f, 4.0f );
        w.spawn( d, { FieldW * 0.5f, FieldH * 0.5f }, nullptr );
        w.m_enemies[0].m_entered = true;
        w.m_enemies[0].m_face = 180.0f;
        w.m_enemies[0].m_head = 180.0f;
        r.draw( w, m_cat );
        BeginTextureMode( sheet );
        Rectangle dst = { (float)( ( i % cols ) * cw ), (float)( ( i / cols ) * ch ), (float)cw, (float)ch };
        DrawTexturePro( r.output().texture, { 400 * 0.5f - 200, 0, 400, -400 }, dst, { 0, 0 }, 0, WHITE );
        DrawText( names[i].c_str(), (int)dst.x + 6, (int)dst.y + 4, 16, WHITE );
        EndTextureMode();
        delete d;
    }
    Image img = LoadImageFromTexture( sheet.texture );
    ImageFlipVertical( &img );
    ExportImage( img, file.c_str() );
    UnloadImage( img );
    UnloadRenderTexture( sheet );
    r.shutdown();
    (void)mat;
}

}   // namespace zenith
