#!/bin/bash

SCRIPT_PATH="$(cd "$(dirname "$0")" >/dev/null 2>&1; pwd -P)"
SOURCE_ROOT="${SCRIPT_PATH}/.."

source "${SOURCE_ROOT}/scripts/util.sh"

"${SOURCE_ROOT}/kernel/scripts/generate.sh" || command-failed
