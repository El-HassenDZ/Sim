# SPDX-License-Identifier: GPL-2.0-only

build_lib_example(
  NAME mtcaodv-gate1
  SOURCE_FILES mtcaodv-gate1.cc
  LIBRARIES_TO_LINK
    ${libmtcaodv}
    ${libcore}
    ${libnetwork}
)

