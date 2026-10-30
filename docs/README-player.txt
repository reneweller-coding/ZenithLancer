ZENITH LANCER
=============

Ein Shoot 'em up mit Werkstatt: fünf Episoden Kampagne mit verzweigten Einsätzen,
Geheimmissionen und Logkristallen, ein Arcade-Modus für einen oder zwei Spieler,
eine Bossjagd und sieben Schwierigkeitsgrade. Deutsch und Englisch (Optionen > Sprache).

Starten
-------
Windows:     ZenithLancer.exe
Linux:       ./ZenithLancer
Steam Deck:  Im Desktop-Modus entpacken, in Steam "Ein Nicht-Steam-Spiel hinzufügen"
             und ZenithLancer auswählen. Das Spiel erkennt das Deck, startet im
             Vollbild (1280 x 800) und spielt sich mit den Deck-Knöpfen
             (Steuerungsvorlage "Gamepad").

Steuerung
---------
                     Tastatur              Gamepad / Steam Deck
Fliegen              Pfeile oder W A S D   linker Stick, Steuerkreuz
Feuer                Leertaste, J          A oder RT
Langsam (präzise)    Umschalt, K           LB oder LT
Heckwaffe umschalten X, L                  B oder RB
Spezialwaffe         C, I                  X oder Y
Pause                Esc, P                Start
Vollbild             F11 (oder Alt+Eingabe)
Bildschirmfoto       F12

Mit der Maussteuerung (Optionen) folgt das Schiff dem Mauszeiger: linke Taste feuert,
rechte schaltet die Heckwaffe um, die mittlere löst die Spezialwaffe aus.
Alle Tasten und Knöpfe lassen sich unter Optionen > Tasten und Knöpfe ändern.
Mit "Dauerfeuer" schaltet der Feuerknopf das Feuer an und aus.

Das Spiel
---------
Generator: Front- und Heckkanone sowie der Schild ziehen Energie aus dem Generator.
           Eine starke Kanone braucht einen starken Generator.
Schild:    schluckt Treffer und wächst nach. Erst wenn er leer ist, leidet die Panzerung.
Werkstatt: Zwischen den Einsätzen kaufst du bei Quill Rümpfe, Kanonen, Begleiter,
           Generatoren, Schilde und Spezialwaffen. Was eingebaut ist, nimmt er zum vollen
           Wert in Zahlung. Front- und Heckkanone haben elf Leistungsstufen.
Baupläne:  In manchen Einsätzen liegen Baupläne für besondere Ausrüstung.
Bosse:     Wer einen Bosskampf verliert, darf den Boss direkt noch einmal angehen,
           mit voller Panzerung und allem, was vorher verdient war.
Geheimes:  Manche Einsätze verbergen ein Peilsignal. Wer es findet, entdeckt einen
           geheimen Kurs.
Kette:     Jeder Abschuss innerhalb von zwei Sekunden nach dem letzten verlängert die
           Kette und hebt den Punkte-Multiplikator (bis fünffach). Ein Treffer bricht sie.
           Kugeln, die knapp vorbeiziehen (Streifschuss), bringen Punkte und halten die
           Kette am Leben. Die Kredite der Kampagne bleiben davon unberührt.

Spielarten
----------
Kampagne für zwei:  Zwei Piloten mit eigener Ausrüstung und eigener Kasse. In der Werkstatt
                    wechselt Tab / Y den Piloten. Ein abgeschossener Pilot kehrt nach acht
                    Sekunden zurück, solange der andere noch fliegt. In der Einsatzplanung
                    steigt der zweite Pilot jederzeit ein oder aus.
Neues Spiel+:       Nach dem Ende der Kampagne geht der Speicherplatz mit aller Ausrüstung
                    wieder in Episode 1 weiter, die Gegner zäher und schneller, alle Waren offen.
Überleben:          Welle um Welle bis zum letzten Leben. Der Tagesflug hat für alle am selben
                    Tag dieselben Wellen, der Wochenflug eine Woche lang dieselben und zwei
                    Sonderregeln (Doppelfeuer, Glaskanone, Nur Heck ...). Im Tages- und im
                    Wochenflug fliegt der Geist deines besten Flugs als heller Ring mit.
Flugschule:         Neun kurze Lektionen: Fliegen, Feuer, Fokus, Streifschuss, Kette, Heckwaffe,
                    Spezialwaffe, Seitenschiffe, Werkstatt. Hier kann nichts passieren.
Archiv:             Jeder Gegner, dem du begegnet bist, als Modell mit seinen Werten; das
                    Musikzimmer mit jedem gehörten Stück; Lackierungen für beide Piloten, die
                    Erfolge freischalten.
Übung:              Jeder schon geflogene Einsatz, ganz oder nur sein Boss, mit der Ausrüstung
                    der Episode oder eines Speicherplatzes. Nichts davon zählt.
Wiederholungen:     Jeder Einsatz wird aufgenommen und lässt sich ansehen (Tab / Y: Tempo).
Erfolge:            53 Erfolge und eine Statistik über alle Spiele.
Mods:               Eigene Waffen, Schiffe, Gegner und Einsätze im Ordner mods (siehe
                    docs/MODDING.md im Quelltext oder mods/example).

