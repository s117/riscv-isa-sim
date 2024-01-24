#!/bin/bash
set -e
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
pushd "${SCRIPT_DIR}/../" >/dev/null
autoreconf -i
if [ ! -e scripts/config.guess ]; then
    curl -o scripts/config.guess 'https://git.savannah.gnu.org/gitweb/?p=config.git;a=blob_plain;f=config.guess;hb=HEAD'
    chmod +x scripts/config.guess
fi
if [ ! -e scripts/config.sub ]; then
    curl -o scripts/config.sub 'https://git.savannah.gnu.org/gitweb/?p=config.git;a=blob_plain;f=config.sub;hb=HEAD'
    chmod +x scripts/config.sub
fi
popd >/dev/null
