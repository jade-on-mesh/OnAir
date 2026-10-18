#!/bin/bash
# Fetches what tools/package/cross_deps_windows.sh needs into build-windows-deps/src (override with ONAIR_WIN_DEPS_SRC), for a machine
# (or a CI runner) that does not have the vendor folder. Everything is pinned:
#   - release files: downloaded and checked against the SHA-256 below;
#   - source trees: the tag's commit is fetched by its hash (git checks it) and packed with git archive under the folder name
#     cross_deps_windows.sh unpacks (GitHub's own source archives are git archive output too; submodules are not included in either);
#   - MSYS2 UCRT64 packages (UHD, the GCC runtime, libusb and what they depend on): resolved from MSYS2's package database by
#     msys2_fetch.py, each one checked against the SHA-256 the database lists.
# Needs: curl, git, gzip, xz, zstd, python3, and shasum or sha256sum. Files already present with the right checksum are kept.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SRC=${ONAIR_WIN_DEPS_SRC:-$ROOT/build-windows-deps/src}
mkdir -p "$SRC/msys2"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1" | awk '{print $1}'; else shasum -a 256 "$1" | awk '{print $1}'; fi; }

# name  url  sha256
release() {
    local name=$1 url=$2 want=$3
    if [ -f "$SRC/$name" ] && [ "$(sha256 "$SRC/$name")" = "$want" ]; then echo "ok        $name"; return; fi
    curl -fsSL --retry 4 --retry-delay 5 -o "$TMP/$name" "$url"
    local got; got=$(sha256 "$TMP/$name")
    [ "$got" = "$want" ] || { echo "error: $name: SHA-256 $got, expected $want ($url)"; exit 1; }
    mv "$TMP/$name" "$SRC/$name"
    echo "fetched   $name"
}

# name  folder-in-archive  github-repo  commit  (gz or xz from the name)
source_tree() {
    local name=$1 dir=$2 repo=$3 commit=$4
    if [ -f "$SRC/$name" ]; then echo "ok        $name (present)"; return; fi
    local g=$TMP/git-$dir
    git init -q "$g"
    git -C "$g" fetch -q --depth 1 "https://github.com/$repo" "$commit"
    local got; got=$(git -C "$g" rev-parse FETCH_HEAD)
    [ "$got" = "$commit" ] || { echo "error: $repo: fetched $got, expected $commit"; exit 1; }
    case "$name" in
        *.tar.xz) git -C "$g" archive --format=tar --prefix="$dir/" FETCH_HEAD | xz -T0 -6 > "$TMP/$name" ;;
        *.tar.gz) git -C "$g" archive --format=tar --prefix="$dir/" FETCH_HEAD | gzip -n -6 > "$TMP/$name" ;;
        *) echo "error: $name: unknown archive type"; exit 1 ;;
    esac
    mv "$TMP/$name" "$SRC/$name"
    rm -rf "$g"
    echo "packed    $name  ($repo @ ${commit:0:12})"
}

echo "=== source archives -> $SRC"
release libusb-1.0.27.tar.bz2   https://github.com/libusb/libusb/releases/download/v1.0.27/libusb-1.0.27.tar.bz2 \
        ffaa41d741a8a3bee244ac8e54a72ea05bf2879663c098c82fc5757853441575
release glfw-3.4.zip            https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.zip \
        b5ec004b2712fd08e8861dc271428f048775200a2df719ccf575143ba749a3e9
release hackrf-2024.02.1.tar.xz https://github.com/greatscottgadgets/hackrf/releases/download/v2024.02.1/hackrf-2024.02.1.tar.xz \
        d9ced67e6b801cd02c18d0c4654ed18a4bcb36c24a64330c347dfccbd859ad16
# Not fetched: hamlib-4.6.2.tar.gz. cross_deps_windows.sh builds Hamlib with CMake, but the 4.6.2 release is autotools only (no
# CMakeLists.txt), so supplying it would stop the build; without it the script skips Hamlib, as it is written to.

source_tree ffmpeg-8.0.tar.xz            ffmpeg-8.0                FFmpeg/FFmpeg              140fd653aed8cad774f991ba083e2d01e86420c7   # tag n8.0
source_tree rtl-sdr-blog-1.3.6.tar.gz    rtl-sdr-blog-1.3.6        rtlsdrblog/rtl-sdr-blog    240bd0e1e6d9f64361b6949047468958cd08aa31   # tag v1.3.6
source_tree airspyone_host-1.0.10.tar.gz airspyone_host-1.0.10     airspy/airspyone_host      229d5a5045b890621cd9cc200a19b1541c6021ae   # tag v1.0.10
source_tree soapysdr-0.8.1.tar.gz        SoapySDR-soapy-sdr-0.8.1  pothosware/SoapySDR        1cf5a539a21414ff509ff7d0eedfc5fa8edb90c6   # tag soapy-sdr-0.8.1
source_tree libxml2-2.12.9.tar.xz        libxml2-2.12.9            GNOME/libxml2              00301f0fe8bccdb9945fb684e9bbd72449b961a5   # tag v2.12.9 (GNOME's mirror)
source_tree libiio-0.25.tar.gz           libiio-0.25               analogdevicesinc/libiio    b6028fdeef888ab45f7c1dd6e4ed9480ae4b55e3   # tag v0.25
source_tree bladeRF-2023.02.tar.gz       bladeRF-2023.02           Nuand/bladeRF              41ef63460956e833c9b321252245257ab3946055   # tag 2023.02
source_tree no-OS-0bba46e.tar.gz         no-OS-0bba46e             analogdevicesinc/no-OS     0bba46e6f6f75785a65d425ece37d0a04daf6157   # bladeRF 2023.02's submodule
source_tree LimeSuite-23.11.0.tar.gz     LimeSuite-23.11.0         myriadrf/LimeSuite         c2d9e87754586304577beff4dc5b16fb9b72c826   # tag v23.11.0
source_tree airspyhf-1.6.8.tar.gz        airspyhf-1.6.8            airspy/airspyhf            8891387edddcd185e2949e9814e9ef35f46f0722   # tag 1.6.8

echo "=== MSYS2 UCRT64 packages -> $SRC/msys2"
python3 "$ROOT/tools/package/msys2_fetch.py" "$SRC/msys2" \
    mingw-w64-ucrt-x86_64-uhd mingw-w64-ucrt-x86_64-gcc-libs mingw-w64-ucrt-x86_64-libwinpthread mingw-w64-ucrt-x86_64-libusb
