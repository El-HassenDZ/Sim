# MTC-AODV — implémentation ns-3.48

Implémentation expérimentale de **MTC-AODV** — *Mobility- and Opportunity-Aware Beta
Trust with a Partition-Tolerant Micro-Blockchain for Multiple Blackhole Mitigation in
MANETs* — d'après la spécification mathématique et algorithmique v1.0 (2026-09-03).

Mécanismes : **OCEA** (attribution d'évidence conditionnée par l'opportunité),
**MOBeta-Trust** (posterior Beta temporalisé) et **PTMB** (micro-registre tolérant aux
partitions).

## Mise en route

```bash
scripts/fetch_ns3.sh     # récupère ns-3.48 et relie contrib/mtcaodv
scripts/fork_aodv.sh     # génère le fork AODV (renommage mécanique seul)
scripts/build.sh         # configure et compile (profil `default` = assertions actives)
```

`NS3_DIR` (défaut `~/ns3/ns-3.48`), `PROFILE` (`default` | `optimized`) et `JOBS`
paramètrent les scripts. Le profil `optimized` désactive les `NS_ASSERT` : il ne doit
servir qu'après validation complète en `default`.

## Organisation

```
contrib/mtcaodv/     module ns-3 (fork AODV + composants MTC-AODV)
experiments/         orchestration, validation et analyse en Python 3
scripts/             récupération, fork, build, tests
docs/                ARCHITECTURE, PARAMETERS, DIVERGENCES, LICENSES, RESULTS_STATUS
```

## Documents à lire avant toute modification

| Fichier | Contenu |
|---|---|
| `docs/ARCHITECTURE.md` | correspondance concept → classe → fichier → algorithme → équation |
| `docs/PARAMETERS.md` | les 35 points ouverts C-01…C-35, valeurs par défaut et justifications |
| `docs/DIVERGENCES.md` | **tout écart à la spécification**, avec cause et impact |
| `docs/LICENSES.md` | répartition MIT / GPL-2.0-only par répertoire |
| `docs/RESULTS_STATUS.md` | ce qui a été réellement mesuré, et ce qui ne l'a pas été |

## Règles de projet non négociables

1. `src/aodv/` de ns-3.48 n'est **jamais** modifié (invariant 20.3.1). C'est la
   baseline A.
2. La vérité terrain des attaquants n'est accessible qu'au générateur de scénario et à
   l'évaluation hors ligne (invariant 20.2.8), propriété vérifiée par le test T-34.
3. Une valeur non applicable est exportée `NaN`/`N/A`, jamais zéro (invariant 20.4.6).
4. Aucun résultat non exécuté n'est présenté comme mesuré ; voir
   `docs/RESULTS_STATUS.md`.
5. Tout écart à la spécification est consigné dans `docs/DIVERGENCES.md` avant que
   l'étape correspondante soit déclarée validée.
