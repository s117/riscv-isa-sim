#!/bin/bash
set -e
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
pushd "${SCRIPT_DIR}/../" >/dev/null
autoreconf -i
popd >/dev/null
