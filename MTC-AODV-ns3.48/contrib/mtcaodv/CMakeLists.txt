# SPDX-License-Identifier: GPL-2.0-only

# Gate 1A deliberately depends only on stable ns-3 public modules.  The
# separate AODV routing fork will be added after this small integration unit
# has compiled successfully against the user's exact ns-3.48 installation.
build_lib(
  LIBNAME mtcaodv
  SOURCE_FILES
    helper/attack-manager.cc
    model/attack-behavior.cc
    model/blackhole-behavior.cc
  HEADER_FILES
    helper/attack-manager.h
    model/attack-behavior.h
    model/blackhole-behavior.h
  LIBRARIES_TO_LINK
    ${libcore}
    ${libnetwork}
  TEST_SOURCES
    test/attack-manager-test-suite.cc
    test/blackhole-behavior-test-suite.cc
)

