/**
 * @file AppPlay.cpp
 * @brief The mission on screen: stepping the World, the HUD beside the field, radio lines, the boss bar, the
 *        warning, the pause, and what follows a mission.
 * @author Rene Weller
 */
#include "App.h"

#include "Crash.h"
#include "Files.h"
#include "Lang.h"
#include "Rumble.h"
#include "Ui.h"

#include <raylib.h>

#include <algorithm>
#include <cmath>

namespace zenith
{

void App::startMission( const std::string &name )
{
    const LevelDef *lv = m_cat.level( name );
    if ( !lv ) return;
    m_level = name;
    crash::context( "mission " + name );
    // The set-up, written down first: the replay starts the same World from it.
    bool         camp = m_mode == ModeE::CAMPAIGN && !m_practice;
    ReplayHeader head;
    head.m_version = ZENITH_VERSION;
    head.m_mode = m_mode;
    head.m_mission = name;
    head.m_difficulty = camp ? m_camp.m_difficulty : m_diff;
    // The survival flight's World plays from its own seed too: a day's flight is the same for everyone.
    head.m_seed = m_survival ? m_survivalSeed : (uint32_t)( GetTime() * 1000.0 ) ^ 0x5eed;
    head.m_lang = language();
    if ( camp )
    {
        // The campaign: New Game+ makes it harder; for two, the second pilot flies with gear of their own.
        head.m_ngPlus = m_camp.m_plus;
        head.m_pilots = m_camp.m_coop ? 2 : 1;
        head.m_load[0] = m_camp.m_load;
        head.m_load[1] = m_camp.m_load2;
    }
    else if ( m_practice )
    {
        head.m_pilots = 1;
        head.m_load[0] = practiceLoad( *lv );
    }
    else
    {
        head.m_pilots = m_players;
        for ( int i = 0; i < m_players; ++i )
        {
            head.m_load[i] = m_carry[i];
            head.m_lives[i] = std::max( 0, m_lives[i] - 1 );
            head.m_score[i] = m_score[i];
        }
    }
    m_players = head.m_pilots;
    startWorld( m_world, m_cat, head );
    m_prof.m_flown.insert( name );
    m_rec.begin( head );
    {
        char   stamp[32];
        time_t now = time( nullptr );
        strftime( stamp, sizeof( stamp ), "%Y%m%d-%H%M%S", localtime( &now ) );
        m_recPath = userDir() + "replays/" + stamp + "_" + name + ReplayExt;
    }
    m_replaying = false;
    m_bossHit = false;
    m_view.setTheme( m_cat, m_opt.m_theme.empty() ? lv->m_theme : m_opt.m_theme, lv->m_seed );
    m_view.reset();
    m_audio.music( lv->m_music );
    m_acc = 0;
    m_paused = false;
    m_radio.clear();
    m_radioT = 0;
    m_warnT = 0;
    m_hasBossSave = false;
    m_bossTries = 0;
    m_themeT = 0;
    m_themeNext.clear();
    m_introT = 0;
    m_outroT = -1;
    go( ScreenE::PLAY );
}

void App::startBossRush()
{
    // A mission made of every boss, one after the other, in space.
    m_cat.m_levels["bossrush"] = bossRush( m_cat );
    // The pilot's own guns at full power, with the strongest generator and shield to carry them.
    m_carry[0] = m_cat.m_arcadeShips[(size_t)m_pilot[0]].m_loadout;
    m_carry[0].m_frontLv = 11;
    if ( m_carry[0].m_rear.empty() ) m_carry[0].m_rear = "tailgun";
    m_carry[0].m_rearLv = 9;
    if ( m_cat.generator( "singularity" ) ) m_carry[0].m_gen = "singularity";
    if ( m_cat.shield( "halo_s" ) ) m_carry[0].m_shield = "halo_s";
    m_lives[0] = 3;
    m_score[0] = 0;
    m_episode = 5;
    m_run = { "bossrush" };
    m_runIdx = 0;
    startMission( "bossrush" );
}

void App::endMission( bool won )
{
    m_last = m_world.m_stats;
    m_prof.m_kills += m_last.m_kills;
    logMission( won ? "won" : "lost" );
    saveReplay( won );
    countMission( won );
    if ( m_practice )
    {
        // A practice flight keeps nothing but the log.
        go( won ? ScreenE::RESULTS : ScreenE::FAILED );
        return;
    }
    for ( const std::string &c : m_last.m_cubes ) m_prof.m_cubes.insert( c );
    if ( m_mode == ModeE::CAMPAIGN )
    {
        if ( !won )
        {
            go( ScreenE::FAILED );
            return;
        }
        const Player &p = m_world.m_players[0];
        m_earned = p.m_credits;
        m_camp.m_credits += p.m_credits;
        m_camp.m_score += p.m_score;
        m_earned2 = 0;
        if ( m_camp.m_coop )
        {
            const Player &p2 = m_world.m_players[1];
            m_earned2 = p2.m_credits;
            m_camp.m_credits2 += p2.m_credits;
            m_camp.m_score2 += p2.m_score;
        }
        m_camp.m_done.insert( m_level );
        m_camp.m_last = m_level;
        ++m_camp.m_missions;
        m_camp.m_kills += m_last.m_kills;
        m_camp.m_seconds += m_last.m_time;
        m_newCubes.clear();
        for ( const std::string &c : m_last.m_cubes )
            if ( m_camp.m_cubes.insert( c ).second ) m_newCubes.push_back( c );
        for ( const std::string &pl : m_last.m_plans ) m_camp.m_plans.insert( pl );
        m_secretFound = false;
        const LevelDef *lv = m_cat.level( m_level );
        for ( const std::string &key : m_last.m_keys )
        {
            m_camp.m_keys.insert( key );
            if ( lv && key == lv->m_secretKey ) m_secretFound = true;
        }
        saveCampaign();
        go( ScreenE::RESULTS );
        return;
    }
    // Arcade and boss rush.
    for ( int i = 0; i < m_players; ++i )
    {
        const Player &p = m_world.m_players[i];
        m_score[i] = p.m_score;
        m_carry[i] = p.m_load;
        m_lives[i] = p.m_lives + ( p.m_alive ? 1 : 0 );
        if ( m_last.m_spawned > 0 && m_last.m_kills >= m_last.m_spawned ) m_score[i] += 10000;
    }
    if ( m_mode == ModeE::BOSSRUSH && !won && m_hasBossSave )
    {
        go( ScreenE::FAILED );
        return;
    }
    if ( m_survival )
    {
        // The survival flight: its time counts; a day's flight keeps its best score.
        if ( m_daily )
        {
            long long &best = m_prof.m_daily[today()];
            best = std::max( best, m_score[0] );
        }
        m_scoreKey = diffKey();
        m_name = "PILOT";
        m_nameCursor = 0;
        m_prof.save( userDir() + "profile.txt" );
        go( m_prof.place( m_scoreKey, m_score[0] ) >= 0 ? ScreenE::NAME : ScreenE::SCORES );
        return;
    }
    if ( m_mode == ModeE::BOSSRUSH )
    {
        m_scoreKey = diffKey();
        m_name = "PILOT";
        m_nameCursor = 0;
        go( m_prof.place( m_scoreKey, m_score[0] ) >= 0 ? ScreenE::NAME : ScreenE::SCORES );
        return;
    }
    go( ScreenE::RESULTS );
}

void App::play( float dt )
{
    // The pause.
    if ( m_outroT < 0 && m_in.pause() && m_t > 0.2f )
    {
        m_paused = !m_paused;
        m_pauseSel = 0;
        m_audio.play( m_paused ? SfxE::BACK : SfxE::OK );
    }
    if ( !m_paused && !IsWindowFocused() && !m_in.scripted() && m_outroT < 0 && m_screen == ScreenE::PLAY ) m_paused = true;
    if ( !m_paused )
    {
        // The aid "game speed" slows the whole game down.
        m_acc += dt * clampf( m_set.m_speed, 0.5f, 1.0f );
        int steps = 0;
        // The mouse on the field (the renderer's pixels are the screen's).
        Vector2 mp = GetMousePosition();
        float   s = m_view.pixelScale();
        Vector2 mf = { FieldW * 0.5f + ( mp.x - GetScreenWidth() * 0.5f ) / s, FieldH * 0.5f + ( mp.y - GetScreenHeight() * 0.5f ) / s };
        if ( m_in.scripted() ) m_acc = Dt * 120;   // the app test: two seconds of mission a frame
        if ( m_replaying ) m_acc += dt * ( m_replaySpeed - clampf( m_set.m_speed, 0.5f, 1.0f ) );
        while ( m_acc >= Dt && steps < ( m_in.scripted() ? 120 : 5 * m_replaySpeed ) )
        {
            PlayerInput in[2];
            if ( m_replaying )
            {
                // A replay: its marks first (a boss fought again, a continue), then the step's recorded inputs.
                for ( ReplayMarkE mk : m_play.marks() )
                {
                    if ( mk == ReplayMarkE::RETRY ) retryBoss();
                    else continueArcade( m_world );
                }
                if ( !m_play.next( in ) )
                {
                    m_replayEnd = true;
                    m_acc = 0;
                    break;
                }
            }
            for ( int i = 0; i < 2 && !m_replaying; ++i )
            {
                if ( !m_world.m_players[i].m_active ) continue;
                in[i] = m_opt.m_auto || m_in.scripted() ? autopilot( m_world, i ) : m_in.player( i, m_players, m_set.m_mouse ? &mf : nullptr, m_set.m_autofire );
                // The aid "auto-bomb": the special fires by itself when the armour is nearly gone and a bullet is close.
                const Player &p = m_world.m_players[i];
                m_autoBombT[i] = std::max( 0.0f, m_autoBombT[i] - Dt );
                if ( m_set.m_autoBomb && p.m_alive && p.m_special && p.m_charges > 0 && m_autoBombT[i] <= 0 &&
                     p.m_armor < p.m_armorMax * 0.25f )
                    for ( const Bullet &b : m_world.m_bullets )
                        if ( !b.m_player && !b.m_dead && dist2( b.m_pos, p.m_pos ) < 110.0f * 110.0f )
                        {
                            in[i].m_special = true;
                            m_autoBombT[i] = 2.0f;
                            break;
                        }
            }
            if ( !m_replaying )
            {
                // What the World gets is what the replay keeps, to the bit.
                for ( int i = 0; i < 2; ++i ) in[i] = quantize( in[i] );
                m_rec.push( in );
            }
            m_world.step( in );
            m_in.consumed();
            m_view.events( m_world );
            m_audio.events( m_world );
            // The checkpoint: the campaign and the boss rush may fight a lost boss again from here.
            for ( const SimEvent &e : m_world.m_events )
            {
                if ( e.m_type == EvE::BOSSSTART )
                {
                    if ( !m_replaying ) m_prof.m_bosses.insert( m_level );
                    m_bossHit = false;
                    if ( m_mode == ModeE::CAMPAIGN || m_mode == ModeE::BOSSRUSH )
                    {
                        m_bossSave = m_world;
                        m_hasBossSave = true;
                        m_bossTries = 0;
                    }
                }
                else if ( e.m_type == EvE::PLAYERHIT && m_world.boss() >= 0 )
                    m_bossHit = true;
                else if ( e.m_type == EvE::BOSSDIE && !m_replaying && !m_practice )
                {
                    // A boss down without a scratch, or after a fight lost before.
                    if ( !m_bossHit ) m_feats.add( "boss_nohit" );
                    if ( m_bossTries > 0 ) m_feats.add( "bossretries" );
                    checkFeats();
                }
            }
            for ( const SimEvent &e : m_world.m_events )
            {
                if ( e.m_type == EvE::TEXT )
                {
                    const StoryDef *st = m_cat.story( e.m_name );
                    if ( st )
                    {
                        std::string all;
                        for ( const std::string &l : st->m_text ) all += ( all.empty() ? "" : " " ) + l;
                        m_radio = all;
                        m_radioFrom = st->m_from;
                        m_radioT = 0;
                        m_audio.play( SfxE::BLIP );
                        m_audio.voice( e.m_name, language() );
                    }
                }
                else if ( e.m_type == EvE::THEME )
                {
                    // A flash of light hides the change of ground (the jump into orbit, out of the clouds).
                    m_themeNext = e.m_name;
                    m_themeT = 1.0f;
                }
                else if ( e.m_type == EvE::WARN )
                {
                    m_warnT = 3.0f;
                    // The boss's music, unless the mission chose its own.
                    bool own = false;
                    if ( m_world.m_level )
                        for ( const LevelEvent &ev : m_world.m_level->m_events )
                            if ( ev.m_type == "music" ) own = true;
                    if ( !own )
                    {
                        int ep = m_world.m_level ? m_world.m_level->m_episode : 0;
                        bool last = m_world.m_level && m_world.m_level->m_next.empty() && m_mode == ModeE::CAMPAIGN;
                        m_audio.music( ep == 5 && last ? "final" : ep >= 1 && ep <= 5 ? "e" + std::to_string( ep ) + "_boss" : "boss" );
                    }
                }
                // The pad shakes with what the pilot feels: hits, big blasts nearby, a boss going down.
                auto shake = [&]( float low, float high, float t ) {
                    if ( !m_set.m_rumble ) return;
                    m_rumbleLow = std::max( m_rumbleLow, low );
                    m_rumbleHigh = std::max( m_rumbleHigh, high );
                    m_rumbleT = std::max( m_rumbleT, t );
                };
                if ( e.m_type == EvE::PLAYERHIT ) shake( e.m_b > 0.5f ? 0.1f : 0.45f, e.m_b > 0.5f ? 0.35f : 0.7f, 0.14f );
                else if ( e.m_type == EvE::PLAYERDIE ) shake( 1.0f, 0.8f, 0.6f );
                else if ( e.m_type == EvE::BOSSDIE ) shake( 0.9f, 0.5f, 0.9f );
                else if ( e.m_type == EvE::NOVA ) shake( 0.6f, 0.3f, 0.35f );
                else if ( e.m_type == EvE::EXPLODE && e.m_a >= 2 ) shake( 0.35f, 0.15f, 0.2f );
                else if ( e.m_type == EvE::PICKUP && e.m_name == "cube" )
                {
                    m_radio = T( "Logkristall geborgen." );
                    m_radioFrom = "ORRIN";
                    m_radioT = 0;
                }
                else if ( e.m_type == EvE::PICKUP && e.m_name == "key" )
                {
                    m_radio = T( "Ein Peilsignal ... da draußen ist etwas, das auf keiner Karte steht." );
                    m_radioFrom = "ORRIN";
                    m_radioT = 0;
                }
                else if ( e.m_type == EvE::PICKUP && e.m_name == "plan" )
                {
                    m_radio = T( "Bauplan gesichert. Quill wird sich freuen." );
                    m_radioFrom = "ORRIN";
                    m_radioT = 0;
                }
            }
            m_acc -= Dt;
            ++steps;
        }
        if ( steps >= 5 ) m_acc = 0;
        m_introT += dt;
        m_radioT += dt;
        // A call stays on screen while its voice speaks.
        if ( m_audio.speaking() && m_radioT > 5.5f ) m_radioT = 5.5f;
        // The music follows the field: its quiet layer while little happens, the full piece in a fight.
        {
            int enemies = 0, shots = 0;
            for ( const Enemy &e : m_world.m_enemies )
                if ( !e.m_dead && !e.m_gone && e.m_entered && e.m_def->m_kind != EnemyKindE::OBSTACLE ) ++enemies;
            for ( const Bullet &b : m_world.m_bullets )
                if ( !b.m_player ) ++shots;
            float x = m_world.boss() >= 0 ? 1.0f : clampf( enemies / 7.0f + shots / 40.0f, 0.0f, 1.0f );
            m_audio.intensity( 0.25f + 0.75f * x );
        }
        if ( m_themeT > 0 )
        {
            float before = m_themeT;
            m_themeT = std::max( 0.0f, m_themeT - dt * 1.6f );
            if ( before > 0.5f && m_themeT <= 0.5f && !m_themeNext.empty() && m_world.m_level )
            {
                m_view.setTheme( m_cat, m_themeNext, m_world.m_level->m_seed + 17u );
                m_themeNext.clear();
            }
        }
        if ( m_rumbleT > 0 )
        {
            m_rumbleT -= dt;
            if ( m_rumbleT > 0 )
                rumble::set( m_rumbleLow, m_rumbleHigh );
            else
            {
                rumble::stop();
                m_rumbleLow = m_rumbleHigh = 0.0f;
            }
        }
        if ( m_warnT > 0 ) m_warnT -= dt;
    }
    // A replay runs to its last step (the outro is in it), then back to the list.
    if ( m_replaying )
    {
        if ( m_replayEnd )
        {
            m_outroT = std::max( m_outroT, 0.0f ) + dt;
            if ( m_outroT > 1.5f )
            {
                stopReplay();
                return;
            }
        }
        else
            m_outroT = -1;
    }
    // The end of the mission: a fade, then what follows.
    else if ( m_outroT < 0 && ( m_world.finished() || m_world.failed() ) )
    {
        m_outroT = 0;
        m_won = m_world.finished();
    }
    if ( m_outroT >= 0 && !m_paused && !m_replaying )
    {
        m_outroT += m_in.scripted() ? 0.5f : dt;
        if ( !m_won && m_mode == ModeE::ARCADE && !m_survival && m_outroT > 0.8f )
        {
            m_outroT = -1;
            m_world.m_failed = true;
            go( ScreenE::CONTINUE );
            return;
        }
        if ( m_outroT > 2.2f )
        {
            m_outroT = -1;
            endMission( m_won );
            return;
        }
    }
    m_view.m_fade = m_introT < 0.6f ? 1.0f - m_introT / 0.6f : m_outroT > 0.8f ? std::min( 1.0f, ( m_outroT - 0.8f ) / 1.2f ) : 0.0f;
    if ( m_replaying && !m_paused && m_in.other() )
    {
        m_replaySpeed = m_replaySpeed >= 4 ? 1 : m_replaySpeed * 2;
        m_audio.play( SfxE::MOVE );
    }
    m_view.update( m_world, m_paused ? 0.0f : dt );
    m_view.draw( m_world, m_cat );
    m_view.present( { 0, 0, (float)GetScreenWidth(), (float)GetScreenHeight() } );
    if ( m_themeT > 0 )
    {
        float a = ( 1.0f - std::fabs( m_themeT - 0.5f ) * 2.0f ) * ( m_set.m_softFlash ? 0.85f : 1.0f );
        // Dimmed flashes turn the white-out into a fade through dark blue.
        Color c = m_set.m_softFlash ? Color{ 10, 20, 40, 255 } : Color{ 235, 245, 255, 255 };
        c.a = (unsigned char)( 255 * clampf( a, 0, 1 ) );
        DrawRectangle( 0, 0, GetScreenWidth(), GetScreenHeight(), c );
    }
    hud();
    if ( m_paused ) pauseMenu( dt );
}

void App::hud()
{
    float     k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    Rectangle f = m_view.fieldRect();
    float     side = f.x;
    bool      narrow = side < 200 * k;
    const LevelDef *lv = m_world.m_level;
    // The left panel: the gear.
    auto gear = [&]( const Player &p, Rectangle r, int idx ) {
        ui::panel( r, 0.55f );
        float x = r.x + 18 * k, y = r.y + 16 * k, w = r.width - 36 * k;
        float ps = std::min( 12.0f, ( w / k ) / 11.0f / 1.32f );
        if ( m_players > 1 ) ui::text( tf( "PILOT %d", idx + 1 ), x, y, 22, idx == 0 ? ui::Accent : ui::Gold ), y += 30 * k;
        if ( p.m_ship ) ui::text( L( p.m_ship->m_title, p.m_ship->m_titleEn ), x, y, 26, WHITE ), y += 36 * k;
        if ( p.m_front )
        {
            ui::text( "FRONT", x, y, 18, ui::Dim );
            y += 22 * k;
            ui::text( L( p.m_front->m_title, p.m_front->m_titleEn ), x, y, 22, ui::Ink );
            y += 28 * k;
            ui::pips( x, y, p.m_load.m_frontLv, 11, ps, { 120, 210, 255, 255 } );
            y += ps * 1.4f * k + 14 * k;
        }
        if ( p.m_rear )
        {
            ui::text( "HECK", x, y, 18, ui::Dim );
            y += 22 * k;
            std::string mode = p.m_rear->m_modes > 1 ? ( p.m_rearMode == 1 ? L( p.m_rear->m_mode1, p.m_rear->m_mode1En )
                                                                           : L( p.m_rear->m_mode2, p.m_rear->m_mode2En ) )
                                                     : "";
            ui::text( L( p.m_rear->m_title, p.m_rear->m_titleEn ) + ( mode.empty() ? "" : "  · " + mode ), x, y, 22, ui::Ink );
            y += 28 * k;
            ui::pips( x, y, p.m_load.m_rearLv, 11, ps, { 255, 190, 90, 255 } );
            y += ps * 1.4f * k + 14 * k;
        }
        for ( int s = 0; s < 2; ++s )
        {
            const Sidekick &sk = p.m_side[s];
            if ( !sk.m_def ) continue;
            ui::text( s == 0 ? "LINKS" : "RECHTS", x, y, 18, ui::Dim );
            ui::text( L( sk.m_def->m_title, sk.m_def->m_titleEn ), x + 90 * k, y - 2 * k, 20, ui::Ink );
            y += 26 * k;
            if ( sk.m_def->m_ammo > 0 ) ui::bar( { x, y, w, 6 * k }, sk.m_ammo / (float)sk.m_def->m_ammo, { 180, 255, 200, 255 } ), y += 14 * k;
            else if ( sk.m_def->m_charge > 0 ) ui::bar( { x, y, w, 6 * k }, sk.m_charge / sk.m_def->m_charge, { 255, 160, 255, 255 } ), y += 14 * k;
        }
        if ( p.m_special )
        {
            ui::text( "SPEZIAL", x, y, 18, ui::Dim );
            ui::text( L( p.m_special->m_title, p.m_special->m_titleEn ), x + 100 * k, y - 2 * k, 20, ui::Ink );
            y += 26 * k;
            for ( int c = 0; c < p.m_charges && c < 9; ++c ) DrawCircle( (int)( x + 8 * k + c * 22 * k ), (int)( y + 8 * k ), 7 * k, { 200, 160, 255, 255 } );
            y += 26 * k;
        }
        ui::text( "GENERATOR", x, y, 18, ui::Dim );
        y += 22 * k;
        ui::bar( { x, y, w, 12 * k }, p.m_energy / std::max( 1.0f, p.m_energyMax ), { 255, 220, 90, 255 } );
    };
    auto status = [&]( const Player &p, Rectangle r, int idx ) {
        ui::panel( r, 0.55f );
        float x = r.x + 18 * k, y = r.y + 16 * k, w = r.width - 36 * k;
        ui::text( "PUNKTE", x, y, 18, ui::Dim );
        y += 22 * k;
        ui::text( fmt( p.m_score ), x, y, 34, WHITE );
        y += 44 * k;
        if ( m_mode == ModeE::CAMPAIGN && !m_practice )
        {
            ui::text( "KREDITE", x, y, 18, ui::Dim );
            y += 22 * k;
            ui::text( fmt( ( idx == 1 ? m_camp.m_credits2 : m_camp.m_credits ) + p.m_credits ), x, y, 28, ui::Gold );
            ui::text( "+" + fmt( p.m_credits ), x + w, y + 4 * k, 20, ui::Gold, ui::AlignE::RIGHT );
            y += 40 * k;
        }
        else
        {
            ui::text( "LEBEN", x, y, 18, ui::Dim );
            y += 24 * k;
            for ( int l = 0; l < p.m_lives + ( p.m_alive ? 1 : 0 ) && l < 9; ++l )
                DrawTriangle( { x + l * 24 * k + 9 * k, y }, { x + l * 24 * k, y + 18 * k }, { x + l * 24 * k + 18 * k, y + 18 * k }, { 120, 210, 255, 255 } );
            y += 32 * k;
        }
        ui::text( "SCHILD", x, y, 18, ui::Dim );
        y += 22 * k;
        ui::bar( { x, y, w, 14 * k }, p.m_shieldDef ? p.m_shield / std::max( 1.0f, p.m_shieldDef->m_max ) : 0.0f, { 90, 170, 255, 255 } );
        y += 26 * k;
        ui::text( "PANZERUNG", x, y, 18, ui::Dim );
        y += 22 * k;
        float a = p.m_armor / std::max( 1.0f, p.m_armorMax );
        bool  low = a < 0.3f && std::fmod( m_t, 0.6f ) < 0.3f;
        ui::bar( { x, y, w, 14 * k }, a, low ? ui::Warn : Color{ 120, 230, 140, 255 } );
        y += 36 * k;
    };
    const Player &p0 = m_world.m_players[0];
    if ( !narrow )
    {
        float pw = std::min( side - 30 * k, 420 * k );
        gear( p0, { side - pw - 15 * k, H * 0.05f, pw, H * ( m_players > 1 ? 0.44f : 0.56f ) }, 0 );
        status( p0, { f.x + f.width + 15 * k, H * 0.05f, pw, H * ( m_players > 1 ? 0.36f : 0.42f ) }, 0 );
        if ( m_players > 1 && m_world.m_players[1].m_active )
        {
            gear( m_world.m_players[1], { side - pw - 15 * k, H * 0.52f, pw, H * 0.44f }, 1 );
            status( m_world.m_players[1], { f.x + f.width + 15 * k, H * 0.45f, pw, H * 0.36f }, 1 );
        }
        // The mission's title and how far along it is.
        if ( lv )
        {
            float x = f.x + f.width + 15 * k, y = H * 0.86f;
            ui::text( L( lv->m_title, lv->m_titleEn ), x + 18 * k, y, 22, ui::Dim );
            if ( m_survival )
            {
                int secs = (int)m_world.m_clock;
                ui::text( std::to_string( secs / 60 ) + ":" + ( secs % 60 < 10 ? "0" : "" ) + std::to_string( secs % 60 ), x + pw - 18 * k, y,
                          22, ui::Gold, ui::AlignE::RIGHT );
            }
            else
            {
                float prog = clampf( m_world.m_time / std::max( 1.0f, lv->m_length ), 0, 1 );
                ui::bar( { x + 18 * k, y + 32 * k, pw - 36 * k, 6 * k }, prog, { 140, 160, 200, 255 } );
            }
            ui::text( L( m_cat.difficulty( m_world.m_diffIndex ).m_title, m_cat.difficulty( m_world.m_diffIndex ).m_titleEn ), x + 18 * k, y + 48 * k, 18, ui::Dim );
        }
    }
    else
    {
        // A narrow screen: a thin strip over the field.
        float y = f.y + 8 * k;
        ui::text( fmt( p0.m_score ), f.x + 12 * k, y, 26, WHITE );
        ui::bar( { f.x + f.width - 212 * k, y + 4 * k, 200 * k, 8 * k }, p0.m_shieldDef ? p0.m_shield / std::max( 1.0f, p0.m_shieldDef->m_max ) : 0, { 90, 170, 255, 255 } );
        ui::bar( { f.x + f.width - 212 * k, y + 16 * k, 200 * k, 8 * k }, p0.m_armor / std::max( 1.0f, p0.m_armorMax ), { 120, 230, 140, 255 } );
        ui::bar( { f.x + f.width - 212 * k, y + 28 * k, 200 * k, 5 * k }, p0.m_energy / std::max( 1.0f, p0.m_energyMax ), { 255, 220, 90, 255 } );
    }
    // The boss bar.
    int b = m_world.boss();
    if ( b >= 0 || m_world.m_bossShown > 0.01f )
    {
        float bw = f.width * 0.7f, x = f.x + ( f.width - bw ) * 0.5f, y = f.y + ( narrow ? 50 : 18 ) * k;
        ui::text( L( m_world.m_bossTitle, m_world.m_bossTitleEn ), f.x + f.width * 0.5f, y, 24, { 255, 150, 130, 255 }, ui::AlignE::CENTER );
        ui::bar( { x, y + 32 * k, bw, 12 * k }, m_world.m_bossShown, { 255, 80, 70, 255 } );
    }
    // The warning.
    if ( m_warnT > 0 )
    {
        float a = std::fabs( std::sin( m_warnT * 5.0f ) );
        float y = f.y + f.height * 0.38f;
        DrawRectangle( (int)f.x, (int)y, (int)f.width, (int)( 90 * k ), { 120, 0, 0, (unsigned char)( 110 * a ) } );
        ui::text( "WARNUNG", f.x + f.width * 0.5f, y + 8 * k, 64, { 255, 90, 80, (unsigned char)( 255 * a ) }, ui::AlignE::CENTER );
    }
    // The radio.
    if ( !m_radio.empty() && m_radioT < 6.5f )
    {
        float a = std::min( 1.0f, std::min( m_radioT * 4.0f, ( 6.5f - m_radioT ) * 2.0f ) );
        float bw = f.width - 40 * k, x = f.x + 20 * k, y = f.y + f.height - 150 * k;
        std::vector<std::string> lines = ui::wrap( m_radio.substr( 0, (size_t)( m_radioT * 70.0f ) ), bw - 40 * k, 24 );
        if ( lines.size() > 3 ) lines.erase( lines.begin(), lines.end() - 3 );
        Rectangle r = { x, y, bw, 130 * k };
        DrawRectangleRec( r, { 4, 10, 20, (unsigned char)( 200 * a ) } );
        DrawRectangleLinesEx( r, 2 * k, { 90, 200, 255, (unsigned char)( 160 * a ) } );
        // The caller's face at the left of the box.
        float tx = x + 20 * k;
        if ( const Texture2D *pt = portrait( m_radioFrom ) )
        {
            float ps = 112 * k;
            DrawTexturePro( *pt, { 0, 0, (float)pt->width, (float)pt->height }, { x + 9 * k, y + 9 * k, ps, ps }, { 0, 0 }, 0,
                            { 255, 255, 255, (unsigned char)( 255 * a ) } );
            tx = x + ps + 24 * k;
            lines = ui::wrap( m_radio.substr( 0, (size_t)( m_radioT * 70.0f ) ), x + bw - 20 * k - tx, 24 );
            if ( lines.size() > 3 ) lines.erase( lines.begin(), lines.end() - 3 );
        }
        ui::text( m_radioFrom, tx, y + 10 * k, 20, { 255, 205, 100, (unsigned char)( 255 * a ) } );
        for ( size_t i = 0; i < lines.size(); ++i ) ui::text( lines[i], tx, y + 38 * k + i * 28 * k, 24, { 226, 236, 248, (unsigned char)( 255 * a ) } );
    }
    // The mission's title at its start.
    if ( lv && m_introT < 4.0f && m_screen == ScreenE::PLAY )
    {
        float a = std::min( 1.0f, std::min( m_introT * 2.0f, ( 4.0f - m_introT ) * 1.5f ) );
        float y = f.y + f.height * 0.3f;
        ui::text( ( m_mode == ModeE::CAMPAIGN || lv->m_episode > 0 ) && !m_survival && lv->m_name != "bossrush" ? tf( "Episode %d", lv->m_episode ) : "",
                  f.x + f.width * 0.5f, y, 26,
                  { 110, 200, 255, (unsigned char)( 255 * a ) }, ui::AlignE::CENTER );
        ui::text( L( lv->m_title, lv->m_titleEn ), f.x + f.width * 0.5f, y + 40 * k, 58, { 255, 255, 255, (unsigned char)( 255 * a ) }, ui::AlignE::CENTER );
    }
    if ( m_outroT > 0.2f && m_won && !m_replaying )
        ui::text( m_mode == ModeE::ARCADE ? "ABSCHNITT GESCHAFFT" : "EINSATZ ERFÜLLT", f.x + f.width * 0.5f, f.y + f.height * 0.4f, 52, ui::Good, ui::AlignE::CENTER );
    if ( m_replaying )
    {
        float a = 0.6f + 0.4f * std::sin( m_t * 3.0f );
        ui::text( tf( "WIEDERHOLUNG  ·  %dx", m_replaySpeed ), f.x + f.width * 0.5f, f.y + f.height - 50 * k, 26,
                  { 255, 220, 140, (unsigned char)( 255 * a ) }, ui::AlignE::CENTER );
    }
    (void)W;
}

void App::retryBoss()
{
    // Back to the moment the boss came, with full armour, shield and energy; what was earned before stays (the
    // World's part is the replay's too).
    if ( !m_replaying ) m_rec.mark( ReplayMarkE::RETRY );
    restoreBoss( m_world, m_bossSave );
    ++m_bossTries;
    m_bossHit = false;
    m_view.reset();
    m_acc = 0;
    m_paused = false;
    m_radio.clear();
    m_warnT = 0;
    m_themeT = 0;
    m_introT = 10.0f;
    m_outroT = -1;
    int ep = m_world.m_level ? m_world.m_level->m_episode : 0;
    bool last = m_world.m_level && m_world.m_level->m_next.empty() && m_mode == ModeE::CAMPAIGN;
    m_audio.music( ep == 5 && last ? "final" : ep >= 1 && ep <= 5 && m_mode == ModeE::CAMPAIGN ? "e" + std::to_string( ep ) + "_boss" : "boss" );
    m_screen = ScreenE::PLAY;
    m_t = 0;
}

void App::pauseMenu( float dt )
{
    (void)dt;
    if ( m_rumbleT > 0 )
    {
        rumble::stop();
        m_rumbleT = 0.0f;
    }
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    DrawRectangle( 0, 0, (int)W, (int)H, { 0, 0, 0, 140 } );
    ui::text( "PAUSE", W * 0.5f, H * 0.25f, 72, WHITE, ui::AlignE::CENTER );
    std::vector<std::string> items = { "Weiter", "Optionen", m_replaying ? "Wiederholung beenden" : m_mode == ModeE::CAMPAIGN ? "Einsatz abbrechen" : "Spiel beenden" };
    ui::panel( { W * 0.5f - 320 * k, H * 0.38f - 30 * k, 640 * k, 240 * k } );
    int c = menu( items, W * 0.5f, H * 0.38f, 560 * k, m_pauseSel, 36, nullptr, true );
    if ( c == 0 || m_in.back() )
    {
        m_paused = false;
        m_t = 0.0f;
    }
    else if ( c == 1 )
    {
        m_back = ScreenE::PLAY;
        m_screen = ScreenE::OPTIONS;
        m_sel = 0;
    }
    else if ( c == 2 && m_replaying )
    {
        m_paused = false;
        stopReplay();
    }
    else if ( c == 2 )
    {
        m_paused = false;
        logMission( "abort" );
        saveReplay( false );
        if ( m_practice )
        {
            m_practiceStep = 1;
            go( ScreenE::PRACTICE );
        }
        else if ( m_mode == ModeE::CAMPAIGN )
            go( ScreenE::PLAN );
        else
        {
            // An arcade game given up still counts: a good score gets its name.
            for ( int i = 0; i < m_players; ++i ) m_score[i] = m_world.m_players[i].m_score;
            m_scoreKey = diffKey();
            m_name = "PILOT";
            m_nameCursor = 0;
            newDemo();
            go( m_prof.place( m_scoreKey, m_score[0] + ( m_players > 1 ? m_score[1] : 0 ) ) >= 0 ? ScreenE::NAME : ScreenE::MAIN );
        }
    }
}

}   // namespace zenith
