/**
 * @file AppShop.cpp
 * @brief The workshop between the campaign's missions: hulls, front and rear guns (with their eleven power
 *        levels), the two sidekicks, generators, shields and specials. What is fitted is traded in at its full
 *        value. A test range beside it shows the chosen piece firing.
 * @author Rene Weller
 */
#include "App.h"

#include "Ui.h"

#include <raylib.h>

#include <algorithm>
#include <cmath>

namespace zenith
{

namespace
{

const char *TabNames[] = { "Rumpf", "Frontkanone", "Heckkanone", "Begleiter links", "Begleiter rechts", "Generator", "Schild", "Spezial" };
constexpr int Tabs = 8;

/** @brief One line of the workshop's list. */
struct Row
{
    std::string m_name;      ///< the good ("" none, "#power" the power level)
    std::string m_title;
    int         m_price = 0;
    bool        m_fitted = false;
};

float dps( const WeaponDef &w, int lv, int mode ) { return weaponDps( w, lv, mode ); }

}   // namespace

Loadout App::previewLoad() const
{
    return m_camp.m_load;
}

void App::previewSetup()
{
    m_shopWorld.begin( &m_cat, nullptr, ModeE::PREVIEW, 2, 7 );
    m_preview.setTheme( m_cat, "range", 3 );
    m_preview.reset();
}

void App::shop( float dt )
{
    float k = ui::k(), W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    DrawRectangleGradientV( 0, 0, (int)W, (int)H, { 10, 16, 30, 255 }, { 2, 4, 10, 255 } );
    heading( "Werkstatt", "Quills fliegender Basar" );
    Loadout &o = m_camp.m_load;
    // The tabs.
    if ( m_in.prevTab() )
    {
        m_tab = ( m_tab + Tabs - 1 ) % Tabs;
        m_item = 0;
        m_audio.play( SfxE::MOVE );
    }
    if ( m_in.nextTab() )
    {
        m_tab = ( m_tab + 1 ) % Tabs;
        m_item = 0;
        m_audio.play( SfxE::MOVE );
    }
    float tx = 60 * k, ty = 200 * k;
    for ( int t = 0; t < Tabs; ++t )
    {
        Rectangle r = { tx, ty + t * 56 * k, 300 * k, 48 * k };
        if ( CheckCollisionPointRec( GetMousePosition(), r ) && IsMouseButtonPressed( MOUSE_BUTTON_LEFT ) )
        {
            m_tab = t;
            m_item = 0;
        }
        if ( t == m_tab ) ui::highlight( r, m_t );
        ui::text( TabNames[t], r.x + 16 * k, r.y + 8 * k, 28, t == m_tab ? WHITE : ui::Dim );
    }
    ui::text( "Q / E oder LB / RB", tx + 16 * k, ty + Tabs * 56 * k + 10 * k, 20, ui::Dim );

    // The list of the tab's goods.
    std::vector<Row> rows;
    auto value = [&]( const std::string &n ) -> int {
        if ( const WeaponDef *w = m_cat.weapon( n ) ) return weaponValue( *w, n == o.m_front ? o.m_frontLv : n == o.m_rear ? o.m_rearLv : 1 );
        if ( const ShipDef *s = m_cat.ship( n ) ) return s->m_price;
        if ( const SidekickDef *s = m_cat.sidekick( n ) ) return s->m_price;
        if ( const GeneratorDef *g = m_cat.generator( n ) ) return g->m_price;
        if ( const ShieldDef *s = m_cat.shield( n ) ) return s->m_price;
        if ( const SpecialDef *s = m_cat.special( n ) ) return s->m_price;
        return 0;
    };
    std::string *slot = nullptr;
    switch ( m_tab )
    {
    case 0:
        slot = &o.m_ship;
        for ( const auto &[n, s] : m_cat.m_ships )
            if ( available( n, s.m_tier, s.m_hidden ) ) rows.push_back( { n, s.m_title, s.m_price, n == o.m_ship } );
        break;
    case 1:
    case 2:
    {
        slot = m_tab == 1 ? &o.m_front : &o.m_rear;
        const WeaponDef *cur = slot->empty() ? nullptr : m_cat.weapon( *slot );
        if ( cur ) rows.push_back( { "#power", "Leistung", 0, false } );
        if ( m_tab == 2 ) rows.push_back( { "", "– keine –", 0, o.m_rear.empty() } );
        for ( const auto &[n, w] : m_cat.m_weapons )
            if ( w.m_slot == ( m_tab == 1 ? "front" : "rear" ) && available( n, w.m_tier, w.m_hidden ) )
                rows.push_back( { n, w.m_title, w.m_price, n == *slot } );
        break;
    }
    case 3:
    case 4:
        slot = m_tab == 3 ? &o.m_sideL : &o.m_sideR;
        rows.push_back( { "", "– keiner –", 0, slot->empty() } );
        for ( const auto &[n, s] : m_cat.m_sidekicks )
            if ( available( n, s.m_tier, s.m_hidden ) ) rows.push_back( { n, s.m_title, s.m_price, n == *slot } );
        break;
    case 5:
        slot = &o.m_gen;
        for ( const auto &[n, g] : m_cat.m_generators )
            if ( available( n, g.m_tier, g.m_hidden ) ) rows.push_back( { n, g.m_title, g.m_price, n == o.m_gen } );
        break;
    case 6:
        slot = &o.m_shield;
        for ( const auto &[n, s] : m_cat.m_shields )
            if ( available( n, s.m_tier, s.m_hidden ) ) rows.push_back( { n, s.m_title, s.m_price, n == o.m_shield } );
        break;
    case 7:
        slot = &o.m_special;
        rows.push_back( { "", "– keines –", 0, o.m_special.empty() } );
        for ( const auto &[n, s] : m_cat.m_specials )
            if ( available( n, s.m_tier, s.m_hidden ) ) rows.push_back( { n, s.m_title, s.m_price, n == o.m_special } );
        break;
    }
    std::stable_sort( rows.begin(), rows.end(), []( const Row &a, const Row &b ) {
        bool fa = a.m_name.empty() || a.m_name[0] == '#', fb = b.m_name.empty() || b.m_name[0] == '#';
        if ( fa != fb ) return fa;
        return a.m_price < b.m_price;
    } );
    int n = (int)rows.size();
    m_item = std::clamp( m_item, 0, std::max( 0, n - 1 ) );
    if ( m_in.up() && n > 0 )
    {
        m_item = ( m_item + n - 1 ) % n;
        m_audio.play( SfxE::MOVE );
    }
    if ( m_in.down() && n > 0 )
    {
        m_item = ( m_item + 1 ) % n;
        m_audio.play( SfxE::MOVE );
    }
    Rectangle list = { 400 * k, 190 * k, 760 * k, H - 330 * k };
    ui::panel( list, 0.7f );
    float rowH = 50 * k;
    int   rowsFit = std::max( 1, (int)( ( list.height - 20 * k ) / rowH ) );
    int   first = std::clamp( m_item - rowsFit / 2, 0, std::max( 0, n - rowsFit ) );
    long long credits = m_camp.m_credits;
    int curValue = slot && !slot->empty() ? value( *slot ) : 0;
    for ( int i = first; i < std::min( n, first + rowsFit ); ++i )
    {
        const Row &rw = rows[(size_t)i];
        float     y = list.y + 12 * k + ( i - first ) * rowH;
        Rectangle r = { list.x + 10 * k, y, list.width - 20 * k, rowH - 6 * k };
        if ( CheckCollisionPointRec( GetMousePosition(), r ) && m_in.mouseMoved() ) m_item = i;
        if ( i == m_item ) ui::highlight( r, m_t );
        if ( rw.m_name == "#power" )
        {
            const WeaponDef *w = m_cat.weapon( *slot );
            int &lv = m_tab == 1 ? o.m_frontLv : o.m_rearLv;
            ui::text( "Leistung", r.x + 16 * k, y + 8 * k, 26, ui::Ink );
            ui::pips( r.x + 170 * k, y + 10 * k, lv, 11, 14, m_tab == 1 ? Color{ 120, 210, 255, 255 } : Color{ 255, 190, 90, 255 } );
            if ( w && lv < 11 ) ui::text( "+" + fmt( upgradePrice( *w, lv ) ) + " Kr", r.x + r.width - 16 * k, y + 8 * k, 24, ui::Gold, ui::AlignE::RIGHT );
            continue;
        }
        int   cost = rw.m_price - curValue;
        Color c = rw.m_fitted ? ui::Good : ( !rw.m_name.empty() && cost > credits ) ? Color{ 150, 90, 90, 255 } : ui::Ink;
        ui::text( rw.m_title, r.x + 16 * k, y + 8 * k, 26, c );
        if ( rw.m_fitted )
            ui::text( "eingebaut", r.x + r.width - 16 * k, y + 8 * k, 24, ui::Good, ui::AlignE::RIGHT );
        else if ( !rw.m_name.empty() )
            ui::text( fmt( rw.m_price ) + " Kr", r.x + r.width - 16 * k, y + 8 * k, 24, cost > credits ? Color{ 150, 90, 90, 255 } : ui::Gold, ui::AlignE::RIGHT );
    }

    // The selected good: its numbers and what it does.
    const Row *sel = n > 0 ? &rows[(size_t)m_item] : nullptr;
    Rectangle info = { 1180 * k, 190 * k, W - 1240 * k, 300 * k };
    Rectangle prev = { 1180 * k, 500 * k, W - 1240 * k, H - 640 * k };
    bool wide = info.width > 300 * k;
    if ( !wide )
    {
        info = { list.x, list.y + list.height - 200 * k, list.width, 0 };
        prev = { 0, 0, 0, 0 };
    }
    ui::text( "Kredite: " + fmt( credits ), W - 60 * k, 140 * k, 34, ui::Gold, ui::AlignE::RIGHT );
    Loadout test = o;
    if ( sel && wide )
    {
        ui::panel( info, 0.7f );
        float x = info.x + 24 * k, y = info.y + 18 * k;
        std::vector<std::string> lines;
        std::string desc;
        std::string name = sel->m_name == "#power" ? *slot : sel->m_name;
        if ( m_tab == 0 )
        {
            if ( const ShipDef *s = m_cat.ship( name ) )
            {
                desc = s->m_desc;
                lines.push_back( "Panzerung " + std::to_string( (int)s->m_armor ) + "    Tempo " + std::to_string( (int)s->m_speed ) );
                lines.push_back( "Trefferzone " + std::to_string( (int)s->m_radius ) + ( s->m_genBonus > 0 ? "    Generator +" + std::to_string( (int)( s->m_genBonus * 100 ) ) + " %" : "" ) );
                test.m_ship = name;
            }
        }
        else if ( m_tab == 1 || m_tab == 2 )
        {
            if ( const WeaponDef *w = m_cat.weapon( name ) )
            {
                desc = w->m_desc;
                int lv = name == *slot ? ( m_tab == 1 ? o.m_frontLv : o.m_rearLv ) : 1;
                char buf[160];
                snprintf( buf, sizeof( buf ), "Stufe %d:  ~%d Schaden/s    Energie %.0f je Salve", lv, (int)dps( *w, lv, 1 ), w->m_energy.at( ( lv - 1 ) / 10.0f ) );
                lines.push_back( buf );
                snprintf( buf, sizeof( buf ), "Stufe 11: ~%d Schaden/s", (int)dps( *w, 11, 1 ) );
                lines.push_back( buf );
                if ( w->m_modes > 1 ) lines.push_back( "Modi: " + w->m_mode1 + " / " + w->m_mode2 );
                if ( m_tab == 1 ) test.m_front = name, test.m_frontLv = lv;
                else test.m_rear = name, test.m_rearLv = lv;
            }
        }
        else if ( m_tab == 3 || m_tab == 4 )
        {
            if ( const SidekickDef *s = m_cat.sidekick( name ) )
            {
                desc = s->m_desc;
                char buf[160];
                snprintf( buf, sizeof( buf ), "%.1f Schuss/s    %s    %s", s->m_rate, s->m_aim == "nearest" ? "zielt selbst" : "feuert nach vorn",
                          s->m_ammo > 0 ? ( std::to_string( s->m_ammo ) + " Schuss" ).c_str() : s->m_charge > 0 ? "lädt auf" : "ohne Munitionsgrenze" );
                lines.push_back( buf );
                ( m_tab == 3 ? test.m_sideL : test.m_sideR ) = name;
            }
            else
                ( m_tab == 3 ? test.m_sideL : test.m_sideR ) = "";
        }
        else if ( m_tab == 5 )
        {
            if ( const GeneratorDef *g = m_cat.generator( name ) )
            {
                desc = g->m_desc;
                lines.push_back( "Leistung " + std::to_string( (int)g->m_power ) + " je Sekunde    Speicher " + std::to_string( (int)g->m_capacity ) );
                test.m_gen = name;
            }
        }
        else if ( m_tab == 6 )
        {
            if ( const ShieldDef *s = m_cat.shield( name ) )
            {
                desc = s->m_desc;
                char buf[160];
                snprintf( buf, sizeof( buf ), "Stärke %d    lädt %.1f/s    kostet %.1f Energie je Punkt", (int)s->m_max, s->m_regen, s->m_cost );
                lines.push_back( buf );
                test.m_shield = name;
            }
        }
        else if ( m_tab == 7 )
        {
            if ( const SpecialDef *s = m_cat.special( name ) )
            {
                desc = s->m_desc;
                lines.push_back( std::to_string( s->m_charges ) + " Ladungen je Einsatz" );
                test.m_special = name;
            }
        }
        ui::text( sel->m_title, x, y, 32, WHITE );
        y += 46 * k;
        for ( const std::string &l : lines )
        {
            ui::text( l, x, y, 22, ui::Accent );
            y += 30 * k;
        }
        for ( const std::string &l : ui::wrap( desc, info.width - 48 * k, 22 ) )
        {
            if ( y > info.y + info.height - 30 * k ) break;
            ui::text( l, x, y, 22, ui::Ink );
            y += 28 * k;
        }
    }

    // The test range: the loadout with the selected good, firing at targets.
    if ( wide && prev.height > 100 * k )
    {
        std::string key = test.m_ship + test.m_front + std::to_string( test.m_frontLv ) + test.m_rear + std::to_string( test.m_rearLv ) +
                          test.m_sideL + test.m_sideR + test.m_gen + test.m_shield;
        if ( key != m_previewKey )
        {
            m_previewKey = key;
            previewSetup();
            m_shopWorld.addPlayer( 0, test, 0 );
            m_previewT = 0;
        }
        static EnemyDef target;
        if ( target.m_name.empty() )
        {
            target.m_name = "target";
            target.m_model = m_cat.model( "target" ) ? "target" : "drone";
            target.m_hp = 60;
            target.m_radius = 18;
            target.m_score = 0;
            target.m_face = "down";
            target.m_path.m_key = "still";
        }
        // Keep five targets standing across the upper field.
        int alive = 0;
        for ( const Enemy &e : m_shopWorld.m_enemies )
            if ( !e.m_dead && !e.m_gone ) ++alive;
        if ( alive < 5 && (int)( m_previewT * 60 ) % 20 == 0 )
        {
            Line still;
            still.m_kv["path"] = "still";
            int i = m_shopWorld.spawn( &target, { 120.0f + ( m_shopWorld.m_nextId % 5 ) * 120.0f, 120.0f + ( m_shopWorld.m_nextId % 3 ) * 70.0f }, &still );
            if ( i >= 0 ) m_shopWorld.m_enemies[(size_t)i].m_entered = true;
        }
        m_previewT += dt;
        PlayerInput in[2];
        in[0].m_fire = true;
        in[0].m_x = std::sin( m_previewT * 1.3f ) * 0.6f;
        in[0].m_y = 0;
        static float acc = 0;
        acc += dt;
        int steps = 0;
        while ( acc >= Dt && steps++ < 4 )
        {
            m_shopWorld.m_players[0].m_armor = m_shopWorld.m_players[0].m_armorMax;
            m_shopWorld.step( in );
            m_preview.events( m_shopWorld );
            acc -= Dt;
        }
        if ( acc > Dt ) acc = 0;
        m_preview.update( m_shopWorld, dt );
        m_preview.draw( m_shopWorld, m_cat );
        float     s = std::min( prev.width / m_preview.width(), prev.height / m_preview.height() );
        Rectangle dst = { prev.x + ( prev.width - m_preview.width() * s ) * 0.5f, prev.y, m_preview.width() * s, m_preview.height() * s };
        m_preview.present( dst );
        DrawRectangleLinesEx( dst, 2 * k, { 90, 170, 230, 160 } );
        ui::text( "Testgelände", dst.x + 12 * k, dst.y + 8 * k, 20, ui::Dim );
    }

    // Buying, fitting, the power level.
    if ( m_shopMsgT > 0 )
    {
        m_shopMsgT -= dt;
        ui::text( m_shopMsg, list.x + list.width * 0.5f, list.y + list.height + 16 * k, 26, ui::Gold, ui::AlignE::CENTER );
    }
    hints( "Hoch/Runter: wählen    Eingabe / A: kaufen und einbauen    Links/Rechts: Leistung    Esc / B: fertig" );
    if ( m_in.back() )
    {
        saveCampaign();
        m_previewKey.clear();
        go( ScreenE::PLAN );
        return;
    }
    if ( !sel || !slot ) return;
    auto say = [&]( const std::string &s ) {
        m_shopMsg = s;
        m_shopMsgT = 2.0f;
    };
    if ( sel->m_name == "#power" )
    {
        const WeaponDef *w = m_cat.weapon( *slot );
        int &lv = m_tab == 1 ? o.m_frontLv : o.m_rearLv;
        if ( w && ( m_in.right() || m_in.accept() ) )
        {
            if ( lv >= 11 ) say( "Höchste Stufe erreicht." );
            else if ( upgradePrice( *w, lv ) > m_camp.m_credits )
            {
                say( "Nicht genug Kredite." );
                m_audio.play( SfxE::DENY );
            }
            else
            {
                m_camp.m_credits -= upgradePrice( *w, lv );
                ++lv;
                m_audio.play( SfxE::BUY );
            }
        }
        if ( w && m_in.left() && lv > 1 )
        {
            --lv;
            m_camp.m_credits += upgradePrice( *w, lv );
            m_audio.play( SfxE::BACK );
        }
        return;
    }
    if ( m_in.accept() && !sel->m_fitted )
    {
        int cost = sel->m_price - curValue;
        if ( !sel->m_name.empty() && cost > m_camp.m_credits )
        {
            say( "Nicht genug Kredite." );
            m_audio.play( SfxE::DENY );
            return;
        }
        m_camp.m_credits -= cost;
        *slot = sel->m_name;
        if ( m_tab == 1 ) o.m_frontLv = 1;
        if ( m_tab == 2 ) o.m_rearLv = 1;
        if ( !sel->m_name.empty() ) m_camp.m_owned.insert( sel->m_name );
        m_audio.play( SfxE::BUY );
        say( sel->m_name.empty() ? "Ausgebaut." : sel->m_title + " eingebaut." );
    }
}

}   // namespace zenith
