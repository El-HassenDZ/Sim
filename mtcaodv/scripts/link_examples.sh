#!/usr/bin/env bash
#
# Rend les programmes de scénario de contrib/mtcaodv/examples/ exécutables par ns-3.
#
# Les scénarios vivent dans contrib/mtcaodv/examples/, comme l'impose la structure
# normative (§2.2 de la spécification). Ils sont exposés à ns-3 via scratch/ plutôt que
# par --enable-examples : cette option compilerait aussi la centaine d'exemples des
# modules wifi, internet et consorts, pour aucun bénéfice ici, et imposerait une
# reconfiguration complète de l'arbre.
#
# Le lien est symbolique : le fichier source reste unique, dans le dépôt. Éditer
# l'exemple et relancer `./ns3 run` suffit ; il n'y a pas de copie à resynchroniser.
#
# Chaque fichier est lié individuellement à la racine de scratch/ car ns-3 refuse
# plusieurs fonctions main dans un même sous-répertoire de scratch.
#
# Usage :
#   scripts/link_examples.sh                       # NS3_DIR par défaut
#   NS3_DIR=/home/hassen/res/ns-3.48 scripts/link_examples.sh
#
set -euo pipefail

NS3_VERSION="${NS3_VERSION:-3.48}"
NS3_DIR="${NS3_DIR:-${HOME}/ns3/ns-${NS3_VERSION}}"
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ ! -x "${NS3_DIR}/ns3" ]]; then
    echo "[link_examples] erreur : ${NS3_DIR} ne contient pas l'exécutable ./ns3" >&2
    exit 1
fi

mkdir -p "${NS3_DIR}/scratch"

shopt -s nullglob
count=0
for example in "${PROJECT_ROOT}"/contrib/mtcaodv/examples/*.cc; do
    target="${NS3_DIR}/scratch/$(basename "${example}")"
    ln -sfn "${example}" "${target}"
    echo "[link_examples] scratch/$(basename "${example}") -> ${example}"
    count=$((count + 1))
done
shopt -u nullglob

if [[ ${count} -eq 0 ]]; then
    echo "[link_examples] aucun exemple trouvé dans ${PROJECT_ROOT}/contrib/mtcaodv/examples/" >&2
    exit 1
fi

echo "[link_examples] ${count} programme(s) lié(s). Compilation : cd ${NS3_DIR} && ./ns3 build"
