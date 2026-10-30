/**
 * @file Process.h
 * @brief Child programs started in the background (ffmpeg for the videos): on Windows without a console window of
 *        their own (the game is a window program, so popen and system would open one), elsewhere through the shell.
 * @author Rene Weller
 */
#pragma once

#include <cstddef>
#include <string>

namespace zenith
{

/** @brief A child program whose standard input the caller writes to. */
struct ChildPipe;

/**
 * @brief Starts a command line whose standard input is a pipe.
 * @param[in] command  the command line (the program found on the PATH)
 * @return the child (nullptr if it could not be started)
 */
ChildPipe *startPiped( const std::string &command );

/**
 * @brief Writes to the child's standard input.
 * @param[in] child  the child
 * @param[in] data   the bytes
 * @param[in] size   how many
 * @return whether all were written
 */
bool writePiped( ChildPipe *child, const void *data, size_t size );

/**
 * @brief Closes the child's input and waits for it to end.
 * @param[in] child  the child (freed)
 * @return its exit code (-1 if it could not be told)
 */
int finishPiped( ChildPipe *child );

/**
 * @brief Runs a command line to its end in the background.
 * @param[in] command  the command line
 * @return its exit code (-1 if it could not be started)
 */
int runQuiet( const std::string &command );

}   // namespace zenith
