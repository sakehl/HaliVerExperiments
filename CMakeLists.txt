cmake_minimum_required(VERSION 3.16)
project(HaliVerExperiments)

include(CTest)

set(CMAKE_CXX_STANDARD 11)  # or newer
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)
set(CMAKE_PREFIX_PATH /haliver)

find_package(Halide REQUIRED)

set(OUTFILE halide.out)

function(build_unit_test)
  set(options)
  set(oneValueArgs TARGET DIR)
  set(multiValueArgs LABELS)
  cmake_parse_arguments(UT "${options}" "${oneValueArgs}"
                          "${multiValueArgs}" ${ARGN} )

  add_executable(${UT_TARGET} ${UT_DIR}/${UT_TARGET}.cpp)

  target_link_libraries(${UT_TARGET} PRIVATE Halide::Halide)

  add_custom_command(
    OUTPUT ${UT_TARGET}_front_pvl.pvl
    COMMAND ./${UT_TARGET} front
    DEPENDS ${UT_TARGET}
    VERBATIM
  )

  add_custom_command(
    OUTPUT ${UT_TARGET}_pvl-0.pvl
    COMMAND ./${UT_TARGET} 0
    DEPENDS ${UT_TARGET}
    VERBATIM
  )

  add_custom_command(
    OUTPUT ${UT_TARGET}_pvl-1.pvl
    COMMAND ./${UT_TARGET} 1
    DEPENDS ${UT_TARGET}
    VERBATIM
  )

  add_custom_command(
    OUTPUT ${UT_TARGET}_pvl-2.pvl
    COMMAND ./${UT_TARGET} 2
    DEPENDS ${UT_TARGET}
    VERBATIM
  )

  add_custom_command(
    OUTPUT ${UT_TARGET}_pvl-3.pvl
    COMMAND ./${UT_TARGET} 3
    DEPENDS ${UT_TARGET}
    VERBATIM
  )

  add_custom_target(
    ${UT_TARGET}_pvl ALL
    DEPENDS ${UT_TARGET}_front_pvl.pvl ${UT_TARGET}_pvl-0.pvl
    ${UT_TARGET}_pvl-1.pvl ${UT_TARGET}_pvl-2.pvl ${UT_TARGET}_pvl-3.pvl
  )

  add_test(NAME ${UT_TARGET}_front_pvl.pvl
    COMMAND vct --backend-option --conditionalizePermissions --silicon-quiet ${UT_TARGET}_front_pvl.pvl
    )
  set_tests_properties(${UT_TARGET}_front_pvl.pvl PROPERTIES
    LABELS ${UT_DIR}:front:${UT_LABELS}
  )

  add_test(NAME ${UT_TARGET}_pvl-0.pvl
    COMMAND vct --backend-option --conditionalizePermissions --silicon-quiet ${UT_TARGET}_pvl-0.pvl
    )
  set_tests_properties(${UT_TARGET}_pvl-0.pvl PROPERTIES
    LABELS ${UT_DIR}:back:${UT_LABELS}
  )

  add_test(NAME ${UT_TARGET}_pvl-1.pvl
    COMMAND vct --backend-option --conditionalizePermissions --silicon-quiet ${UT_TARGET}_pvl-1.pvl
    )
  set_tests_properties(${UT_TARGET}_pvl-1.pvl PROPERTIES
    LABELS ${UT_DIR}:back:${UT_LABELS}
  )

  add_test(NAME ${UT_TARGET}_pvl-2.pvl
    COMMAND vct --backend-option --conditionalizePermissions --silicon-quiet ${UT_TARGET}_pvl-2.pvl
    )
  set_tests_properties(${UT_TARGET}_pvl-2.pvl PROPERTIES
    LABELS ${UT_DIR}:back:${UT_LABELS}
  )

  add_test(NAME ${UT_TARGET}_pvl-3.pvl
    COMMAND vct --backend-option --conditionalizePermissions --silicon-quiet ${UT_TARGET}_pvl-3.pvl
    )
  set_tests_properties(${UT_TARGET}_pvl-3.pvl PROPERTIES
    LABELS ${UT_DIR}:back:${UT_LABELS}
  )
  
endfunction()

build_unit_test(TARGET blur DIR blur)

build_unit_test(TARGET hist DIR hist)

build_unit_test(TARGET conv_layer DIR conv_layer)

build_unit_test(TARGET gemm DIR gemm)

build_unit_test(TARGET auto_viz DIR auto_viz)