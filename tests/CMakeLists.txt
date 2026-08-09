add_executable(glyphware_smoke smoke.cpp)
target_link_libraries(glyphware_smoke PRIVATE glyphware::glyphware)

add_test(NAME smoke COMMAND glyphware_smoke)
set_tests_properties(smoke PROPERTIES SKIP_RETURN_CODE 77)
