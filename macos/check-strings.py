#!/usr/bin/env python3
# Checks the string tables against the code: every L("…") key in
# macos/*.swift is in zh-Hans and zh-Hant Localizable.strings, with the same
# %@ / %ld, and no table has keys the code no longer uses. Run from anywhere;
# macos/build.sh runs it and warns.
import glob, json, os, re, subprocess, sys

macos = os.path.dirname(os.path.abspath(__file__))
keys = []
for path in sorted(glob.glob(os.path.join(macos, '*.swift'))):
    for match in re.finditer(r'\bL\("((?:[^"\\]|\\.)*)"', open(path).read()):
        if match.group(1) not in keys:
            keys.append(match.group(1))

def formats(text):
    return re.findall(r'%(?:@|ld|d)', text)

bad = False
for language in ['zh-Hans', 'zh-Hant']:
    path = os.path.join(macos, 'Resources', language + '.lproj', 'Localizable.strings')
    table = json.loads(subprocess.check_output(
        ['plutil', '-convert', 'json', '-o', '-', path]))
    for key in keys:
        if key not in table:
            print(f'{language}: missing {key!r}')
            bad = True
        elif formats(key) != formats(table[key]):
            print(f'{language}: {key!r} has other formats: {table[key]!r}')
            bad = True
    for key in table:
        if key not in keys:
            print(f'{language}: unused {key!r}')
            bad = True
sys.exit(1 if bad else 0)
