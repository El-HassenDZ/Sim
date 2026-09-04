# STEP 0 — Baseline AODV mesurée et reproductible

## 1. Numéro de l'étape

**Étape 0** du plan de développement MTC-AODV : baseline expérimentale.

Aucune baseline 20 nœuds / 60 s / 1–20 m/s / 10.1.0.0-24 n'existait auparavant dans ce
dépôt. Les mesures antérieures consignées dans `docs/RESULTS_STATUS.md` portaient sur
d'autres populations (25, 50, 100 nœuds), d'autres zones et un autre schéma de sortie.
L'étape 0 est donc obligatoire et constitue le point de départ de la nouvelle séquence.

## 2. Objectif scientifique

Établir et **caractériser** le comportement d'AODV standard, sans attaque et sans
défense, sur le scénario normatif du projet pilote. Trois questions, toutes tranchées par
la mesure :

1. Quel est le PDR de référence, et avec quelle dispersion entre réalisations aléatoires ?
2. La perte résiduelle vient-elle du protocole ou de l'absence physique de chemin ? La
   réponse conditionne toute la suite : si le réseau est structurellement partitionné,
   aucune logique de sécurité ne peut y remédier, et prétendre le contraire serait une
   erreur d'attribution causale.
3. Le fork `contrib/mtcaodv/` a-t-il dérivé de son amont `src/aodv/` ?

La règle de méthode est explicite et a été appliquée : **si le PDR sans attaque est
faible, on diagnostique la connectivité, la portée, la mobilité et la charge — on ne
compense jamais par la logique de sécurité.**

## 3. Objectif logiciel

- Un programme de scénario `mtc-aodv-pilot` exposant les huit options normatives
  (`--nodes`, `--simTime`, `--minSpeed`, `--maxSpeed`, `--attackerRatio`,
  `--attackStart`, `--seed`, `--run`) et tous les paramètres physiques encore non gelés.
- Les équations (20), (24), (25), (26), (27) et (28) implémentées **une seule fois**,
  hors du simulateur, donc testables au niveau 1.
- Un CSV au schéma normatif, extensible par les étapes suivantes sans casser les colonnes
  déjà validées.
- Une chaîne Python d'orchestration, de validation fail-closed et d'agrégation.

## 4. Dépendances

| Dépendance | Version | Rôle |
|---|---|---|
| ns-3 | **3.48** | simulateur ; modules `aodv`, `internet`, `wifi`, `mobility`, `applications`, `propagation`, `stats`, `energy`, `flow-monitor`, `internet-apps` |
| C++ | **17** | langage du module |
| Python | **3.9+** | orchestration, validation, agrégation — bibliothèque standard uniquement, aucune dépendance externe pour cette étape |
| Composants du dépôt déjà validés | — | `CbrTrafficSource/Sink`, `MtcTrafficHeader`, `MetricsCollector`, `TopologyProbe`, `AttackManager`, `MtcAodvHelper` |

`src/aodv/` de ns-3.48 n'est **pas** modifié (invariant 20.3.1).

## 5. Fichiers ajoutés

| Fichier | Rôle |
|---|---|
| `contrib/mtcaodv/model/network-metrics.h/.cc` | Équations (20), (24)–(28) sous forme purement numérique ; contrôle d'invariants ; formatage `NaN` (CSV) et `null` (JSON) |
| `contrib/mtcaodv/model/run-record.h/.cc` | Ligne de résultats CSV : ordre normatif des colonnes, extension par ajout en fin, validation fail-closed |
| `contrib/mtcaodv/helper/pilot-configuration.h/.cc` | Configuration normative du pilote : CLI, validation §13.3, description canonique, empreinte de scénario |
| `contrib/mtcaodv/examples/mtc-aodv-pilot.cc` | Programme de scénario de l'étape 0 |
| `contrib/mtcaodv/test/mtcaodv-metrics-test-suite.cc` | 13 tests de niveau 1 (M-01 à M-13) |
| `experiments/schemas/run_record_step0.json` | Schéma normatif du CSV et des contrôles X-01…X-08, MF-01…MF-05 |
| `experiments/validate_step0.py` | Validation fail-closed d'une exécution (A7.2) |
| `experiments/run_pilot.py` | Balayage de seeds et blocs appariés (A7.1) |
| `experiments/aggregate_step0.py` | Agrégation des exécutions **validées** uniquement |
| `scripts/link_examples.sh` | Expose les programmes de `examples/` à ns-3 via `scratch/` |
| `STEP_README.md` | Ce document |

