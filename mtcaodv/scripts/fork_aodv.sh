#!/usr/bin/env bash
#
# Fork isolé du module AODV de ns-3.48 vers contrib/mtcaodv.
#
# Décision normative de la spécification (§2.2, invariant 20.3.1) : le module
# standard src/aodv/ NE DOIT PAS être modifié. Le protocole sécurisé est un fork
# dérivé des sources exactes de ns-3.48, conservant les notices de licence
# d'origine (GPL-2.0-only, IITP RAS).
#
# Le fork consiste uniquement en un renommage mécanique et traçable :
#   fichiers      aodv-*.{h,cc}      -> mtc-aodv-*.{h,cc}
#   namespace     ns3::aodv          -> ns3::mtcaodv
#   TypeId        "ns3::aodv::X"     -> "ns3::mtcaodv::X"
#   helper        AodvHelper         -> MtcAodvHelper
#   log component "AodvX"            -> "MtcAodvX"
#
# Aucune logique de routage n'est modifiée par ce script. Les hooks MTC-AODV
# (A2.3, A2.4, A3.1, A3.2, A6) sont ajoutés ensuite par des patchs versionnés,
# afin que « fork mécanique » et « modification fonctionnelle » restent
# distinguables dans l'historique Git.
#
# Le script est idempotent : il refuse d'écraser un fork déjà modifié à la main
# sauf si FORCE=1.
#
set -euo pipefail

NS3_VERSION="${NS3_VERSION:-3.48}"
NS3_DIR="${NS3_DIR:-${HOME}/ns3/ns-${NS3_VERSION}}"
UPSTREAM="${NS3_DIR}/src/aodv"
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="${PROJECT_ROOT}/contrib/mtcaodv"
STAMP="${TARGET}/model/.fork-provenance"

log() { printf '[fork_aodv] %s\n' "$*"; }

[[ -d "${UPSTREAM}" ]] || { echo "source AODV introuvable : ${UPSTREAM}" >&2; exit 1; }

if [[ -f "${STAMP}" && "${FORCE:-0}" != "1" ]]; then
    log "fork déjà présent (${STAMP}) ; relancer avec FORCE=1 pour régénérer"
    exit 0
fi

mkdir -p "${TARGET}/model" "${TARGET}/helper"

# Couples (source, destination). Le préfixe mtc- évite toute collision de nom de
# fichier d'en-tête dans le répertoire d'include agrégé de ns-3 (build/include/ns3).
declare -a MODEL_STEMS=(dpd id-cache neighbor packet routing-protocol rqueue rtable)

fork_one() {
    local src="$1" dst="$2"

    # Renommages, du plus spécifique au plus général.
    sed -E \
        -e 's/AodvHelper/MtcAodvHelper/g' \
        -e 's/\bAodv([A-Za-z]*)/MtcAodv\1/g' \
        -e 's/\bAODV([A-Z_]*_H)\b/MTC_AODV\1/g' \
        -e 's|#include "aodv-|#include "mtc-aodv-|g' \
        -e 's|#include "ns3/aodv-|#include "ns3/mtc-aodv-|g' \
        -e 's/\bnamespace aodv\b/namespace mtcaodv/g' \
        -e 's|// namespace aodv|// namespace mtcaodv|g' \
        -e 's/\baodv::/mtcaodv::/g' \
        -e 's/(\\|@)(ingroup|defgroup|addtogroup) aodv\b/\1\2 mtcaodv/g' \
        "${src}" > "${dst}"
}

for stem in "${MODEL_STEMS[@]}"; do
    fork_one "${UPSTREAM}/model/aodv-${stem}.h"  "${TARGET}/model/mtc-aodv-${stem}.h"
    fork_one "${UPSTREAM}/model/aodv-${stem}.cc" "${TARGET}/model/mtc-aodv-${stem}.cc"
done
fork_one "${UPSTREAM}/helper/aodv-helper.h"  "${TARGET}/helper/mtc-aodv-helper.h"
fork_one "${UPSTREAM}/helper/aodv-helper.cc" "${TARGET}/helper/mtc-aodv-helper.cc"

# Trace de provenance exigée par la GPL (marquage des fichiers modifiés) et par
# le §2.2 de la spécification.
upstream_hash=$(cd "${UPSTREAM}" && find . -type f \( -name '*.h' -o -name '*.cc' \) \
                  | sort | xargs sha256sum | sha256sum | cut -d' ' -f1)
cat > "${STAMP}" <<EOF
# Provenance du fork AODV (généré par scripts/fork_aodv.sh — ne pas éditer)
upstream            = ns-${NS3_VERSION} src/aodv
upstream_tree_sha256= ${upstream_hash}
forked_at           = $(date -u +%Y-%m-%dT%H:%M:%SZ)
license             = GPL-2.0-only (Copyright (c) 2009 IITP RAS) — conservée
transformation      = renommage mécanique uniquement, aucune modification de logique
EOF

log "fork généré dans ${TARGET} ; empreinte amont ${upstream_hash:0:16}"

# Garde-fou : plus aucune référence au namespace amont ne doit subsister.
if grep -rn '\bnamespace aodv\b\|"ns3::aodv::' "${TARGET}/model" "${TARGET}/helper" ; then
    echo "ERREUR : références résiduelles au namespace aodv" >&2
    exit 1
fi
log "vérification namespace : OK"
