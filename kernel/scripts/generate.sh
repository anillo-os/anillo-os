#!/bin/bash

SCRIPT_PATH="$(cd "$(dirname "$0")" >/dev/null 2>&1; pwd -P)"
SOURCE_ROOT="${SCRIPT_PATH}/../.."
KERNEL_SOURCE_ROOT="${SOURCE_ROOT}/kernel"

source "${SOURCE_ROOT}/scripts/util.sh"

CURRENT_BUILD_DIR="${BUILD_DIR}/kernel"

mkdir -p "${CURRENT_BUILD_DIR}"

pushd "${CURRENT_BUILD_DIR}" >/dev/null

#
# font processing
#

OUTPUT_HEADER_PATH="${CURRENT_BUILD_DIR}/include/gen/ferro/font.h"
INPUT_FONT_PATH="${KERNEL_SOURCE_ROOT}/resources/Lat15-TerminusBold32x16.psf"
if file-is-newer "${SCRIPT_PATH}/process-font.py" "${OUTPUT_HEADER_PATH}" || file-is-newer "${INPUT_FONT_PATH}" "${OUTPUT_HEADER_PATH}"; then
	echo "$(color-blue GEN) $(normalize "${OUTPUT_HEADER_PATH}")"
	"${SCRIPT_PATH}/process-font.py" || command-failed
fi

#
# offset calculation
#

OUTPUT_HEADER_PATH="${CURRENT_BUILD_DIR}/include/gen/ferro/offsets.h"
echo "$(color-blue GEN) $(normalize "${OUTPUT_HEADER_PATH}")"
"${SCRIPT_PATH}/calculate-offsets.py" || command-failed

#
# target.xml encoding
#
OUTPUT_HEADER_PATH="${CURRENT_BUILD_DIR}/include/gen/ferro/gdbstub/target.xml.h"
INPUT_FILE_PATH="${KERNEL_SOURCE_ROOT}/src/gdbstub/${ARCH}/target.xml"
if file-is-newer "${SCRIPT_PATH}/encode-file.py" "${OUTPUT_HEADER_PATH}" || file-is-newer "${INPUT_FILE_PATH}" "${OUTPUT_HEADER_PATH}"; then
	echo "$(color-blue GEN) $(normalize "${OUTPUT_HEADER_PATH}")"
	"${SCRIPT_PATH}/encode-file.py" || command-failed
fi

popd >/dev/null
