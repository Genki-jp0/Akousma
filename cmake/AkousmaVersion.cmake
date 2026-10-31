# Akousma version
# ===============
#
# The Akousma version is set here and only here. It is a plain release version: no git describe output, no -dev or rc
# suffix. It changes ONLY when Genki says so; rebasing onto a new Strawberry release or adding commits does not change it.
set(AKOUSMA_VERSION_MAJOR 1)
set(AKOUSMA_VERSION_MINOR 0)
set(AKOUSMA_VERSION_PATCH 0)

# The Strawberry release and upstream commit Akousma is based on, shown as "based on Strawberry <version> (<commit>)" in the
# About dialog and in --version. Update both when rebasing onto a newer Strawberry.
set(AKOUSMA_BASE_STRAWBERRY_VERSION "1.2.31")
set(AKOUSMA_BASE_STRAWBERRY_COMMIT "82309fc")

# Own builds can append Akousma's own short git commit: "1.0.0 (git abc1234)". Off by default, so release builds say 1.0.0.
# AKOUSMA_GIT_COMMIT can be set instead (e.g. -DAKOUSMA_GIT_COMMIT=abc1234) when building without a .git directory.
option(AKOUSMA_INCLUDE_GIT_COMMIT "Append Akousma's own short git commit to the version shown" OFF)
set(AKOUSMA_GIT_COMMIT "" CACHE STRING "Akousma git commit to show with AKOUSMA_INCLUDE_GIT_COMMIT (empty: ask git)")

set(AKOUSMA_VERSION "${AKOUSMA_VERSION_MAJOR}.${AKOUSMA_VERSION_MINOR}.${AKOUSMA_VERSION_PATCH}")

# Upstream's STRAWBERRY_VERSION_* (cmake/Version.cmake) are left as they are: they describe the Strawberry code base. Remind
# whoever rebases to update the base version above as well.
set(_akousma_upstream_version "${STRAWBERRY_VERSION_MAJOR}.${STRAWBERRY_VERSION_MINOR}.${STRAWBERRY_VERSION_PATCH}")
if(NOT AKOUSMA_BASE_STRAWBERRY_VERSION STREQUAL _akousma_upstream_version)
  message(WARNING "cmake/AkousmaVersion.cmake: AKOUSMA_BASE_STRAWBERRY_VERSION is ${AKOUSMA_BASE_STRAWBERRY_VERSION}, but cmake/Version.cmake says Strawberry ${_akousma_upstream_version}. Update AKOUSMA_BASE_STRAWBERRY_VERSION and AKOUSMA_BASE_STRAWBERRY_COMMIT.")
endif()
unset(_akousma_upstream_version)

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
    # Configure again when HEAD moves (new commit, checkout), so the commit shown is never stale.
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" rev-parse --absolute-git-dir
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      OUTPUT_VARIABLE _akousma_git_dir
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(_akousma_git_dir AND EXISTS "${_akousma_git_dir}/logs/HEAD")
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_akousma_git_dir}/logs/HEAD")
    elseif(_akousma_git_dir AND EXISTS "${_akousma_git_dir}/HEAD")
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_akousma_git_dir}/HEAD")
    endif()
    unset(_akousma_git_dir)
  endif()
endif()

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
message(STATUS "Based on:         Strawberry ${AKOUSMA_BASE_STRAWBERRY_VERSION} (${AKOUSMA_BASE_STRAWBERRY_COMMIT})")
