# framework_manet — Registre d'audit et de décisions

Ce registre trace les constats techniques, les ambiguïtés de la spécification
et les décisions d'ingénierie de la baseline AODV. Chaque entrée a un
identifiant stable (F-xx pour un constat, D-xx pour une décision) que le code
et les étapes suivantes citent.

Statuts utilisés, conformes à la spécification du projet : `TESTÉ`,
`IMPLÉMENTÉ — À VÉRIFIER`, `PROPOSÉ`, `À CONFIRMER`, `BLOQUÉ`.

Nature des affirmations :

- **[FAIT]** : vérifié dans les sources officielles de ns-3.48 (fichier cité) ;
- **[CALCUL]** : arithmétique exacte à partir de faits vérifiés ;
- **[ESTIMATION]** : inférence qui repose sur des hypothèses explicites, à
  confirmer par une mesure ;
- **[AVIS]** : recommandation d'ingénierie.

Sources consultées : archive GitLab du tag `ns-3.48`
(`gitlab.com/nsnam/ns-3-dev`, fichier `VERSION` = `3.48`).

---

## A. Constats vérifiés dans ns-3.48

| ID | Constat | Source | Nature |
|----|---------|--------|--------|
| F-01 | `WifiPhyHelper` installe par défaut `ns3::ThresholdPreambleDetectionModel`, avec `MinimumRssi = -82 dBm` et `Threshold = 4 dB`. Une trame reçue sous -82 dBm n'est pas détectée, quel que soit son débit. Le modèle s'applique aussi au DSSS (`PhyEntity`, sans surcharge dans `DsssPhy`). | `src/wifi/helper/wifi-helper.cc:179`, `src/wifi/model/threshold-preamble-detection-model.cc`, `src/wifi/model/phy-entity.cc:1087` | [FAIT] |
| F-02 | Défauts PHY : `TxPowerStart = TxPowerEnd = 16.0206 dBm`, `RxSensitivity = -101 dBm`, `CcaEdThreshold = -62 dBm`, `RxNoiseFigure = 7 dB`, `TxGain = RxGain = 0 dB`. | `src/wifi/model/wifi-phy.cc` | [FAIT] |
| F-03 | `LogDistancePropagationLossModel` : `ReferenceLoss` vaut 46.6777 dB par défaut (Friis à 1 m, 5,15 GHz), et non 40.05 dB. | `src/propagation/model/propagation-loss-model.cc` | [FAIT] |
| F-04 | `RangePropagationLossModel` renvoie la puissance émise telle quelle (16 dBm) si d ≤ MaxRange, et -1000 dBm au-delà. Il décrit un disque idéal : aucune perte et aucune erreur à l'intérieur, aucune interférence à l'extérieur. | `propagation-loss-model.cc:907-920` | [FAIT] |
| F-05 | Le modèle d'erreur DSSS/CCK à 5,5 et 11 Mbit/s dépend de `HAVE_GSL` au moment du configure : intégration numérique exacte avec GSL, approximation « Matlab » sans GSL. Deux machines qui diffèrent sur ce point ne sont pas numériquement équivalentes. | `src/wifi/model/non-ht/dsss-error-rate-model.cc`, `build-support/macros-and-definitions.cmake:896-903` | [FAIT] |
| F-06 | Si `NonUnicastMode` n'est pas fixé, les trames broadcast utilisent le premier mode de base (sinon le mode par défaut du PHY). En 802.11b, tous les modes DSSS sont créés « mandatory » ; le premier est `DsssRate1Mbps`. | `wifi-remote-station-manager.cc:2094-2106`, `dsss-phy.cc` | [FAIT] ; débit effectif des RREQ/HELLO : [ESTIMATION], à mesurer à STEP 1 |
| F-07 | `MobilityHelper::AssignStreams` ne couvre pas l'allocateur des positions initiales, car ses variables aléatoires sont consommées dès `Install()`. `RandomWaypointMobilityModel::DoAssignStreams` utilise 2 streams (vitesse, pause) plus ceux de son allocateur de waypoints. | `mobility-helper.h:195-212`, `random-waypoint-mobility-model.cc:121-129` | [FAIT] |
| F-08 | Énergie : `BasicEnergySourceInitialEnergyJ = 10 J`, tension 3,0 V ; `WifiRadioEnergyModel` : Idle et CcaBusy 0,273 A, Tx 0,380 A, Rx 0,313 A, Sleep 0,033 A. À l'épuisement, `WifiRadioEnergyModelHelper` appelle par défaut `WifiPhy::SetOffMode` : la radio s'éteint. | `basic-energy-source.cc`, `wifi-radio-energy-model.cc`, `wifi-radio-energy-model-helper.cc:69-75` | [FAIT] |
| F-09 | Les classes énergie sont dans `ns3::energy` (`ns3::energy::BasicEnergySource`). `ns3::BasicEnergySource` n'est plus qu'un alias déprécié. | `basic-energy-source.cc:29-30` | [FAIT] |
| F-10 | AODV ne déclare **aucune** TraceSource. L'overhead de routage doit donc être mesuré hors d'AODV : à la couche IP, sur les paquets UDP du port 654 (`RoutingProtocol::AODV_PORT`). | `src/aodv/model/aodv-routing-protocol.cc`, `.h:55` | [FAIT] |
| F-11 | Avec `EnableSeqTsSizeHeader = true`, `OnOffApplication` crée des paquets de `PacketSize - taille(en-tête)` octets avant d'ajouter l'en-tête : la charge utile UDP reste de 512 octets. | `onoff-application.cc:253-270` | [FAIT] |
| F-12 | ns-3.48 : `Remote` et `Tx` sont déclarés dans `ns3::SourceApplication`, `Local` et `Rx` dans `ns3::SinkApplication`, qui sont les parents de `OnOffApplication` et `PacketSink`. | `source-application.cc`, `sink-application.cc` | [FAIT] |

