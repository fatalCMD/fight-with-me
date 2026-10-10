cmake_minimum_required(VERSION 3.20)

# Fight With Me: battle orders for followers.
#
# Roles, focus and position for each companion, carried out by the game's own
# combat AI through two seams it already has: the per-fighter combat style and
# the per-fighter target selector. Pairs with Walk With Me, but neither needs
# the other.
#
# Hard requirements: SKSE and Address Library. SKSE Menu Framework 3 provides
# the battle plan window; without it the plugin still runs on its INI defaults.

project(
	FightWithMe
	VERSION 0.1.0
	LANGUAGES CXX
)

add_library("${PROJECT_NAME}" SHARED)

target_compile_features("${PROJECT_NAME}" PRIVATE cxx_std_23)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

file(GLOB_RECURSE FWM_HEADERS LIST_DIRECTORIES false CONFIGURE_DEPENDS "include/*.h")
file(GLOB_RECURSE FWM_SOURCES LIST_DIRECTORIES false CONFIGURE_DEPENDS "src/*.cpp")

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/include" PREFIX "Header Files" FILES ${FWM_HEADERS})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${FWM_SOURCES})
target_sources("${PROJECT_NAME}" PRIVATE ${FWM_HEADERS} ${FWM_SOURCES})

configure_file(cmake/Plugin.h.in "${CMAKE_CURRENT_BINARY_DIR}/cmake/Plugin.h" @ONLY)
configure_file(cmake/version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/cmake/version.rc" @ONLY)
target_sources("${PROJECT_NAME}" PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}/cmake/Plugin.h"
	"${CMAKE_CURRENT_BINARY_DIR}/cmake/version.rc")

target_precompile_headers("${PROJECT_NAME}" PRIVATE include/FWM/PCH.h)

target_include_directories("${PROJECT_NAME}"
	PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
	PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/cmake")

# CommonLibSSE-NG 7.2.0, vendored like Heft and Scene Director: the vcpkg port
# stopped at 3.7.0, which reads 1.7.x as SE. 7.2.0 covers 1.5.97 through
# 1.7.104 from one DLL.
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/CommonLibSSE-NG)

target_link_libraries("${PROJECT_NAME}"
	PUBLIC CommonLibSSE::CommonLibSSE)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)

set(FWM_MSVC_OPTIONS
	/MP /W4 /WX /permissive- /utf-8
	/Zc:alignedNew /Zc:auto /Zc:__cplusplus /Zc:externC /Zc:externConstexpr
	/Zc:forScope /Zc:hiddenFriend /Zc:implicitNoexcept /Zc:lambda
	/Zc:noexceptTypes /Zc:preprocessor /Zc:referenceBinding /Zc:rvalueCast
	/Zc:sizedDealloc /Zc:strictStrings /Zc:ternary /Zc:threadSafeInit
	/Zc:trigraphs
	/wd4200)

if(MSVC)
	add_compile_definitions(_UNICODE)
	target_compile_definitions("${PROJECT_NAME}" PRIVATE "$<$<CONFIG:DEBUG>:DEBUG>" FIGHTWITHME_EXPORTS)
	target_compile_options("${PROJECT_NAME}" PRIVATE ${FWM_MSVC_OPTIONS})

	target_compile_options("${PROJECT_NAME}" PRIVATE
		"$<$<CONFIG:DEBUG>:/fp:strict;/ZI;/Od;/Gy>"
		"$<$<CONFIG:RELEASE>:/Zi;/fp:fast;/GL;/Gy-;/Gm-;/Gw;/sdl-;/GS-;/guard:cf-;/O2;/Ob2;/Oi;/Ot;/Oy;/fp:except->")

	target_link_options("${PROJECT_NAME}" PRIVATE
		/WX
		"$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
		"$<$<CONFIG:RELEASE>:/LTCG;/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>")
endif()

# The rules (who a taunt holds, who a healer picks, which enemy a focus prefers,
# how a role bends a combat style) and the save format have no game types, so
# they are tested here against cases whose answers are known.
option(FWM_BUILD_TESTS "Build Fight With Me rule tests" ON)
if(FWM_BUILD_TESTS)
	add_executable(FightWithMeTests
		tests/RulesTests.cpp
		src/Rules.cpp
		src/StyleLayer.cpp
		src/OrdersCodec.cpp)
	target_compile_features(FightWithMeTests PRIVATE cxx_std_23)
	target_include_directories(FightWithMeTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
	if(MSVC)
		target_compile_options(FightWithMeTests PRIVATE ${FWM_MSVC_OPTIONS})
	endif()
	enable_testing()
	add_test(NAME FightWithMeTests COMMAND FightWithMeTests)
endif()

add_custom_command(TARGET "${PROJECT_NAME}" POST_BUILD
	COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/stage/SKSE/Plugins"
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different
		"$<TARGET_FILE:${PROJECT_NAME}>" "${CMAKE_BINARY_DIR}/stage/SKSE/Plugins/"
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different
		"${CMAKE_CURRENT_SOURCE_DIR}/config/FightWithMe.ini" "${CMAKE_BINARY_DIR}/stage/SKSE/Plugins/"
	VERBATIM)
