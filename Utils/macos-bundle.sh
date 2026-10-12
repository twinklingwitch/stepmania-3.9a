#!/bin/bash
# Make StepMania.app from a macOS build:
#
#   Utils/macos-bundle.sh <build dir> [<output dir>]
#
# The app gets the executable, the game data in Contents/Resources, and every
# library the game needs that isn't part of macOS (the Homebrew ones) in
# Contents/Frameworks, with the paths to them pointed there.  Then everything
# is signed ad hoc, which Apple Silicon requires to run at all.  The output
# directory defaults to the build directory.
#
# This only runs on macOS (otool, install_name_tool and codesign come with the
# Xcode command line tools).  It sticks to the bash 3.2 that macOS has.
set -eu

top=$(cd "$(dirname "$0")/.." && pwd)
build=${1:?usage: $0 <build dir> [<output dir>]}
out=${2:-$build}
exe="$build/src/stepmania"

if [ "$(uname -s)" != Darwin ]; then
	echo "This makes a macOS app bundle; run it on macOS." >&2
	exit 1
fi
if [ ! -x "$exe" ]; then
	echo "$exe not found; build the game first." >&2
	exit 1
fi
mkdir -p "$out"
exe=$(cd "$(dirname "$exe")" && pwd)/stepmania
out=$(cd "$out" && pwd)

brew_prefix=$(brew --prefix 2>/dev/null || echo /opt/homebrew)
version=$(sed -n 's/^#define PRODUCT_VER "\(.*\)"/\1/p' "$top/src/ProductInfo.h")

app="$out/StepMania.app"
contents="$app/Contents"
fw="$contents/Frameworks"
target_exe="$contents/MacOS/stepmania"

echo "Making $app"
rm -rf "$app"
mkdir -p "$contents/MacOS" "$contents/Resources" "$fw"

sed "s/@VERSION@/$version/g" "$top/Utils/macos/Info.plist.in" > "$contents/Info.plist"
printf 'APPL????' > "$contents/PkgInfo"
cp "$top/Utils/macos/StepMania.icns" "$contents/Resources/"
cp "$exe" "$target_exe"
chmod 755 "$target_exe"

# The game data, as "meson install" would put it in share/stepmania.
for d in Announcers BGAnimations CDTitles Characters Courses Data NoteSkins \
	Packages RandomMovies Songs Themes Visualizations; do
	cp -R "$top/$d" "$contents/Resources/"
done
mkdir -p "$contents/Resources/Docs"
cp -R "$top/Docs/." "$contents/Resources/Docs/"
cp "$top/NEWS" "$top/README-FIRST.html" "$top/FAQ.html" "$top/README.md" "$contents/Resources/Docs/"

# Libraries.

# install_name_tool, without the warning that every change invalidates the
# signature; everything gets signed again at the end.
relink() {
	local msg
	if ! msg=$(install_name_tool "$@" 2>&1); then
		echo "$msg" >&2
		return 1
	fi
}

is_system() {
	case "$1" in
	/usr/lib/*|/System/*) return 0 ;;
	esac
	return 1
}

# The install names a Mach-O file links to, other than its own.
deps() {
	local self
	self=$(otool -D "$1" | tail -n +2)
	otool -L "$1" | tail -n +2 | awk '{print $1}' | while read -r ref; do
		[ "$ref" = "$self" ] || echo "$ref"
	done
}

# Its LC_RPATH entries.
rpaths() {
	otool -l "$1" | awk '$2 == "LC_RPATH" { getline; getline; print $2 }'
}

# Find the file an install name refers to, from the file that refers to it.
resolve() {
	local ref=$1 from=$2 dir cand="" rp
	dir=$(dirname "$from")
	case "$ref" in
	@loader_path/*)
		cand="$dir/${ref#@loader_path/}" ;;
	@executable_path/*)
		cand="$(dirname "$exe")/${ref#@executable_path/}" ;;
	@rpath/*)
		for rp in $(rpaths "$from"); do
			rp=${rp/#@loader_path/$dir}
			rp=${rp/#@executable_path/$(dirname "$exe")}
			if [ -e "$rp/${ref#@rpath/}" ]; then cand="$rp/${ref#@rpath/}"; break; fi
		done
		if [ -z "$cand" ]; then
			for rp in $(rpaths "$exe"); do
				rp=${rp/#@loader_path/$(dirname "$exe")}
				rp=${rp/#@executable_path/$(dirname "$exe")}
				if [ -e "$rp/${ref#@rpath/}" ]; then cand="$rp/${ref#@rpath/}"; break; fi
			done
		fi
		;;
	*)
		cand=$ref ;;
	esac
	if [ -z "$cand" ] || [ ! -e "$cand" ]; then
		cand="$brew_prefix/lib/$(basename "$ref")"
	fi
	[ -e "$cand" ] || return 1
	echo "$cand"
}

# Copy what <original> links to into Frameworks, and point <copy> at the copies.
# Dependencies are looked up from the original, so @loader_path still works.
bundle_libs() {
	local orig=$1 copy=$2 ref path name list
	list=$(deps "$orig")
	local IFS='
'
	for ref in $list; do
		is_system "$ref" && continue
		if ! path=$(resolve "$ref" "$orig"); then
			echo "Can't find $ref, which $orig needs." >&2
			exit 1
		fi
		name=$(basename "$ref")
		relink -change "$ref" "@executable_path/../Frameworks/$name" "$copy"
		if [ ! -e "$fw/$name" ]; then
			echo "  $name"
			cp "$path" "$fw/$name"
			chmod 644 "$fw/$name"
			relink -id "@executable_path/../Frameworks/$name" "$fw/$name"
			bundle_libs "$path" "$fw/$name"
		fi
	done
}

echo "Copying libraries:"
bundle_libs "$exe" "$target_exe"

# The build's search paths point into Homebrew; the bundle doesn't need them.
for rp in $(rpaths "$target_exe"); do
	relink -delete_rpath "$rp" "$target_exe"
done

# Make sure nothing still points outside the bundle or the system.
bad=0
for f in "$target_exe" "$fw"/*.dylib; do
	[ -e "$f" ] || continue
	for ref in $(deps "$f"); do
		case "$ref" in
		/usr/lib/*|/System/*|@executable_path/../Frameworks/*) ;;
		*) echo "$f still links to $ref" >&2; bad=1 ;;
		esac
	done
done
[ $bad = 0 ] || exit 1

# install_name_tool broke the signatures; Apple Silicon won't run unsigned code.
# Sign ad hoc: enough to run here, not to pass Gatekeeper elsewhere (see README).
echo "Signing"
for f in "$fw"/*.dylib; do
	[ -e "$f" ] && codesign --force --sign - "$f"
done
codesign --force --deep --sign - "$app"
codesign --verify --deep --strict "$app"

echo "Done: $app"
