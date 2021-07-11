#!/bin/bash

SCRIPT_PATH="$(cd "$(dirname "$0")" >/dev/null 2>&1; pwd -P)"
SOURCE_ROOT="${SCRIPT_PATH}/.."

source "${SOURCE_ROOT}/scripts/util.sh"

"${SCRIPT_PATH}/generate.sh"

"${SOURCE_ROOT}/kernel/scripts/bootstraps/uefi/compile.sh" || command-failed
"${SOURCE_ROOT}/kernel/scripts/compile.sh" || command-failed