Hilfen und Grafik (Optionen)
----------------------------
Gegnergeschosse kräftig umrandet oder in Farben für Rot-Grün- und Blau-Gelb-Sehschwäche,
Spieltempo 70 bis 100 %, Auto-Bombe, gedämpfte Blitze, Stimmen an oder aus.
Grafik "Flüssig", "Ausgewogen" (Steam Deck) oder "Qualität". Wer messen will:
    ZenithLancer --bench all
fliegt schwere Abschnitte und schreibt die Bildraten nach bench.txt im Spielerordner.
Jeder Einsatz schreibt eine Zeile in playlog.txt (was geflogen wurde, wo und wodurch
abgeschossen).

Speicherstände und Einstellungen
--------------------------------
Windows: %APPDATA%\ZenithLancer
Linux:   ~/.local/share/ZenithLancer

Lizenz
------
Zenith Lancer ist freie Software unter der GNU Affero General Public License 3.0
(LICENSE); Mitwirkende und die Lizenzen der Bibliotheken stehen in CREDITS.txt.
Quelltext: https://github.com/reneweller-coding/ZenithLancer


ZENITH LANCER (English)
=======================

A shoot 'em up with a workshop: a five-episode campaign with branching missions,
secret missions and log crystals, an arcade mode for one or two players, a boss
rush and seven difficulties. The game speaks German and English (Options >
Language; it starts in your system's language).

Starting
--------
Windows:     ZenithLancer.exe
Linux:       ./ZenithLancer
Steam Deck:  unpack in desktop mode, add it to Steam with "Add a Non-Steam Game"
             and choose ZenithLancer. The game recognises the Deck, starts in
             full screen (1280 x 800) and plays with the Deck's controls
             (controller template "Gamepad").

Controls
--------
                     Keyboard              Gamepad / Steam Deck
Fly                  Arrows or W A S D     left stick, D-pad
Fire                 Space, J              A or RT
Slow (precise)       Shift, K              LB or LT
Switch rear gun      X, L                  B or RB
Special weapon       C, I                  X or Y
Pause                Esc, P                Start
Full screen          F11 (or Alt+Enter)
Screenshot           F12

With mouse control (Options) the ship follows the pointer: the left button fires,
the right one switches the rear gun, the middle one fires the special weapon.
Every key and button can be changed under Options > Keys and buttons.
With "Autofire" the fire button switches firing on and off.

The game
--------
Generator: front and rear guns and the shield draw energy from the generator.
           A strong gun needs a strong generator.
Shield:    takes hits and grows back. The armour suffers only once it is empty.
Workshop:  between missions Quill sells hulls, guns, sidekicks, generators,
           shields and special weapons. What is fitted is taken back at full value.
           Front and rear guns have eleven power levels.
Bosses:    lose a boss fight and you may fight the boss again from its start,
           with full armour and everything you earned before it.
Blueprints: some missions hide blueprints for special equipment.
Secrets:   some missions hide a beacon. Find it and a secret course opens.
Chain:     every kill within two seconds of the last one extends the chain and raises
           the score multiplier (up to five times). A hit breaks it. Bullets that pass
           close by (grazes) score points and keep the chain alive. The campaign's credits
           are not touched by it.

Ways to play
------------
Campaign for two:  two pilots with gear and money of their own. In the workshop Tab / Y
                   serves the other pilot. A pilot shot down comes back after eight seconds
                   as long as the other one still flies. In mission planning the second
                   pilot joins or leaves at any time.
New Game+:         after the campaign's end the save slot goes on in episode 1 with all its
                   gear, the enemies tougher and faster, every good on sale.
Survival:          wave after wave until the last life. The daily flight has the same waves
                   for everyone on the same day, the weekly flight the same for a week and two
                   house rules (Double Fire, Glass Cannon, Rear Only ...). In the daily and the
                   weekly flight the ghost of your best flight flies along as a pale ring.
Flight school:     nine short lessons: flying, fire, focus, grazes, chains, the rear gun, the
                   special, sidekicks, the workshop. Nothing can happen to you there.
Archive:           every enemy you have met as a model with its numbers; the music room with
                   every piece you have heard; paints for both pilots, opened by achievements.
Practice:          any mission flown before, whole or just its boss, with the episode's gear
                   or a save slot's. None of it counts.
Replays:           every mission is recorded and can be watched (Tab / Y: speed).
Achievements:      53 achievements and statistics across all games.
Mods:              your own guns, ships, enemies and missions in the mods folder (see
                   docs/MODDING.md in the source or mods/example).

Aids and graphics (Options)
---------------------------
Enemy bullets with a strong rim or in colours for red-green and blue-yellow colour
blindness, game speed 70 to 100 %, auto-bomb, dimmed flashes, voices on or off.
Graphics "Smooth", "Balanced" (Steam Deck) or "Quality". To measure:
    ZenithLancer --bench all
flies heavy stretches and writes the frame rates to bench.txt in the player folder.
Every mission writes a line to playlog.txt (what was flown, where and by what the
pilots were shot down).

Saves and settings
------------------
Windows: %APPDATA%\ZenithLancer
Linux:   ~/.local/share/ZenithLancer

Licence
-------
Zenith Lancer is free software under the GNU Affero General Public License 3.0
(LICENSE); the credits and the libraries' licences are in CREDITS.txt.
Source: https://github.com/reneweller-coding/ZenithLancer
