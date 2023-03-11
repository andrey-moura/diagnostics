add_library(uva-debugger
	${CMAKE_CURRENT_LIST_DIR}/src/basic_debugger.cpp
)

if(WIN32)
	target_link_libraries(uva-debugger Dbghelp.lib)
endif()

# ccov
include(${CMAKE_CURRENT_LIST_DIR}/ccov/CMakeLists.txt)

include_directories(${CMAKE_CURRENT_LIST_DIR}/include)