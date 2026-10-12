#!/usr/bin/env python3
# Copy the DLLs a Windows executable needs next to it.
#
# usage: windows-dlls.py --objdump OBJDUMP [--dll-dir DIR]... EXE...
#
# The imports of each EXE are read with objdump, and every DLL that turns up in one
# of the --dll-dir directories (looked up case-insensitively) is copied into the
# directory of the first EXE, then searched in turn.  Windows' own DLLs are not in
# those directories, so they are skipped.  As a meson install script, a relative EXE
# is looked for in the install prefix, under $DESTDIR.

import argparse
import os
import re
import shutil
import subprocess
import sys


def imports(objdump, path):
    out = subprocess.run([objdump, '-p', path], check=True, capture_output=True, text=True).stdout
    return re.findall(r'DLL Name: (\S+)', out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--objdump', required=True)
    ap.add_argument('--dll-dir', action='append', default=[])
    ap.add_argument('exe', nargs='+')
    args = ap.parse_args()

    dll_dirs = [d for d in args.dll_dir if os.path.isdir(d)]
    index = {}
    for d in dll_dirs:
        for name in os.listdir(d):
            if name.lower().endswith('.dll'):
                index.setdefault(name.lower(), os.path.join(d, name))

    root = os.environ.get('MESON_INSTALL_DESTDIR_PREFIX', '')
    exes = [os.path.join(root, e) for e in args.exe]
    dest = os.path.dirname(os.path.abspath(exes[0]))
    todo = list(exes)
    copied = []
    seen = set()
    system = []
    while todo:
        path = todo.pop()
        for dll in imports(args.objdump, path):
            key = dll.lower()
            if key in seen:
                continue
            seen.add(key)
            src = index.get(key)
            if src is None:
                system.append(dll)
                continue
            target = os.path.join(dest, os.path.basename(src))
            shutil.copyfile(src, target)
            os.chmod(target, 0o755)
            copied.append(target)
            todo.append(target)

    total = sum(os.path.getsize(t) for t in copied)
    print('windows-dlls: copied %d DLLs (%.1f MB) to %s' % (len(copied), total / 1e6, dest))
    for target in sorted(copied):
        print('  ' + os.path.basename(target))
    print('windows-dlls: left for Windows to provide: ' + ' '.join(sorted(system)))


if __name__ == '__main__':
    sys.exit(main())
