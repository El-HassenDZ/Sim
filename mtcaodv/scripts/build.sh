#!/usr/bin/env bash
#
# Configure et compile ns-3.48 avec le module contrib/mtcaodv.
#
# Deux profils sont utilisés dans le projet :
#   default   -> assertions et logs actifs ; profil de développement et de test.
#                Toutes les assertions d'invariants (§20) sont vérifiées.
#   optimized -> profil de campagne (A7). Les NS_ASSERT sont désactivés, donc ce
#                profil ne doit servir qu'après validation complète en `default`.
#
# Les modules activés sont restreints à ceux réellement nécessaires, afin de
# tenir le temps de compilation dans un environnement à 4 cœurs.
#
set -euo pipefail

NS3_VERSION="${NS3_VERSION:-3.48}"
NS3_DIR="${NS3_DIR:-${HOME}/ns3/ns-${NS3_VERSION}}"
PROFILE="${PROFILE:-default}"
JOBS="${JOBS:-$(nproc)}"

# flow-monitor fournit les compteurs de bout en bout (Éq. 20, 24-27) ;
# energy fournit la décomposition énergétique (Éq. 30).
MODULES="${MODULES:-mtcaodv,aodv,internet,wifi,mobility,applications,energy,flow-monitor,stats,propagation,internet-apps}"

cd "${NS3_DIR}"

echo "[build] profil=${PROFILE} jobs=${JOBS}"
./ns3 configure \
    --build-profile="${PROFILE}" \
    --enable-modules="${MODULES}" \
    --disable-werror \
    --enable-tests \
    --disable-examples

./ns3 build -j"${JOBS}"
echo "[build] terminé"
