# CMakeHub - Unified CMake Module Manager
# Loader script for managing and loading CMake modules

cmake_minimum_required(VERSION 3.19)

# =============================================================================
# Configuration Variables
# =============================================================================

# Cache directory configuration
if(NOT DEFINED CMH_CACHE_DIR)
    if(DEFINED ENV{CMH_CACHE_DIR})
        set(CMH_CACHE_DIR "$ENV{CMH_CACHE_DIR}" CACHE PATH "CMakeHub cache directory")
    elseif(CMAKE_HOST_WIN32)
        set(CMH_CACHE_DIR "$ENV{USERPROFILE}/.cmakehub/cache" CACHE PATH "CMakeHub cache directory")
    else()
        set(CMH_CACHE_DIR "$ENV{HOME}/.cmakehub/cache" CACHE PATH "CMakeHub cache directory")
    endif()
endif()

# Version check mode: STRICT, WARNING, SILENT
if(NOT DEFINED CMAKEHUB_VERSION_CHECK_MODE)
    set(CMAKEHUB_VERSION_CHECK_MODE "STRICT" CACHE STRING "Version check mode: STRICT, WARNING, SILENT")
endif()

# Verbose mode
if(NOT DEFINED CMAKEHUB_VERBOSE)
    set(CMAKEHUB_VERBOSE FALSE CACHE BOOL "Enable verbose output")
endif()

