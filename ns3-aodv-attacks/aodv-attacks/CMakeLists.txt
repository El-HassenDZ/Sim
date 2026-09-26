# contrib/aodv-attacks/CMakeLists.txt
# ns-3.48 (CMake build). Drop this folder into <ns-3.48>/contrib/aodv-attacks/
# and reconfigure. Examples build only when ns-3 is configured with
# --enable-examples.
#
# NS3-VERSION: build_lib / build_lib_example and the ${lib<module>} variables
# are the ns-3 CMake API (>= 3.36). The examples/ and test/ subdirectories are
# picked up automatically by ns-3 when examples/tests are enabled.

build_lib(
  LIBNAME aodv-attacks
  SOURCE_FILES
    model/malicious-aodv.cc
    model/blackhole-aodv.cc
    model/rreq-flooder.cc
  HEADER_FILES
    model/malicious-aodv.h
    model/blackhole-aodv.h
    model/rreq-flooder.h
  LIBRARIES_TO_LINK
    ${libcore}
    ${libnetwork}
    ${libinternet}
    ${libaodv}
    ${libwifi}
    ${libmobility}
    ${libenergy}
    ${libflow-monitor}
    ${libapplications}
    ${libpropagation}
)
