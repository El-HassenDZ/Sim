# SPDX-License-Identifier: GPL-2.0-only

# Gate 1B adds an AODV-interoperable malicious routing protocol.  The stock
# `src/aodv` module is linked, not modified: its packet headers, routing
# table, request queue, neighbour table and id cache are consumed through the
# exported ns3/aodv-*.h headers so the fork cannot drift from upstream wire
# format or table semantics.
build_lib(
  LIBNAME mtcaodv
  SOURCE_FILES
    helper/attack-manager.cc
    helper/blackhole-aodv-helper.cc
    model/attack-behavior.cc
    model/blackhole-aodv-routing-protocol.cc
    model/blackhole-behavior.cc
  HEADER_FILES
    helper/attack-manager.h
    helper/blackhole-aodv-helper.h
    model/attack-behavior.h
    model/blackhole-aodv-routing-protocol.h
    model/blackhole-behavior.h
  LIBRARIES_TO_LINK
    ${libaodv}
    ${libapplications}
    ${libcore}
    ${libinternet}
    ${libmobility}
    ${libnetwork}
    ${libpropagation}
    ${libwifi}
  TEST_SOURCES
    test/attack-manager-test-suite.cc
    test/blackhole-behavior-test-suite.cc
    test/blackhole-routing-test-suite.cc
)
