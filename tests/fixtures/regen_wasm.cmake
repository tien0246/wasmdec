# B5 helper: regenerate <wasm> from <wat> only when the .wat is newer.
if(EXISTS "${WASM_FILE}")
  if("${WAT_FILE}" IS_NEWER_THAN "${WASM_FILE}")
    separate_arguments(FLAGS_LIST NATIVE_COMMAND "${FLAGS}")
    execute_process(
      COMMAND "${WAT}" ${FLAGS_LIST} "${WAT_FILE}" -o "${WASM_FILE}"
      RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
      message(FATAL_ERROR "wat2wasm failed for ${WAT_FILE}")
    endif()
    message(STATUS "regenerated ${WASM_FILE}")
  endif()
endif()
file(WRITE "${STAMP}" "")
