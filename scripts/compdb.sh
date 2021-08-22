#!/bin/bash

#
# this script implements a basic compilation database for Anillo's build system, to avoid unnecessarily recompiling files
#

#
# ***
# THIS FILE IS STILL IN DEVELOPMENT
# ***
#

_ANILLO_INCLUDED_COMPBD=1

if [ -z ${_ANILLO_INCLUDED_UTIL+x} ]; then
	# since we don't have util.sh included, we can't use `die`
	echo "Include util.sh before including comdb.sh" >&2
	exit 1
fi

if [ -z ${_ANILLO_INCLUDED_FIND_PROGRAMS+x} ]; then
	die-red "Include find-programs.sh before including compdb.sh"
fi

COMPDB_PATH="${BUILD_DIR}/compdb.json"
JQ="jq"

#
# returns the SHA256 hash of the given file AND its compilation flags
#
compdb-hash() {
	EXTENSION="$(extname "${1}")"

	case "${EXTENSION}" in
		s|S)
			shasum -a 256 -b - <<< "${1}${ASFLAGS_ALL[@]}" | cut -d ' ' -f 1
			;;

		c)
			shasum -a 256 -b - <<< "${1}${CFLAGS_ALL[@]}" | cut -d ' ' -f 1
			;;

		*)
			die-red "Unsupported/unrecognized file extension \"${EXTENSION}\" (for file \"${1}\")"
			;;
	esac
}

#
# returns a list of headers on which the given file depends (not including itself)
#
compdb-cc-deps() {
	EXTENSION="$(extname "${1}")"

	case "${EXTENSION}" in
		s|S)
			ls -1 2>&- "$("${CC}" -M "${1}" "${ASFLAGS_ALL[@]}" | grep -v "${1}")"
			;;

		c)
			ls -1 2>&- "$("${CC}" -M "${1}" "${CFLAGS_ALL[@]}" | grep -v "${1}")"
			;;

		*)
			die-red "Unsupported/unrecognized file extension \"${EXTENSION}\" (for file \"${1}\")"
			;;
	esac

	return 0
}

#
# looks up the saved hash for the given file
#
# if the compdb does not contain a hash for the given file, returns an empty string
#
compdb-lookup-hash() {
	"${JQ}" ".hashes[\"${1}\"] // empty" "${COMPDB_PATH}"
}

#
# updates the hashes of the given file and its dependencies in the compilation database
#
compdb-update() {
	FILES=(
		"${1}"
		$(compdb-cc-deps "${1}")
	)
	MAIN_HASH="$(compdb-hash "${1}")"
	SAVED_MAIN_HASH="$(compdb-lookup-hash "${1}")"

	if [ "${SAVED_MAIN_HASH}" != "${MAIN_HASH}" ]; then
		# we need to re-generate
	fi
}

#
# returns 0/true if the file and its dependencies are up-to-date,
# or 1/false otherwise
#
compdb-check() {

}

if ! [ -f "${COMPDB_PATH}" ]; then
	echo "{}" > "${COMPDB_PATH}"
fi
