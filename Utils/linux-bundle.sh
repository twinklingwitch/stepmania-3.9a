#!/bin/bash
# Make a game folder from a Linux build, to zip up and run on other systems:
#
#   Utils/linux-bundle.sh <build dir> <game folder>
#
# The folder gets the executable, the game data and the docs (laid out like the
# Windows folder: everything at the top, the game finds its data next to the
# executable), and in lib/ the libraries the game needs that can't be counted
# on to be installed, or not in a compatible version.  The executable finds
# them through its run path ($ORIGIN/lib).
#
# Left to the system: glibc, libstdc++, OpenGL, zlib, OpenSSL's libcrypto (3.x)
# and libcurl, and X11, Wayland and the sound servers, which SDL loads at run
# time (or links to, if it was built that way).  Those are part of every
# desktop install, and libcurl and libcrypto are better taken from the system,
# which knows where its CA certificates are.  The folder runs where the glibc
# is at least as new as the one it was built against.
#
# Needs patchelf and readelf.  Library paths are taken from ldd, so set
# LD_LIBRARY_PATH if some of them (say, a self-built SDL) aren't on the
# default search path.
set -eu

top=$(cd "$(dirname "$0")/.." && pwd)
build=${1:?usage: $0 <build dir> <game folder>}
out=${2:?usage: $0 <build dir> <game folder>}
exe="$build/src/stepmania"

if [ ! -x "$exe" ]; then
	echo "$exe not found; build the game first." >&2
	exit 1
fi
for tool in patchelf readelf ldd strip python3; do
	if ! command -v $tool > /dev/null; then
		echo "$tool is needed." >&2
		exit 1
	fi
done

echo "Making $out"
rm -rf "$out"
mkdir -p "$out/lib"
cp "$exe" "$out/stepmania"
strip "$out/stepmania"

# Plain 3.9 or the one with the extras.
extras=$(python3 -c '
import json, sys
opts = [o for o in json.load(open(sys.argv[1])) if o["name"] == "extras"]
print("false" if opts and not opts[0]["value"] else "true")
' "$build/meson-info/intro-buildoptions.json")

# The game data, as "meson install" would install it, and the docs next to it,
# as on Windows.
for d in Announcers BGAnimations CDTitles Characters Courses Data Docs NoteSkins \
	Packages RandomMovies Songs Themes Visualizations; do
	cp -R "$top/$d" "$out/"
done
cp "$top/NEWS" "$top/README-FIRST.html" "$top/FAQ.html" "$out/"
# Nocturnal Dream comes with the extras only (see meson.build).
[ "$extras" = true ] || rm -rf "$out/Themes/Nocturnal Dream"

# Libraries.

is_system() {
	case "$1" in
	ld-linux*|libc.so.*|libm.so.*|libdl.so.*|libpthread.so.*|librt.so.*|libresolv.so.*|\
	libutil.so.*|libmvec.so.*|libstdc++.so.*|libgcc_s.so.*|\
	libGL.so.*|libGLX.so.*|libGLdispatch.so.*|libOpenGL.so.*|libEGL.so.*|\
	libz.so.*|libcrypto.so.*|libssl.so.*|libcurl.so.*|libcurl-gnutls.so.*|\
	libX*|libxcb*|libwayland-*|libdecor-*|libxkbcommon*|libdrm.so.*|libgbm.so.*|\
	libasound.so.*|libpulse*|libpipewire-*|libjack.so.*|libsndio.so.*|\
	libdbus-1.so.*|libudev.so.*|libsystemd.so.*)
		return 0 ;;
	esac
	return 1
}

# Where each library the executable ends up loading is, as "soname path".
paths=$(ldd "$exe" | awk '$2 == "=>" && $3 ~ /^\// { print $1, $3 }')
missing=$(ldd "$exe" | awk '$3 == "not" { print $1 }')
if [ -n "$missing" ]; then
	echo "Not found: $missing" >&2
	exit 1
fi

needed() {
	readelf -d "$1" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'
}

echo "Copying libraries:"
todo=$(needed "$exe")
done_list=" "
while [ -n "$todo" ]; do
	set -- $todo
	name=$1
	shift
	todo="$*"
	case "$done_list" in *" $name "*) continue ;; esac
	done_list="$done_list$name "
	is_system "$name" && continue
	path=$(echo "$paths" | awk -v n="$name" '$1 == n { print $2; exit }')
	if [ -z "$path" ]; then
		echo "Can't find $name." >&2
		exit 1
	fi
	echo "  $name"
	cp -L "$path" "$out/lib/$name"
	chmod 644 "$out/lib/$name"
	strip --strip-unneeded "$out/lib/$name"
	patchelf --set-rpath '$ORIGIN' "$out/lib/$name"
	todo="$todo $(needed "$path")"
done

patchelf --set-rpath '$ORIGIN/lib' "$out/stepmania"

# Make sure everything is found in the folder or the system.
if LD_LIBRARY_PATH= ldd "$out/stepmania" | grep -q 'not found'; then
	LD_LIBRARY_PATH= ldd "$out/stepmania" | grep 'not found' >&2
	exit 1
fi

glibc=$(objdump -T "$out/stepmania" "$out"/lib/*.so* 2>/dev/null |
	grep -o 'GLIBC_[0-9.]*[0-9]' | sort -V -u | tail -n 1)
echo "Done: $out (needs ${glibc:-glibc})"