## 6. Fichiers modifiés

| Fichier | Modification | Justification |
|---|---|---|
| `contrib/mtcaodv/model/metrics-collector.h` | remplace la définition locale de `MetricValue` par l'inclusion de `network-metrics.h` | une seule définition du type dans le dépôt |
| `contrib/mtcaodv/model/metrics-collector.cc` | délègue le calcul des équations (20), (24)–(28) à `ComputeDerivedNetworkMetrics()` ; supprime la copie locale de `FormatMetric()` | supprime la duplication des formules ; comportement inchangé, vérifié par la reconstruction et l'exécution du programme hérité |
| `contrib/mtcaodv/CMakeLists.txt` | déclare les nouvelles sources, en-têtes et la nouvelle suite de tests | — |
| `docs/RESULTS_STATUS.md` | ajoute les mesures réelles de l'étape 0 | — |
| `docs/DIVERGENCES.md` | ajoute D-I8 à D-I11 | toute divergence doit être consignée avant validation de l'étape |

Le programme hérité `mtcaodv-manet-scenario` et `ExperimentConfiguration` ne sont **pas**
modifiés ; ils compilent et s'exécutent toujours.

## 7. Classes

| Classe / structure | Fichier | Rôle |
|---|---|---|
| `ObservedCounters` | `network-metrics.h` | compteurs bruts observés pendant la fenêtre d'évaluation ; jamais déduits de la configuration (invariant 20.4.5) |
| `DerivedNetworkMetrics` | `network-metrics.h` | métriques dérivées, chacune définie ou explicitement absente |
| `MetricValue` | `network-metrics.h` | `std::optional<double>` ; rend l'inapplicabilité exprimable |
| `RunRecord` | `run-record.h` | ligne CSV ordonnée, extensible, validée |
| `PilotConfiguration` | `pilot-configuration.h` | paramètres exogènes du pilote, validation et empreinte |
| `PilotProtocol`, `PilotMobility`, `PilotPropagation` | `pilot-configuration.h` | énumérations de configuration et leurs conversions texte |

## 8. Méthodes importantes

| Méthode | Rôle |
|---|---|
| `ComputeDerivedNetworkMetrics(counters, delays, window)` | applique les Éq. (20), (24)–(28) ; ne borne ni ne corrige aucune valeur |
| `CheckNetworkMetricInvariants(metrics, &violation)` | vérifie `0 <= PDR,PLR <= 1` et `PDR + PLR = 1` (§20.1) |
| `FormatMetric(value)` / `FormatJsonNumber(value)` | export `NaN` en CSV, `null` en JSON, jamais `0` |
| `RunRecord::SetMetric()` / `Validate()` / `WriteCsv()` | remplit, vérifie et écrit la ligne de résultats |
| `PilotConfiguration::Validate()` | 25 contrôles fail-closed avant toute simulation |
| `PilotConfiguration::ComputeScenarioHash()` | empreinte 32 bits du bloc exogène, protocole exclu |
| `MetricsCollector::ComputeReport()` | assemble les compteurs et **délègue** les équations |
| `RunPilot()` / `main()` | corps du scénario ; toute exception devient un message lisible et un code de retour non nul |

## 9. Paramètres

