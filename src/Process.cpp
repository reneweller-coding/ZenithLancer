/**
 * @file Process.cpp
 * @brief Child programs in the background (kept apart from raylib: windows.h and raylib.h do not mix).
 * @author Rene Weller
 */
#include "Process.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
#endif

namespace zenith
{

#ifdef _WIN32

struct ChildPipe
{
    HANDLE m_in = nullptr;        ///< our end of the child's standard input
    HANDLE m_process = nullptr;
};

namespace
{

/** @brief Starts a command line without a window; its standard input from stdinRead (or none). */
HANDLE start( const std::string &command, HANDLE stdinRead )
{
    STARTUPINFOA si = {};
    si.cb = sizeof( si );
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = stdinRead ? stdinRead : GetStdHandle( STD_INPUT_HANDLE );
    si.hStdOutput = GetStdHandle( STD_OUTPUT_HANDLE );
    si.hStdError = GetStdHandle( STD_ERROR_HANDLE );
    PROCESS_INFORMATION pi = {};
    std::vector<char>   line( command.begin(), command.end() );
    line.push_back( 0 );
    if ( !CreateProcessA( nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr,
                          nullptr, &si, &pi ) )
        return nullptr;
    CloseHandle( pi.hThread );
    return pi.hProcess;
}

int wait( HANDLE process )
{
    WaitForSingleObject( process, INFINITE );
    DWORD code = 0;
    if ( !GetExitCodeProcess( process, &code ) ) code = (DWORD)-1;
    CloseHandle( process );
    return (int)code;
}

}   // namespace

ChildPipe *startPiped( const std::string &command )
{
    SECURITY_ATTRIBUTES sa = { sizeof( sa ), nullptr, TRUE };
    HANDLE              read = nullptr, write = nullptr;
    if ( !CreatePipe( &read, &write, &sa, 1 << 20 ) ) return nullptr;
    // Only the child's end is inherited.
    SetHandleInformation( write, HANDLE_FLAG_INHERIT, 0 );
    HANDLE process = start( command, read );
    CloseHandle( read );
    if ( !process )
    {
        CloseHandle( write );
        return nullptr;
    }
    ChildPipe *c = new ChildPipe();
    c->m_in = write;
    c->m_process = process;
    return c;
}

bool writePiped( ChildPipe *child, const void *data, size_t size )
{
    const char *p = (const char *)data;
    while ( size > 0 )
    {
        DWORD done = 0;
        if ( !WriteFile( child->m_in, p, (DWORD)std::min<size_t>( size, 1u << 24 ), &done, nullptr ) || done == 0 ) return false;
        p += done;
        size -= done;
    }
    return true;
}

int finishPiped( ChildPipe *child )
{
    CloseHandle( child->m_in );
    int code = wait( child->m_process );
    delete child;
    return code;
}

int runQuiet( const std::string &command )
{
    HANDLE process = start( command, nullptr );
    return process ? wait( process ) : -1;
}

#else

struct ChildPipe
{
    FILE *m_pipe = nullptr;
};

ChildPipe *startPiped( const std::string &command )
{
    FILE *p = popen( command.c_str(), "w" );
    if ( !p ) return nullptr;
    ChildPipe *c = new ChildPipe();
    c->m_pipe = p;
    return c;
}

bool writePiped( ChildPipe *child, const void *data, size_t size ) { return fwrite( data, 1, size, child->m_pipe ) == size; }

int finishPiped( ChildPipe *child )
{
    int code = pclose( child->m_pipe );
    delete child;
    return code;
}

int runQuiet( const std::string &command ) { return std::system( command.c_str() ); }

#endif

}   // namespace zenith
