#   Copyright (C) 2015-2019 Virgil Security Inc.
#
#   All rights reserved.
#
#   Redistribution and use in source and binary forms, with or without
#   modification, are permitted provided that the following conditions are
#   met:
#
#       (1) Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#
#       (2) Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#
#       (3) Neither the name of the copyright holder nor the names of its
#       contributors may be used to endorse or promote products derived from
#       this software without specific prior written permission.
#
#   THIS SOFTWARE IS PROVIDED BY THE AUTHOR ''AS IS'' AND ANY EXPRESS OR
#   IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
#   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
#   DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
#   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
#   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
#   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
#   HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
#   STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
#   IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
#   POSSIBILITY OF SUCH DAMAGE.
#
#   Lead Maintainer: Virgil Security Inc. <support@virgilsecurity.com>

cmake_minimum_required(VERSION 3.11 FATAL_ERROR)

project(virgil-iot-sdk-tests VERSION 0.1.0 LANGUAGES C)

#
#  Tests library
#

add_library(virgil-iot-sdk-tests)

target_sources(virgil-iot-sdk-tests
        PRIVATE
        src/tests.c
        src/sdmp/prvs_tests.c
        src/sdmp/sdmp_tests.c
        src/sdmp/test_prvs.c
        src/sdmp/test_netif.c
        src/crypto/aes.c
        src/crypto/ecdh.c
        src/crypto/ecdsa.c
        src/crypto/hash.c
        src/crypto/hmac.c
        src/crypto/kdf.c
        src/crypto/keypair.c
        src/crypto/random.c
        src/crypto/helpers.c
        src/crypto/trust_list.c
        src/crypto/secbox_test.c
        src/crypto/virgil_ecies.c
        )

if (NOT VIRGIL_IOT_MCU_BUILD)
    target_sources(virgil-iot-sdk-tests
            PRIVATE
            src/crypto/sign_converters.c
            src/crypto/pubkeys_converters.c
            )
    target_link_libraries(virgil-iot-sdk-tests
            converters
            )
endif()

target_link_libraries(virgil-iot-sdk-tests
        hsm
        secbox
        sdmp
        trust_list
        macros
        )

#
#   Set additional compiler flags
#

if ("${CFLAGS_PLATFORM}" STREQUAL "")
    message("No CFLAGS_PLATFORM")
else()
    message("CFLAGS_PLATFORM = ${CFLAGS_PLATFORM}")
    string(REPLACE " " ";" CFLAGS_PLATFORM ${CFLAGS_PLATFORM})
endif()

target_compile_options(virgil-iot-sdk-tests
        PRIVATE -Wall -Werror ${CFLAGS_PLATFORM})

target_compile_definitions(virgil-iot-sdk-tests
        PRIVATE "VIRGIL_IOT_MCU_BUILD=$<BOOL:${VIRGIL_IOT_MCU_BUILD}>"
        )

#
#   Config path
#

if(NOT VIRGIL_IOT_CONFIG_DIRECTORY)
    message(FATAL_ERROR "[virgil-iot-sdk-tests] VIRGIL_IOT_CONFIG_DIRECTORY variable containing path to the Virgil IOT SDK config is not specified")
else()
    message(STATUS "[virgil-iot-sdk-tests] path to the Virgil IOT SDK config : ${VIRGIL_IOT_CONFIG_DIRECTORY}")
endif()

#
#   Common include directories
#
target_include_directories(virgil-iot-sdk-tests
        PRIVATE
        "$<BUILD_INTERFACE:${VIRGIL_IOT_CONFIG_DIRECTORY}>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
        "$<BUILD_INTERFACE:${VIRGIL_IOT_SDK_HAL_INC_PATH}>"

        INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
        )

#add_subdirectory(fuzzytesting)

cmake_minimum_required(VERSION 3.11 FATAL_ERROR)

if (NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    # not using regular Clang or AppleClang

    message("Error. Use Clang for fuzzy testing.")
endif()

macro(_add_test test)
    add_executable(${test} src/sdmp/${test}.c
            src/sdmp/test_netif.c
            )
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

    target_include_directories(${test}
            PRIVATE
            "$<BUILD_INTERFACE:${VIRGIL_IOT_CONFIG_DIRECTORY}>"
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
            "$<BUILD_INTERFACE:${VIRGIL_IOT_SDK_HAL_INC_PATH}>"

            INTERFACE
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
            "$<INSTALL_INTERFACE:include>"
            )

    if(COMMAND add_clangformat)
        add_clangformat(${test})
    endif()
endmacro()

_add_test(sdmp-fuzz)