Options normatives (valeurs par défaut de l'étape 0) :

| Option | Défaut | Unité |
|---|---:|---|
| `--nodes` | 20 | nœuds |
| `--simTime` | 60 | s |
| `--minSpeed` | 1 | m/s |
| `--maxSpeed` | 20 | m/s |
| `--attackerRatio` | 0 | — |
| `--attackStart` | 10 | s |
| `--seed` | 12345 | — |
| `--run` | 1 | — |

Paramètres calibrables (point ouvert **C-28**, non gelés) : `--protocol`, `--areaWidth`,
`--areaHeight`, `--pause`, `--mobility`, `--propagation`, `--pathLossExponent`,
`--txPowerDbm`, `--radioRange`, `--connectivityRadius`, `--dataRate`, `--controlRate`,
`--nonUnicastRate`, `--enableHello`, `--helloInterval`, `--flows`, `--packetSize`,
`--packetRate`, `--warmup`, `--drain`, `--excludeTrafficEndpoints`, `--outputDir`,
`--label`, et les six index de flux RNG.

Valeurs par défaut retenues pour le pilote : zone 600 × 600 m, pause 2 s, 802.11b
`DsssRate11Mbps` (données et diffusions) / `DsssRate1Mbps` (contrôle 802.11), puissance
16 dBm, log-distance d'exposant 2,2, HELLO actifs à 1 s, 4 flux CBR de 512 octets à
4 paquets/s, warm-up 10 s, vidange 5 s — soit \(T_{eval} = 45\) s.

## 10. Équations implémentées

| Équation | Objet | Emplacement |
|---|---|---|
| (2) | \(N_A=\lfloor r_aN+0{,}5\rfloor\) | `AttackManager` (déjà présent) ; contrôlé par X-04 |
| (20) | PDR | `network-metrics.cc` |
| (24) | PLR | `network-metrics.cc` |
| (25) | throughput et goodput | `network-metrics.cc` |
| (26) | délai moyen | `network-metrics.cc` |
| (27) | jitter | `network-metrics.cc` |
| (28) | NRO et RDF | `network-metrics.cc` |
| (29) | RUD | **non instrumentée** ; exportée `NaN`, jamais 0 |
| (30) | énergie | non connectée dans le pilote ; exportée `NaN` |

## 11. TraceSources

Cette étape n'introduit **aucune** nouvelle `TraceSource`. Elle consomme celles qui
existent déjà :

| Source | Origine | Usage |
|---|---|---|
| `CbrTrafficSource::Tx` | `(flowId, seq, payloadBytes)` | \(N_{app}^{tx}\), octets émis |
| `CbrTrafficSink::Rx` | `(flowId, seq, delay, payloadBytes)` | \(N_{app}^{rx}\), délais, octets livrés |
| `Ipv4L3Protocol::Tx` | ns-3 | comptage hop par hop du contrôle AODV (port UDP 654) et des RREQ originés |

## 12. RNG streams

| Composant | Index | Consommation |
|---|---:|---|
| Placement initial | 70000 | 2 sous-flux (X, Y) de l'allocateur rectangulaire |
| Mobilité | 71000 | 2 sous-flux par nœud (vitesse, pause) + réaffectation de l'allocateur partagé |
| Wi-Fi | 72000 | `WifiHelper::AssignStreams` |
| Tirage d'attaquants | 73001 | `AttackManager` (valeur d'Annexe C) |
| Trafic | 74000 + indice de flux | gigue de démarrage de chaque source CBR |
| Routage | 75000 | aléa interne d'AODV ou du fork |

`std::rand()` n'est utilisé nulle part. `RngSeedManager::SetSeed/SetRun` puis
`ResetNextStreamIndex()` sont appelés avant toute création d'objet aléatoire.

## 13. Compilation

Depuis un arbre ns-3.48 où `contrib/mtcaodv` est déjà en place :

```bash
cd /home/hassen/res/ns-3.48

# 1. Exposer le programme de scénario à ns-3 (une seule fois ; idempotent).
ln -sfn /home/hassen/res/ns-3.48/contrib/mtcaodv/examples/mtc-aodv-pilot.cc \
        /home/hassen/res/ns-3.48/scratch/mtc-aodv-pilot.cc

# 2. Compilation incrémentale. Aucune reconfiguration n'est nécessaire si l'arbre a
#    déjà été configuré avec --enable-tests ; CMake détecte seul les nouvelles sources
#    déclarées dans contrib/mtcaodv/CMakeLists.txt.
./ns3 build
```

Si l'arbre n'a jamais été configuré, ou s'il l'a été sans les tests :

```bash
cd /home/hassen/res/ns-3.48
./ns3 configure \
    --enable-modules=mtcaodv,aodv,internet,wifi,mobility,applications,propagation,stats,energy,flow-monitor,internet-apps \
    --enable-tests --disable-examples --disable-werror -d default
./ns3 build
```

