# Module ns-3.48 « mtcaodv ».
#
# Deux familles de fichiers cohabitent ici et ne doivent pas être confondues :
#
#   1. le fork mécanique du module AODV amont (mtc-aodv-*.{h,cc} + helper),
#      sous GPL-2.0-only, généré par scripts/fork_aodv.sh ;
#   2. les composants originaux MTC-AODV (attaque, détection, OCEA, MOBeta,
#      certification, PTMB, métriques), ajoutés au fil des étapes.
#
# Voir docs/LICENSES.md pour la répartition des licences par fichier.

build_lib(
  LIBNAME mtcaodv
  SOURCE_FILES
    # --- fork AODV ns-3.48 (GPL-2.0-only, IITP RAS) -------------------------
    helper/mtc-aodv-helper.cc
    model/mtc-aodv-dpd.cc
    model/mtc-aodv-id-cache.cc
    model/mtc-aodv-neighbor.cc
    model/mtc-aodv-packet.cc
    model/mtc-aodv-routing-protocol.cc
    model/mtc-aodv-rqueue.cc
    model/mtc-aodv-rtable.cc
    # --- composants MTC-AODV originaux --------------------------------------
    helper/attack-manager.cc
    model/attack-behavior.cc
    model/blackhole-behavior.cc
  HEADER_FILES
    helper/mtc-aodv-helper.h
    model/mtc-aodv-dpd.h
    model/mtc-aodv-id-cache.h
    model/mtc-aodv-neighbor.h
    model/mtc-aodv-packet.h
    model/mtc-aodv-routing-protocol.h
    model/mtc-aodv-rqueue.h
    model/mtc-aodv-rtable.h
    # --- composants MTC-AODV originaux --------------------------------------
    helper/attack-manager.h
    model/attack-behavior.h
    model/blackhole-behavior.h
  LIBRARIES_TO_LINK
    ${libapplications}
    ${libinternet-apps}
    ${libwifi}
    ${libmobility}
    ${libenergy}
    ${libflow-monitor}
  TEST_SOURCES
    test/mtcaodv-attack-test-suite.cc
)
