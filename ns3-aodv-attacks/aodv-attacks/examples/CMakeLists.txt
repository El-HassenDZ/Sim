# contrib/aodv-attacks/examples/CMakeLists.txt
# Builds the main scenario as an ns-3 example program. Requires ns-3 to be
# configured with examples enabled (./ns3 configure --enable-examples).

build_lib_example(
  NAME aodv-attack-sim
  SOURCE_FILES aodv-attack-sim.cc
  LIBRARIES_TO_LINK
    ${libaodv-attacks}
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
