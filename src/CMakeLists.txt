# Build injector
add_library(injector SHARED
    injector.cpp
    crashlog.cpp
    autologin/autologin.cpp
    autologin/autologin_hooks.cpp
    customactions/customactions.cpp
    customskills/skills.cpp
    system.cpp
    bypass.cpp
    inlink.cpp
    resman.cpp
    avatar.cpp
    itemeff.cpp
    resolution.cpp
    mobhptag.cpp
    tooltip.cpp
    gamemods/gamemods.cpp
    itemicon.cpp
    tempstat.cpp
    stringpool.cpp
    fusionanvil/fusionanvil.cpp
    storagebag/storagebag.cpp
    weapontint/weapontint.cpp
    coloringprism/coloringprism.cpp
    cashshop/cashshopwnd.cpp
    compactstorage/compactstorage.cpp
    slotlock/slotlock.cpp
    worldmapinfo/worldmapinfo.cpp
    assetutils/getwzinfo.cpp
    assetutils/iconprovider.cpp
    damagerank/uiDamageRank.cpp
    damagerank/hotkey.cpp
    rainbownames/rainbownames.cpp
    debug.cpp
    hook.cpp
)

target_precompile_headers(injector PRIVATE pch.h)

target_include_directories(injector PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_options(injector PRIVATE
    "$<$<CONFIG:Release>:/Zi>"
)

target_link_options(injector PRIVATE
    "$<$<CONFIG:Release>:/DEBUG:FULL>"
    "$<$<CONFIG:Release>:/MAP:${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/Kaentake.map>"
)

target_link_libraries(injector PRIVATE
    Detours
    StackWalker
    WzLib
)

set_target_properties(injector PROPERTIES
    OUTPUT_NAME ${PROJECT_NAME}
    PDB_NAME ${PROJECT_NAME}
)

add_custom_command(TARGET injector POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Custom.wz"
        "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
)


# Build launcher
add_executable(launcher WIN32
    launcher.manifest
    launcher.rc
    launcher.cpp
    debug.cpp
)

target_link_options(launcher PRIVATE "/MANIFEST:NO")

target_link_libraries(launcher PRIVATE
    Detours
)

set_target_properties(launcher PROPERTIES
    OUTPUT_NAME ${PROJECT_NAME}
    PDB_NAME "${PROJECT_NAME}-launcher"
)
