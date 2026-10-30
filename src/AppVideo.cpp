/**
 * @file AppVideo.cpp
 * @brief Two runs without a player: a replay (or a mission flown by the autopilot) rendered frame by frame into a
 *        video through ffmpeg, the music of the mission under it; and the Steam Deck check, every screen drawn at
 *        the window's size with the smallest text measured.
 * @author Rene Weller
 */
#include "App.h"

#include "Files.h"
#include "Ui.h"

#include <raylib.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace zenith
{

namespace fs = std::filesystem;

int App::video()
{
    const int W = GetScreenWidth(), H = GetScreenHeight(), fps = std::clamp( m_opt.m_videoFps, 10, 120 );
    // The game's pace as made, whatever the aids say.
    m_set.m_speed = 1.0f;
    if ( !m_opt.m_replay.empty() )
    {
        ReplayPlayer probe;
        if ( !probe.load( m_opt.m_replay ) )
        {
            printf( "cannot play %s: %s\n", m_opt.m_replay.c_str(), probe.error().c_str() );
            return 1;
        }
        ReplayHeader h = probe.header();
        h.m_path = m_opt.m_replay;
        startReplay( h );
        if ( !m_replaying )
        {
            printf( "the replay's mission %s is not in the data\n", h.m_mission.c_str() );
            return 1;
        }
    }
    else if ( m_screen != ScreenE::PLAY )
    {
        printf( "--video needs --replay FILE or --level NAME\n" );
        return 1;
    }
    std::string out = fs::absolute( m_opt.m_video ).string(), tmp = out + ".video.mp4";
    char        cmd[2048];
    snprintf( cmd, sizeof( cmd ),
              "ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgba -s %dx%d -r %d -i - -vf vflip -c:v libx264 -preset medium "
              "-crf 18 -pix_fmt yuv420p \"%s\"",
              W, H, fps, tmp.c_str() );
    FILE *pipe = popen( cmd, "wb" );
    if ( !pipe )
    {
        printf( "cannot start ffmpeg (is it on the PATH?)\n" );
        return 1;
    }
    // Drawn into a picture of its own, read back and handed to ffmpeg; the pieces the music played noted with
    // their frames. A mission flown by the autopilot stops before its end saves anything.
    RenderTexture2D rt = LoadRenderTexture( W, H );
    Renderer::s_outer = &rt;
    std::vector<std::pair<int, std::string>> music;
    int  most = m_opt.m_videoSeconds > 0.0f ? (int)( m_opt.m_videoSeconds * fps ) : 3600 * fps;
    int  frames = 0;
    bool ok = true;
    for ( ; frames < most && m_screen == ScreenE::PLAY && !( !m_replaying && m_outroT > 2.0f ); ++frames )
    {
        m_in.update();
        BeginDrawing();
        BeginTextureMode( rt );
        ClearBackground( BLACK );
        frame( 1.0f / fps );
        EndTextureMode();
        EndDrawing();
        m_t += 1.0f / fps;
        if ( music.empty() || music.back().second != m_audio.piece() ) music.push_back( { frames, m_audio.piece() } );
        Image img = LoadImageFromTexture( rt.texture );
        ok = ok && img.data && fwrite( img.data, 1, (size_t)W * H * 4, pipe ) == (size_t)W * H * 4;
        UnloadImage( img );
        if ( !ok ) break;
        if ( frames % ( fps * 10 ) == 0 ) printf( "%d s\n", frames / fps ), fflush( stdout );
    }
    Renderer::s_outer = nullptr;
    UnloadRenderTexture( rt );
    ok = pclose( pipe ) == 0 && ok;
    if ( !ok || frames == 0 )
    {
        printf( "ffmpeg failed\n" );
        return 1;
    }
    // The music: each piece from its start for as long as it played, one after the other (silence where none did).
    std::string inputs, filter, joined;
    int         n = 0;
    for ( size_t i = 0; i < music.size(); ++i )
    {
        float       dur = ( ( i + 1 < music.size() ? music[i + 1].first : frames ) - music[i].first ) / (float)fps;
        std::string file = fs::absolute( m_assets + "/music/" + music[i].second + ".ogg" ).string();
        if ( dur <= 0.0f ) continue;
        if ( !music[i].second.empty() && fs::exists( file ) )
            inputs += " -stream_loop -1 -i \"" + file + "\"";
        else
            inputs += " -f lavfi -t " + std::to_string( dur ) + " -i anullsrc=r=48000:cl=stereo";
        char part[256];
        snprintf( part, sizeof( part ), "[%d:a]atrim=0:%.3f,asetpts=N/SR/TB,aresample=48000,afade=t=in:d=0.3,afade=t=out:st=%.3f:d=0.6[a%d];",
                  n + 1, dur, std::max( 0.0f, dur - 0.6f ), n );
        filter += part;
        joined += "[a" + std::to_string( n ) + "]";
        ++n;
    }
    std::error_code e;
    if ( n == 0 )
    {
        fs::rename( tmp, out, e );
        return e ? 1 : 0;
    }
    std::string mux = "ffmpeg -y -loglevel error -i \"" + tmp + "\"" + inputs + " -filter_complex \"" + filter + joined +
                      "concat=n=" + std::to_string( n ) + ":v=0:a=1[aout]\" -map 0:v -map \"[aout]\" -c:v copy -c:a aac -b:a 192k -shortest \"" +
                      out + "\"";
    int r = std::system( mux.c_str() );
    fs::remove( tmp, e );
    printf( "%s: %d frames, %.1f s, %d piece(s) of music\n", r == 0 ? out.c_str() : "FAILED", frames, frames / (float)fps, n );
    return r == 0 ? 0 : 1;
}

int App::deckCheck()
{
    // Each screen as --screen draws it, at the window's size (1280x800 by default) and the Deck's text size; the
    // pictures go to a folder of their own to look at.
    const char *names[] = { "title", "main",    "options",  "controls", "scores",   "credits", "logbook", "slots", "difficulty",
                            "episode", "story", "plan",     "shop",     "pause",    "results", "failed",  "pilots", "continue",
                            "name",  "practice", "survival", "replays", "stats",    "mods",    "archive" };
    std::error_code e;
    fs::path        dir = fs::temp_directory_path( e ) / "zenith_deckcheck";
    fs::create_directories( dir, e );
    m_set.m_textSize = 1.0f;
    ui::setTextSize( 1.12f );
    float       worst = 1e9f;
    std::string worstAt;
    printf( "Steam Deck check at %dx%d (smallest text a screen draws, font size in pixels)\n", GetScreenWidth(), GetScreenHeight() );
    for ( const char *n : names )
    {
        m_opt.m_screen = n;
        m_opt.m_shot = ( dir / ( std::string( n ) + ".png" ) ).string();
        ui::resetTextStats();
        screenShot();
        std::string what;
        float       px = ui::smallestText( what );
        printf( "%-10s %5.1f px  %s  \"%s\"\n", n, px, px < 9.0f ? "FAIL" : px < 12.0f ? "small" : "ok  ", what.substr( 0, 50 ).c_str() );
        if ( px > 0.0f && px < worst )
        {
            worst = px;
            worstAt = n;
        }
    }
    printf( "smallest: %.1f px on %s (Valve asks for 9 px at 1280x800); pictures in %s\n", worst, worstAt.c_str(), dir.string().c_str() );
    return worst < 9.0f ? 1 : 0;
}

}   // namespace zenith
