"""Sums up the play log the game writes (<user>/playlog.txt, one line a mission): per mission how often it was flown,
won, lost and given up, the time, the deaths and where and by what they came, the boss fights tried again. Balance
from real play instead of the autopilot.

    python tools/playlog_report.py [PLAYLOG] [--mode campaign]

Without PLAYLOG: %APPDATA%\\ZenithLancer\\playlog.txt (Windows) or ~/.local/share/ZenithLancer/playlog.txt.
"""
import argparse
import collections
import os
import sys


def default_path():
    if os.name == "nt":
        return os.path.join(os.environ.get("APPDATA", ""), "ZenithLancer", "playlog.txt")
    return os.path.expanduser("~/.local/share/ZenithLancer/playlog.txt")


def parse(line):
    """date time key=value ... -> dict."""
    words = line.split()
    out = {"when": " ".join(words[:2])}
    for w in words[2:]:
        if "=" in w:
            k, v = w.split("=", 1)
            out[k] = v
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log", nargs="?", default=default_path())
    ap.add_argument("--mode", help="only this mode (campaign, arcade, survival, daily, bossrush, practice)")
    a = ap.parse_args()
    if not os.path.exists(a.log):
        sys.exit("no play log at " + a.log)
    rows = [parse(l) for l in open(a.log, encoding="utf-8") if l.strip()]
    if a.mode:
        rows = [r for r in rows if r.get("mode") == a.mode]
    per = collections.defaultdict(list)
    for r in rows:
        per[r.get("mission", "?")].append(r)
    killers = collections.Counter()
    print("%-14s %5s %5s %5s %6s %7s %7s %9s" % ("mission", "flown", "won", "lost", "abort", "deaths", "tries", "avg time"))
    for m in sorted(per):
        rs = per[m]
        res = collections.Counter(r.get("result") for r in rs)
        deaths = sum(int(r.get("deaths", 0)) for r in rs)
        tries = sum(int(r.get("bosstries", 0)) for r in rs)
        times = [int(r.get("time", 0)) for r in rs]
        print("%-14s %5d %5d %5d %6d %7d %7d %8.0fs" % (m, len(rs), res["won"], res["lost"], res["abort"], deaths, tries,
                                                     sum(times) / max(1, len(times))))
        for r in rs:
            for d in filter(None, r.get("deathsat", "").split("|")):
                parts = d.split(":")
                if len(parts) >= 5:
                    killers[(m, parts[3], parts[4], "boss" if len(parts) > 5 else "")] += 1
    if killers:
        print("\nwhat shot the pilots down most (mission, enemy, how):")
        for (m, by, how, boss), n in killers.most_common(25):
            print("  %3d  %-14s %-22s %-6s %s" % (n, m, by, how, boss))
    won = sum(1 for r in rows if r.get("result") == "won")
    print("\n%d missions in the log, %d won (%.0f %%)" % (len(rows), won, 100.0 * won / max(1, len(rows))))


if __name__ == "__main__":
    main()
