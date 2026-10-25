file(MAKE_DIRECTORY "${WORK_DIR}")
set(program "${WORK_DIR}/program")

execute_process(
  COMMAND "${THOTH}" ${FLAGS} -o "${program}" "${SOURCE}"
  RESULT_VARIABLE compile_result
  ERROR_VARIABLE compile_errors)
if(NOT compile_result EQUAL 0)
  message(FATAL_ERROR "compiling ${SOURCE} failed:\n${compile_errors}")
endif()

execute_process(
  COMMAND "${program}"
  RESULT_VARIABLE run_result
  OUTPUT_VARIABLE actual)
if(NOT DEFINED EXPECTED_EXIT)
  set(EXPECTED_EXIT 0)
endif()
if(NOT run_result EQUAL EXPECTED_EXIT)
  message(FATAL_ERROR "program exited with ${run_result}, expected ${EXPECTED_EXIT}")
endif()

file(READ "${EXPECTED}" expected)
if(NOT actual STREQUAL expected)
  message(FATAL_ERROR "output differs\n--- expected\n${expected}--- actual\n${actual}")
endif()
