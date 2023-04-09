#Require a minimum version
cmake_minimum_required(VERSION 3.10)

project(ccov)

add_console_app(ccov)

target_link_libraries(ccov uva-console uva-core uva-routing uva-debugger uva-json uva-file)

if(WIN32)
    set(extension ".exe")
else()
    set(extension "")
endif()

add_custom_command(TARGET ccov 
                   POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:ccov> "${CMAKE_SOURCE_DIR}/bin/ccov${extension}")

