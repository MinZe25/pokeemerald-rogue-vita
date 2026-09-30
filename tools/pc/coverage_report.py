#!/usr/bin/env python3
# Summarises a tools/pc/battle_coverage.sh run: every problem found, grouped by
# kind and source location, with the test cases that hit it.
# usage: coverage_report.py <output dir>
import glob, os, re, subprocess, sys
from collections import OrderedDict, defaultdict

out = sys.argv[1]
game = os.path.join(out, 'game')
SKIP_FRAMES = ('LogLine', 'LogBacktrace', 'OnFault', 'OnStep', '_start', 'main', 'RunGameFrame',
               'MainLoop', 'CallCallbacks', '__libc_start', 'AgbMain', '__asan', '__interceptor')

_cache = {}
def resolve(addrs):
    """address -> list of 'func file:line' (innermost first, inlined frames included)"""
    todo = [a for a in addrs if a not in _cache]
    if todo:
        # addr2line -i prints a variable number of (func, file:line) pairs per
        # address (inlined frames), so resolve them one at a time
        for a in todo:
            r = subprocess.run(['addr2line', '-e', game, '-f', '-i', '-C', a],
                               capture_output=True, text=True).stdout.splitlines()
            frames = []
            for i in range(0, len(r) - 1, 2):
                func, loc = r[i], r[i + 1]
                loc = re.sub(r' \(discriminator \d+\)', '', loc)
                loc = loc.split('pokeemerald-rogue/')[-1]
                frames.append('%s %s' % (func, loc))
            _cache[a] = frames or ['?? ' + a]
    return [f for a in addrs for f in _cache[a]]

def interesting(frames):
    return [f for f in frames if not f.startswith(SKIP_FRAMES) and not f.startswith('??')]

cases = {}                     # case tag -> CASE description
problems = OrderedDict()       # key -> dict(kind, where, stack, cases)
hangs, skipped, failed_runs = [], [], []

def add(kind, frames, tag, extra=''):
    frames = interesting(frames)
    where = frames[0] if frames else '??'
    key = (kind, where)
    p = problems.setdefault(key, {'kind': kind, 'where': where, 'stack': frames[:8], 'cases': [], 'extra': extra})
    if tag not in p['cases']:
        p['cases'].append(tag)

for d in sorted(glob.glob(os.path.join(out, 'w_*'))):
    log = os.path.join(d, 'log.txt')
    if os.path.exists(log):
        text = open(log, errors='replace').read()
        for m in re.finditer(r'^CASE (\S+) (.*)$', text, re.M):
            cases[m.group(1)] = m.group(2)
        for m in re.finditer(r'^HANG (\S+) (.*)$', text, re.M):
            hangs.append((m.group(1), m.group(2)))
        for m in re.finditer(r'^SKIP (\S+) (.*)$', text, re.M):
            skipped.append((m.group(1), m.group(2)))
        m = re.search(r'^exit=(\d+)', text, re.M)
        if m and m.group(1) not in ('0', '3') and 'COVERAGE FINISHED' not in text:
            last = re.findall(r'^CASE (\S+)', text, re.M)
            failed_runs.append((os.path.basename(d), m.group(1), last[-1] if last else '-'))

    trap = os.path.join(d, 'trap.txt')
    if os.path.exists(trap):
        lines = open(trap, errors='replace').read().splitlines()
        for i, line in enumerate(lines):
            m = re.match(r'^(NULL|CRASH|DIV0) (.*)', line)
            if not m:
                continue
            fields = dict(re.findall(r'(\w+)=(\S+)', m.group(2)))
            bt = lines[i + 1][len('  bt:'):].split() if i + 1 < len(lines) and lines[i + 1].startswith('  bt:') else []
            addrs = [fields['pc']] + [a for a in bt if a != fields['pc']]
            extra = 'addr=%s' % fields.get('addr', '') if m.group(1) != 'DIV0' else ''
            if m.group(1) == 'CRASH':
                extra = 'signal %s addr=%s' % (fields.get('sig'), fields.get('addr'))
            add(m.group(1), resolve(addrs), fields.get('case', '-'), extra)

    # AddressSanitizer reports (ASAN=1 builds) go to the game's log, after the
    # CASE line of the battle that was running
    if os.path.exists(log):
        tag = '-'
        # each block starts with a report (except the first) and continues
        # with the normal log that followed it
        for block in re.split(r'={10,}\n', open(log, errors='replace').read()):
            m = re.search(r'ERROR: AddressSanitizer: ([\w-]+)', block)
            if not m:
                for m in re.finditer(r'^CASE (\S+)', block, re.M):
                    tag = m.group(1)
                continue
            addrs = re.findall(r'^\s+#\d+ (0x[0-9a-f]+)', block, re.M)
            access = re.search(r'^(READ|WRITE) of size (\d+)', block, re.M)
            near = re.search(r"of global variable '([^']+)'", block)
            extra = (access.group(0) if access else '') + (" near '%s'" % near.group(1) if near else '')
            add('ASAN ' + m.group(1), resolve(addrs[:10]), tag, extra)
            for m in re.finditer(r'^CASE (\S+)', block, re.M):  # CASE lines after the report
                tag = m.group(1)

def desc(tag):
    return '%s %s' % (tag, cases.get(tag, ''))

print('Battle coverage report: %d cases run' % len(cases))
print('=' * 72)
if not problems and not hangs and not failed_runs:
    print('No problems found.')
for p in sorted(problems.values(), key=lambda p: -len(p['cases'])):
    print('\n[%s] %s  %s' % (p['kind'], p['where'], p['extra']))
    for f in p['stack'][1:6]:
        print('      <- %s' % f)
    print('    %d case(s), e.g.:' % len(p['cases']))
    for tag in p['cases'][:4]:
        print('      ' + desc(tag))
for tag, what in hangs:
    print('\n[HANG] %s: %s\n      %s' % (tag, what, desc(tag)))
for run, code, last in failed_runs:
    print('\n[RUN DIED] %s exit=%s during %s' % (run, code, desc(last)))
if skipped:
    print('\n%d case(s) skipped (e.g. abilities no species has)' % len(skipped))
