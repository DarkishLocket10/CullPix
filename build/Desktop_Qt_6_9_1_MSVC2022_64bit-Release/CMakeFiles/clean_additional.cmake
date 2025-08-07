# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "CMakeFiles\\photo_triage_cpp_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\photo_triage_cpp_autogen.dir\\ParseCache.txt"
  "photo_triage_cpp_autogen"
  )
endif()