Le profil `default` est requis : `optimized` désactive les `NS_ASSERT`, donc les
invariants du §20 ne seraient plus vérifiés.

## 14. Tests

```bash
cd /home/hassen/res/ns-3.48

# Niveau 1 — nouvelle suite de l'étape 0 (13 cas, M-01 à M-13).
./test.py -s mtcaodv-metrics

# Non-régression de la couche attaque déjà validée.
./test.py -s mtcaodv-attack

# Détail cas par cas si un test échoue.
./ns3 run "test-runner --suite=mtcaodv-metrics --verbose" --no-build
```

## 15. Simulation

```bash
cd /home/hassen/res/ns-3.48

# Exécution unique, paramètres normatifs de l'étape 0.
./ns3 run "mtc-aodv-pilot \
  --nodes=20 \
  --simTime=60 \
  --minSpeed=1 \
  --maxSpeed=20 \
  --attackerRatio=0 \
  --seed=12345 \
  --run=1"

# Balayage de 10 seeds (une exécution isolée ne caractérise pas une baseline).
python3 contrib/mtcaodv/../../experiments/run_pilot.py \
  --ns3Dir /home/hassen/res/ns-3.48 --seeds 1001-1010 --outputDir results/step0

# Validation fail-closed puis agrégation des seules exécutions validées.
python3 experiments/validate_step0.py results/step0/*_metrics.csv
python3 experiments/aggregate_step0.py results/step0/*_metrics.csv --groupBy protocol

# Bloc apparié : le fork doit reproduire AODV standard à l'identique.
python3 experiments/run_pilot.py --ns3Dir /home/hassen/res/ns-3.48 \
  --seeds 1001-1010 --protocols aodv,mtcaodv --outputDir results/paired
python3 experiments/validate_step0.py --paired \
  results/paired/aodv_s1001_r1_metrics.csv results/paired/mtcaodv_s1001_r1_metrics.csv
```

