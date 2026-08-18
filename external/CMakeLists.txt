# Build Detours
add_library(Detours STATIC
        Detours/src/creatwth.cpp
        Detours/src/modules.cpp
        Detours/src/detours.cpp
        Detours/src/disolx86.cpp
        Detours/src/disasm.cpp
        Detours/src/image.cpp
)
target_include_directories(Detours PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Detours/src)

# StackWalker is kept as a small, source-built static dependency.  Its upstream
# CMake project also builds tests and changes global compiler flags, neither of
# which is desirable for the injected DLL.
add_library(StackWalker STATIC
        StackWalker/Main/StackWalker/StackWalker.cpp
)
target_include_directories(StackWalker PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/StackWalker/Main/StackWalker
)
target_link_libraries(StackWalker PUBLIC dbghelp)

# Build spdlog as a static dependency. Keep its optional targets out of the
# injected DLL build and expose only the namespaced library target.
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(spdlog)


# Build WzLib
add_subdirectory(WzLib)
