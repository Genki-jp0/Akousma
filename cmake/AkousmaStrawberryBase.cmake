# The Strawberry code Akousma is based on
# =======================================
#
# akousma_strawberry_base(<source dir> <first Akousma commit>) works out from git which Strawberry commit the source is based
# on, so it stays right after merging upstream (git merge, or "Sync fork" on GitHub) without editing anything:
#
# - The base commit is the newest Strawberry commit in the history. Akousma's own commits all descend from its first commit,
#   Strawberry's never do, so it is the newest parent of a merge (or of the first commit) which is not one of Akousma's own.
# - The Strawberry version is the newest release at or before the base commit: its release tag when upstream's tags were
#   fetched, otherwise upstream's "Release x.y.z" commit. When the base commit is that release, Akousma is based on the
#   release; otherwise it is based on the development version after it.
#
# It sets, in the caller's scope:
#   AKOUSMA_BASE_STRAWBERRY_VERSION  The release: "1.2.31".
#   AKOUSMA_BASE_STRAWBERRY_COMMIT   The base commit, abbreviated: "d74ab4f". Empty when it is not known.
#   AKOUSMA_BASE_STRAWBERRY_RELEASE  ON when the base commit is the release itself.
#   AKOUSMA_BASE_STRAWBERRY_DISPLAY  For people: "Strawberry 1.2.31 (19d8c0b)",
#                                    "Strawberry development version after 1.2.31 (d74ab4f)" or "Strawberry 1.2.31 or later".
#   AKOUSMA_BASE_STRAWBERRY_GIT_DIR  The git directory used, to configure again when HEAD moves. Empty without git.
#
# Without the history (a source tarball, a shallow clone), the version is the one in cmake/Version.cmake
# (STRAWBERRY_VERSION_*), which upstream only changes in a release, and the commit is not known.

