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
| Baseline AODV (variante A) | **NON MESURÉ** | étape 3 |
| Attaque A2.3/A2.4 *dans* le protocole | **NON MESURÉ** | étape 4 : seule la politique est testée, pas encore son câblage au fork |
| Détection RREP, OCEA, MOBeta | **NON MESURÉ** | étapes 5 et 6 |
| Certification et PTMB | **NON MESURÉ** | étape 7 |
| Campagne confirmatoire A7 | **NON MESURÉ** | étape 11 — hors budget de l'environnement de développement, voir ci-dessous |
| Statuts « Testé Gate 1A » du §18.1 de la spécification | **NON MESURÉ** | code inexistant, statuts requalifiés — voir `DIVERGENCES.md` |

---

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
