#!/usr/bin/env python3
"""What changes the schema hash in a class ID, and what doesn't."""
import pathlib
import re
import subprocess
import sys
import tempfile

npidl = sys.argv[1]

BASE = """module test.schema;
import './types.npidl';

/// The doc comment.
enum Color : u32 { Red, Green }
alias Id = u64;

message Point {
  x: f32;
  y: f32;
}

message Shape {
  id: Id;
  color: Color;
  points: vector<Point>;
  extra?: Point;
  shared: Shared;
}

exception Failed {
  why: string;
}

interface Service {
  void Get(id: Id, shape: out Shape) raises (Failed);
  u32 Watch(interval: u32);
  server_stream<Point> Follow(id: Id);
}
"""
TYPES = """message Shared {
  value: u32;
}
"""


def generate(flag, source, types=TYPES):
    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        (root / 'types.npidl').write_text(types)
        (root / 'main.npidl').write_text(source)
        result = subprocess.run([npidl, flag, str(root / 'main.npidl'), '--output-dir', str(root)],
                                capture_output=True, text=True)
        if result.returncode != 0:
            raise AssertionError(f'npidl {flag}:\n{result.stdout}\n{result.stderr}')
        text = ''.join(p.read_text(errors='replace') for p in root.iterdir()
                       if p.is_file() and not p.name.endswith('.npidl'))
    hashes = set(re.findall(r'main/test\.schema\.Service#([0-9a-f]{16})', text))
    if len(hashes) != 1:
        raise AssertionError(f'{flag}: expected one hash, found {hashes}')
    return hashes.pop()


def replaced(old, new, source=BASE):
    if old not in source:
        raise AssertionError('not in the IDL: ' + old)
    return source.replace(old, new)


base = generate('--swift', BASE)

for flag in ('--cpp', '--ts'):
    if generate(flag, BASE) != base:
        raise AssertionError(flag + ' disagrees with --swift')
print('PASS: C++, Swift and TypeScript agree')

same = {
    'a comment': replaced('/// The doc comment.', '/// Another comment.'),
    'whitespace': replaced('  x: f32;', '\n\n     x:   f32;'),
    "an alias's name": replaced('alias Id = u64;', 'alias Key = u64;').replace('id: Id;', 'id: Key;')
                       .replace('Get(id: Id', 'Get(id: Key').replace('Follow(id: Id)', 'Follow(id: Key)'),
    'an unused message': BASE + '\nmessage Unused { a: u8; }\n',
}
for label, source in same.items():
    if generate('--swift', source) != base:
        raise AssertionError(f'{label} changed the hash')
    print(f'PASS: {label} leaves the hash alone')

different = {
    'a new field': replaced('  y: f32;', '  y: f32;\n  z: f32;'),
    "a field's type": replaced('  x: f32;', '  x: f64;'),
    'field order': replaced('  x: f32;\n  y: f32;', '  y: f32;\n  x: f32;'),
    "a field's name": replaced('  x: f32;', '  w: f32;'),
    'optional': replaced('extra?: Point', 'extra: Point'),
    'an enum value': replaced('{ Red, Green }', '{ Green, Red }'),
    'a new method': replaced('  u32 Watch(interval', '  u32 Ping();\n  u32 Watch(interval'),
    'method order': replaced('  void Get(id: Id, shape: out Shape) raises (Failed);\n  u32 Watch(interval: u32);',
                             '  u32 Watch(interval: u32);\n  void Get(id: Id, shape: out Shape) raises (Failed);'),
    'an argument': replaced('Watch(interval: u32)', 'Watch(interval: u64)'),
    'a return type': replaced('u32 Watch', 'u64 Watch'),
    'a stream payload': replaced('server_stream<Point>', 'server_stream<Id>'),
    'an exception': replaced('why: string;', 'why: string;\n  code: u32;'),
}
for label, source in different.items():
    if generate('--swift', source) == base:
        raise AssertionError(f'{label} did not change the hash')
    print(f'PASS: {label} changes the hash')

if generate('--swift', BASE, TYPES.replace('value: u32', 'value: u64')) == base:
    raise AssertionError('a change in an imported file did not change the hash')
print('PASS: a type in an imported file changes the hash')
