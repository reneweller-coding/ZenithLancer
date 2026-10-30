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

}   // namespace zenith
