#!/usr/bin/env bash
#
# Exécute la totalité des tests du projet : suites ns-3 du module mtcaodv, puis tests
# Python de la couche d'orchestration et d'analyse.
#
# Les suites ns-3 doivent tourner en profil `default` : le profil `optimized` désactive
# les NS_ASSERT, donc les invariants numériques (§20.1) ne seraient plus vérifiés.
#
set -euo pipefail

NS3_VERSION="${NS3_VERSION:-3.48}"
NS3_DIR="${NS3_DIR:-${HOME}/ns3/ns-${NS3_VERSION}}"
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "=== suites ns-3 du module mtcaodv ==="
cd "${NS3_DIR}"
# --suite accepte un préfixe : toutes les suites nommées mtcaodv-* sont exécutées.
for suite in $(./ns3 run "test-runner --print-test-name-list" --no-build 2>/dev/null | grep '^mtcaodv-' || true); do
    echo "--- ${suite}"
    ./ns3 run "test-runner --suite=${suite} --verbose" --no-build
done

echo
echo "=== tests Python ==="
cd "${PROJECT_ROOT}"
# pytest est préférable (sortie plus lisible, collecte des tests écrits en style pytest),
# mais les tests de l'étape 0 sont écrits en unittest afin de rester exécutables sans
# aucune dépendance externe. On bascule donc automatiquement si pytest est absent.
if python3 -c "import pytest" 2>/dev/null; then
    python3 -m pytest experiments/tests/ -q
else
    echo "[run_tests] pytest absent — installez-le (pip install pytest) pour la suite"
    echo "[run_tests] complète. Repli : seuls les modules écrits en unittest sont exécutés."
    unittest_modules=()
    for module in experiments/tests/test_*.py; do
        # Un module qui importe pytest ne peut pas être chargé par unittest : l'exclure
        # explicitement vaut mieux qu'un ImportError qui masquerait les modules valides.
        if ! grep -q '^import pytest' "${module}"; then
            unittest_modules+=("$(basename "${module}" .py)")
        else
            echo "[run_tests] ignoré (nécessite pytest) : ${module}"
        fi
    done
    if [[ ${#unittest_modules[@]} -gt 0 ]]; then
        (cd experiments/tests && python3 -m unittest "${unittest_modules[@]}" -v)
    fi
fi

echo
echo "=== validation de la configuration ==="
python3 experiments/validate_config.py
