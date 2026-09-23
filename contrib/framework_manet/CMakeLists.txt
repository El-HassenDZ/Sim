# SPDX-License-Identifier: GPL-2.0-only
#
# framework_manet — module contrib ns-3.48 (baseline AODV instrumentée).
#
# STEP 0  : structure du module et validation de l'environnement.
# STEP 1a : helper radio commun + sonde de liaison + calibration statique.
#
# Le module est découvert automatiquement par ns-3 (tout sous-dossier de
# contrib/ contenant un CMakeLists.txt). LIBNAME doit être identique au nom
# du dossier pour que --enable-modules=framework_manet fonctionne.
#
# Dépendances déclarées dès STEP 0 : ce sont les modules que la baseline
# utilisera à STEP 1–4. Les déclarer maintenant fait échouer STEP 0 — et non
# une étape ultérieure — si l'un d'eux est absent de la configuration locale.
# Ordre : du plus spécifique au plus général (dépendances transitives de ns-3
# résolues par les modules eux-mêmes).

# Version de ns-3 (fichier VERSION lu par le CMakeLists.txt racine dans
# NS3_VER) injectée dans le code, pour que la validation d'environnement
# puisse détecter une compilation contre une autre version que 3.48.
# add_compile_definitions() a une portée de dossier : elle s'applique à la
# bibliothèque, à sa bibliothèque de tests et aux exemples de ce module.
add_compile_definitions(FMANET_NS3_VERSION="${NS3_VER}")

build_lib(
  LIBNAME framework_manet
  SOURCE_FILES
    helper/framework-manet-link-probe.cc
    helper/framework-manet-radio-helper.cc
    model/framework-manet-api-contract.cc
    model/framework-manet-statistics.cc
  HEADER_FILES
    helper/framework-manet-link-probe.h
    helper/framework-manet-radio-helper.h
    model/framework-manet-api-contract.h
    model/framework-manet-statistics.h
  LIBRARIES_TO_LINK
    ${libaodv}
    ${libnetanim}
    ${libflow-monitor}
    ${libenergy}
    ${libwifi}
    ${libapplications}
    ${libinternet}
    ${libmobility}
    ${libpropagation}
    ${libnetwork}
    ${libcore}
  TEST_SOURCES
    test/framework-manet-radio-test-suite.cc
    test/framework-manet-test-suite.cc
)