### Conséquence de F-01 à F-03 sur le scénario de référence

Bilan de liaison du profil principal, sans évanouissement :

    RSSI(d) = 16 − 40.05 − 30·log10(d)   [dBm, d en m, d0 = 1 m]

- Seuil de détection du préambule de -82 dBm : **d_max ≈ 85,4 m** [CALCUL].
- Si ce détecteur était désactivé, `RxSensitivity = -101 dBm` donnerait
  d_max ≈ 367 m [CALCUL]. La portée utile serait alors fixée par le modèle
  d'erreur : environ 100 m à 11 Mbit/s et environ 290 m à 1 Mbit/s
  [ESTIMATION, sans GSL, trames de 512 octets et de contrôle].
- Degré moyen attendu pour 20 nœuds placés uniformément dans 500 m × 500 m
  (probabilité de lien corrigée des effets de bord) [CALCUL, hypothèse de
  distribution uniforme] :

| portée r | degré moyen |
|---------:|------------:|
| 85 m     | ≈ 1,5 |
| 100 m    | ≈ 2,0 |
| 150 m    | ≈ 4,1 |
| 250 m    | ≈ 9,2 |

Avec un degré moyen de 1,5, un graphe géométrique aléatoire est très loin
de la connectivité. La plupart des couples source/destination seront
déconnectés la majeure partie du temps [ESTIMATION]. Le PDR de la baseline
refléterait alors surtout la topologie, et non le comportement d'AODV.
La régime stationnaire du RandomWaypoint concentre les nœuds au centre, ce
qui augmente un peu ce degré, sans changer l'ordre de grandeur [ESTIMATION].
Voir D-05.

---

## B. Décisions et points ouverts

