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
	VERSION 0.3.0
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
		src/Cooldowns.cpp
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

	# The Paths trees, their rules and their save record.
	add_executable(FightWithMePathsTests
		tests/PathsTests.cpp
		src/Paths.cpp)
	target_compile_features(FightWithMePathsTests PRIVATE cxx_std_23)
	target_include_directories(FightWithMePathsTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
	if(MSVC)
		target_compile_options(FightWithMePathsTests PRIVATE ${FWM_MSVC_OPTIONS})
	endif()
	add_test(NAME FightWithMePathsTests COMMAND FightWithMePathsTests)

	# Exercise runtime log arguments without starting Skyrim, including the null
	# particle model name seen in the Earthshaker crash report.
	add_executable(FightWithMeLogTests tests/LogTests.cpp)
	target_compile_features(FightWithMeLogTests PRIVATE cxx_std_23)
	target_include_directories(FightWithMeLogTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
	if(MSVC)
		target_compile_options(FightWithMeLogTests PRIVATE ${FWM_MSVC_OPTIONS})
	endif()
	add_test(NAME FightWithMeLogTests COMMAND FightWithMeLogTests)
endif()

# Paths - Follower Ability Skilltree, the perk tree add-on: its own DLL and release, built from the same
# trees (src/Paths.cpp) so the two can never disagree about them. Its source is a repository of its own,
# checked out into paths/; without it, Fight With Me builds alone.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/paths/src/Main.cpp")
set(FWMP_VERSION_MAJOR 0)
set(FWMP_VERSION_MINOR 1)
set(FWMP_VERSION_PATCH 0)
set(FWMP_VERSION "${FWMP_VERSION_MAJOR}.${FWMP_VERSION_MINOR}.${FWMP_VERSION_PATCH}")
configure_file(paths/cmake/version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/paths/version.rc" @ONLY)
add_library(FightWithMePaths SHARED
	paths/src/Main.cpp
	src/Paths.cpp
	include/FWM/Paths.h
	include/FightWithMePathsAPI.h
	"${CMAKE_CURRENT_BINARY_DIR}/paths/version.rc")
target_compile_features(FightWithMePaths PRIVATE cxx_std_23)
target_precompile_headers(FightWithMePaths PRIVATE include/FWM/PCH.h)
target_include_directories(FightWithMePaths PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(FightWithMePaths PRIVATE CommonLibSSE::CommonLibSSE)
if(MSVC)
	target_compile_definitions(FightWithMePaths PRIVATE FIGHTWITHMEPATHS_EXPORTS
		FWMP_VERSION_MAJOR=${FWMP_VERSION_MAJOR} FWMP_VERSION_MINOR=${FWMP_VERSION_MINOR} FWMP_VERSION_PATCH=${FWMP_VERSION_PATCH})
	target_compile_options(FightWithMePaths PRIVATE ${FWM_MSVC_OPTIONS}
		"$<$<CONFIG:RELEASE>:/Zi;/fp:fast;/GL;/Gy-;/Gm-;/Gw;/sdl-;/GS-;/guard:cf-;/O2;/Ob2;/Oi;/Ot;/Oy;/fp:except->")
	target_link_options(FightWithMePaths PRIVATE /WX "$<$<CONFIG:RELEASE>:/LTCG;/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>")
endif()
add_custom_command(TARGET FightWithMePaths POST_BUILD
	COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/stage-paths/SKSE/Plugins"
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different
		"$<TARGET_FILE:FightWithMePaths>" "${CMAKE_BINARY_DIR}/stage-paths/SKSE/Plugins/"
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different
		"${CMAKE_CURRENT_SOURCE_DIR}/paths/config/FightWithMePaths.ini" "${CMAKE_BINARY_DIR}/stage-paths/SKSE/Plugins/"
	VERBATIM)
endif()

add_custom_command(TARGET "${PROJECT_NAME}" POST_BUILD
	COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/stage/SKSE/Plugins"
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different
		"$<TARGET_FILE:${PROJECT_NAME}>" "${CMAKE_BINARY_DIR}/stage/SKSE/Plugins/"
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different
		"${CMAKE_CURRENT_SOURCE_DIR}/config/FightWithMe.ini" "${CMAKE_BINARY_DIR}/stage/SKSE/Plugins/"
	VERBATIM)
