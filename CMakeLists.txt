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
    OUTPUT ${UT_TARGET}_front.pvl
    COMMAND ./${UT_TARGET} ${UT_TARGET}_front front
    DEPENDS ${UT_TARGET}
    VERBATIM
  )

  add_test(NAME ${UT_TARGET}_front.pvl
    COMMAND vct --backend-option --conditionalizePermissions --silicon-quiet ${UT_TARGET}_front.pvl
    )
  set_tests_properties(${UT_TARGET}_front.pvl PROPERTIES
    LABELS ${UT_DIR}:front:${UT_LABELS}
  )

  foreach(V RANGE 3)
    add_custom_command(
      OUTPUT ${UT_TARGET}_${V}.pvl
      COMMAND ./${UT_TARGET} ${UT_TARGET}_${V} ${V}
      DEPENDS ${UT_TARGET}
      VERBATIM
    )

    add_test(NAME ${UT_TARGET}_${V}.pvl
    COMMAND vct --backend-option --conditionalizePermissions --silicon-quiet ${UT_TARGET}_${V}.pvl
    )
    set_tests_properties(${UT_TARGET}_${V}.pvl PROPERTIES
      LABELS ${UT_DIR}:back:${UT_LABELS}
    )

  endforeach()

  foreach(V RANGE 3)
    add_custom_command(
      OUTPUT ${UT_TARGET}_${V}_mem.pvl
      COMMAND ./${UT_TARGET} ${UT_TARGET}_${V}_mem ${V} mem
      DEPENDS ${UT_TARGET}
      VERBATIM
    )

    add_test(NAME ${UT_TARGET}_${V}_mem.pvl
    COMMAND vct --backend-option --conditionalizePermissions --silicon-quiet ${UT_TARGET}_${V}_mem.pvl
    )
    set_tests_properties(${UT_TARGET}_${V}_mem.pvl PROPERTIES
      LABELS ${UT_DIR}:back:${UT_LABELS}:mem
    )

  endforeach()


  add_custom_target(
    ${UT_TARGET}_pvl ALL
    DEPENDS ${UT_TARGET}_front.pvl ${UT_TARGET}_0.pvl
    ${UT_TARGET}_1.pvl ${UT_TARGET}_2.pvl ${UT_TARGET}_3.pvl
  )

  add_custom_target(
    ${UT_TARGET}_pvl_mem ALL
    DEPENDS ${UT_TARGET}_0_mem.pvl
    ${UT_TARGET}_1_mem.pvl ${UT_TARGET}_2_mem.pvl ${UT_TARGET}_3_mem.pvl
  )
  
endfunction()

function(build_single_unit_test)
  set(options)
  set(oneValueArgs TARGET DIR)
  set(multiValueArgs LABELS)
  cmake_parse_arguments(UT "${options}" "${oneValueArgs}"
                          "${multiValueArgs}" ${ARGN} )

  add_executable(${UT_TARGET} ${UT_DIR}/${UT_TARGET}.cpp)

  target_link_libraries(${UT_TARGET} PRIVATE Halide::Halide)

  add_custom_command(
    OUTPUT ${UT_TARGET}_mem.pvl
    COMMAND ./${UT_TARGET} ${UT_TARGET}_mem
    DEPENDS ${UT_TARGET}
    VERBATIM
  )

  add_custom_target(
    ${UT_TARGET}_pvl ALL
    DEPENDS ${UT_TARGET}_mem.pvl
  )

  add_test(NAME ${UT_TARGET}_mem.pvl
    COMMAND vct --backend-option --conditionalizePermissions --silicon-quiet ${UT_TARGET}_mem.pvl
    )
  set_tests_properties(${UT_TARGET}_mem.pvl PROPERTIES
    LABELS ${UT_DIR}:back:${UT_LABELS}
  )
endfunction()

build_unit_test(TARGET blur DIR src)

build_unit_test(TARGET hist DIR src)

build_unit_test(TARGET conv_layer DIR src)

build_unit_test(TARGET gemm DIR src)

build_unit_test(TARGET auto_viz DIR src)

# build_single_unit_test(TARGET bgu DIR bgu)

build_single_unit_test(TARGET bilateral_grid DIR src)

build_single_unit_test(TARGET camera_pipe DIR src)

build_single_unit_test(TARGET depthwise_separable_conv DIR src)