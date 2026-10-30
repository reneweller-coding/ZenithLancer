/**
 * @file Forge.h
 * @brief Builds the ships, enemies and pickups from their model lines (data/models): hulls lofted along their
 *        length, wings, fins, engines with glowing nozzles, domes, guns, rings, crystals, greebles. Flat-shaded
 *        facets with a little brightness noise per face; a colour's alpha carries how much it glows. A model has up
 *        to three meshes: the base, a head that turns to aim, and a part that spins.
 *
 *        Model space: x to the right, y up towards the camera, -z is the nose (a pilot's ship flies up the screen).
 *        One unit is one field unit.
 * @author Rene Weller
 */
#pragma once

#include "Data.h"

#include <raylib.h>

#include <map>
#include <string>
#include <vector>

namespace zenith
{

/** @brief A built model. */
struct ModelMesh
{
    Mesh                 m_base = {};
    Mesh                 m_head = {};
    Mesh                 m_spin = {};
    bool                 m_hasBase = false;
    bool                 m_hasHead = false;
    bool                 m_hasSpin = false;
    Vector3              m_pivot = { 0, 0, 0 };   ///< where the head turns
    float                m_spinSpeed = 90.0f;     ///< degrees a second
    float                m_radius = 20.0f;        ///< how far it reaches from its middle
    std::vector<Vector3> m_engines;               ///< nozzles (for the exhaust)
    std::vector<Color>   m_engineColors;
};

/** @brief The workshop of meshes: builds a model when it is first asked for and keeps it. */
class Forge
{
public:
    /** @brief Lets go of every mesh. */
    void clear();

    /**
     * @brief A model, built on first use.
     * @param[in] cat   the catalog with the model lines
     * @param[in] name  the model's name (an unknown name gets a plain stand-in)
     * @return the meshes
     */
    const ModelMesh &get( const Catalog &cat, const std::string &name );

    /**
     * @brief Builds a model's meshes (without uploading them: for counting and tests).
     * @param[in] def     the model
     * @param[in] upload  send to the graphics card
     * @return the meshes
     */
    static ModelMesh build( const ModelDef &def, bool upload = true );

private:
    std::map<std::string, ModelMesh> m_cache;
};

}   // namespace zenith