# Location of modules.json (relative to this file)
get_filename_component(CMAKEHUB_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(CMAKEHUB_MODULES_INDEX "${CMAKEHUB_ROOT_DIR}/modules.json")

# Global property to track used modules
set_property(GLOBAL PROPERTY CMAKEHUB_USED_MODULES "")
set_property(GLOBAL PROPERTY CMAKEHUB_USED_MODULES_BRIEF_DOCS "List of modules used via CMakeHub")
set_property(GLOBAL PROPERTY CMAKEHUB_USED_MODULES_FULL_DOCS "List of module names that have been loaded through CMakeHub")

# =============================================================================
# Internal Helper Functions
# =============================================================================

function(cmakehub_log level message)
    if(CMAKEHUB_VERBOSE OR level STREQUAL "WARNING" OR level STREQUAL "FATAL_ERROR")
        message(${level} "CMakeHub: ${message}")
    endif()
endfunction()

function(cmakehub_get_module_info module_name out_var)
    if(NOT EXISTS ${CMAKEHUB_MODULES_INDEX})
        message(FATAL_ERROR "CMakeHub modules index not found at: ${CMAKEHUB_MODULES_INDEX}")
    endif()

    file(READ ${CMAKEHUB_MODULES_INDEX} index_content)
    
    # JSON parsing
    string(JSON modules_array GET "${index_content}" modules)
    
    set(found FALSE)
    set(module_obj "")
    
    string(JSON modules_length LENGTH "${modules_array}")
    math(EXPR max_index "${modules_length} - 1")
    
    foreach(index RANGE ${max_index})
        string(JSON current_module_obj GET "${modules_array}" ${index})
        string(JSON module_name_check GET "${current_module_obj}" name)
        
        if(module_name_check STREQUAL module_name)
            set(found TRUE)
            set(module_obj "${current_module_obj}")
            break()
        endif()
    endforeach()
    
    if(NOT found)
        message(FATAL_ERROR "Module '${module_name}' not found in CMakeHub index")
    endif()
    
    # Store module info in global properties to avoid semicolon parsing issues
    set_property(GLOBAL PROPERTY "CMH_MODULE_${module_name}_INFO_JSON" "${module_obj}")
    
    # Set result as boolean success indicator
    set(${out_var} TRUE PARENT_SCOPE)
endfunction()

function(cmakehub_get_module_property module_name property out_var)
    # Get the module JSON from property
    get_property(module_json GLOBAL PROPERTY "CMH_MODULE_${module_name}_INFO_JSON")
    
    if(NOT module_json)
        message(FATAL_ERROR "Module '${module_name}' info not found. Call cmakehub_get_module_info first.")
    endif()
    
    # Try to get the property, if it fails set to empty string
    string(JSON value ERROR_VARIABLE property_error GET "${module_json}" ${property})
    if(property_error)
        set(value "")
    endif()
    
    set(${out_var} "${value}" PARENT_SCOPE)
endfunction()

function(cmakehub_parse_list json_list out_list)
    set(result_list "")

    # Check if JSON list is empty
    if(json_list STREQUAL "[]" OR json_list STREQUAL "")
        set(${out_list} "" PARENT_SCOPE)
        return()
    endif()

    string(JSON length LENGTH "${json_list}")
    
    if(length GREATER 0)
        math(EXPR max_index "${length} - 1")
        foreach(index RANGE ${max_index})
            string(JSON item GET "${json_list}" ${index})
            list(APPEND result_list "${item}")
        endforeach()
    endif()
    
    set(${out_list} "${result_list}" PARENT_SCOPE)
endfunction()

function(cmakehub_check_cmake_version required_version)
    if(NOT required_version OR required_version STREQUAL "")
        return()
    endif()
    
    if(CMAKE_VERSION VERSION_LESS required_version)
        if(CMAKEHUB_VERSION_CHECK_MODE STREQUAL "STRICT")
            message(FATAL_ERROR
                "CMake version ${required_version} or higher is required. "
                "Current version: ${CMAKE_VERSION}"
            )
        elseif(CMAKEHUB_VERSION_CHECK_MODE STREQUAL "WARNING")
            message(WARNING
                "CMake version ${required_version} or higher is recommended. "
                "Current version: ${CMAKE_VERSION}. Some features may not work correctly."
            )
        endif()
    endif()
endfunction()

function(cmakehub_check_cpp_standard required_standard)
    if(NOT required_standard OR required_standard STREQUAL "")
        return()
    endif()
    
    # Check if CXX_STANDARD is set
    if(NOT CMAKE_CXX_STANDARD)
        cmakehub_log(WARNING 
            "CMAKE_CXX_STANDARD is not set. Cannot verify C++${required_standard} requirement."
        )
        return()
    endif()
    
    if(CMAKE_CXX_STANDARD LESS required_standard)
        if(CMAKEHUB_VERSION_CHECK_MODE STREQUAL "STRICT")
            message(FATAL_ERROR
                "C++${required_standard} or higher is required. "
                "Current standard: C++${CMAKE_CXX_STANDARD}"
            )
        elseif(CMAKEHUB_VERSION_CHECK_MODE STREQUAL "WARNING")
            message(WARNING
                "C++${required_standard} or higher is recommended. "
                "Current standard: C++${CMAKE_CXX_STANDARD}. Some features may not work correctly."
            )
        endif()
    endif()
endfunction()

function(cmakehub_check_conflicts module_name conflicts_json)
    cmakehub_parse_list("${conflicts_json}" conflict_list)
    
    if(NOT conflict_list)
        return()
    endif()
    
    get_property(used_modules GLOBAL PROPERTY CMAKEHUB_USED_MODULES)
    
    foreach(conflict ${conflict_list})
        if(conflict IN_LIST used_modules)
            message(FATAL_ERROR
                "Module '${module_name}' conflicts with already loaded module '${conflict}'"
            )
        endif()
    endforeach()
endfunction()

function(cmakehub_check_dependencies module_name dependencies_json)
    cmakehub_parse_list("${dependencies_json}" dependency_list)
    
    if(NOT dependency_list)
        return()
    endif()
    
    cmakehub_log(STATUS "Resolving dependencies for '${module_name}': ${dependency_list}")
    
    foreach(dep ${dependency_list})
        cmakehub_log(STATUS "Loading dependency: ${dep}")
        cmakehub_use(${dep})
    endforeach()
endfunction()

# =============================================================================
# Core API Functions
# =============================================================================

function(cmakehub_use module_name)
    cmakehub_log(STATUS "Loading module: ${module_name}")
    
    # Parse optional VERSION and CONFIG arguments
    set(OPTIONS "")
    set(SINGLE_ARGS VERSION)
    set(MULTI_ARGS "")
    cmake_parse_arguments(USE "${OPTIONS}" "${SINGLE_ARGS}" "${MULTI_ARGS}" ${ARGN})
    
    # Capture additional config arguments (everything after VERSION)
    set(remaining_args "${USE_UNPARSED_ARGUMENTS}")
    
    # Get module information
    cmakehub_get_module_info(${module_name} success)
    if(NOT success)
        message(FATAL_ERROR "Failed to get module info for '${module_name}'")
    endif()
    
    # Extract module properties using property-based access
    cmakehub_get_module_property(${module_name} repository REPOSITORY)
    cmakehub_get_module_property(${module_name} path PATH)
    cmakehub_get_module_property(${module_name} category CATEGORY)
    cmakehub_get_module_property(${module_name} license LICENSE)
    cmakehub_get_module_property(${module_name} description DESCRIPTION)
    cmakehub_get_module_property(${module_name} cmake_minimum_required CMAKE_MINIMUM_REQUIRED)
    cmakehub_get_module_property(${module_name} cpp_minimum_required CPP_MINIMUM_REQUIRED)
    cmakehub_get_module_property(${module_name} dependencies DEPENDENCIES_JSON)
    cmakehub_get_module_property(${module_name} conflicts CONFLICTS_JSON)
    cmakehub_get_module_property(${module_name} version VERSION)
    
    # Use user-provided version if specified
    if(USE_VERSION)
        set(VERSION ${USE_VERSION})
        cmakehub_log(STATUS "Using specified version: ${VERSION}")
    endif()
    
    # Set defaults for optional fields
    if(NOT VERSION OR VERSION STREQUAL "")
        set(VERSION "main")
    endif()
    
    if(NOT DEPENDENCIES_JSON OR DEPENDENCIES_JSON STREQUAL "")
        set(DEPENDENCIES_JSON "[]")
    endif()
    
    if(NOT CONFLICTS_JSON OR CONFLICTS_JSON STREQUAL "")
        set(CONFLICTS_JSON "[]")
    endif()
    
    # Version checks
    cmakehub_check_cmake_version("${CMAKE_MINIMUM_REQUIRED}")
    cmakehub_check_cpp_standard("${CPP_MINIMUM_REQUIRED}")
    
    # Platform compatibility check
    cmakehub_get_module_property(${module_name} platform PLATFORM_JSON)
    cmakehub_parse_list("${PLATFORM_JSON}" platform_list)
    
    if(platform_list)
        # Detect current platform
        set(current_platform "")
        if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
            set(current_platform "windows")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            set(current_platform "linux")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            set(current_platform "macos")
        elseif(CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
            set(current_platform "freebsd")
        elseif(IOS)
            set(current_platform "ios")
        elseif(ANDROID)
            set(current_platform "android")
        endif()
        
        # Check if current platform is in the list
        set(platform_compatible FALSE)
        foreach(p ${platform_list})
            if(p STREQUAL current_platform)
                set(platform_compatible TRUE)
                break()
            endif()
        endforeach()
        
        if(NOT platform_compatible AND current_platform)
            cmakehub_log(WARNING "Module '${module_name}' is not compatible with platform '${current_platform}'. Supported platforms: ${platform_list}")
        endif()
    endif()
    
    # Check conflicts
    cmakehub_check_conflicts(${module_name} "${CONFLICTS_JSON}")
    
    # Resolve dependencies
    cmakehub_check_dependencies(${module_name} "${DEPENDENCIES_JSON}")

    # Setup cache directory with version to avoid conflicts
    set(module_cache_dir "${CMH_CACHE_DIR}/${module_name}/${VERSION}")
    file(MAKE_DIRECTORY ${module_cache_dir})
    
    # Check if module is already cached
    set(module_file "${module_cache_dir}/${PATH}")
    set(meta_file "${module_cache_dir}/.cmh_meta.json")
    
    if(EXISTS ${module_file} AND EXISTS ${meta_file})
        cmakehub_log(STATUS "Using cached module: ${module_name}")
        
        # Verify metadata
        file(READ ${meta_file} meta_content)
        # In production, verify that the cached version matches expected version
        
    else()
        cmakehub_log(STATUS "Downloading module: ${module_name}")

        # Download using FetchContent
        include(FetchContent)

        FetchContent_Declare(
            ${module_name}
            GIT_REPOSITORY ${REPOSITORY}
            GIT_TAG ${VERSION}
            SOURCE_DIR ${module_cache_dir}
            SUBBUILD_DIR "${module_cache_dir}-subbuild"
        )

        # Check if already populated
        FetchContent_GetProperties(${module_name} POPULATED populated)
        if(NOT populated)
            # Try to download and populate
            FetchContent_MakeAvailable(${module_name})
        endif()

        # Verify population was successful
        FetchContent_GetProperties(${module_name} POPULATED populated)
        if(NOT populated)
            message(FATAL_ERROR "Failed to download module '${module_name}' from ${REPOSITORY}")
        endif()
        
        # Write metadata
        string(TIMESTAMP download_timestamp "%Y-%m-%dT%H:%M:%S")
        file(WRITE ${meta_file}
            "{\n"
            "  \"module\": \"${module_name}\",\n"
            "  \"repository\": \"${REPOSITORY}\",\n"
            "  \"version\": \"${VERSION}\",\n"
            "  \"path\": \"${PATH}\",\n"
            "  \"downloaded_at\": \"${download_timestamp}\"\n"
            "}"
        )
        
        cmakehub_log(STATUS "Module downloaded successfully")
    endif()
    
    # Include the module file
    if(EXISTS ${module_file})
        # Set config variables from remaining arguments (CONFIG穿透)
        if(remaining_args)
            cmakehub_log(STATUS "Setting config variables for ${module_name}: ${remaining_args}")
            list(LENGTH remaining_args remaining_len)
            math(EXPR remaining_idx_max "${remaining_len} - 1")
            
            foreach(idx RANGE 0 ${remaining_idx_max})
                list(GET remaining_args ${idx} current)
                math(EXPR next_idx "${idx} + 1")
                if(next_idx LESS_EQUAL remaining_idx_max)
                    list(GET remaining_args ${next_idx} next_val)
                    if(next_val STREQUAL "ON" OR next_val STREQUAL "OFF" OR next_val STREQUAL "TRUE" OR next_val STREQUAL "FALSE" OR next_val MATCHES "^[0-9]+$" OR next_val MATCHES "^\".*\"$" OR next_val MATCHES "^'.*'$")
                        # This is a value, set the variable
                        string(REPLACE "\"" "" clean_key "${current}")
                        set(${clean_key} ${next_val} CACHE BOOL "" FORCE)
                        cmakehub_log(STATUS "  Set ${clean_key} = ${next_val}")
                        # Skip the next iteration since we consumed the value
                        math(EXPR idx "${idx} + 1")
                    endif()
                endif()
            endforeach()
        endif()
        
        include(${module_file})
        cmakehub_log(STATUS "Module '${module_name}' loaded successfully")
    else()
        message(FATAL_ERROR 
            "Module file not found at: ${module_file}\n"
            "This might indicate the module structure has changed or the download failed."
        )
    endif()
    
    # Record as used module
    set_property(GLOBAL APPEND PROPERTY CMAKEHUB_USED_MODULES ${module_name})
endfunction()

function(cmakehub_use_category category_name)
    cmakehub_log(STATUS "Loading all modules in category: ${category_name}")
    
    if(NOT EXISTS ${CMAKEHUB_MODULES_INDEX})
        message(FATAL_ERROR "CMakeHub modules index not found at: ${CMAKEHUB_MODULES_INDEX}")
    endif()
    
    file(READ ${CMAKEHUB_MODULES_INDEX} index_content)
    string(JSON modules_array GET "${index_content}" modules)
    
    string(JSON modules_length LENGTH "${modules_array}")
    math(EXPR max_index "${modules_length} - 1")
    
    set(loaded_count 0)
    
    foreach(index RANGE ${max_index})
        string(JSON module_obj GET "${modules_array}" ${index})
        string(JSON module_category GET "${module_obj}" category)
        string(JSON module_name GET "${module_obj}" name)
        
        if(module_category STREQUAL category_name)
            cmakehub_log(STATUS "Loading module: ${module_name}")
            cmakehub_use(${module_name})
            math(EXPR loaded_count "${loaded_count} + 1")
        endif()
    endforeach()
    
    cmakehub_log(STATUS "Loaded ${loaded_count} module(s) from category '${category_name}'")
endfunction()

function(cmakehub_show_licenses)
    cmakehub_log(STATUS "Collecting license information...")
    
    get_property(used_modules GLOBAL PROPERTY CMAKEHUB_USED_MODULES)
    
    if(NOT used_modules)
        message(STATUS "No modules have been loaded via CMakeHub")
        return()
    endif()
    
    message(STATUS "")
    message(STATUS "CMakeHub Module Licenses:")
    message(STATUS "========================")
    
    foreach(module_name ${used_modules})
        # Get module info
        cmakehub_get_module_info(${module_name} success)
        if(success)
            cmakehub_get_module_property(${module_name} license license)
            cmakehub_get_module_property(${module_name} repository repo)
            cmakehub_get_module_property(${module_name} description description)
            
            message(STATUS "")
            message(STATUS "  Module: ${module_name}")
            message(STATUS "  License: ${license}")
            message(STATUS "  Repository: ${repo}")
            if(description)
                message(STATUS "  Description: ${description}")
            endif()
        endif()
    endforeach()
    
    message(STATUS "")
    message(STATUS "Please ensure your project complies with all license requirements.")
endfunction()

function(cmakehub_list_compatible_modules)
    cmakehub_log(STATUS "Listing modules compatible with CMake ${CMAKE_VERSION}...")
    
    if(NOT EXISTS ${CMAKEHUB_MODULES_INDEX})
        message(FATAL_ERROR "CMakeHub modules index not found at: ${CMAKEHUB_MODULES_INDEX}")
    endif()
    
    file(READ ${CMAKEHUB_MODULES_INDEX} index_content)
    string(JSON modules_array GET "${index_content}" modules)
    
    string(JSON modules_length LENGTH "${modules_array}")
    math(EXPR max_index "${modules_length} - 1")
    
    message(STATUS "")
    message(STATUS "Compatible Modules (CMake ${CMAKE_VERSION}):")
    message(STATUS "=======================================")
    
    set(compatible_count 0)
    set(incompatible_count 0)
    
    foreach(index RANGE ${max_index})
        string(JSON module_obj GET "${modules_array}" ${index})
        string(JSON module_name GET "${module_obj}" name)
        string(JSON module_description GET "${module_obj}" description)
        
        # Check version requirement - try to get the property, if it fails set to empty
        string(JSON cmake_min_required ERROR_VARIABLE cmake_min_error GET "${module_obj}" cmake_minimum_required)
        if(cmake_min_error)
            set(cmake_min_required "")
        endif()
        
        if(NOT cmake_min_required OR CMAKE_VERSION VERSION_GREATER_EQUAL cmake_min_required)
            message(STATUS "  ✓ ${module_name} - ${module_description}")
            math(EXPR compatible_count "${compatible_count} + 1")
        else()
            math(EXPR incompatible_count "${incompatible_count} + 1")
        endif()
    endforeach()
    
    message(STATUS "")
    message(STATUS "Compatible: ${compatible_count}, Incompatible: ${incompatible_count}")
endfunction()

function(cmakehub_info module_name)
    cmakehub_log(STATUS "Showing information for module: ${module_name}")
    
    cmakehub_get_module_info(${module_name} success)
    if(NOT success)
        message(FATAL_ERROR "Module '${module_name}' not found")
    endif()
    
    message(STATUS "")
    message(STATUS "Module Information: ${module_name}")
    message(STATUS "=====================")
    
    # Get all properties
    cmakehub_get_module_property(${module_name} name name)
    cmakehub_get_module_property(${module_name} description description)
    cmakehub_get_module_property(${module_name} category category)
    cmakehub_get_module_property(${module_name} author author)
    cmakehub_get_module_property(${module_name} repository repository)
    cmakehub_get_module_property(${module_name} path path)
    cmakehub_get_module_property(${module_name} license license)
    cmakehub_get_module_property(${module_name} version version)
    cmakehub_get_module_property(${module_name} cmake_minimum_required cmake_min)
    cmakehub_get_module_property(${module_name} cpp_minimum_required cpp_min)
    cmakehub_get_module_property(${module_name} stars stars)
    cmakehub_get_module_property(${module_name} last_updated last_updated)
    
    cmakehub_get_module_property(${module_name} dependencies deps_json)
    cmakehub_parse_list("${deps_json}" dependencies)
    
    cmakehub_get_module_property(${module_name} conflicts conflicts_json)
    cmakehub_parse_list("${conflicts_json}" conflicts)
    
    cmakehub_get_module_property(${module_name} tags tags_json)
    cmakehub_parse_list("${tags_json}" tags)
    
    # Display information
    if(name)
        message(STATUS "  Name: ${name}")
    endif()
    if(description)
        message(STATUS "  Description: ${description}")
    endif()
    if(category)
        message(STATUS "  Category: ${category}")
    endif()
    if(author)
        message(STATUS "  Author: ${author}")
    endif()
    if(repository)
        message(STATUS "  Repository: ${repository}")
    endif()
    if(path)
        message(STATUS "  Path: ${path}")
    endif()
    if(license)
        message(STATUS "  License: ${license}")
    endif()
    if(version)
        message(STATUS "  Version: ${version}")
    endif()
    if(cmake_min)
        message(STATUS "  CMake Minimum: ${cmake_min}")
    endif()
    if(cpp_min)
        message(STATUS "  C++ Minimum: ${cpp_min}")
    endif()
    if(stars)
        message(STATUS "  Stars: ${stars}")
    endif()
    if(last_updated)
        message(STATUS "  Last Updated: ${last_updated}")
    endif()
    
    if(dependencies)
        message(STATUS "  Dependencies: ${dependencies}")
    endif()
    
    if(conflicts)
        message(STATUS "  Conflicts: ${conflicts}")
    endif()
    
    if(tags)
        message(STATUS "  Tags: ${tags}")
    endif()
    
    message(STATUS "")
endfunction()

# =============================================================================
# Module Discovery and Management Functions
# =============================================================================

function(cmakehub_list)
    cmakehub_log(STATUS "Listing all available modules...")
    
    if(NOT EXISTS ${CMAKEHUB_MODULES_INDEX})
        message(FATAL_ERROR "CMakeHub modules index not found at: ${CMAKEHUB_MODULES_INDEX}")
    endif()
    
    file(READ ${CMAKEHUB_MODULES_INDEX} index_content)
    string(JSON modules_array GET "${index_content}" modules)
    
    string(JSON modules_length LENGTH "${modules_array}")
    math(EXPR max_index "${modules_length} - 1")
    
    message(STATUS "")
    message(STATUS "Available Modules (${modules_length}):")
    message(STATUS "========================")
    
    foreach(index RANGE ${max_index})
        string(JSON module_obj GET "${modules_array}" ${index})
        string(JSON module_name GET "${module_obj}" name)
        string(JSON module_desc GET "${module_obj}" description)
        string(JSON module_cat GET "${module_obj}" category)
        string(JSON module_lic GET "${module_obj}" license)
        
        message(STATUS "  ${module_name}")
        message(STATUS "    Description: ${module_desc}")
        message(STATUS "    Category: ${module_cat} | License: ${module_lic}")
        message(STATUS "")
    endforeach()
endfunction()

function(cmakehub_search search_term)
    cmakehub_log(STATUS "Searching for modules matching: ${search_term}")
    
    if(NOT EXISTS ${CMAKEHUB_MODULES_INDEX})
        message(FATAL_ERROR "CMakeHub modules index not found at: ${CMAKEHUB_MODULES_INDEX}")
    endif()
    
    file(READ ${CMAKEHUB_MODULES_INDEX} index_content)
    string(JSON modules_array GET "${index_content}" modules)
    
    string(JSON modules_length LENGTH "${modules_array}")
    math(EXPR max_index "${modules_length} - 1")
    
    message(STATUS "")
    message(STATUS "Search Results for '${search_term}':")
    message(STATUS "=================================")
    
    set(found_count 0)
    string(TOLOWER "${search_term}" search_lower)
    
    foreach(index RANGE ${max_index})
        string(JSON module_obj GET "${modules_array}" ${index})
        string(JSON module_name GET "${module_obj}" name)
        string(JSON module_desc GET "${module_obj}" description)
        string(JSON module_cat GET "${module_obj}" category)
        string(JSON module_tags_json GET "${module_obj}" tags)
        cmakehub_parse_list("${module_tags_json}" tags)
        
        # Search in name, description, category, and tags
        string(TOLOWER "${module_name}" name_lower)
        string(TOLOWER "${module_desc}" desc_lower)
        string(TOLOWER "${module_cat}" cat_lower)
        
        set(matches FALSE)
        if(name_lower MATCHES "${search_lower}" OR desc_lower MATCHES "${search_lower}" OR cat_lower MATCHES "${search_lower}")
            set(matches TRUE)
        endif()
        
        # Search in tags
        if(NOT matches AND tags)
            foreach(tag ${tags})
                string(TOLOWER "${tag}" tag_lower)
                if(tag_lower MATCHES "${search_lower}")
                    set(matches TRUE)
                    break()
                endif()
            endforeach()
        endif()
        
        if(matches)
            message(STATUS "  ${module_name}")
            message(STATUS "    Description: ${module_desc}")
            message(STATUS "    Category: ${module_cat}")
            if(tags)
                message(STATUS "    Tags: ${tags}")
            endif()
            message(STATUS "")
            math(EXPR found_count "${found_count} + 1")
        endif()
    endforeach()
    
    if(found_count EQUAL 0)
        message(STATUS "  No modules found matching '${search_term}'")
    else()
        message(STATUS "Found ${found_count} module(s)")
    endif()
endfunction()

function(cmakehub_cache_clear module_name)
    if(NOT DEFINED module_name OR module_name STREQUAL "")
        # Clear all cache
        cmakehub_log(STATUS "Clearing entire CMakeHub cache at: ${CMH_CACHE_DIR}")
        if(EXISTS ${CMH_CACHE_DIR})
            file(REMOVE_RECURSE ${CMH_CACHE_DIR})
            message(STATUS "Cache cleared successfully")
        else()
            message(STATUS "Cache directory does not exist")
        endif()
    else()
        # Clear specific module cache
        cmakehub_log(STATUS "Clearing cache for module: ${module_name}")
        set(module_cache_dir "${CMH_CACHE_DIR}/${module_name}")
        if(EXISTS ${module_cache_dir})
            file(REMOVE_RECURSE ${module_cache_dir})
            message(STATUS "Cache for '${module_name}' cleared successfully")
        else()
            message(STATUS "No cache found for '${module_name}'")
        endif()
    endif()
endfunction()

function(cmakehub_cache_info)
    cmakehub_log(STATUS "CMakeHub cache information:")
    cmakehub_log(STATUS "  Cache directory: ${CMH_CACHE_DIR}")
    
    if(EXISTS ${CMH_CACHE_DIR})
        file(GLOB_RECURSE cached_files "${CMH_CACHE_DIR}/*")
        list(LENGTH cached_files total_files)
        
        # Calculate total size
        set(total_size 0)
        foreach(file ${cached_files})
            file(SIZE ${file} file_size)
            math(EXPR total_size "${total_size} + ${file_size}")
        endforeach()
        
        # Convert to human readable
        if(total_size GREATER 1073741824)
            math(EXPR size_gb "${total_size} / 1073741824")
            set(size_str "${size_gb} GB")
        elseif(total_size GREATER 1048576)
            math(EXPR size_mb "${total_size} / 1048576")
            set(size_str "${size_mb} MB")
        elseif(total_size GREATER 1024)
            math(EXPR size_kb "${total_size} / 1024")
            set(size_str "${size_kb} KB")
        else()
            set(size_str "${total_size} bytes")
        endif()
        
        message(STATUS "  Cached modules:")
        file(GLOB module_dirs "${CMH_CACHE_DIR}/*")
        foreach(module_dir ${module_dirs})
            if(IS_DIRECTORY ${module_dir})
                get_filename_component(module_name ${module_dir} NAME)
                message(STATUS "    - ${module_name}")
            endif()
        endforeach()
        message(STATUS "  Total files: ${total_files}")
        message(STATUS "  Total size: ${size_str}")
    else()
        message(STATUS "  Cache is empty")
    endif()
endfunction()

function(cmakehub_dependency_graph_all output_file)
    # Generate dependency graph for ALL modules (not just loaded ones)
    cmakehub_log(STATUS "Generating dependency graph for ALL modules...")
    
    # Read modules.json
    if(NOT EXISTS ${CMAKEHUB_MODULES_INDEX})
        message(FATAL_ERROR "Modules index not found: ${CMAKEHUB_MODULES_INDEX}")
    endif()
    
    file(READ ${CMAKEHUB_MODULES_INDEX} index_content)
    string(JSON modules_json GET ${index_content} modules)
    
    # Generate DOT format graph with category grouping
    set(dot_content "digraph CMakeHub_Dependencies {\n")
    set(dot_content "${dot_content}  rankdir=LR;\n")
    set(dot_content "${dot_content}  splines=ortho;\n")
    set(dot_content "${dot_content}  nodesep=0.5;\n")
    set(dot_content "${dot_content}  ranksep=0.8;\n")
    set(dot_content "${dot_content}  node [shape=box, fontsize=10, fontname=\"Arial\"];\n")
    set(dot_content "${dot_content}  edge [fontsize=8, fontname=\"Arial\"];\n")
    set(dot_content "${dot_content}  \"CMakeHub\" [style=filled, fillcolor=\"#ADD8E6\", fontsize=14, fontname=\"Arial\", shape=doublebox];\n\n")
    
    # Group modules by category
    set(categories "")
    set(modules_by_category "")
    
    # Parse all modules
    string(JSON modules_count LENGTH ${modules_json})
    math(EXPR max_index "${modules_count} - 1")
    
    foreach(idx RANGE ${max_index})
        string(JSON module_json GET ${modules_json} ${idx})
        string(JSON name GET ${module_json} name)
        string(JSON category GET ${module_json} category)
        
        # Set default category
        if("${category}" STREQUAL "")
            set(category "unknown")
        endif()
        
        # Color mapping
        set(fillcolor "#D3D3D3")
        if("${category}" STREQUAL "code_quality")
            set(fillcolor "#90EE90")
        elseif("${category}" STREQUAL "testing")
            set(fillcolor "#E0FFFF")
        elseif("${category}" STREQUAL "platform")
            set(fillcolor "#FFFFE0")
        elseif("${category}" STREQUAL "build_optimization")
            set(fillcolor "#B0C4DE")
        elseif("${category}" STREQUAL "debugging")
            set(fillcolor "#DDA0DD")
        elseif("${category}" STREQUAL "dependency")
            set(fillcolor "#FAFAD2")
        elseif("${category}" STREQUAL "docs")
            set(fillcolor "#ADD8E6")
        elseif("${category}" STREQUAL "packaging")
            set(fillcolor "#20B2AA")
        elseif("${category}" STREQUAL "utils")
            set(fillcolor "#F5DEB3")
        endif()
        
        # Add to category group
        string(APPEND modules_by_category "${category}|${name}|${fillcolor}\n")
        
        # Add connection from CMakeHub to module
        string(APPEND dot_content "  \"CMakeHub\" -> \"${name}\" [color=\"#666666\", penwidth=1.0];\n")
    endforeach()
    
    # Create subgraphs for each category
    # Parse modules_by_category and create groups
    string(REPLACE "\n" ";" entries "${modules_by_category}")
    
    # Sort entries by category (first field)
    list(SORT entries)
    
    set(current_category "")
    set(has_open_subgraph FALSE)
    
    foreach(entry ${entries})
        string(REPLACE "|" ";" parts ${entry})
        list(GET parts 0 category)
        list(GET parts 1 name)
        list(GET parts 2 fillcolor)
        
        # Check if we need to close previous subgraph
        if(NOT "${category}" STREQUAL "${current_category}" AND has_open_subgraph)
            string(APPEND dot_content "  }\n\n")
            set(has_open_subgraph FALSE)
        endif()
        
        # Check if we need to create new subgraph
        if(NOT "${category}" STREQUAL "${current_category}")
            set(current_category "${category}")
            
            # Start new subgraph
            string(APPEND dot_content "  subgraph cluster_${category} {\n")
            string(APPEND dot_content "    label = \"${category}\";\n")
            string(APPEND dot_content "    style=filled;\n")
            string(APPEND dot_content "    fillcolor=\"${fillcolor}20\";\n")
            string(APPEND dot_content "    color=\"${fillcolor}\";\n")
            string(APPEND dot_content "    penwidth=2;\n")
            string(APPEND dot_content "    fontsize=12;\n")
            string(APPEND dot_content "    fontname=\"Arial\";\n")
            string(APPEND dot_content "    rank=same;\n\n")
            set(has_open_subgraph TRUE)
        endif()
        
        # Add module node
        string(APPEND dot_content "    \"${name}\" [style=filled, fillcolor=\"${fillcolor}\", fontsize=10, fontname=\"Arial\"];\n")
    endforeach()
    
    # Close last subgraph if open
    if(has_open_subgraph)
        string(APPEND dot_content "  }\n")
    endif()
    
    # Close digraph
    string(APPEND dot_content "}\n")
    
    # Write to file
    if(NOT DEFINED output_file OR output_file STREQUAL "")
        set(output_file "${CMAKE_BINARY_DIR}/cmakehub_all_modules.dot")
    endif()
    
    file(WRITE ${output_file} "${dot_content}")
    message(STATUS "Dependency graph saved to: ${output_file}")
    message(STATUS "To visualize, use: dot -Tpng ${output_file} -o dependencies.png")
endfunction()

function(cmakehub_dependency_graph output_file)
    cmakehub_log(STATUS "Generating dependency graph...")
    
    get_property(used_modules GLOBAL PROPERTY CMAKEHUB_USED_MODULES)
    
    if(NOT used_modules)
        message(STATUS "No modules loaded, dependency graph will be empty")
    endif()
    
    # Generate DOT format graph
    set(dot_content "digraph CMakeHub_Dependencies {\n")
    set(dot_content "${dot_content}  rankdir=LR;\n")
    set(dot_content "${dot_content}  node [shape=box];\n")
    set(dot_content "${dot_content}  \"CMakeHub\" [style=filled, fillcolor=lightblue];\n\n")
    
    # Add all used modules
    foreach(module_name ${used_modules})
        cmakehub_get_module_info(${module_name} success)
        if(success)
            cmakehub_get_module_property(${module_name} category category)
            set(dot_content "${dot_content}  \"${module_name}\" [category=\"${category}\"];\n")
            # Add connection from CMakeHub to each module
            set(dot_content "${dot_content}  \"CMakeHub\" -> \"${module_name}\";\n")
        endif()
    endforeach()
    
    # Add dependencies between modules
    set(visited_modules "")
    foreach(module_name ${used_modules})
        if(NOT module_name IN_LIST visited_modules)
            cmakehub_get_module_info(${module_name} success)
            if(success)
                cmakehub_get_module_property(${module_name} dependencies deps_json)
                cmakehub_parse_list("${deps_json}" dependencies)
                foreach(dep ${dependencies})
                    # Only add dependency if both modules are in used_modules
                    if(dep IN_LIST used_modules)
                        set(dot_content "${dot_content}  \"${module_name}\" -> \"${dep}\" [style=dashed, color=gray];\n")
                    endif()
                endforeach()
                list(APPEND visited_modules ${module_name})
            endif()
        endif()
    endforeach()
    
    set(dot_content "${dot_content}}\n")
    
    # Write to file
    if(NOT DEFINED output_file OR output_file STREQUAL "")
        set(output_file "${CMAKE_BINARY_DIR}/cmakehub_dependencies.dot")
    endif()
    
    file(WRITE ${output_file} "${dot_content}")
    message(STATUS "Dependency graph saved to: ${output_file}")
    message(STATUS "To visualize, use: dot -Tpng ${output_file} -o dependencies.png")
endfunction()

function(cmakehub_update module_name)
    cmakehub_log(STATUS "Checking for updates...")
    
    if(DEFINED module_name AND NOT module_name STREQUAL "")
        # Update specific module
        cmakehub_log(STATUS "Updating module: ${module_name}")
        cmakehub_cache_clear(${module_name})
        message(STATUS "Cache cleared. Module will be re-downloaded on next use.")
    else()
        # Update all used modules
        get_property(used_modules GLOBAL PROPERTY CMAKEHUB_USED_MODULES)
        if(used_modules)
            message(STATUS "Clearing cache for all used modules...")
            foreach(module_name ${used_modules})
                cmakehub_cache_clear(${module_name})
            endforeach()
            message(STATUS "All modules will be re-downloaded on next use.")
        else()
            message(STATUS "No modules are currently in use")
        endif()
    endif()
endfunction()

function(cmakehub_check_compatibility module_name)
    cmakehub_log(STATUS "Checking compatibility for: ${module_name}")
    
    cmakehub_get_module_info(${module_name} success)
    if(NOT success)
        message(FATAL_ERROR "Module '${module_name}' not found")
    endif()
    
    cmakehub_get_module_property(${module_name} cmake_minimum_required cmake_min)
    cmakehub_get_module_property(${module_name} cpp_minimum_required cpp_min)
    
    message(STATUS "Compatibility Check for ${module_name}:")
    message(STATUS "==================================")
    
    # Check CMake version
    message(STATUS "CMake Version:")
    message(STATUS "  Required: ${cmake_min}")
    message(STATUS "  Current:  ${CMAKE_VERSION}")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL cmake_min)
        message(STATUS "  Status: ✓ Compatible")
    else()
        message(STATUS "  Status: ✗ Incompatible")
    endif()
    
    message(STATUS "")
    
    # Check C++ version
    message(STATUS "C++ Standard:")
    message(STATUS "  Required: ${cpp_min}")
    if(CMAKE_CXX_STANDARD)
        message(STATUS "  Current:  C++${CMAKE_CXX_STANDARD}")
        if(CMAKE_CXX_STANDARD GREATER_EQUAL cpp_min)
            message(STATUS "  Status: ✓ Compatible")
        else()
            message(STATUS "  Status: ✗ Incompatible")
        endif()
    else()
        message(STATUS "  Current:  Not set")
        message(STATUS "  Status: ⚠ Unknown (CMAKE_CXX_STANDARD not set)")
    endif()
    
    # Check platform compatibility
    message(STATUS "")
    message(STATUS "Platform:")
    message(STATUS "  OS:       ${CMAKE_SYSTEM_NAME}")
    message(STATUS "  Arch:     ${CMAKE_SYSTEM_PROCESSOR}")
    message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
endfunction()

# =============================================================================
# Initialization
# =============================================================================

cmakehub_log(STATUS "CMakeHub initialized")
cmakehub_log(STATUS "Cache directory: ${CMH_CACHE_DIR}")
cmakehub_log(STATUS "Modules index: ${CMAKEHUB_MODULES_INDEX}")
cmakehub_log(STATUS "Version check mode: ${CMAKEHUB_VERSION_CHECK_MODE}")