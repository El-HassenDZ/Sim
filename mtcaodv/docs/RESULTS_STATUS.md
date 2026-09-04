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

## Étape 0 — baseline du pilote `mtc-aodv-pilot`

Toutes les lignes de cette section proviennent d'exécutions réelles sur ns-3.48, profil
`default` (NS3_ASSERT=ON), dans l'environnement de développement. Aucune n'a valeur
confirmatoire : 10 seeds ne constituent pas une estimation appariée au sens d'A7.

**Configuration de référence.** N = 20 ; 600 × 600 m ; Random Waypoint 1–20 m/s, pause
2 s ; IEEE 802.11b, `ns3::AdhocWifiMac`, `DsssRate11Mbps` (données et diffusions),
`DsssRate1Mbps` (contrôle 802.11), 16 dBm ; log-distance d'exposant 2,2 ; IPv4
10.1.0.0/24 ; HELLO 1 s ; 4 flux CBR UDP de 512 octets à 4 paquets/s ; simTime 60 s,
warm-up 10 s, vidange 5 s, donc \(T_{eval} = 45\) s ; seeds 1001–1010, run 1.

| Métrique | moyenne | écart-type | médiane | Q1 | Q3 | min | max |
|---|---:|---:|---:|---:|---:|---:|---:|
| PDR | 0,8168 | 0,1093 | 0,8826 | 0,7656 | 0,8858 | 0,6306 | 0,9194 |
| délai moyen (s) | 0,0300 | 0,0113 | 0,0323 | 0,0229 | 0,0385 | 0,0116 | 0,0458 |
| NRO | 3,148 | 0,528 | 3,056 | 2,795 | 3,400 | 2,417 | 4,134 |
| degré moyen | 7,93 | 1,28 | 7,52 | 7,38 | 8,08 | 6,32 | 10,83 |
| graphe connexe | 0,861 | 0,119 | 0,913 | 0,745 | 0,935 | 0,696 | 1,000 |

**Diagnostic à facteur unique** (mêmes 10 seeds, un seul paramètre modifié) :

| Configuration | PDR moyen | degré | graphe connexe | sauts moyens |
|---|---:|---:|---:|---:|
| 600 × 600 m (référence) | 0,817 | 7,93 | 0,861 | 2,18 |
| 450 × 450 m | 0,881 | 11,73 | 1,000 | 1,58 |
| 400 × 400 m | 0,924 | 13,63 | 1,000 | 1,40 |
| vitesse 1–5 m/s | 0,808 | 7,63 | 0,776 | 3,07 |
| HELLO désactivés | 0,735 | 7,93 | 0,861 | — |
| HELLO à 3 s | 0,803 | 7,93 | 0,861 | — |

**Interprétation mesurée, non supposée.** La perte résiduelle est majoritairement
géométrique : à 400 × 400 m le graphe est connexe en permanence et le PDR atteint 0,924.
Mais le nombre de sauts moyen y tombe à 1,40, c'est-à-dire que la plupart des flux
n'ont plus de nœud intermédiaire — or un Blackhole n'intercepte que du trafic en transit,
et l'observation du forwarding (étape 3) exige un relais observable. Resserrer la zone
améliorerait donc le PDR en supprimant le phénomène à étudier. La référence reste
600 × 600 m. Réduire la mobilité n'aide pas non plus : à 1–5 m/s la connectivité se
dégrade (0,776), le RWP lent ne résorbant pas les partitions initiales.

**Conséquence à trancher.** À 20 nœuds, connectivité et profondeur multi-saut s'opposent :
un PDR sain proche de 100 % et des chemins de 2–3 sauts ne sont pas simultanément
atteignables. L'objectif « PDR > 90 % sous attaque » doit être lu relativement à une
baseline saine de 0,82, ou la population doit être augmentée (le §16.1 fixe N = 100 pour
l'étude confirmatoire).

| Contrôle de l'étape 0 | Statut | Détail |
|---|---|---|
| Compilation du pilote et du module sur ns-3.48 | **MESURÉ** | profil `default`, exit 0 |
| Suite `mtcaodv-metrics` (M-01 à M-13) | **MESURÉ** | 1/1 suite PASS |
| Suite `mtcaodv-attack` (non-régression) | **MESURÉ** | 1/1 suite PASS |
| Reproductibilité T-32 | **MESURÉ** | même seed exécuté deux fois : CSV et manifest identiques bit à bit |
| Fork `mtcaodv` ≡ AODV standard | **MESURÉ** | 10 seeds appariés, 0 divergence sur les 9 colonnes de mesure |
| Appariement (empreinte de scénario) | **MESURÉ** | identique entre protocoles, différente dès qu'un paramètre exogène change |
| Rejet fail-closed d'une configuration impossible | **MESURÉ** | `--nodes=255`, warm-up couvrant la simulation, ratio hors [0,1] |
| Rejet d'un ratio d'attaquants non nul à l'étape 0 | **MESURÉ** | code de retour 1 |
| Rejet de données falsifiées par `validate_step0.py` | **MESURÉ** | PDR modifié sans ses compteurs : rejeté |
| Attaque A2.3/A2.4 dans le protocole | **NON MESURÉ** | étape 1 |
| Détection RREP, OCEA, MOBeta, certificats, PTMB | **NON MESURÉ** | étapes 2 à 10 |

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