function(akousma_strawberry_base source_dir first_commit)

  set(_version "")
  set(_commit "")
  set(_release OFF)
  set(_git_dir "")

  find_package(Git QUIET)
  if(GIT_FOUND)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" rev-parse --absolute-git-dir
      WORKING_DIRECTORY "${source_dir}"
      RESULT_VARIABLE _result
      OUTPUT_VARIABLE _git_dir
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(NOT _result EQUAL 0)
      set(_git_dir "")
    endif()
  endif()

  # The first Akousma commit must be in the history of HEAD. This also skips a source tree inside another git repository.
  if(_git_dir)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" merge-base --is-ancestor "${first_commit}" HEAD
      WORKING_DIRECTORY "${source_dir}"
      RESULT_VARIABLE _result
      OUTPUT_QUIET
      ERROR_QUIET
    )
    if(NOT _result EQUAL 0)
      set(_git_dir "")
    endif()
  endif()

  if(_git_dir)
    # Strawberry commits which are parents of Akousma commits: the parents of the first commit, and the parents of merges
    # which don't descend from the first commit.
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" rev-list --parents --no-walk "${first_commit}"
      WORKING_DIRECTORY "${source_dir}"
      OUTPUT_VARIABLE _first
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" rev-list --parents --merges HEAD "^${first_commit}"
      WORKING_DIRECTORY "${source_dir}"
      OUTPUT_VARIABLE _merges
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    string(REGEX MATCHALL "[0-9a-f]+" _first "${_first}")
    list(POP_FRONT _first)
    string(REGEX MATCHALL "[0-9a-f]+" _parents "${_merges}")
    list(APPEND _parents ${_first})
    list(REMOVE_DUPLICATES _parents)
    set(_candidates "")
    foreach(_parent IN LISTS _parents)
      execute_process(
        COMMAND "${GIT_EXECUTABLE}" merge-base --is-ancestor "${first_commit}" "${_parent}"
        WORKING_DIRECTORY "${source_dir}"
        RESULT_VARIABLE _result
        OUTPUT_QUIET
        ERROR_QUIET
      )
      if(_result EQUAL 1)
        list(APPEND _candidates "${_parent}")
      endif()
    endforeach()
    # The newest of them, the one which has all the others in its history.
    set(_base "")
    if(_candidates)
      execute_process(
        COMMAND "${GIT_EXECUTABLE}" merge-base --independent ${_candidates}
        WORKING_DIRECTORY "${source_dir}"
        OUTPUT_VARIABLE _base
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
      )
      if(_base MATCHES "\n")
        message(WARNING "Akousma has several Strawberry commits which don't have each other in their history: ${_base}. Using the newest.")
        execute_process(
          COMMAND "${GIT_EXECUTABLE}" rev-list --no-walk=sorted --max-count=1 ${_base}
          WORKING_DIRECTORY "${source_dir}"
          OUTPUT_VARIABLE _base
          OUTPUT_STRIP_TRAILING_WHITESPACE
          ERROR_QUIET
        )
      endif()
    endif()

    if(_base)
      execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=7 "${_base}"
        WORKING_DIRECTORY "${source_dir}"
        OUTPUT_VARIABLE _commit
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
      )
      # The release: a tag from upstream, else the "Release x.y.z" commit (Akousma has no tags on Strawberry's commits).
      set(_release_commit "")
      execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --abbrev=0 --match "[0-9]*.[0-9]*.[0-9]*" "${_base}"
        WORKING_DIRECTORY "${source_dir}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _tag
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
      )
      if(_result EQUAL 0 AND _tag)
        set(_version "${_tag}")
        execute_process(
          COMMAND "${GIT_EXECUTABLE}" rev-parse "${_tag}^{commit}"
          WORKING_DIRECTORY "${source_dir}"
          OUTPUT_VARIABLE _release_commit
          OUTPUT_STRIP_TRAILING_WHITESPACE
          ERROR_QUIET
        )
      else()
        execute_process(
          COMMAND "${GIT_EXECUTABLE}" log -1 -E "--grep=^Release [0-9]+\\.[0-9]+\\.[0-9]+" --format=%H%n%s "${_base}"
          WORKING_DIRECTORY "${source_dir}"
          OUTPUT_VARIABLE _log
          OUTPUT_STRIP_TRAILING_WHITESPACE
          ERROR_QUIET
        )
        if(_log MATCHES "^([0-9a-f]+)\nRelease ([0-9]+\\.[0-9]+\\.[0-9]+[^ \n]*)")
          set(_release_commit "${CMAKE_MATCH_1}")
          set(_version "${CMAKE_MATCH_2}")
        endif()
      endif()
      if(_release_commit STREQUAL _base)
        set(_release ON)
      endif()
    else()
      set(_git_dir "")
    endif()
  endif()

  if(NOT _version)
    set(_version "${STRAWBERRY_VERSION_MAJOR}.${STRAWBERRY_VERSION_MINOR}.${STRAWBERRY_VERSION_PATCH}")
    if(STRAWBERRY_VERSION_PRERELEASE)
      string(APPEND _version "-${STRAWBERRY_VERSION_PRERELEASE}")
    endif()
  endif()

  if(NOT _commit)
    set(_display "Strawberry ${_version} or later")
  elseif(_release)
    set(_display "Strawberry ${_version} (${_commit})")
  else()
    set(_display "Strawberry development version after ${_version} (${_commit})")
  endif()

  set(AKOUSMA_BASE_STRAWBERRY_VERSION "${_version}" PARENT_SCOPE)
  set(AKOUSMA_BASE_STRAWBERRY_COMMIT "${_commit}" PARENT_SCOPE)
  set(AKOUSMA_BASE_STRAWBERRY_RELEASE ${_release} PARENT_SCOPE)
  set(AKOUSMA_BASE_STRAWBERRY_DISPLAY "${_display}" PARENT_SCOPE)
  set(AKOUSMA_BASE_STRAWBERRY_GIT_DIR "${_git_dir}" PARENT_SCOPE)

endfunction()
