# SPDX-License-Identifier: GPL-2.0-only
#
# Exemples du module framework_manet (compilés si ./ns3 configure --enable-examples).
#
# STEP 0  : programme de validation de l'environnement (aucune simulation).
# STEP 1a : calibration radio statique (sondes de liaison à deux nœuds).
build_lib_example(
  NAME framework-manet-env-check
  SOURCE_FILES framework-manet-env-check.cc
  LIBRARIES_TO_LINK
    ${libframework_manet}
    ${libcore}
)

build_lib_example(
  NAME framework-manet-link-calibration
  SOURCE_FILES framework-manet-link-calibration.cc
  LIBRARIES_TO_LINK
    ${libframework_manet}
    ${libwifi}
    ${libnetwork}
    ${libcore}
)
