#!/usr/bin/env python3
# Unpack MSYS2 packages into a directory, to cross-compile against them.
#
# usage: msys2-sysroot.py [--repo mingw64|ucrt64] SYSROOT [PACKAGE...]
#
# Downloads the package database from repo.msys2.org, follows the dependencies of
# the named packages (names without the mingw-w64-x86_64- prefix, eg. sdl3),
# checks the SHA-256 of each package against the database, and unpacks them under
# SYSROOT.  It also writes SYSROOT/cross.ini, a meson cross file for mingw-w64 gcc
# that uses them.  With no PACKAGE, the ones the game needs are fetched; add
# ffmpeg for movie backgrounds.
#
# The compiler's own runtime (gcc-libs, winpthreads, the C runtime) is left out,
# since the cross compiler brings its own.  Needs the `zstd` and `tar` commands.

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import urllib.request

MIRROR = 'https://repo.msys2.org/mingw/%s/'
DEFAULT_PACKAGES = ['sdl3', 'libpng', 'zlib', 'sqlite3', 'openssl', 'pcre2',
                    'libsamplerate', 'libjpeg-turbo', 'libvorbis', 'libmad', 'libiconv']
SKIP = ('gcc-libs', 'libgcc', 'libstdc++', 'libwinpthread', 'libwinpthread-git', 'crt', 'crt-git',
        'headers', 'headers-git', 'winpthreads', 'winpthreads-git', 'gcc', 'gcc-compat',
        # pulled in by sqlite3's command line tool
        'tcl', 'tzdata', 'wineditline')

CROSS = """\
[binaries]
c = '%(triplet)s-gcc-posix'
cpp = '%(triplet)s-g++-posix'
ar = '%(triplet)s-ar'
strip = '%(triplet)s-strip'
windres = '%(triplet)s-windres'
objdump = '%(triplet)s-objdump'
pkg-config = 'pkg-config'

[built-in options]
c_args = ['-isystem', '%(prefix)s/include']
cpp_args = ['-isystem', '%(prefix)s/include']
c_link_args = ['-L%(prefix)s/lib']
cpp_link_args = ['-L%(prefix)s/lib']

[properties]
sys_root = '%(sysroot)s'
pkg_config_libdir = '%(prefix)s/lib/pkgconfig'

[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'

[project options]
dll_dirs = %(dll_dirs)s
"""


def fetch(url):
    with urllib.request.urlopen(url) as r:
        return r.read()


def load_db(repo, cache):
    """Read the package database into {name: fields} and {provided name: name}."""
    dbfile = os.path.join(cache, repo + '.db')
    with open(dbfile, 'wb') as f:
        f.write(fetch(MIRROR % repo + repo + '.db'))
    dbdir = os.path.join(cache, repo + '-db')
    shutil.rmtree(dbdir, ignore_errors=True)
    os.makedirs(dbdir)
    # Newer databases are zstd compressed, which Python's tarfile can't read before 3.14.
    subprocess.run(['tar', '-xf', dbfile, '-C', dbdir], check=True)

    pk, prov = {}, {}
    for d in os.listdir(dbdir):
        with open(os.path.join(dbdir, d, 'desc'), encoding='utf-8') as f:
            txt = f.read()
        fields = {}
        for g in re.finditer(r'%([A-Z0-9]+)%\n(.*?)(?:\n\n|\Z)', txt, re.S):
            fields[g.group(1)] = g.group(2).split('\n')
        name = fields['NAME'][0]
        pk[name] = fields
        for p in fields.get('PROVIDES', []):
            prov[re.split('[<>=]', p)[0]] = name
    return pk, prov


def main():
    ap = argparse.ArgumentParser(description='Unpack MSYS2 packages into a directory, to cross-compile against them.')
    ap.add_argument('--repo', default='mingw64', choices=['mingw64', 'ucrt64'])
    ap.add_argument('sysroot')
    ap.add_argument('packages', nargs='*')
    args = ap.parse_args()

    prefix = 'mingw-w64-ucrt-x86_64-' if args.repo == 'ucrt64' else 'mingw-w64-x86_64-'
    skip = set(prefix + s for s in SKIP)
    sysroot = os.path.abspath(args.sysroot)
    cache = os.path.join(sysroot, '.cache')
    os.makedirs(cache, exist_ok=True)

    pk, prov = load_db(args.repo, cache)
    wanted = [prefix + p for p in (args.packages or DEFAULT_PACKAGES)]

    need, todo = [], list(wanted)
    while todo:
        n = todo.pop()
        n = n if n in pk else prov.get(n, n)
        if n in need or n in skip:
            continue
        if n not in pk:
            print('msys2-sysroot: no package named %s' % n, file=sys.stderr)
            continue
        need.append(n)
        todo += [re.split('[<>=]', d)[0] for d in pk[n].get('DEPENDS', [])]

    for n in sorted(need):
        fn = pk[n]['FILENAME'][0]
        want = pk[n]['SHA256SUM'][0]
        path = os.path.join(cache, fn)
        if not os.path.exists(path) or hashlib.sha256(open(path, 'rb').read()).hexdigest() != want:
            print('fetching ' + fn)
            data = fetch(MIRROR % args.repo + fn)
            if hashlib.sha256(data).hexdigest() != want:
                sys.exit('msys2-sysroot: checksum mismatch for ' + fn)
            with open(path, 'wb') as f:
                f.write(data)
        # The .PKGINFO and friends at the top are package metadata, not files.
        subprocess.run(['tar', '--zstd', '-xf', path, '-C', sysroot, '--exclude=.PKGINFO',
                        '--exclude=.BUILDINFO', '--exclude=.MTREE', '--exclude=.INSTALL'], check=True)

    # The compiler's runtime DLLs are needed by FFmpeg and its codecs; let the install step find them.
    triplet = 'x86_64-w64-mingw32'
    dll_dirs = []
    for gcc, dll in ((triplet + '-gcc-posix', 'libgcc_s_seh-1.dll'), (triplet + '-g++-posix', 'libstdc++-6.dll'),
                     (triplet + '-gcc-posix', 'libwinpthread-1.dll')):
        try:
            out = subprocess.run([gcc, '-print-file-name=' + dll], capture_output=True, text=True).stdout.strip()
        except OSError:
            continue
        if os.path.isabs(out) and os.path.exists(out):
            d = os.path.dirname(os.path.realpath(out))
            if d not in dll_dirs:
                dll_dirs.append(d)

    mingw = os.path.join(sysroot, args.repo)
    with open(os.path.join(sysroot, 'cross.ini'), 'w') as f:
        f.write(CROSS % {'triplet': triplet, 'prefix': mingw, 'sysroot': sysroot, 'dll_dirs': repr(dll_dirs)})

    print('unpacked %d packages into %s' % (len(need), sysroot))
    print('DLLs are in %s/bin; the cross file is %s/cross.ini' % (mingw, sysroot))


if __name__ == '__main__':
    main()
