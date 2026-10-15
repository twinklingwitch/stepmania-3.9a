#!/usr/bin/env python3
# Write the header that makes an official build, for meson's -Donline_build.
#
# usage: ONLINE_BUILD_KEY=<hex> online-build-header.py NAME OUT
#
# NAME names the build (eg. v1.2.0-windows-x64).  The build's key is
# HMAC-SHA256(ONLINE_BUILD_KEY, NAME); the game signs its online requests with
# it, and a server with the same ONLINE_BUILD_KEY (39server's
# SMSERVER_BUILD_KEY) works the build's key out again from the name it sends.
# Only the release workflow has ONLINE_BUILD_KEY, as a repository secret, so
# only its builds can sign.  The header holds the build's key, never
# ONLINE_BUILD_KEY: one build's key, dug out of its executable, is no use for
# another name, and the server can refuse a name.
#
# The key is written masked with random bytes, so it isn't a plain string in
# the executable.  That only slows down someone looking for it.

import hashlib
import hmac
import os
import re
import sys


def array(name, data):
    return 'static const unsigned char %s[%d] = { %s };\n' % (
        name, len(data), ', '.join('0x%02x' % b for b in data))


def main():
    if len(sys.argv) != 3:
        sys.exit('usage: ONLINE_BUILD_KEY=<hex> online-build-header.py NAME OUT')
    name, out = sys.argv[1:]
    if not re.fullmatch(r'[A-Za-z0-9._-]{1,64}', name):
        sys.exit('online-build-header.py: the name is 1 to 64 of A-Z a-z 0-9 . _ -')
    try:
        secret = bytes.fromhex(os.environ.get('ONLINE_BUILD_KEY', ''))
    except ValueError:
        sys.exit('online-build-header.py: ONLINE_BUILD_KEY isn\'t hex')
    if len(secret) < 32:
        sys.exit('online-build-header.py: ONLINE_BUILD_KEY should be at least 32 bytes, in hex')

    key = hmac.new(secret, name.encode(), hashlib.sha256).digest()
    mask = os.urandom(len(key))
    masked = bytes(a ^ b for a, b in zip(key, mask))
    with open(out, 'w') as f:
        f.write('/* Written by Utils/online-build-header.py for an official build. */\n')
        f.write('static const char ONLINE_BUILD_NAME[] = "%s";\n' % name)
        f.write(array('ONLINE_BUILD_KEY_MASKED', masked))
        f.write(array('ONLINE_BUILD_KEY_MASK', mask))


main()
