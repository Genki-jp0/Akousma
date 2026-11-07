# Akousma version
# ===============
#
# The Akousma version is set here and only here. It is a plain release version: no git describe output, no -dev or rc
# suffix. It changes ONLY when Genki says so; rebasing onto a new Strawberry release or adding commits does not change it.
set(AKOUSMA_VERSION_MAJOR 1)
set(AKOUSMA_VERSION_MINOR 0)
set(AKOUSMA_VERSION_PATCH 0)

# The Strawberry code Akousma is based on, shown in the About dialog, in --version, in the log and in exported settings. It is
# worked out from git (cmake/AkousmaStrawberryBase.cmake), from the history after Akousma's first commit, which stays the same.
include(cmake/AkousmaStrawberryBase.cmake)
akousma_strawberry_base("${CMAKE_SOURCE_DIR}" c3a553a26502cdc6a3b123e11654c414bc44c492)

# Own builds can append Akousma's own short git commit: "1.0.0 (git abc1234)". Off by default, so release builds say 1.0.0.
# AKOUSMA_GIT_COMMIT can be set instead (e.g. -DAKOUSMA_GIT_COMMIT=abc1234) when building without a .git directory.
option(AKOUSMA_INCLUDE_GIT_COMMIT "Append Akousma's own short git commit to the version shown" OFF)
set(AKOUSMA_GIT_COMMIT "" CACHE STRING "Akousma git commit to show with AKOUSMA_INCLUDE_GIT_COMMIT (empty: ask git)")

set(AKOUSMA_VERSION "${AKOUSMA_VERSION_MAJOR}.${AKOUSMA_VERSION_MINOR}.${AKOUSMA_VERSION_PATCH}")

set(AKOUSMA_GIT_COMMIT_SHORT "")
if(AKOUSMA_INCLUDE_GIT_COMMIT)
  if(AKOUSMA_GIT_COMMIT)
    set(AKOUSMA_GIT_COMMIT_SHORT "${AKOUSMA_GIT_COMMIT}")
  else()
    find_package(Git QUIET)
    if(NOT GIT_FOUND)
      message(FATAL_ERROR "AKOUSMA_INCLUDE_GIT_COMMIT is ON, but git was not found. Install git or pass -DAKOUSMA_GIT_COMMIT=<commit>.")
    endif()
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" rev-parse --short=7 HEAD
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      RESULT_VARIABLE _akousma_git_result
      OUTPUT_VARIABLE AKOUSMA_GIT_COMMIT_SHORT
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(NOT _akousma_git_result EQUAL 0 OR NOT AKOUSMA_GIT_COMMIT_SHORT)
      message(FATAL_ERROR "AKOUSMA_INCLUDE_GIT_COMMIT is ON, but ${CMAKE_SOURCE_DIR} is not a git checkout. Pass -DAKOUSMA_GIT_COMMIT=<commit>.")
    endif()
    unset(_akousma_git_result)
  endif()
endif()

# Configure again when HEAD moves (new commit, checkout, merge from upstream), so the commits shown are never stale.
set(_akousma_git_dir "${AKOUSMA_BASE_STRAWBERRY_GIT_DIR}")
if(AKOUSMA_GIT_COMMIT_SHORT AND NOT AKOUSMA_GIT_COMMIT)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --absolute-git-dir
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE _akousma_git_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
endif()
if(_akousma_git_dir AND EXISTS "${_akousma_git_dir}/logs/HEAD")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_akousma_git_dir}/logs/HEAD")
elseif(_akousma_git_dir AND EXISTS "${_akousma_git_dir}/HEAD")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_akousma_git_dir}/HEAD")
endif()
unset(_akousma_git_dir)

if(AKOUSMA_GIT_COMMIT_SHORT)
  # Shown to people: "1.0.0 (git abc1234)". Packages and file names: "1.0.0+gitabc1234".
  set(AKOUSMA_VERSION_DISPLAY "${AKOUSMA_VERSION} (git ${AKOUSMA_GIT_COMMIT_SHORT})")
  set(AKOUSMA_VERSION_PACKAGE "${AKOUSMA_VERSION}+git${AKOUSMA_GIT_COMMIT_SHORT}")
else()
  set(AKOUSMA_VERSION_DISPLAY "${AKOUSMA_VERSION}")
  set(AKOUSMA_VERSION_PACKAGE "${AKOUSMA_VERSION}")
endif()
set(AKOUSMA_VERSION_RPM_V "${AKOUSMA_VERSION_PACKAGE}")
set(AKOUSMA_VERSION_RPM_R "1")

message(STATUS "Akousma version:  ${AKOUSMA_VERSION_DISPLAY}")
message(STATUS "Package version:  ${AKOUSMA_VERSION_PACKAGE}")
message(STATUS "Based on:         ${AKOUSMA_BASE_STRAWBERRY_DISPLAY}")
