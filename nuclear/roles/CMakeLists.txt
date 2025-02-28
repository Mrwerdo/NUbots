# Get our role directories
set(role_source_dir "${PROJECT_SOURCE_DIR}/${NUCLEAR_ROLES_DIR}")
set(role_binary_dir "${PROJECT_BINARY_DIR}/${NUCLEAR_ROLES_DIR}")

# This gets all of our source files
file(GLOB rolefiles "${role_source_dir}/*.role")

# Backup the state of all the role variables before we re read them This is needed so we can delete roles that no longer
# exist
get_cmake_property(vars CACHE_VARIABLES)
foreach(var ${vars})
  if(var MATCHES "^ROLE_[a-zA-Z0-9]+")
    set("BACKUP_${var}" ${${var}})
    unset(${var} CACHE)
  endif()
endforeach(var)

# Find all of our role files
foreach(rolefile ${rolefiles})
  get_filename_component(roleext ${rolefile} EXT)

  # Check it is a .role file
  if(roleext STREQUAL .role)

    # Get role name and add it to our list of roles
    get_filename_component(role_name ${rolefile} NAME_WE)

    # If we had a backup as to if this role is enabled use it. Otherwise default to true.
    if(DEFINED "BACKUP_ROLE_${role_name}")
      option("ROLE_${role_name}" "Build the role ${role_name}" ${BACKUP_ROLE_${role_name}})
    else()
      option("ROLE_${role_name}" "Build the role ${role_name}" ON)
    endif()
  endif()
endforeach(rolefile)

# Set our NUClear roles based on the ROLES cache variables that are set to true This makes it so that only the roles
# that are enabled will be built
get_cmake_property(vars CACHE_VARIABLES)
unset(NUCLEAR_ROLES CACHE)
foreach(var ${vars})
  if(var MATCHES "^ROLE_[a-zA-Z0-9_]+")
    if(${var})
      string(SUBSTRING ${var} 5 -1 role_name)
      set(NUCLEAR_ROLES
          ${NUCLEAR_ROLES} ${role_name}
          CACHE INTERNAL "A list of the roles that are enabled to be built." FORCE
      )
    endif()
  endif()
endforeach()

# We need the NUClear role function here so it can be used in the role files
include(NUClearRole.cmake)

# Clear our NUClear modules variable so we can repopulate it
unset(NUCLEAR_MODULES CACHE)

# Note that the loop value of this for loop sets the local role variable that is used in the included role file
foreach(role ${NUCLEAR_ROLES})
  include("${role_source_dir}/${role}.role")
endforeach(role)

# If we want to build all the modules, we will list them all in the required format then create the role manually
if(BUILD_ALL_MODULES)

  # ${role} is the name of the role created when you call nuclear_role()
  set(role "allmodules")

  # * We find all the modules by finding all CMakeLists.txt files in the module directory
  # * A directory containing a CMakeLists.txt file in the module directory is assumed to be a module
  file(
    GLOB_RECURSE module_cmakelists
    RELATIVE "${PROJECT_SOURCE_DIR}/${NUCLEAR_MODULE_DIR}"
    "${PROJECT_SOURCE_DIR}/${NUCLEAR_MODULE_DIR}/**/CMakeLists.txt"
  )

  # The paths are those of the CMakeLists.txt files, so we make them the expected format here
  foreach(module ${module_cmakelists})
    # Strip the trailing filename to get the module directory
    string(REPLACE "/CMakeLists.txt" "" module ${module})
    # Replace path separators with the namespace-style colon pairs nuclear_role() expects
    string(REPLACE "/" "::" module ${module})
    # Add the resulting string to the list of all modules
    list(APPEND ALL_MODULES ${module})
  endforeach(module ${module_cmakelists})

  # Actually generate the role with the module names
  nuclear_role(${ALL_MODULES})
endif()