### D-01 — Emplacement de `experiments/` — `À CONFIRMER`
La spécification montre `contrib/framework_manet/` et `experiments/` comme
deux arborescences, sans préciser le parent commun.
[AVIS] Placer `experiments/` dans `contrib/framework_manet/experiments/`.
Un seul dépôt versionné couvre ainsi le simulateur et l'analyse, et un
commit identifie les deux. CMake ignore ce dossier, qui ne contient pas de
`CMakeLists.txt`. Ce choix n'est pas nécessaire avant STEP 2, où apparaît le
premier script.

### D-02 — `maxRange` : 250 m (tableau) ou 150 m (contrôle positif) — `À CONFIRMER`
Le tableau des paramètres donne 250 m par défaut ; la section propagation
fixe le contrôle positif à 150 m. Proposition : conserver 250 m comme valeur
par défaut de la CLI (tableau), et lancer le contrôle positif avec
`--propagationModel=range --maxRange=150`, passé explicitement.
[AVIS] 150 m est le contrôle le plus informatif. Il donne un degré moyen
d'environ 4 : réseau multi-sauts et le plus souvent connexe. À 250 m (degré
d'environ 9), la plupart des routes font 1 ou 2 sauts.

### D-03 — `outputDir` par défaut `resulls_framwork` — `À CONFIRMER`
Il s'agit probablement d'une faute de frappe pour `results_framework`.
Conformément aux règles de priorité, la valeur littérale est conservée tant
qu'elle n'est pas confirmée.

### D-04 — Nom canonique `seed` — adopté
`seed` désigne la graine ns-3 (`RngSeedManager::SetSeed`, doit être ≥ 1) ;
`minSpeed` et `maxSpeed` désignent les vitesses. Le paramètre ambigu `speed`
n'est pas utilisé.

### D-05 — Seuil de détection du préambule (F-01) — `À CONFIRMER` (bloquant pour STEP 1b)
Sans décision explicite, le scénario de référence hérite d'un seuil conçu
pour l'OFDM : -82 dBm est la sensibilité CCA exigée pour le débit minimal
en 802.11a/g. Ce seuil limite toutes les liaisons à environ 85 m.
Options :

- **A** — conserver les défauts ns-3.48. Le choix est reproductible et sans
  intervention, mais le réseau est fortement partitionné (degré ≈ 1,5).
- **B** — `DisablePreambleDetectionModel()`. La détection est alors fixée par
  `RxSensitivity` (-101 dBm) et par les modèles d'erreur. La « zone grise »
  de D-06 réapparaît : broadcasts à 1 Mbit/s portant jusqu'à environ 290 m,
  données à 11 Mbit/s jusqu'à environ 100 m [ESTIMATION].