*(Le chemin exact de `run_pilot.py` dépend de l'endroit où vous placez `experiments/` ;
les commandes ci-dessus supposent `experiments/` à la racine de l'arbre ns-3.)*

## 16. Sorties

Par exécution, dans `--outputDir`, préfixées par `--label` :

| Fichier | Contenu |
|---|---|
| `<label>_metrics.csv` | en-tête + une ligne ; 19 colonnes normatives puis 18 colonnes d'étape |
| `<label>_manifest.json` | version ns-3, protocole, empreinte de scénario, adressage, 34 paramètres exogènes, tirage d'attaquants, compteurs observés, détail par flux, diagnostic topologique |

Colonnes normatives, dans l'ordre :
`protocol, nodes, simTime, minSpeed, maxSpeed, seed, run, attackerRatio, attackerCount,
attackStart, appTxPackets, appRxPackets, appTxBytes, appRxBytes, pdr, plr,
throughput_bps, goodput_bps, meanDelay_s`

Colonnes ajoutées par l'étape 0, après les précédentes :
`medianDelay_s, jitter_s, nro, rdf_per_s, aodvControlTx, routeDiscoveries,
deliveredNetworkBytes, evalWindow_s, flows, areaWidth_m, areaHeight_m, meanDegree,
connectedGraphFraction, mobility, propagation, scenarioHash`

`run_pilot.py` écrit en outre `campaign_plan<tag>.json`, qui conserve ce qui a été
*demandé* — sans lui, une exécution manquante serait indiscernable d'une exécution jamais
lancée.

## 17. Critères PASS/FAIL

| # | Critère | Vérification | État dans cet environnement |
|---|---|---|---|
| P-1 | Le module et le pilote compilent sur ns-3.48 sans erreur | `./ns3 build` | **PASS** |
| P-2 | Les 13 tests de niveau 1 passent | `./test.py -s mtcaodv-metrics` | **PASS** (1/1 suite) |
| P-3 | La suite `mtcaodv-attack` passe toujours | `./test.py -s mtcaodv-attack` | **PASS** |
| P-4 | Le pilote produit un CSV et un manifest complets | exécution | **PASS** |
| P-5 | `validate_step0.py` déclare l'exécution VALID | validation | **PASS** |
| P-6 | Reproductibilité : même seed, fichiers identiques bit à bit (T-32) | double exécution + `diff` | **PASS** |
| P-7 | Le fork reproduit AODV standard à l'identique | 10 seeds appariés | **PASS**, 0 divergence |
| P-8 | Empreinte de scénario identique entre variantes appariées | `validate_step0.py --paired` | **PASS** |
| P-9 | Une configuration impossible est refusée, jamais corrigée | `--nodes=255`, `--warmup=40 --drain=25` | **PASS** |
| P-10 | Un ratio d'attaquants non nul est refusé à l'étape 0 | `--attackerRatio=0.20` | **PASS**, code 1 |
| P-11 | Une métrique inapplicable sort `NaN`/`null`, jamais 0 | M-02, M-09, M-13 | **PASS** |
| P-12 | Le validateur rejette des données falsifiées | PDR modifié sans les compteurs | **PASS**, rejeté |

**Ce qui n'est pas un critère PASS/FAIL de cette étape :** la valeur du PDR. L'étape 0
mesure et diagnostique la baseline ; elle ne fixe pas de seuil à atteindre.

### Baseline mesurée

Configuration de référence, 10 seeds (1001–1010), profil `default`, ns-3.48 :

| Métrique | moyenne | écart-type | médiane | Q1 | Q3 | min | max |
|---|---:|---:|---:|---:|---:|---:|---:|
| PDR | 0,8168 | 0,1093 | 0,8826 | 0,7656 | 0,8858 | 0,6306 | 0,9194 |
| délai moyen (s) | 0,0300 | 0,0113 | 0,0323 | 0,0229 | 0,0385 | 0,0116 | 0,0458 |
| NRO | 3,148 | 0,528 | 3,056 | 2,795 | 3,400 | 2,417 | 4,134 |
| degré moyen | 7,93 | 1,28 | 7,52 | 7,38 | 8,08 | 6,32 | 10,83 |
| graphe connexe (fraction) | 0,861 | 0,119 | 0,913 | 0,745 | 0,935 | 0,696 | 1,000 |

Diagnostic à facteur unique, mêmes 10 seeds, un seul paramètre modifié à la fois :

| Configuration | PDR moyen | degré | graphe connexe | sauts moyens |
|---|---:|---:|---:|---:|
| **600 × 600 m (référence)** | **0,817** | 7,93 | 0,861 | **2,18** |
| 450 × 450 m | 0,881 | 11,73 | 1,000 | 1,58 |
| 400 × 400 m | 0,924 | 13,63 | 1,000 | 1,40 |
| vitesse 1–5 m/s | 0,808 | 7,63 | 0,776 | 3,07 |
| HELLO désactivés | 0,735 | 7,93 | 0,861 | — |
| HELLO à 3 s | 0,803 | 7,93 | 0,861 | — |

**Lecture.** Trois conclusions, toutes mesurées, aucune supposée.

1. *La perte résiduelle est en grande partie géométrique.* Réduire la zone à 400 × 400 m
   porte la connectivité du graphe à 1,000 et le PDR à 0,924 avec une dispersion divisée
   par deux. Le protocole n'est donc pas en cause au premier ordre.
2. *Mais resserrer la zone détruit l'objet de l'étude.* À 400 × 400 m le nombre de sauts
   moyen tombe à 1,40 : la majorité des flux n'a plus de nœud intermédiaire. Or un
   Blackhole ne peut intercepter que du trafic **en transit**, et l'observation du
   forwarding de l'étape 3 n'a de sens que s'il existe un relais à observer. Choisir
   400 × 400 m améliorerait le PDR de la baseline en supprimant le phénomène à étudier.
   La configuration de référence est donc conservée à 600 × 600 m.
3. *Réduire la mobilité ne résout rien.* À 1–5 m/s le PDR ne s'améliore pas (0,808) et la
   connectivité se dégrade (0,776) : le Random Waypoint lent ne mélange pas la topologie,
   de sorte qu'une partition initiale persiste au lieu d'être résorbée par le mouvement.

**Conséquence pour la suite, à trancher avant l'étape 12.** Avec 20 nœuds, connectivité et
profondeur multi-saut s'opposent directement : à densité fixée, le nombre de sauts varie
comme l'inverse de la racine du degré. Un PDR sain proche de 100 % **et** des chemins à
2–3 sauts ne sont pas simultanément atteignables à cette population. L'objectif « PDR
> 90 % sous attaque » énoncé dans le plan doit donc être lu relativement à une baseline
saine de 0,82, et non dans l'absolu : aucun mécanisme de sécurité ne peut livrer plus de
paquets que le réseau n'en peut acheminer. Les deux voies possibles — augmenter \(N\)
(la spécification fixe \(N = 100\) pour l'étude confirmatoire, §16.1) ou assumer une
baseline à 0,82 pour le pilote — relèvent d'une décision à prendre explicitement, pas
d'un ajustement silencieux de paramètre.

## 18. Limitations de cette étape

1. **Un seul régime physique mesuré.** Les chiffres ci-dessus valent pour la
   configuration de référence et 10 seeds. Ce n'est ni une étude de sensibilité complète,
   ni un résultat confirmatoire. La spécification exige au moins 30 paires par condition
   (A7) pour toute inférence.
2. **Paramètres physiques non gelés.** Zone, propagation, puissance, débits, charge et
   warm-up restent au statut **C-28**. Ils devront être figés avant la phase
   confirmatoire, puis utilisés sans optimisation post hoc.
3. **RUD (Éq. 29) et énergie (Éq. 30) non instrumentées.** Exportées `NaN`, jamais 0.
4. **Le diagnostic topologique est géométrique**, fondé sur un rayon de connectivité fixé
   (215 m par défaut) et non sur la réception radio réelle. Il indique si un chemin
   *pouvait* exister ; il ne mesure pas la qualité du lien.
5. **Aucune analyse statistique appariée** (A7.3 : bootstrap, Wilcoxon, correction de
   Holm). L'agrégateur produit moyennes, médianes et quartiles, rien de plus.
6. **`RandomWaypointMobilityModel` retenu par défaut**, alors que le §16.1 propose le
   régime stationnaire. Voir D-I10 dans `docs/DIVERGENCES.md` ; l'option `--mobility=ssrwp`
   reste disponible.
7. **Deux classes de configuration coexistent** (`PilotConfiguration` et
   `ExperimentConfiguration`). Voir D-I11.

## 19. Éléments volontairement non implémentés

- **Attaque Blackhole dans le protocole** (A2.3, A2.4) : étape 1. Les options
  `--attackerRatio` et `--attackStart` sont exposées, le tirage reproductible des
  attaquants s'exécute et est journalisé, mais aucun comportement n'est attaché et un
  ratio non nul est **refusé** avec un code de retour non nul.
- **Filtrage RREP, OCEA, MOBeta-Trust, machine à états, témoins, certificats, PTMB,
  réconciliation, admission sécurisée** : étapes 2 à 10. Aucune amorce, aucun fichier
  fantôme.
- **Métriques de sécurité et de registre** (Éq. 31–39) : étape 11. Aucune colonne
  correspondante n'est écrite — une colonne à zéro serait une donnée fausse.
- **Analyse statistique appariée** (Éq. 40–42) : étape 13.

## 20. Prochaine étape prévue

**Étape 1 — Blackholes multiples.** Câbler `BlackholeBehavior` au fork
`ns3::mtcaodv::RoutingProtocol` :

- A2.3 dans `RecvRequest()` / `SendForgedReply()` : RREP forgé, Éq. (23) avec
  wrap-around sur \(2^{32}\) ;
- A2.4 dans `Forwarding()` : abandon silencieux des données en transit, plan de contrôle
  préservé ;
- installation d'une instance **distincte** par attaquant (invariant 20.4.3) ;
- levée du garde-fou `--attackerRatio > 0` et ajout des colonnes `forgedRrepCount` et
  `blackholeDropCount` **après** les colonnes de l'étape 0 ;
- critère de causalité : à seeds appariés, le PDR sous attaque doit être strictement
  inférieur au PDR de la baseline mesurée ici, et `blackholeDropCount` strictement positif.

La vérité terrain des attaquants restera confinée au générateur de scénario, à
`AttackManager`, à l'installation des comportements et à l'évaluation hors ligne
(invariant 20.2.8).
