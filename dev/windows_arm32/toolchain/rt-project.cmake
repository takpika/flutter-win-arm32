# The stock Windows template invokes the SDK .bat backend. On a Unix cross
# host invoke the identical SDK Dart backend through its Unix launcher.
if(NOT CMAKE_HOST_WIN32 AND NOT COMMAND _flutter_arm32_backend_registered)
  # Engine-owned renderer DLLs are runtime dependencies of this SDK build.
  # Defer until the unchanged application chooses its bundle destination.
  function(_flutter_arm32_install_engine_runtime)
    if(NOT TARGET flutter)
      return()
    endif()
    install(FILES
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../libEGL.dll"
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../libGLESv2.dll"
      DESTINATION "${CMAKE_INSTALL_PREFIX}" COMPONENT Runtime)
  endfunction()
  cmake_language(DEFER CALL _flutter_arm32_install_engine_runtime)
  include("${CMAKE_CURRENT_LIST_DIR}/native-package-compat.cmake")
  cmake_language(DEFER CALL _flutter_arm32_native_package_compatibility)
  set(CMAKE_SHARED_LIBRARY_PREFIX "")
  set(CMAKE_SHARED_MODULE_PREFIX "")
  function(_flutter_arm32_backend_registered)
  endfunction()
  function(execute_process)
    # Cargokit sees WIN32 for the cross target and repeats path resolution
    # through PowerShell. Resolve the same path on the Unix build host.
    if(ARGC EQUAL 10 AND EXISTS "${ARGV5}")
      get_filename_component(_flutter_arm32_process_script "${ARGV5}" REALPATH)
      get_filename_component(_flutter_arm32_cargokit_script
        "${cargokit_cmake_root}/cmake/resolve_symlinks.ps1" REALPATH)
    endif()
    if(ARGC EQUAL 10 AND "${ARGV0}" STREQUAL "COMMAND"
        AND "${ARGV1}" STREQUAL "powershell"
        AND "${ARGV2}" STREQUAL "-ExecutionPolicy" AND "${ARGV3}" STREQUAL "Bypass"
        AND "${ARGV4}" STREQUAL "-File"
        AND "${_flutter_arm32_process_script}" STREQUAL "${_flutter_arm32_cargokit_script}"
        AND EXISTS "${_flutter_arm32_process_script}"
        AND "${ARGV6}" STREQUAL "${cargokit_cmake_root}"
        AND "${ARGV7}" STREQUAL "OUTPUT_VARIABLE"
        AND "${ARGV9}" STREQUAL "OUTPUT_STRIP_TRAILING_WHITESPACE")
      get_filename_component(_flutter_arm32_resolved_cargokit "${ARGV6}" REALPATH)
      set("${ARGV8}" "${_flutter_arm32_resolved_cargokit}" PARENT_SCOPE)
      return()
    endif()
    set(_flutter_arm32_process_result_variables)
    set(_flutter_arm32_process_result_next FALSE)
    foreach(_flutter_arm32_process_argument IN LISTS ARGV)
      if(_flutter_arm32_process_result_next)
        list(APPEND _flutter_arm32_process_result_variables "${_flutter_arm32_process_argument}")
        set(_flutter_arm32_process_result_next FALSE)
      elseif(_flutter_arm32_process_argument MATCHES "^(RESULT_VARIABLE|RESULTS_VARIABLE|OUTPUT_VARIABLE|ERROR_VARIABLE)$")
        set(_flutter_arm32_process_result_next TRUE)
      endif()
    endforeach()
    _execute_process(${ARGV})
    foreach(_flutter_arm32_process_variable IN LISTS _flutter_arm32_process_result_variables)
      set("${_flutter_arm32_process_variable}" "${${_flutter_arm32_process_variable}}" PARENT_SCOPE)
    endforeach()
  endfunction()
  function(add_custom_command)
    set(_flutter_arm32_arguments ${ARGV})
    if(DEFINED FLUTTER_ARM32_CARGOKIT_PROJECT_ROOT)
      list(TRANSFORM _flutter_arm32_arguments REPLACE "^CARGOKIT_ROOT_PROJECT_DIR=.*$"
        "CARGOKIT_ROOT_PROJECT_DIR=${FLUTTER_ARM32_CARGOKIT_PROJECT_ROOT}")
    endif()
    list(FIND _flutter_arm32_arguments "${FLUTTER_ROOT}/packages/flutter_tools/bin/tool_backend.bat" _flutter_arm32_backend_index)
    if(NOT _flutter_arm32_backend_index EQUAL -1)
      list(REMOVE_AT _flutter_arm32_arguments ${_flutter_arm32_backend_index})
      list(INSERT _flutter_arm32_arguments ${_flutter_arm32_backend_index} "${FLUTTER_ROOT}/packages/flutter_tools/bin/tool_backend.sh")
      # The SDK unpack step also writes the import library. Visual Studio's
      # template relies on target ordering; Ninja needs the file producer.
      list(FIND _flutter_arm32_arguments OUTPUT _flutter_arm32_output_index)
      list(FIND _flutter_arm32_arguments "${FLUTTER_LIBRARY}.lib" _flutter_arm32_import_index)
      if(NOT _flutter_arm32_output_index EQUAL -1 AND _flutter_arm32_import_index EQUAL -1)
        math(EXPR _flutter_arm32_output_index "${_flutter_arm32_output_index}+1")
        list(INSERT _flutter_arm32_arguments ${_flutter_arm32_output_index} "${FLUTTER_LIBRARY}.lib")
      endif()
    endif()
    if("${CARGOKIT_TARGET_PLATFORM}" STREQUAL "windows-arm")
      set(_flutter_arm32_cargo_index 0)
      foreach(_flutter_arm32_argument IN LISTS _flutter_arm32_arguments)
        if(_flutter_arm32_argument MATCHES "(^|/)run_build_tool\\.cmd$")
          if(NOT EXISTS "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/cargo-backend")
            message(FATAL_ERROR "Windows ARM SDK Cargo backend is not installed")
          endif()
          get_filename_component(_flutter_arm32_cargokit_directory "${_flutter_arm32_argument}" DIRECTORY)
          set(_flutter_arm32_cargo_platform "rt")
          if(DEFINED ENV{FLUTTER_WINDOWS_ARM32_PLATFORM})
            set(_flutter_arm32_cargo_platform "$ENV{FLUTTER_WINDOWS_ARM32_PLATFORM}")
          endif()
          if(NOT _flutter_arm32_cargo_platform MATCHES "^(rt|phone)$")
            message(FATAL_ERROR "Unknown SDK Cargo OS family")
          endif()
          list(REMOVE_AT _flutter_arm32_arguments ${_flutter_arm32_cargo_index})
          list(INSERT _flutter_arm32_arguments ${_flutter_arm32_cargo_index}
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/cargo-backend"
            "--cargokit-directory" "${_flutter_arm32_cargokit_directory}"
            "--platform" "${_flutter_arm32_cargo_platform}")
          break()
        endif()
        math(EXPR _flutter_arm32_cargo_index "${_flutter_arm32_cargo_index}+1")
      endforeach()
    endif()
    _add_custom_command(${_flutter_arm32_arguments})
  endfunction()
endif()
