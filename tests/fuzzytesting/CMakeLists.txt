cmake_minimum_required(VERSION 3.11 FATAL_ERROR)

if (NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    # not using regular Clang or AppleClang

    message("Error. Use Clang for fuzzy testing.")
endif()

macro(_add_test test)
    add_executable(${test} src/${test}.c)
    set_target_properties(${test} PROPERTIES C_STANDARD "99")
    set_target_properties(${test} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${test})
    add_test (NAME ${test} COMMAND ${test})

    target_compile_options(${test}
            PRIVATE
            $<$<C_COMPILER_ID:Clang>: -Wall -Werror -g -O1 -fsanitize=fuzzer,address -fno-omit-frame-pointer>
            $<$<C_COMPILER_ID:AppleClang>: -Wall -Werror -g -O1 -fsanitize=fuzzer,address -fno-omit-frame-pointer>
            )

    target_link_libraries(${test}
            sdmp
            $<$<C_COMPILER_ID:Clang>:-fsanitize=fuzzer,address>
            $<$<C_COMPILER_ID:AppleClang>:-fsanitize=fuzzer,address>
            )

    target_include_directories(virgil-iot-sdk-tests
            PRIVATE
            $<BUILD_INTERFACE:${VIRGIL_IOT_CONFIG_DIRECTORY}>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../protocols/sdmp/include>
            )

    if(COMMAND add_clangformat)
        add_clangformat(${test})
    endif()
endmacro()

_add_test(sdmp-fuzz)