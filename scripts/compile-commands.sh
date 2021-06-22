#!/bin/bash

_ANILLO_INCLUDED_FIND_PROGRAMS=1

if [ -z ${_ANILLO_INCLUDED_UTIL+x} ]; then
	echo "Include util.sh before including compile-commands.sh" >&2
	exit 1
fi

COMPILE_COMMANDS_PATH="${BUILD_DIR}/compile_commands.json"

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
	cat "${COMPILE_COMMANDS_PATH}" | jq --tab '. += [{ "directory": "'"$(pwd)"'", "file": "'"$1"'", "command": "'"$2"'" }]' > "${COMPILE_COMMANDS_PATH}"
}
