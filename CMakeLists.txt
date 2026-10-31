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
	VERSION 0.6.0
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
    add_executable(FightWithMePartyBalanceTests tests/PartyBalanceTests.cpp)
    target_compile_features(FightWithMePartyBalanceTests PRIVATE cxx_std_23)
    target_include_directories(FightWithMePartyBalanceTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    if(MSVC)
        target_compile_options(FightWithMePartyBalanceTests PRIVATE ${FWM_MSVC_OPTIONS})
    endif()
    add_test(NAME FightWithMePartyBalanceTests COMMAND FightWithMePartyBalanceTests)
	foreach(target FightWithMeSplitTests FightWithMePathsSplitTests)
		add_executable(${target} tests/PluginSplitTests.cpp src/Host.cpp)
		target_compile_features(${target} PRIVATE cxx_std_23)
		target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
		if(MSVC)
			target_compile_options(${target} PRIVATE ${FWM_MSVC_OPTIONS})
		endif()
		add_test(NAME ${target} COMMAND ${target})
	endforeach()
	target_compile_definitions(FightWithMePathsSplitTests PRIVATE FWM_HOST_PATHS)
	add_executable(FightWithMeAbilityTests tests/AbilityPolicyTests.cpp src/AbilityPolicy.cpp)
	target_compile_features(FightWithMeAbilityTests PRIVATE cxx_std_23)
	target_include_directories(FightWithMeAbilityTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
	if(MSVC)
		target_compile_options(FightWithMeAbilityTests PRIVATE ${FWM_MSVC_OPTIONS})
	endif()
	add_test(NAME FightWithMeAbilityTests COMMAND FightWithMeAbilityTests)
	add_executable(FightWithMeTests
		tests/RulesTests.cpp
		src/Cooldowns.cpp
		src/Text.cpp
		src/Rules.cpp
		src/StyleLayer.cpp
		src/TraitRules.cpp
		src/LevelRules.cpp
		src/OrdersCodec.cpp
		src/WolfBite.cpp
		src/HealCircle.cpp
		src/Rejuvenation.cpp
		src/Blast.cpp
		src/MagnusOrb.cpp
		src/Quake.cpp
		src/ArrowRain.cpp
		src/WaterCloak.cpp
		src/Haunt.cpp)
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
		src/Paths.cpp
		src/Text.cpp)
	target_compile_features(FightWithMePathsTests PRIVATE cxx_std_23)
	target_include_directories(FightWithMePathsTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
	if(MSVC)
		target_compile_options(FightWithMePathsTests PRIVATE ${FWM_MSVC_OPTIONS})
	endif()
	add_test(NAME FightWithMePathsTests COMMAND FightWithMePathsTests)

	# Tank rescue thresholds and final health-loss sharing are independent of game state.
	add_executable(FightWithMeTankTests tests/TankPolicyTests.cpp)
	target_compile_features(FightWithMeTankTests PRIVATE cxx_std_23)
	target_include_directories(FightWithMeTankTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
	if(MSVC)
		target_compile_options(FightWithMeTankTests PRIVATE ${FWM_MSVC_OPTIONS})
	endif()
	add_test(NAME FightWithMeTankTests COMMAND FightWithMeTankTests)

	# Weak Will's helmet preview uses the same pose and material sampler as Skyrim.
	add_executable(FightWithMeWeakWillLook tools/weakwill/WeakWillLook.cpp src/WeakWill.cpp)
	add_executable(FightWithMeWeakWillTests tests/WeakWillTests.cpp src/WeakWill.cpp)
	target_compile_features(FightWithMeWeakWillLook PRIVATE cxx_std_23)
	target_include_directories(FightWithMeWeakWillLook PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
	target_compile_features(FightWithMeWeakWillTests PRIVATE cxx_std_23)
	target_include_directories(FightWithMeWeakWillTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
	if(MSVC)
		target_compile_options(FightWithMeWeakWillLook PRIVATE ${FWM_MSVC_OPTIONS})
		target_compile_options(FightWithMeWeakWillTests PRIVATE ${FWM_MSVC_OPTIONS})
	endif()
	add_test(NAME FightWithMeWeakWillTests COMMAND FightWithMeWeakWillTests)

	add_executable(FightWithMeBastionLook tools/bastion/BastionLook.cpp src/Bastion.cpp)
	add_executable(FightWithMeBastionTests tests/BastionTests.cpp src/Bastion.cpp)
	foreach(target FightWithMeBastionLook FightWithMeBastionTests)
		target_compile_features(${target} PRIVATE cxx_std_23)
		target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
		if(MSVC)
			target_compile_options(${target} PRIVATE ${FWM_MSVC_OPTIONS})
		endif()
	endforeach()
	add_test(NAME FightWithMeBastionTests COMMAND FightWithMeBastionTests)
	add_executable(FightWithMeSoundLook tools/fx/SoundLook.cpp src/Soundscape.cpp)
	add_executable(FightWithMeSoundTests tests/SoundscapeTests.cpp src/Soundscape.cpp)
	foreach(target FightWithMeSoundLook FightWithMeSoundTests)
		target_compile_features(${target} PRIVATE cxx_std_23)
		target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
		if(MSVC)
			target_compile_options(${target} PRIVATE ${FWM_MSVC_OPTIONS})
		endif()
	endforeach()
	add_test(NAME FightWithMeSoundTests COMMAND FightWithMeSoundTests)
	add_executable(FightWithMeVisualPersistenceTests tests/VisualPersistenceTests.cpp src/HealCircle.cpp src/Rejuvenation.cpp src/ChaosPulse.cpp src/WolfBite.cpp)
	target_compile_features(FightWithMeVisualPersistenceTests PRIVATE cxx_std_23)
	target_include_directories(FightWithMeVisualPersistenceTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
	if(MSVC)
		target_compile_options(FightWithMeVisualPersistenceTests PRIVATE ${FWM_MSVC_OPTIONS})
	endif()
	add_test(NAME FightWithMeVisualPersistenceTests COMMAND FightWithMeVisualPersistenceTests)

	# Exercise runtime log arguments without starting Skyrim, including the null
	# particle model name seen in the Earthshaker crash report.
	add_executable(FightWithMeLogTests tests/LogTests.cpp)
	target_compile_features(FightWithMeLogTests PRIVATE cxx_std_23)
	target_include_directories(FightWithMeLogTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
	if(MSVC)
		target_compile_options(FightWithMeLogTests PRIVATE ${FWM_MSVC_OPTIONS})
	endif()
	add_test(NAME FightWithMeLogTests COMMAND FightWithMeLogTests)

	# The healer's circle, sampled the same way.
	add_executable(FightWithMeCircleLook tools/healcircle/CircleLook.cpp src/HealCircle.cpp)
	target_compile_features(FightWithMeCircleLook PRIVATE cxx_std_23)
	target_include_directories(FightWithMeCircleLook PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
	if(MSVC)
		target_compile_options(FightWithMeCircleLook PRIVATE ${FWM_MSVC_OPTIONS})
	endif()

	# Elemental Burst's blasts, sampled the same way.
	add_executable(FightWithMeBlastLook tools/blast/BlastLook.cpp src/Blast.cpp)
	target_compile_features(FightWithMeBlastLook PRIVATE cxx_std_23)
	target_include_directories(FightWithMeBlastLook PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
	if(MSVC)
		target_compile_options(FightWithMeBlastLook PRIVATE ${FWM_MSVC_OPTIONS})
	endif()

    add_executable(FightWithMeSupportArtTests tests/SupportArtTests.cpp)
    target_compile_features(FightWithMeSupportArtTests PRIVATE cxx_std_23)
    target_include_directories(FightWithMeSupportArtTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    if(MSVC)
        target_compile_options(FightWithMeSupportArtTests PRIVATE ${FWM_MSVC_OPTIONS})
    endif()
    add_test(NAME FightWithMeSupportArtTests COMMAND FightWithMeSupportArtTests)
    add_executable(FightWithMeSupportLook tools/fx/SupportLook.cpp)
    target_compile_features(FightWithMeSupportLook PRIVATE cxx_std_23)
    target_include_directories(FightWithMeSupportLook PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    if(MSVC)
        target_compile_options(FightWithMeSupportLook PRIVATE ${FWM_MSVC_OPTIONS})
    endif()


	# Rejuvenation's seal, sampled from its runtime animation.
	add_executable(FightWithMeRejuvenationLook tools/rejuvenation/RejuvenationLook.cpp src/Rejuvenation.cpp)
	target_compile_features(FightWithMeRejuvenationLook PRIVATE cxx_std_23)
	target_include_directories(FightWithMeRejuvenationLook PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
	if(MSVC)
		target_compile_options(FightWithMeRejuvenationLook PRIVATE ${FWM_MSVC_OPTIONS})
	endif()

	# Magnus's orb, sampled the same way.
	add_executable(FightWithMeMagnusLook tools/magnus/MagnusLook.cpp src/MagnusOrb.cpp)
	target_compile_features(FightWithMeMagnusLook PRIVATE cxx_std_23)
	target_include_directories(FightWithMeMagnusLook PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
	if(MSVC)
		target_compile_options(FightWithMeMagnusLook PRIVATE ${FWM_MSVC_OPTIONS})
	endif()

    add_executable(FightWithMeDisarmLook tools/disarm/DisarmLook.cpp src/DisarmFx.cpp)
    add_executable(FightWithMeDisarmTests tests/DisarmTests.cpp src/DisarmFx.cpp)
    foreach(target FightWithMeDisarmLook FightWithMeDisarmTests)
        target_compile_features(${target} PRIVATE cxx_std_23)
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
        if(MSVC)
            target_compile_options(${target} PRIVATE ${FWM_MSVC_OPTIONS})
        endif()
    endforeach()
    add_test(NAME FightWithMeDisarmTests COMMAND FightWithMeDisarmTests)
    add_executable(FightWithMeSecondWindLook tools/secondwind/SecondWindLook.cpp src/SecondWindFx.cpp)
    add_executable(FightWithMeSecondWindTests tests/SecondWindTests.cpp src/SecondWindFx.cpp)
    foreach(target FightWithMeSecondWindLook FightWithMeSecondWindTests)
        target_compile_features(${target} PRIVATE cxx_std_23)
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
        if(MSVC)
            target_compile_options(${target} PRIVATE ${FWM_MSVC_OPTIONS})
        endif()
    endforeach()
    add_test(NAME FightWithMeSecondWindTests COMMAND FightWithMeSecondWindTests)
    add_executable(FightWithMeWindstepLook tools/windstep/WindstepLook.cpp src/WindstepFx.cpp)
    add_executable(FightWithMeWindstepTests tests/WindstepTests.cpp src/WindstepFx.cpp)
    foreach(target FightWithMeWindstepLook FightWithMeWindstepTests)
        target_compile_features(${target} PRIVATE cxx_std_23)
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
        if(MSVC)
            target_compile_options(${target} PRIVATE ${FWM_MSVC_OPTIONS})
        endif()
    endforeach()
    add_test(NAME FightWithMeWindstepTests COMMAND FightWithMeWindstepTests)
    add_executable(FightWithMeArrowArtsLook tools/arrowarts/ArrowArtsLook.cpp src/ArrowArtsFx.cpp)
    add_executable(FightWithMeArrowArtsTests tests/ArrowArtsTests.cpp src/ArrowArtsFx.cpp)
    foreach(target FightWithMeArrowArtsLook FightWithMeArrowArtsTests)
        target_compile_features(${target} PRIVATE cxx_std_23)
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
        if(MSVC)
            target_compile_options(${target} PRIVATE ${FWM_MSVC_OPTIONS})
        endif()
    endforeach()
    add_test(NAME FightWithMeArrowArtsTests COMMAND FightWithMeArrowArtsTests)

    add_executable(FightWithMeRiposteLook tools/riposte/RiposteLook.cpp src/RiposteFx.cpp)
    add_executable(FightWithMeRiposteTests tests/RiposteTests.cpp src/RiposteFx.cpp)
    foreach(target FightWithMeRiposteLook FightWithMeRiposteTests)
        target_compile_features(${target} PRIVATE cxx_std_23)
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
        if(MSVC)
            target_compile_options(${target} PRIVATE ${FWM_MSVC_OPTIONS})
        endif()
    endforeach()
    add_test(NAME FightWithMeRiposteTests COMMAND FightWithMeRiposteTests)

    add_executable(FightWithMeMoonLook tools/azurasmoon/MoonLook.cpp src/MoonFx.cpp)
    add_executable(FightWithMeMoonTests tests/MoonTests.cpp src/MoonFx.cpp)
    foreach(target FightWithMeMoonLook FightWithMeMoonTests)
        target_compile_features(${target} PRIVATE cxx_std_23)
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
        if(MSVC)
            target_compile_options(${target} PRIVATE ${FWM_MSVC_OPTIONS})
        endif()
    endforeach()
    add_test(NAME FightWithMeMoonTests COMMAND FightWithMeMoonTests)

	# Sunder's fracture, sampled and checked from its runtime animation.
	add_executable(FightWithMeSunderLook tools/sunder/SunderLook.cpp src/Sunder.cpp)
	add_executable(FightWithMeSunderTests tests/SunderTests.cpp src/Sunder.cpp)
	foreach(target FightWithMeSunderLook FightWithMeSunderTests)
		target_compile_features(${target} PRIVATE cxx_std_23)
		target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
		if(MSVC)
			target_compile_options(${target} PRIVATE ${FWM_MSVC_OPTIONS})
		endif()
	endforeach()
	add_test(NAME FightWithMeSunderTests COMMAND FightWithMeSunderTests)

	add_executable(FightWithMeBerserkLook tools/berserk/BerserkLook.cpp src/Berserk.cpp)
	add_executable(FightWithMeBerserkTests tests/BerserkTests.cpp src/Berserk.cpp)
	foreach(target FightWithMeBerserkLook FightWithMeBerserkTests)
		target_compile_features(${target} PRIVATE cxx_std_23)
		target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
		if(MSVC)
			target_compile_options(${target} PRIVATE ${FWM_MSVC_OPTIONS})
		endif()
	endforeach()
	add_test(NAME FightWithMeBerserkTests COMMAND FightWithMeBerserkTests)
	add_executable(FightWithMeArcaneSurgeLook tools/arcanesurge/ArcaneSurgeLook.cpp)
	add_executable(FightWithMeArcaneSurgeTests tests/ArcaneSurgeTests.cpp)
	foreach(target FightWithMeArcaneSurgeLook FightWithMeArcaneSurgeTests)
		target_compile_features(${target} PRIVATE cxx_std_23)
		target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
		if(MSVC)
			target_compile_options(${target} PRIVATE ${FWM_MSVC_OPTIONS})
		endif()
	endforeach()
	add_test(NAME FightWithMeArcaneSurgeTests COMMAND FightWithMeArcaneSurgeTests)

	# Earthshaker's slam, sampled the same way.
	add_executable(FightWithMeQuakeLook tools/quake/QuakeLook.cpp src/Quake.cpp)
	target_compile_features(FightWithMeQuakeLook PRIVATE cxx_std_23)
	target_include_directories(FightWithMeQuakeLook PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
	if(MSVC)
		target_compile_options(FightWithMeQuakeLook PRIVATE ${FWM_MSVC_OPTIONS})
	endif()

	# Arrow Rain's look, sampled the same way, with stand-in arrows.
	add_executable(FightWithMeArrowRainLook tools/arrowrain/ArrowRainLook.cpp src/ArrowRain.cpp src/Blast.cpp)
	target_compile_features(FightWithMeArrowRainLook PRIVATE cxx_std_23)
	target_include_directories(FightWithMeArrowRainLook PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
	if(MSVC)
		target_compile_options(FightWithMeArrowRainLook PRIVATE ${FWM_MSVC_OPTIONS})
	endif()

	# Renewal's cloak of water, sampled the same way.
	add_executable(FightWithMeCloakLook tools/watercloak/CloakLook.cpp src/WaterCloak.cpp)
	target_compile_features(FightWithMeCloakLook PRIVATE cxx_std_23)
	target_include_directories(FightWithMeCloakLook PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
	if(MSVC)
		target_compile_options(FightWithMeCloakLook PRIVATE ${FWM_MSVC_OPTIONS})
	endif()

	add_executable(FightWithMeBreathLook tools/dragonbreath/BreathLook.cpp)
	add_executable(FightWithMeBreathTests tests/DragonBreathTests.cpp)
	foreach(target FightWithMeBreathLook FightWithMeBreathTests)
		target_compile_features(${target} PRIVATE cxx_std_23)
		target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
		if(MSVC)
			target_compile_options(${target} PRIVATE ${FWM_MSVC_OPTIONS})
		endif()
	endforeach()
	add_test(NAME FightWithMeBreathTests COMMAND FightWithMeBreathTests)
    add_executable(FightWithMeCompactEffectsTests tests/CompactEffectsTests.cpp src/BearRoar.cpp src/Bastion.cpp src/MoonFx.cpp src/WolfBite.cpp src/ArrowRain.cpp src/Blast.cpp)
    target_compile_features(FightWithMeCompactEffectsTests PRIVATE cxx_std_23)
    target_include_directories(FightWithMeCompactEffectsTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    if(MSVC)
        target_compile_options(FightWithMeCompactEffectsTests PRIVATE ${FWM_MSVC_OPTIONS})
    endif()
    add_test(NAME FightWithMeCompactEffectsTests COMMAND FightWithMeCompactEffectsTests)


	# Taunt's bear roar, sampled with the plugin's own code for tools/wolfhead/preview.
	add_executable(FightWithMeBearLook tools/bearhead/BearLook.cpp src/BearRoar.cpp)
	add_executable(FightWithMeBearTests tests/BearRoarTests.cpp src/BearRoar.cpp)
	foreach(target FightWithMeBearLook FightWithMeBearTests)
		target_compile_features(${target} PRIVATE cxx_std_23)
		target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
		if(MSVC)
			target_compile_options(${target} PRIVATE ${FWM_MSVC_OPTIONS})
		endif()
	endforeach()
	add_test(NAME FightWithMeBearTests COMMAND FightWithMeBearTests)

	add_executable(FightWithMeChaosLook tools/chaosdown/ChaosLook.cpp src/ChaosPulse.cpp)
	add_executable(FightWithMeChaosTests tests/ChaosPulseTests.cpp src/ChaosPulse.cpp)
	foreach(target FightWithMeChaosLook FightWithMeChaosTests)
		target_compile_features(${target} PRIVATE cxx_std_23)
		target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
		if(MSVC)
			target_compile_options(${target} PRIVATE ${FWM_MSVC_OPTIONS})
		endif()
	endforeach()
	add_test(NAME FightWithMeChaosTests COMMAND FightWithMeChaosTests)

	# Kynareth's Maw's bite, sampled with the plugin's own code for tools/wolfhead/preview.
	add_executable(FightWithMeWolfPose tools/wolfhead/WolfPose.cpp src/WolfBite.cpp src/Haunt.cpp)
	target_compile_features(FightWithMeWolfPose PRIVATE cxx_std_23)
	target_include_directories(FightWithMeWolfPose PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
	if(MSVC)
		target_compile_options(FightWithMeWolfPose PRIVATE ${FWM_MSVC_OPTIONS})
	endif()
endif()

# Path of the Companions: its own DLL and release, built from the same sources so the two
# can never disagree about the trees or the abilities. Paths always runs abilities and its panel;
# Fight With Me runs battle orders. Host gates keep ownership separate, using everything in src/ but Fight
# With Me's plugin entry. Its source is a repository of its own, checked out into paths/; without it,
# Fight With Me builds alone.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/paths/src/Main.cpp")
set(FWMP_VERSION_MAJOR 1)
set(FWMP_VERSION_MINOR 2)
set(FWMP_VERSION_PATCH 0)
set(FWMP_VERSION "${FWMP_VERSION_MAJOR}.${FWMP_VERSION_MINOR}.${FWMP_VERSION_PATCH}")
configure_file(paths/cmake/version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/paths/version.rc" @ONLY)
set(FWMP_SOURCES ${FWM_SOURCES})
list(FILTER FWMP_SOURCES EXCLUDE REGEX "/src/Plugin\.cpp$")
add_library(FightWithMePaths SHARED
	paths/src/Main.cpp
	${FWMP_SOURCES}
	${FWM_HEADERS}
	"${CMAKE_CURRENT_BINARY_DIR}/paths/version.rc")
target_compile_features(FightWithMePaths PRIVATE cxx_std_23)
target_precompile_headers(FightWithMePaths PRIVATE include/FWM/PCH.h)
target_include_directories(FightWithMePaths PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(FightWithMePaths PRIVATE CommonLibSSE::CommonLibSSE)
if(MSVC)
	target_compile_definitions(FightWithMePaths PRIVATE FIGHTWITHMEPATHS_EXPORTS FWM_HOST_PATHS
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
