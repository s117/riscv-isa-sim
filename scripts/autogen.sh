#!/usr/bin/env bash
# Bootstrap the autotools project: generate configure, and put the auxiliary
# scripts it needs (config.guess, config.sub) in scripts/.
set -euo pipefail

die() {
    echo "autogen.sh: $*" >&2
    exit 1
}

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "${SCRIPT_DIR}/.."

command -v autoreconf >/dev/null 2>&1 ||
    die "autoreconf not found: install autoconf (and automake)"

# Autoconf 2.70 and later also install config.guess and config.sub when
# automake is installed. Older ones leave them to us.
autoreconf -i

# A usable copy is a shell script with a timestamp, not an HTML page that a
# failed download left behind.
usable() {
    local first
    [ -s "$1" ] && IFS= read -r first <"$1" &&
        [[ "${first}" == "#!"*"/bin/sh"* ]] && grep -q '^timestamp=' "$1"
}

tmp=""
trap 'rm -f "${tmp}"' EXIT

get_aux_script() {
    local name=$1 dest=scripts/$1 libdir url
    # savannah's two web front ends, which fail now and then (502, hangs),
    # then GCC's mirror: older copies, but they know every host we build on.
    local urls=(
        "https://gitweb.git.savannah.gnu.org/gitweb/?p=config.git;a=blob_plain;f=$1;hb=HEAD"
        "https://cgit.git.savannah.gnu.org/cgit/config.git/plain/$1"
        "https://raw.githubusercontent.com/gcc-mirror/gcc/master/$1"
    )
    usable "${dest}" && return 0
    [ -e "${dest}" ] && echo "autogen.sh: replacing the unusable ${dest}" >&2

    tmp=$(mktemp "scripts/.${name}.XXXXXX")
    # automake ships both scripts: prefer its copy, then download the latest.
    libdir=$(automake --print-libdir 2>/dev/null || true)
    if [ -n "${libdir}" ] && usable "${libdir}/${name}"; then
        cp "${libdir}/${name}" "${tmp}"
    else
        for url in "${urls[@]}"; do
            if command -v curl >/dev/null 2>&1; then
                curl -fsSL --retry 3 --connect-timeout 30 --max-time 120 \
                    -o "${tmp}" "${url}" || true
            elif command -v wget >/dev/null 2>&1; then
                wget -q -t 3 -T 60 -O "${tmp}" "${url}" || true
            fi
            usable "${tmp}" && break
        done
    fi
    usable "${tmp}" ||
        die "could not get ${name}: install automake, or save ${urls[0]} as ${dest}"
    mv "${tmp}" "${dest}"
    tmp=""
}

get_aux_script config.guess
get_aux_script config.sub

# Some autoreconf versions install the scripts readable by their owner only,
# which breaks configure for anyone else building in a shared tree.
for f in scripts/config.guess scripts/config.sub scripts/install-sh; do
    if [ -e "${f}" ]; then
        chmod 755 "${f}"
    fi
done
