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
python3 -m pytest experiments/tests/ -q

echo
echo "=== validation de la configuration ==="
python3 experiments/validate_config.py
