/**
 * @file Input.h
 * @brief Keys, pads and the mouse: the pilots' actions (each with two keys and two pad buttons, rebound in the
 *        options), the menus' fixed keys with repeat, and which pad belongs to which pilot (one pilot: the keys and
 *        every pad; two pilots: the first pad or the keys for the first, the second pad for the second).
 * @author Rene Weller
 */
#pragma once

#include "Sim.h"

#include <string>
#include <vector>

namespace zenith
{

/** @brief What can be bound. */
enum class ActE : int
{
    UP, DOWN, LEFT, RIGHT, FIRE, FOCUS, MODE, SPECIAL, PAUSE, COUNT
};

/** @brief One action's keys and pad buttons (0: none). */
struct Bind
{
    int m_key = 0, m_key2 = 0;
    int m_pad = 0, m_pad2 = 0;
};

/** @brief The input. */
class Input
{
public:
    Input();

    /** @brief Once a frame before anything reads it. */
    void update();

    /**
     * @brief A pilot's input for the next steps.
     * @param[in] i          0 or 1
     * @param[in] players    how many fly
     * @param[in] mouse      where the mouse points on the field (nullptr: the mouse does not steer)
     * @param[in] autofire   the trigger toggles instead of being held
     * @return the input
     */
    PlayerInput player( int i, int players, const Vector2 *mouse, bool autofire );

    /** @brief Clears what was pressed (a step consumed it). */
    void consumed();

    /** @return the menu moves (with key repeat) */
    bool up();
    bool down();
    bool left();
    bool right();
    /** @return accept (Enter, Space, A) */
    bool accept();
    /** @return back (Esc, Backspace, B) */
    bool back();
    /** @return the pause (Esc, P, Start) */
    bool pause();
    /** @return the shop's tabs (Q/E, LB/RB) */
    bool prevTab();
    bool nextTab();
    /** @return the other choice of a screen (Tab, Y): the workshop's other pilot, a replay's speed */
    bool other();

    /** @return a label for an action's binding ("Leertaste / Z", "A / RT") */
    std::string label( ActE a, bool pad ) const;

    /** @return a key pressed this frame (0 none), for rebinding */
    static int capturedKey();
    /** @return a pad button pressed this frame (0 none) */
    int capturedPad() const;

    /** @return the settings lines of the bindings */
    std::vector<std::string> save() const;
    /** @brief Reads one settings line ("bind <action> key key2 pad pad2"). */
    void load( const std::string &line );
    /** @brief Back to the defaults. */
    void reset();

    /** @return an action's name */
    static const char *name( ActE a );
    /** @return a key's label */
    static std::string keyLabel( int key );
    /** @return a pad button's label */
    static std::string padLabel( int button );

    /** @return whether a pad was used last (the menus show its buttons) */
    bool padUsed() const { return m_padUsed; }

    /** @return whether the mouse moved this frame */
    bool mouseMoved() const { return m_mouseMoved; }

    /**
     * @brief Plays a script instead of the keys (the app test): one word a frame, each followed by idle frames.
     * @param[in] words  accept back up down left right pause prevtab nexttab, or anything else to wait a frame
     */
    void script( const std::vector<std::string> &words );

    /** @return whether a script plays */
    bool scripted() const { return m_scripted; }

    /** @return the scripted word of this frame ("" none) */
    const std::string &word() const { return m_word; }

    Bind m_binds[(int)ActE::COUNT];

private:
    bool held( ActE a, int mask, bool keys ) const;
    bool pressedAct( ActE a, int mask, bool keys ) const;
    void route( int i, int &mask, bool &keys ) const;
    bool repeat( bool down, int slot );
    bool padButtonDown( int pad, int b ) const;
    bool padButtonPressed( int pad, int b ) const;

    float m_rep[4] = { 0, 0, 0, 0 };
    bool  m_prev[4] = { false, false, false, false };
    bool  m_fireToggle[2] = { false, false };
    bool  m_mode[2] = { false, false }, m_special[2] = { false, false };
    bool  m_padUsed = false;
    bool  m_mouseMoved = false;
    bool  m_trigPrev[4][2] = {};
    bool  m_trigNow[4][2] = {};
    int   m_players = 1;
    bool  m_scripted = false;
    std::vector<std::string> m_script;
    size_t m_scriptAt = 0;
    int    m_idle = 0;
    std::string m_word;
    bool take( const char *w );
};

}   // namespace zenith
