if(NOT DEFINED INPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE is required")
endif()

file(READ "${INPUT_FILE}" SOURCE)

set(WEBPACK_NODE_IMPORT [[await import(/* webpackIgnore: true */ "node:module")]])

string(FIND "${SOURCE}" "${WEBPACK_NODE_IMPORT}" PATCHED_IMPORT_INDEX)
if(NOT PATCHED_IMPORT_INDEX EQUAL -1)
    return()
endif()

set(EMSCRIPTEN_NODE_IMPORTS
    [[await import("module")]]
    [[await import("node:module")]])

set(NODE_IMPORT "")
foreach(CANDIDATE IN LISTS EMSCRIPTEN_NODE_IMPORTS)
    string(FIND "${SOURCE}" "${CANDIDATE}" NODE_IMPORT_INDEX)
    if(NOT NODE_IMPORT_INDEX EQUAL -1)
        set(NODE_IMPORT "${CANDIDATE}")
        break()
    endif()
endforeach()

if(NODE_IMPORT STREQUAL "")
    message(FATAL_ERROR "Could not find the Emscripten Node import in ${INPUT_FILE}")
endif()

string(REPLACE "${NODE_IMPORT}" "${WEBPACK_NODE_IMPORT}" SOURCE "${SOURCE}")
file(WRITE "${INPUT_FILE}" "${SOURCE}")
