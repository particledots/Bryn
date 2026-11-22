# The Bryn engine. Plain C++17, no JUCE, no plugin code: the function (Function, Curve), the gate from notes (Keys),
# the CC stream (CcStream), the Rise gate, Fall gate and EOC (Pulses) and the whole per-sample engine (Engine).
# Curve, Keys and CcStream are Hillside 0.2.0's, unchanged except the namespace; the rest is adapted from Hillside.

add_library(bryn_engine STATIC
    src/Function.cpp
    src/Keys.cpp
    src/CcStream.cpp
    src/Pulses.cpp
    src/Engine.cpp
)

target_include_directories(bryn_engine PUBLIC include)
target_compile_features(bryn_engine PUBLIC cxx_std_17)

if(BRYN_BUILD_TESTS)
    add_executable(bryn_engine_test tests/engine_test.cpp)
    target_link_libraries(bryn_engine_test PRIVATE bryn_engine)
    add_test(NAME engine COMMAND bryn_engine_test)

    # Writes the engine's real MIDI stream to stdout; the mapper test (Node.js) feeds it through the Scripter mapper.
    add_executable(bryn_ccdump tests/ccdump.cpp)
    target_link_libraries(bryn_ccdump PRIVATE bryn_engine)

    find_program(NODE_EXE node)
    if(NODE_EXE)
        add_test(NAME mapper
                 COMMAND ${CMAKE_COMMAND} -E env
                         BRYN_CCDUMP=$<TARGET_FILE:bryn_ccdump>
                         ${NODE_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/mapper_test.js
                         ${CMAKE_SOURCE_DIR}/tools/mapper/Bryn-Mapper.js
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/Hillside-Mapper.js
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/Ogdoad-Mapper.js
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/StrangeDrift-Mapper.js)
    endif()
endif()