- **C** — conserver le détecteur avec un `MinimumRssi` justifié pour le DSSS
  (valeur tirée d'une fiche technique de récepteur 802.11b). Le paramètre
  devient alors physique et documenté.

Règle proposée : décider **avant** de connaître le PDR, à partir de la seule
mesure de calibration radio de STEP 1a, et consigner la décision ici.
Choisir la valeur qui maximise le PDR serait exactement l'ajustement que la
spécification interdit.
[AVIS] L'option A ne ment pas, mais elle rend la baseline peu informative
pour évaluer une attaque : l'effet d'un blackhole est borné par la fraction
de trafic routable.

### D-06 — Débit des trames non-unicast (`NonUnicastMode`) — `À CONFIRMER`
D'après F-06, les RREQ, HELLO et RERR partiraient à 1 Mbit/s, alors que les
données et les RREP partent à 11 Mbit/s. Sous l'option D-05 A, ce point est
masqué, puisque toutes les liaisons sont limitées à environ 85 m. Sous B ou
C, AODV apprend des voisins et des routes par des liens incapables de
porter des données unicast (Lundgren et al., 2002, « communication gray
zones »).
[AVIS] Fixer explicitement `NonUnicastMode = DsssRate11Mbps`, en plus de
`DataMode` et `ControlMode`. C'est la lecture la plus littérale de « débit
PHY DsssRate11Mbps ». Surtout, cela supprime un facteur confondant qui agit
précisément sur le chemin RREQ/RREP exploité par un blackhole. Le débit
effectif sera vérifié à STEP 1a avec la TraceSource `WifiPhy::MonitorSnifferTx`.

### D-07 — Plan des streams RNG — `PROPOSÉ` (STEP 1)
Des blocs fixes permettent qu'une variation du nombre de streams d'un
composant ne décale pas les autres. Tous les mécanismes aléatoires de la
baseline sont affectés explicitement, y compris le backoff MAC, le jitter
AODV et l'ARP. Sinon, leur stream est attribué automatiquement dans l'ordre
de création des objets, qu'une future extension de sécurité pourrait
modifier, ce qui casserait l'appariement des expériences.

| bloc | usage |
|------|-------|
| 0 | sélection des couples de flux |
| 1 | start jitter des sources |
| 2–3 | allocateur des positions initiales (X, Y), affecté **avant** `Install()` (F-07) |
| 10 000–19 999 | RandomWaypoint : un allocateur de waypoints **par nœud**, pour que la trajectoire d'un nœud ne dépende que de ses propres streams |
| 20 000–29 999 | Wi-Fi (`WifiHelper::AssignStreams`) |
| 30 000–39 999 | pile Internet (`InternetStackHelper::AssignStreams`) |
| 40 000–49 999 | AODV (`AodvHelper::AssignStreams`) |
| ≥ 100 000 | réservé aux extensions futures (attaquants, confiance, …) |

À l'exécution, le nombre de streams retourné par chaque helper sera comparé
à la taille de son bloc ; un débordement sera une erreur fatale.

### D-08 — Sélection des flux — `PROPOSÉ` (STEP 1)
La sélection se fait avant la simulation, sur le stream 0, par un mélange
de Fisher–Yates des identifiants de nœuds (`UniformRandomVariable::GetInteger`).
Si `2·flowCount ≤ numNodes`, les sources sont les `flowCount` premiers
éléments et les destinations les `flowCount` suivants. On obtient ainsi des
ensembles disjoints, des couples uniques et s ≠ d. Sinon, repli sur des
couples ordonnés uniques avec s ≠ d (exige `flowCount ≤ N(N−1)`), signalé
dans la sortie. Le résultat ne dépend que de (seed, run, numNodes,
flowCount).

### D-09 — Énergie initiale — `PROPOSÉ` (STEP 3)
Le défaut ns-3 de 10 J s'épuise en environ 12 s rien qu'en IDLE
(0,273 A × 3 V = 0,819 W) ; la radio s'éteint ensuite (F-08). Ce serait un
artefact majeur.
Proposition : paramètre `initialEnergy` en joules, 1000 J par défaut, **non
calibré**. Cette valeur n'a d'autre rôle que de rendre l'épuisement
impossible pendant `simTime`. Borne de sécurité : la puissance maximale est
0,380 A × 3 V = 1,14 W. Une alerte est émise si
`initialEnergy < 1,14 W × simTime`.
Conséquence à assumer : sans épuisement, l'énergie est une pure comptabilité
dominée par l'écoute (au moins 0,819 W × simTime par nœud). Les différences
entre variantes seront relativement faibles. Il faudra donc rapporter la
décomposition par état, et pas seulement la moyenne.

### D-10 — `T_observation` du throughput — `PROPOSÉ` (STEP 2)
`T_observation = trafficStop − trafficStart`, soit la fenêtre nominale
(90 s par défaut). Elle est identique pour toutes les variantes et
indépendante du comportement du réseau. La fenêtre [premier Tx, dernier Rx]
est rejetée : son dénominateur dépendrait du protocole évalué. Les paquets
émis avant `trafficStop` et reçus pendant le drain sont comptés, ce qui est
cohérent avec le PDR.

### D-11 — Routing overhead — `PROPOSÉ` (STEP 2)
`routingTxPackets` et `routingTxBytes` comptent chaque émission IP
(`Ipv4L3Protocol::Tx`, hors loopback) d'un datagramme UDP de port 654, sur
tous les nœuds. Les RREQ réémis, les RREP relayés, les RERR et les HELLO
sont inclus. Les retransmissions MAC sont exclues par construction.
`Routing_overhead = routingTxPackets / appTxPackets`, conformément à la
spécification, et `NaN` si `appTxPackets = 0`. La NRL classique
(`routingTxPackets / appRxPackets`) pourra être ajoutée en colonne
supplémentaire.

### D-12 — Agrégation du jitter — `PROPOSÉ` (STEP 2)
Définition cohérente avec FlowMonitor : pour chaque flux,
`J_k = |D_k − D_{k−1}|` sur les paquets consécutifs dans l'ordre d'arrivée.
L'agrégat est une moyenne pondérée par échantillon :
`Jitter = Σ_f Σ_k J_k / Σ_f (rx_f − 1)`, en secondes. Un flux avec moins de
2 réceptions ne fournit aucun échantillon. S'il n'y a aucun échantillon :
`NaN`.

### D-13 — Colonne `Speed` de `traffic_log.csv` — `À CONFIRMER`
La spécification ne dit pas si `Speed` est une valeur instantanée ou
moyenne. Proposition : vitesse moyenne dans le temps, en m/s, calculée sur
les échantillons de mobilité. La vitesse instantanée finale irait dans une
colonne supplémentaire.

### D-14 — Instants d'export de `routing_table.txt` — `PROPOSÉ` (STEP 4)
Deux instants : le milieu de la fenêtre de trafic, et `trafficStop − 1 s`.
Le second est un snapshot tardif, pris pendant que les routes actives sont
encore rafraîchies par le trafic.

### D-15 — Non-stationnarité du RandomWaypoint — limite documentée
La spécification impose des positions initiales uniformes. Le régime
stationnaire du RWP est pourtant concentré au centre, et sa vitesse moyenne
en régime vaut (vmax − vmin)/ln(vmax/vmin) ≈ 3,9 m/s, contre 5,5 m/s au
départ (Yoon, Liu, Noble, INFOCOM 2003). Avec `trafficStart = 5 s`, les
mesures portent sur le régime transitoire. Le modèle n'est pas remplacé
(la spécification est explicite) ; le point devra figurer dans les limites
de toute publication.

### D-16 — Profil de compilation et GSL — `PROPOSÉ`
Les deux sont consignés à chaque exécution : aujourd'hui par
`framework-manet-env-check`, et plus tard par des colonnes supplémentaires
de `run_metrics.csv`. Une campagne n'utilise qu'un seul profil et un seul
état GSL (F-05). Les résultats `default` (-Os) et `optimized` (-O3,
éventuellement `-march=native`) peuvent différer numériquement.

### D-17 — Valeurs radio fixées explicitement — `PROPOSÉ` (STEP 1)
`TxPowerStart = TxPowerEnd = 16.0 dBm` et `TxPowerLevels = 1` : le défaut
ns-3 est de 16,0206 dBm (F-02). `ReferenceLoss = 40.05 dB` et
`ReferenceDistance = 1 m` : le défaut est de 46,6777 dB (F-03).
40,05 dB correspond à la perte de Friis à 1 m pour 2,4 GHz,
20·log10(4π/λ) avec λ = 0,125 m, et reste donc cohérent avec le 802.11b.

### D-18 — Niveau de mesure — `PROPOSÉ` (STEP 2)
Les métriques primaires sont applicatives : `OnOffApplication` et
`PacketSink` avec `SeqTsSizeHeader` (F-11) donnent Tx, Rx, délai et jitter
par flux. FlowMonitor ne sert qu'à une validation croisée, filtrée sur le
port des données. Il observe la couche IP et suit aussi les flux AODV
unicast (RREP) : les deux sources ne doivent pas être mélangées.
