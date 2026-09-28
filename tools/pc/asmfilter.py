#!/usr/bin/env python3
# Converts ARM-flavoured GNU as data files into host (x86) GNU as syntax
# for the PORTABLE build. Reads a file (or stdin), writes stdout.
#  - inlines .include "file" (search: dir of file, cwd, sound/, include/)
#  - strips ARM '@' line comments
#  - .word/.4byte -> .int, .hword/.2byte -> .short, .align N -> .p2align N
#  - drops ARM/ELF-only directives (.thumb, .arm, .type, .size, .ltorg ...)
#  - normalises custom sections to .data
import sys, os, re

SEARCH = ['.', 'sound', 'include']
DROP = re.compile(r'^\s*\.(thumb_func|thumb|arm|code|syntax|cpu|fpu|type|size|ltorg|pool|force_thumb)\b')
SECTION = re.compile(r'^(\s*)\.section\s+([^\s,]+)(.*)$')
INCLUDE = re.compile(r'^\s*\.include\s+"([^"]+)"')
ALIGN = re.compile(r'(^|[\s:;])\.align(\s)')
SUBS = [
    (re.compile(r'(^|[\s:;])\.(word|4byte)(\s)'), r'\1.int\3'),
    (re.compile(r'(^|[\s:;])\.(hword|2byte)(\s)'), r'\1.short\3'),
    (re.compile(r'(^|[\s:;])\.4bye(\s)'), r'\1.int\2'),
]
KNOWN_SECTIONS = {'.rodata', '.data', '.bss', '.text'}
BACKSLASH = chr(92)


def strip_comment(line):
    # remove '@' comments, ignoring anything inside "strings"
    if '@' not in line:
        return line
    out = []
    inq = False
    i = 0
    n = len(line)
    while i < n:
        c = line[i]
        if c == '"':
            inq = not inq
        elif c == BACKSLASH and inq:
            out.append(line[i:i + 2])
            i += 2
            continue
        elif c == '@' and not inq:
            if i > 0 and line[i - 1] == BACKSLASH:
                out.append(c)  # macro invocation counter
                i += 1
                continue
            break
        out.append(c)
        i += 1
    return ''.join(out)


def find_include(name, curdir):
    for d in [curdir] + SEARCH:
        p = os.path.join(d, name)
        if os.path.isfile(p):
            return p
    return None


def process(lines, curdir, out):
    for line in lines:
        line = strip_comment(line.rstrip('\r\n'))
        m = INCLUDE.match(line)
        if m:
            p = find_include(m.group(1), curdir)
            if p is None:
                sys.stderr.write('asmfilter: cannot find include %s\n' % m.group(1))
                sys.exit(1)
            with open(p, encoding='utf-8', errors='surrogateescape') as f:
                process(f.readlines(), os.path.dirname(p), out)
            continue
        if DROP.match(line):
            continue
        m = SECTION.match(line)
        if m:
            name = m.group(2)
            if name not in KNOWN_SECTIONS:
                name = '.data'
            out.append('%s.section %s' % (m.group(1), name))
            continue
        for rx, rep in SUBS:
            line = rx.sub(rep, line)
        line = ALIGN.sub(r'\1.p2align\2', line)
        out.append(line)


def main():
    args = sys.argv[1:]
    if args and args[0] != '-':
        src = args[0]
        with open(src, encoding='utf-8', errors='surrogateescape') as f:
            lines = f.readlines()
        curdir = os.path.dirname(src) or '.'
    else:
        sys.stdin.reconfigure(encoding='utf-8', errors='surrogateescape')
        lines = sys.stdin.readlines()
        curdir = '.'
    out = []
    process(lines, curdir, out)
    sys.stdout.reconfigure(encoding='utf-8', errors='surrogateescape')
    sys.stdout.write('\n'.join(out) + '\n')


if __name__ == '__main__':
    main()
