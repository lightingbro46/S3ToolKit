#!/usr/bin/env bash
set -uo pipefail

readonly PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly BUILD_DIR="${PROJECT_ROOT}/build-test"
readonly REPORT_DIR="${BUILD_DIR}/reports"
readonly JUNIT_REPORT="${REPORT_DIR}/junit/unit-tests.xml"
readonly COVERAGE_REPORT="${REPORT_DIR}/coverage/coverage.xml"

rm -rf "${BUILD_DIR}"
mkdir -p "$(dirname "${JUNIT_REPORT}")" "$(dirname "${COVERAGE_REPORT}")"

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_TESTING=ON \
    -DENABLE_COVERAGE=ON \
    -DENABLE_MYSQL=OFF \
    -DENABLE_OPENSSL=OFF \
    -DENABLE_SQLITE=OFF || exit $?

cmake --build "${BUILD_DIR}" --target unit_tests --parallel || exit $?

test_status=0
GTEST_OUTPUT="xml:${JUNIT_REPORT}" ctest --test-dir "${BUILD_DIR}" --output-on-failure || test_status=$?

coverage_status=0
if ! command -v gcovr >/dev/null 2>&1; then
    echo "error: gcovr is required to generate ${COVERAGE_REPORT}" >&2
    coverage_status=1
else
    gcovr --root "${PROJECT_ROOT}" \
        --filter "${PROJECT_ROOT}/src/" \
        --exclude "${PROJECT_ROOT}/src/win32/" \
        --exclude "${PROJECT_ROOT}/tests/" \
        --fail-under-line 60 \
        --xml-pretty --output "${COVERAGE_REPORT}" \
        "${BUILD_DIR}" || coverage_status=$?
fi

if (( test_status != 0 )); then
    exit "${test_status}"
fi
exit "${coverage_status}"
