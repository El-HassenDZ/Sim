#!/usr/bin/env bash
#
# Récupère et déploie l'arbre ns-3.48 utilisé par MTC-AODV.
#
# La spécification (§16.1, Annexe C) fige ns-3.48 comme simulateur cible. Le
# miroir officiel www.nsnam.org n'est pas joignable depuis tous les
# environnements de développement ; l'archive GitLab du dépôt ns-3-dev porte le
# même tag et sert donc de source primaire ici.
#
# L'arbre ns-3 n'est volontairement PAS versionné dans ce dépôt : seul le
# module contrib/mtcaodv l'est. Le script recrée l'arbre à l'identique.
#
set -euo pipefail

NS3_VERSION="${NS3_VERSION:-3.48}"
NS3_TAG="ns-${NS3_VERSION}"
NS3_PARENT="${NS3_PARENT:-${HOME}/ns3}"
NS3_DIR="${NS3_PARENT}/ns-${NS3_VERSION}"
ARCHIVE_URL="https://gitlab.com/nsnam/ns-3-dev/-/archive/${NS3_TAG}/ns-3-dev-${NS3_TAG}.tar.gz"

# Racine du module MTC-AODV, déduite de l'emplacement de ce script.
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

log() { printf '[fetch_ns3] %s\n' "$*"; }

if [[ -x "${NS3_DIR}/ns3" ]]; then
    log "arbre ${NS3_TAG} déjà présent dans ${NS3_DIR}"
else
    mkdir -p "${NS3_PARENT}"
    local_archive="${NS3_PARENT}/${NS3_TAG}.tar.gz"

    if [[ ! -s "${local_archive}" ]]; then
        log "téléchargement de ${ARCHIVE_URL}"
        curl --fail --location --show-error --silent \
             --retry 4 --retry-delay 2 --max-time 900 \
             --output "${local_archive}.part" "${ARCHIVE_URL}"
        mv "${local_archive}.part" "${local_archive}"
    fi

    log "extraction dans ${NS3_DIR}"
    rm -rf "${NS3_DIR}.tmp"
    mkdir -p "${NS3_DIR}.tmp"
    # L'archive GitLab encapsule tout dans ns-3-dev-<tag>/ ; on aplatit ce niveau.
    tar -xzf "${local_archive}" -C "${NS3_DIR}.tmp" --strip-components=1
    rm -rf "${NS3_DIR}"
    mv "${NS3_DIR}.tmp" "${NS3_DIR}"
fi

# Le module vit dans le dépôt Git ; ns-3 le voit via un lien symbolique. Cela
# garde une source unique de vérité et évite de recopier le code à chaque build.
link_target="${NS3_DIR}/contrib/mtcaodv"
if [[ -L "${link_target}" || -e "${link_target}" ]]; then
    rm -rf "${link_target}"
fi
ln -s "${PROJECT_ROOT}/contrib/mtcaodv" "${link_target}"
log "contrib/mtcaodv -> ${PROJECT_ROOT}/contrib/mtcaodv"

# Les programmes de scénario vivent dans contrib/mtcaodv/examples/, comme l'impose la
# structure normative (§2.2). Ils sont exposés à ns-3 via scratch/ plutôt que par
# --enable-examples : cette option compilerait aussi la centaine d'exemples des modules
# wifi, internet et consorts, pour aucun bénéfice ici. Chaque fichier est lié
# individuellement car ns-3 refuse plusieurs fonctions main dans un même sous-répertoire
# de scratch.
shopt -s nullglob
for example in "${PROJECT_ROOT}"/contrib/mtcaodv/examples/*.cc; do
    scratch_link="${NS3_DIR}/scratch/$(basename "${example}")"
    rm -f "${scratch_link}"
    ln -s "${example}" "${scratch_link}"
    log "scratch/$(basename "${example}") -> ${example}"
done
shopt -u nullglob

# Vérification de version : le fichier VERSION de ns-3-dev doit annoncer le tag.
if [[ -f "${NS3_DIR}/VERSION" ]]; then
    log "VERSION déclarée : $(cat "${NS3_DIR}/VERSION")"
fi

log "NS3_DIR=${NS3_DIR}"
