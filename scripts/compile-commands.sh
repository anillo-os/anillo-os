#!/bin/bash

SCRIPT_PATH="$(cd "$(dirname "$0")" >/dev/null 2>&1; pwd -P)"
SOURCE_ROOT="${SCRIPT_PATH}/.."

source "${SOURCE_ROOT}/scripts/util.sh"

#
# clear the compile_commands.json file
#
clear-compile-commands() {
	echo "[]" > "${COMPILE_COMMANDS_PATH}"
}

#
# argument 1 is the file path, any arguments after that are joined with spaces to form the compile command
#
add-compile-command() {
	cat "${COMPILE_COMMANDS_PATH}" | "${JQ}" --tab '. += [{ "directory": "'"$(pwd)"'", "file": "'"$1"'", "command": "'"$2"'" }]' > "${COMPILE_COMMANDS_PATH}"
}

#
# start of execution
#

if command-exists jq; then
	JQ=jq
else
	die-red "jq not found"
fi

for ARCH in x86_64 aarch64; do
	export COMPILE_COMMANDS_PATH="${BUILD_ROOT}/${ARCH}/compile_commands.json"
	export ARCH
	export -f add-compile-command
	export ANILLO_GENERATING_COMPILE_COMMANDS=1
	export JQ

	mkdir -p "$(dirname "${COMPILE_COMMANDS_PATH}")"
	clear-compile-commands

	"${SOURCE_ROOT}/kernel/scripts/bootstraps/uefi/compile.sh"
	"${SOURCE_ROOT}/kernel/scripts/compile.sh"
done
