# The studio, installed as the add-on manager installs a tool (<data>/tools/texstudio/), works on that Hailstone
# folder; given --data-dir, on that one. Run by ctest with PROGRAM (the built texstudio) and WORK (a scratch folder).
file(REMOVE_RECURSE ${WORK})
get_filename_component(dist ${PROGRAM} DIRECTORY)
get_filename_component(program_name ${PROGRAM} NAME)
set(data ${WORK}/Hailstone)
file(MAKE_DIRECTORY ${data}/tools ${data}/addons)
file(WRITE ${data}/hailstone.ini "")
file(COPY ${dist}/ DESTINATION ${data}/tools/texstudio)

# Installed in tools/: the folder two up. --devices exits at once; the log names the data folder.
execute_process(COMMAND ${data}/tools/texstudio/${program_name} --devices RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "texstudio --devices from tools/texstudio exited with ${rc}")
endif()
if(NOT EXISTS ${data}/logs/texstudio.log)
  message(FATAL_ERROR "texstudio installed in tools/texstudio did not work on the Hailstone folder (no logs/texstudio.log)")
endif()
file(READ ${data}/logs/texstudio.log log)
if(NOT log MATCHES "the Hailstone folder it is installed in")
  message(FATAL_ERROR "texstudio's log does not say it chose the folder it is installed in:\n${log}")
endif()

# --data-dir wins.
set(other ${WORK}/Other)
file(MAKE_DIRECTORY ${other})
execute_process(COMMAND ${data}/tools/texstudio/${program_name} --data-dir ${other} --devices RESULT_VARIABLE rc
                OUTPUT_QUIET ERROR_QUIET)
if(NOT rc EQUAL 0 OR NOT EXISTS ${other}/logs/texstudio.log)
  message(FATAL_ERROR "texstudio --data-dir did not work on the folder given (${rc})")
endif()
file(READ ${other}/logs/texstudio.log log)
if(NOT log MATCHES "--data-dir")
  message(FATAL_ERROR "texstudio's log does not name --data-dir:\n${log}")
endif()
file(REMOVE_RECURSE ${WORK})
