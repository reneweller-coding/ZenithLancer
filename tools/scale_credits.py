"""Scales an episode's money: every enemy's `score` (the credits a kill brings), the credits enemies drop and the
credit pickups, wave bonuses and credit drops of its missions, by one factor (rounded to 5). Used to bring each
episode's credits a mission (ZenithLancer --simall --episode E, immortal, with the episode's sim gear) to the targets
in docs/DESIGN.md: e1 4-8k, e2 8-15k, e3 15-25k, e4 25-40k, e5 40-60k.

    python tools/scale_credits.py <episode> <factor>
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def scaled(v, k):
    return str(max(5, int(round(float(v) * k / 5.0)) * 5))


def main():
    ep, k = sys.argv[1], float(sys.argv[2])
    files = [os.path.join(ROOT, 'data', 'enemies', 'e%s.txt' % ep)]
    files += glob.glob(os.path.join(ROOT, 'data', 'enemies', 'e%s_*.txt' % ep))
    files += glob.glob(os.path.join(ROOT, 'data', 'levels', 'e%s' % ep, '*.txt'))
    changed = 0
    for fn in files:
        if not os.path.isfile(fn):
            continue
        with open(fn, encoding='utf-8', newline='') as f:
            lines = f.read().split('\n')
        for i, line in enumerate(lines):
            s = line.strip()
            new = line
            if s.startswith('score '):
                new = re.sub(r'score (\d+)', lambda m: 'score ' + scaled(m.group(1), k), line)
            elif s.startswith('drop credits'):
                new = re.sub(r'value=(\d+)', lambda m: 'value=' + scaled(m.group(1), k), line)
            elif s.startswith('at ') and ' pickup credits' in s:
                new = re.sub(r'value=(\d+)', lambda m: 'value=' + scaled(m.group(1), k), line)
            elif s.startswith('at ') and ( 'drop=credits' in s or 'bonus=' in s ):
                new = re.sub(r'(value|bonus)=(\d+)', lambda m: m.group(1) + '=' + scaled(m.group(2), k), line)
            if new != line:
                lines[i] = new
                changed += 1
        with open(fn, 'w', encoding='utf-8', newline='') as f:
            f.write('\n'.join(lines))
    print('episode %s: %d lines scaled by %.2f' % (ep, changed, k))


if __name__ == '__main__':
    main()
