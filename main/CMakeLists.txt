idf_component_register(SRCS
        "src/main.cpp"
        "src/sdcard.cpp"
        "src/server.cpp"
        "src/wifi.cpp"
        "src/uart.cpp"
        "src/utils.cpp"
        "src/multipart.cpp"
        INCLUDE_DIRS ".")

# ---------------------------------------------------------------
# Generates resource header files from any JS, CSS or HTML files
# ---------------------------------------------------------------

message("Building resource files in ${CMAKE_HOST_SYSTEM_NAME}")

if (${CMAKE_HOST_SYSTEM_NAME} STREQUAL "Windows")
    set(XXD_COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/resources/tools/xxd.exe")
    set(SED_COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/resources/tools/sed.exe")
else()
    set(XXD_COMMAND "xxd")
    set(SED_COMMAND "sed")
endif()

set(EMBEDDED_FILES
        "server_main.css"
        "server_main.html"
        "server_main.js"
        "jquery-3.6.4.min.js"
        "bootstrap.min.css"
        "favicon.png")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/resources/include)
add_custom_target(embed_resources DEPENDS ${EMBEDDED_SRC})

foreach(EMBEDDED_FILE ${EMBEDDED_FILES})
    string(REGEX REPLACE "\\.([A-Za-z]+)$" "_\\1.h" EMBEDDED_SRC ${EMBEDDED_FILE})
    set(INFILE_PATH_TMP ${CMAKE_CURRENT_SOURCE_DIR}/resources/${EMBEDDED_FILE})
    cmake_path(CONVERT ${INFILE_PATH_TMP} TO_NATIVE_PATH_LIST INFILE_PATH NORMALIZE)
    set(OUTFILE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/resources/include/${EMBEDDED_SRC})

    add_custom_command(
            OUTPUT ${OUTFILE_PATH}
            PRE_BUILD
            COMMAND copy ${INFILE_PATH} ${EMBEDDED_FILE}
            COMMAND "${XXD_COMMAND}" -i "${EMBEDDED_FILE}" > ${EMBEDDED_SRC}
            COMMAND ${SED_COMMAND} -e "s/unsigned /const /g" ${EMBEDDED_SRC} > ${OUTFILE_PATH}
            DEPENDS ${INFILE_PATH}
    )
    add_custom_target(${EMBEDDED_SRC} DEPENDS ${OUTFILE_PATH})
    add_dependencies(embed_resources ${EMBEDDED_SRC})
endforeach()

add_dependencies(app embed_resources)
