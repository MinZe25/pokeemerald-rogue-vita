#!/usr/bin/env python3
# Builds the encounter searcher page from the runs of tools/pc/encounters.sh.
# usage: encounters_page.py <work dir> <out.html>
import base64, glob, io, json, os, re, sys

work, out = sys.argv[1], sys.argv[2]
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')

TYPES = ['Normal', 'Fighting', 'Flying', 'Poison', 'Ground', 'Rock', 'Bug', 'Ghost', 'Steel', 'Mystery',
         'Fire', 'Water', 'Grass', 'Electric', 'Psychic', 'Ice', 'Dragon', 'Dark', 'Fairy', 'Stellar']
CHARS = {}
for i in range(26):
    CHARS[0xBB + i] = chr(ord('A') + i)
    CHARS[0xD5 + i] = chr(ord('a') + i)
for i in range(10):
    CHARS[0xA1 + i] = chr(ord('0') + i)
CHARS.update({0x00: ' ', 0xAE: '-', 0xAD: '.', 0xB4: "'", 0x1B: 'é'})

# the adjective the path screen shows for each type hint ("Typical" = Normal)
adjectives = {}
src = open(os.path.join(root, 'src', 'rogue_adventurepaths.c'), encoding='latin-1').read()
for name, text in re.findall(r'gText_Adj(\w+)\[\]\s*=\s*_\("([^"]*)"\)', src):
    if name in TYPES:
        adjectives[TYPES.index(name)] = text

# map folder names, e.g. Rogue_Route_Forest0
groups = json.load(open(os.path.join(root, 'data', 'maps', 'map_groups.json')))
def map_name(group, num):
    try:
        name = groups[groups['group_order'][group]][num]
    except (IndexError, KeyError):
        return '%d.%d' % (group, num)
    return re.sub(r'^Rogue_Route_', '', name).replace('_', ' ')

names, types, levels, pools, routes = {}, {}, {}, {}, {}
for d in range(14):
    log = os.path.join(work, 'pool_%d' % d, 'log.txt')
    if not os.path.exists(log):
        continue
    pools[d] = {}
    for line in open(log, encoding='latin-1'):
        if line.startswith('NAME '):
            parts = line.rstrip('\n').split(' ', 4)
            sp = int(parts[1])
            names[sp] = parts[4]
            types[sp] = sorted({int(parts[2]), int(parts[3])})
        elif line.startswith('POOL '):
            m = re.match(r'POOL route=(\d+) map=(\d+)\.(\d+) layout=\d+ level=(\d+) types=([\d,]+) name=\S* species=(.*)', line)
            r = int(m.group(1))
            levels[d] = int(m.group(4))
            routes.setdefault(r, {'map': map_name(int(m.group(2)), int(m.group(3))),
                                  'types': [int(t) for t in m.group(5).split(',')]})
            pools[d][r] = [[int(a), int(b)] for a, b in (e.split(':') for e in m.group(6).split())]

# a look at each route map, and whether entering it went wrong
for r in routes:
    d = os.path.join(work, 'route_%d' % r)
    log = os.path.join(d, 'log.txt')
    text = open(log, encoding='latin-1').read() if os.path.exists(log) else ''
    ok = 'SCOUT room=' in text and 'exit=0' in text and 'AddressSanitizer' not in text
    routes[r]['ok'] = ok
    if not ok:
        print('route %d (%s): entering it failed, see %s' % (r, routes[r]['map'], log))
    frames = sorted(glob.glob(os.path.join(d, 'frame_*.bmp')))
    if frames:
        try:
            from PIL import Image
            buf = io.BytesIO()
            Image.open(frames[-1]).convert('RGB').save(buf, 'PNG', optimize=True)
            routes[r]['img'] = base64.b64encode(buf.getvalue()).decode()
        except ImportError:
            pass

# the path screen's route icons: one sprite per type hint, facing down (Calm),
# up (Average) or left (Tough) (SelectObjectMovementTypeForRoom)
icons = {}
try:
    from PIL import Image
    for t, name in enumerate(TYPES):
        png = os.path.join(root, 'graphics', 'object_events', 'pics', 'rogue', 'route', name.lower() + '.png')
        if not os.path.exists(png):
            continue
        sheet = Image.open(png).convert('P')
        sheet.info['transparency'] = sheet.getpixel((0, 0))  # the background colour, transparent in game
        sheet = sheet.convert('RGBA')
        icons[t] = []
        for frame in range(3):
            buf = io.BytesIO()
            sheet.crop((frame * 16, 0, frame * 16 + 16, 16)).resize((32, 32), Image.NEAREST).save(buf, 'PNG')
            icons[t].append(base64.b64encode(buf.getvalue()).decode())
except ImportError:
    pass

data = {'names': names, 'icons': icons, 'types': types, 'typeNames': TYPES, 'adjectives': adjectives,
        'levels': levels, 'pools': pools, 'routes': routes}
page = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'encounters_template.html'), encoding='utf-8').read()
open(out, 'w', encoding='utf-8').write(page.replace('/*DATA*/null', json.dumps(data, separators=(',', ':'))))
print('wrote %s: %d routes, %d species, gyms %s' % (out, len(routes), len(names), sorted(pools)))
