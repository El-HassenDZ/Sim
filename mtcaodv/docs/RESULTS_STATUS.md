# État réel des résultats

Ce fichier existe pour empêcher qu'une valeur attendue, théorique ou illustrative soit
un jour reprise comme un résultat expérimental. Il distingue quatre catégories :

- **MESURÉ** — produit par une exécution réelle, avec la commande et l'empreinte de
  configuration correspondantes.
- **NON MESURÉ** — non exécuté à ce jour. Aucune valeur n'est fournie.
- **ATTENDU** — prédiction issue de la conception. Jamais un résultat.
- **ILLUSTRATIF** — oracle mathématique de test (par exemple l'exemple OCEA du §9.3 de
  la spécification), sans valeur de scénario.

---

## Au 2026-09-04

| Élément | Statut | Détail |
|---|---|---|
| Récupération et intégrité de ns-3.48 | **MESURÉ** | tag `ns-3.48` GitLab, fichier `VERSION` = `3.48` |
| Fork AODV (renommage mécanique) | **MESURÉ** | 16 fichiers, 6 383 lignes amont, empreinte d'arbre `6c365f3cd385eafb…` ; aucune référence résiduelle au namespace `aodv` |
| Compilation du module `mtcaodv` | **MESURÉ** | profil `default` (NS3_ASSERT=ON, NS3_LOG=ON), 980/980 cibles, exit 0 |
| Coexistence des deux protocoles | **MESURÉ** | `ns3::aodv::RoutingProtocol` et `ns3::mtcaodv::RoutingProtocol` enregistrés simultanément |
| Politique d'attaque A2.1–A2.4 (hors ns-3) | **MESURÉ** | suite `mtcaodv-attack` : 10/10 cas PASS (T-01 à T-07, D-02, §21, invariant 20.4.3) |
| Validation de configuration (§13.3) | **MESURÉ** | 12 familles de contrôles ; 20/20 tests Python PASS |
| Baseline AODV (variante A) | **MESURÉ** | voir le tableau ci-dessous |
| Reproductibilité (T-32) | **MESURÉ** | deux exécutions identiques à N=25 et N=100 : fichiers de métriques bit-à-bit identiques |
| Attaque A2.3/A2.4 *dans* le protocole | **NON MESURÉ** | étape 4 : seule la politique est testée, pas encore son câblage au fork |
| Détection RREP, OCEA, MOBeta | **NON MESURÉ** | étapes 5 et 6 |
| Certification et PTMB | **NON MESURÉ** | étape 7 |
| Campagne confirmatoire A7 | **NON MESURÉ** | étape 11 — hors budget de l'environnement de développement, voir ci-dessous |
| Statuts « Testé Gate 1A » du §18.1 de la spécification | **NON MESURÉ** | code inexistant, statuts requalifiés — voir `DIVERGENCES.md` |

---

## Baseline AODV — mesures réelles

Toutes les lignes ci-dessous proviennent d'exécutions réelles dans cet environnement,
profil `default`, seed 1001 sauf mention contraire. Aucune n'a valeur confirmatoire.

| Scénario | Degré | Graphe connexe | Sauts | PDR | NRO |
|---|---:|---:|---:|---:|---:|
| **Grille 25 nœuds, 100×100 m, portée 36 m, 4 flux, 60 s** | 5,76 | 1,00 | 4,0 | **1,000** | 1,60 |
| Grille 25 nœuds, 1 flux | 5,76 | 1,00 | 4,0 | **1,000** | 1,60 |
| Mobile 25 nœuds, 700×700 m, 4 flux, 60 s | 9,05 | 1,00 | 1,0–3,1 | 0,983 | 1,37 |
| idem, seed 1002 | 7,55 | 0,72 | 1,0–3,3 | 0,813 | 0,27 |
| idem, seed 1003 | 6,04 | 1,00 | 1,6–3,3 | 0,900 | 1,88 |
| Mobile 25 nœuds, 500×500 m (dense) | 14,56 | 1,00 | 1,0–2,2 | 0,582 | — |
| Mobile 50 nœuds, 800×800 m, 5 flux, 180 s | 12,77 | 1,00 | 1,0–3,0 | 0,960 | 9,39 |
| Mobile 50 nœuds, 900×900 m, 6 flux, 60 s | 11,54 | 0,33 | 1,3–3,5 | 0,801 | 3,58 |
| Mobile 100 nœuds, 1300×1300 m, 10 flux, 60 s, HELLO 1 s | 10,78 | 1,00 | 2,0–5,2 | 0,372 | 67,2 |
| Mobile 100 nœuds, idem, HELLO 3 s | 10,78 | 1,00 | 2,0–5,2 | 0,609 | 23,4 |

Trois enseignements, tous contre-intuitifs et tous reproduits :

1. **Densifier dégrade le PDR.** À 25 nœuds, passer d'un degré de 9,05 à 14,56 fait
   chuter le PDR de 0,983 à 0,582, à connectivité et charge identiques. La contention et
   l'instabilité des routes l'emportent sur le gain de redondance.
2. **Le passage à l'échelle est le point faible.** À 100 nœuds, la surcharge de contrôle
   explose (NRO 67) et le PDR tombe à 0,37. Allonger la période HELLO de 1 s à 3 s le
   ramène à 0,61 : les HELLO sont un amplificateur, pas la cause unique. Ce point n'est
   **pas résolu** et interdit en l'état une campagne confirmatoire à N = 100.
3. **La consommation mémoire suit la même pathologie** : 76 Mo à N = 25, 362 Mo à N = 50,
   2,4 Go à N = 100 sur 60 s, et une exécution de 190 s à N = 100 a été tuée par l'OOM
   à 13,9 Go. Ce n'est pas une limite de la machine mais un symptôme de l'effondrement
   de congestion décrit au point 2.

## Limite d'exécution de l'environnement de développement

La campagne confirmatoire A7 exige au minimum 5 variantes × 4 ratios × 30 seeds = **600
exécutions** par régime de mobilité, soit **1 800** pour les trois régimes, avec
\(N=100\) et 600 s de temps simulé. À raison de quelques minutes de CPU par exécution,
le coût total se situe entre 150 et 500 heures CPU.

L'environnement de développement utilisé ici dispose de 4 cœurs et d'un conteneur
éphémère : **la campagne confirmatoire ne peut pas y être exécutée.** Ce qui y est
produit :

- tests unitaires et d'intégration ;
- fixtures causales (dont le diamant à 4 nœuds du §16.3) ;
- un **pilote réduit** (population et durée réduites, faible nombre de seeds), dont
  chaque résultat est étiqueté `PILOTE` et n'a **aucune** valeur confirmatoire ;
- les scripts de campagne, exécutables tels quels sur une machine dédiée.

Un résultat pilote ne peut être cité comme confirmation d'aucune hypothèse.
