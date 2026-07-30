#!/usr/bin/env python3
"""Fail if a header declares a function that is defined nowhere.

`include/utils/devoptab.hpp` had accumulated 16 of these: MountZip, MountNsp,
MountXci, MountVfsAll, MountWebdavAll ... none of them existed. They cannot be
called, so the compiler and linker never say a word, and the header reads like a
list of features the program has. That is the specific rot this guards.

Deliberately narrow. It only answers "does a definition exist?", which is a
purely syntactic question with an unambiguous answer. It does NOT try to find
functions that are defined but never called -- distinguishing a qualified call
(`utils::reboot(p);`) from an out-of-line definition (`Result utils::reboot(...)  {`)
by regex is guesswork, and a check that cries wolf gets ignored. Use the
compiler for that: -Wunused-function already covers statics, and the release
build's LTO drops the rest.

    python tests/check_dead_symbols.py       # from the repo root

Exits non-zero on any finding, so it works as a pre-commit or CI gate.
"""
import os
import re
import sys

HEADER_DIRS = ('sphaira/include',)
SOURCE_DIRS = ('sphaira/source', 'sphaira/include')

# name -> why it has no C++ definition in this tree
ALLOW = {
    # implemented in libnx / portlibs, only re-declared here
    'amssuInitialize': 'ams_su.h re-declares the atmosphere service API',
    'amssuExit': 'ams_su.h re-declares the atmosphere service API',
}

KEYWORDS = {
    'if', 'for', 'while', 'switch', 'return', 'sizeof', 'operator', 'else', 'do',
    'case', 'catch', 'throw', 'new', 'delete', 'using', 'typedef', 'decltype',
    'static_assert', 'alignof', 'and', 'or', 'not', 'explicit', 'friend',
}

# `<ret> Name(args);` -- a declaration, not a definition (no body)
DECL_RX = re.compile(
    r'^\s*(?:(?:static|virtual|inline|constexpr|explicit|friend|extern)\s+)*'
    r'[\w:<>,\s\*&\[\]]+?[\s\*&]'
    r'(\w+)\s*\([^;{]*\)\s*(?:const)?\s*(?:noexcept)?\s*;\s*$'
)

# a line that opens a function body: `... Name(...) {` or `... Class::Name(...)`
BODY_RX = re.compile(r'(?:^|[\s:~*&])(\w+)\s*\(')


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    text = re.sub(r'//[^\n]*', '', text)
    # Drop preprocessor directives *including* backslash continuations -- macro
    # bodies are full of things that look like declarations (`mutexLock(&m);`).
    return re.sub(r'^[ \t]*#(?:[^\n\\]|\\\n?)*', '', text, flags=re.M)


def walk(dirs):
    for d in dirs:
        for root, _, names in os.walk(d):
            for n in names:
                if n.endswith(('.cpp', '.hpp', '.c', '.h')):
                    yield os.path.join(root, n).replace(os.sep, '/')


def declared():
    """{name: header} for declarations at namespace/class scope."""
    out = {}
    for p in walk(HEADER_DIRS):
        text = strip_comments(open(p, encoding='utf8', errors='replace').read())
        depth_is_func = []
        prev = ''
        for line in text.split('\n'):
            if not any(depth_is_func):
                m = DECL_RX.match(line)
                if m:
                    name = m.group(1)
                    # `void (*cleanupFunc)(void)` is a parameter, not a function
                    is_fn_ptr_param = f'(*{name})' in line.replace(' ', '')
                    if (name not in KEYWORDS and name not in ALLOW
                            and not name.startswith('_') and not is_fn_ptr_param):
                        out.setdefault(name, p)
            opens = line.count('{')
            if opens:
                # K&R: `... foo(args) {`   Allman: `... foo(args)` then a bare `{`
                is_func = bool(re.search(r'\)\s*(?:const\s*)?(?:noexcept\s*)?'
                                         r'(?:override\s*)?(?:->[^{]*)?\{', line))
                # ctor with a member-init list: `Foo(Bar* b) : m_b{b} {`
                if not is_func and re.search(r'\)\s*:.*\{\s*$', line):
                    is_func = True
                if not is_func and line.strip() == '{' and prev.endswith(')'):
                    is_func = True
                depth_is_func.extend([is_func] * opens)
            for _ in range(min(line.count('}'), len(depth_is_func))):
                depth_is_func.pop()
            if line.strip():
                prev = line.strip()
    return out


def defined():
    """Every name that has a body somewhere. Over-approximates on purpose: a
    false 'defined' only costs us a missed finding, a false 'undefined' would
    make the check lie."""
    out = set()
    for p in walk(SOURCE_DIRS):
        text = strip_comments(open(p, encoding='utf8', errors='replace').read())
        # join wrapped signatures so `Foo(\n  args) {` is seen as one line
        text = re.sub(r'\(\s*\n\s*', '(', text)
        for line in text.split('\n'):
            s = line.strip()
            # `{` -> body opens here; `)` -> brace is on the next line;
            # `}` -> whole body on one line (`void App::Foo(Bar) {}`).
            if not s.endswith(('{', ')', '}')) or s.endswith(';'):
                continue
            for m in BODY_RX.finditer(s):
                out.add(m.group(1))
    return out


def main():
    if not os.path.isdir(HEADER_DIRS[0]):
        sys.exit(f'run me from the repo root (missing {HEADER_DIRS[0]})')

    decls = declared()
    defs = defined()
    phantoms = sorted(n for n in decls if n not in defs)

    if phantoms:
        print(f'{len(phantoms)} declaration(s) with no definition anywhere:\n')
        for n in phantoms:
            print(f'  {decls[n]:52} {n}')
        print('\nFAIL: delete them, or add to ALLOW with a reason.')
        return 1

    print(f'OK: {len(decls)} header declarations, all defined.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
