/**
 * @file Survival.h
 * @brief The survival mode: one long mission made of the campaign's own waves, rising from episode 1's enemies to
 *        episode 5's and beyond, a boss every three minutes, a new ground every six. It is flown under arcade rules
 *        (lives, power-ups from the enemies). The same seed gives the same mission; the daily seed is the same for
 *        every player on the same day.
 * @author Rene Weller
 */
#pragma once

#include "Data.h"

#include <cstdint>
#include <string>

namespace zenith
{

/**
 * @brief Builds the survival mission: an hour of waves borrowed from the campaign's missions, picked by a tier that
 *        rises with the time, ever denser and more mixed; a mini-boss or a great boss every three minutes (a warning
 *        and the boss music before, a shield and armour after), and every six minutes a ground of the episode the
 *        waves have come to, with that episode's music.
 * @param[in] cat   the catalog (its missions are where the waves, bosses and grounds come from)
 * @param[in] seed  the run's seed: the same seed gives the same mission
 * @return the mission (name "survival")
 */
LevelDef survival( const Catalog &cat, uint32_t seed );

/**
 * @brief The seed of today: the local date as YYYYMMDD, hashed. Every player gets the same one on the same day.
 * @return the seed
 */
uint32_t dailySeed();

/**
 * @brief The week of today as ISO year and week ("2026-W44"): the weekly flight's name.
 * @return the week
 */
std::string thisWeek();

/**
 * @brief The seed of a week: its name hashed. Every player gets the same one in the same week.
 * @param[in] week  thisWeek()'s text
 * @return the seed
 */
uint32_t weekSeed( const std::string &week );

/**
 * @brief The weekly flight's rules: two of the RuleE bits (Sim.h), picked by the week's seed.
 * @param[in] seed  weekSeed()
 * @return the bits
 */
int weeklyRules( uint32_t seed );

/**
 * @brief A challenge's code: a survival flight's seed, difficulty and rules in ten letters of Crockford's base 32
 *        ("7KQ2M-X0RTB"), with a check of five bits, so a friend flies exactly the same waves.
 * @param[in] seed   the World's seed
 * @param[in] diff   the difficulty's index (0..7)
 * @param[in] rules  the RuleE bits (0..255)
 * @return the code
 */
std::string challengeCode( uint32_t seed, int diff, int rules );

/**
 * @brief Reads a challenge's code (blanks, dashes and case do not matter; O and I count as 0 and 1).
 * @param[in]  code   the code
 * @param[out] seed   the World's seed
 * @param[out] diff   the difficulty's index
 * @param[out] rules  the RuleE bits
 * @return false for a code that is not one
 */
bool readChallenge( const std::string &code, uint32_t &seed, int &diff, int &rules );

}   // namespace zenith
