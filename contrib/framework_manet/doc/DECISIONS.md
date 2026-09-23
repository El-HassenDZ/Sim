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
- **[MESURÉ]** : observé en simulation par un programme du module, avec la
  commande et la sortie de référence citées (`doc/step1a-reference/`) ;
- **[AVIS]** : recommandation d'ingénierie.

Sources consultées : archive GitLab du tag `ns-3.48`
(`gitlab.com/nsnam/ns-3-dev`, fichier `VERSION` = `3.48`).

---

## A. Constats vérifiés dans ns-3.48

| ID | Constat | Source | Nature |
|----|---------|--------|--------|
| F-01 | `WifiPhyHelper` installe par défaut `ns3::ThresholdPreambleDetectionModel`, avec `MinimumRssi = -82 dBm` et `Threshold = 4 dB`. Une trame reçue sous -82 dBm n'est pas détectée, quel que soit son débit. Le modèle s'applique aussi au DSSS (`PhyEntity`, sans surcharge dans `DsssPhy`). | `src/wifi/helper/wifi-helper.cc:179`, `src/wifi/model/threshold-preamble-detection-model.cc`, `src/wifi/model/phy-entity.cc:1087` | [FAIT] ; [MESURÉ] STEP 1a : taux de réception de 1 à 85 m, 0 dès 90 m, en broadcast comme en unicast |
| F-02 | Défauts PHY : `TxPowerStart = TxPowerEnd = 16.0206 dBm`, `RxSensitivity = -101 dBm`, `CcaEdThreshold = -62 dBm`, `RxNoiseFigure = 7 dB`, `TxGain = RxGain = 0 dB`. | `src/wifi/model/wifi-phy.cc` | [FAIT] |
| F-03 | `LogDistancePropagationLossModel` : `ReferenceLoss` vaut 46.6777 dB par défaut (Friis à 1 m, 5,15 GHz), et non 40.05 dB. | `src/propagation/model/propagation-loss-model.cc` | [FAIT] |
| F-04 | `RangePropagationLossModel` renvoie la puissance émise telle quelle (16 dBm) si d ≤ MaxRange, et -1000 dBm au-delà. Il décrit un disque idéal : aucune perte et aucune erreur à l'intérieur, aucune interférence à l'extérieur. | `propagation-loss-model.cc:907-920` | [FAIT] |
| F-05 | Le modèle d'erreur DSSS/CCK à 5,5 et 11 Mbit/s dépend de `HAVE_GSL` au moment du configure : intégration numérique exacte avec GSL, approximation « Matlab » sans GSL. Deux machines qui diffèrent sur ce point ne sont pas numériquement équivalentes. | `src/wifi/model/non-ht/dsss-error-rate-model.cc`, `build-support/macros-and-definitions.cmake:896-903` | [FAIT] |
| F-06 | Si `NonUnicastMode` n'est pas fixé, les trames broadcast utilisent le premier mode de base (sinon le mode par défaut du PHY). En 802.11b, tous les modes DSSS sont créés « mandatory » ; le premier est `DsssRate1Mbps`. | `wifi-remote-station-manager.cc:2094-2106`, `dsss-phy.cc` | [FAIT] ; [MESURÉ] STEP 1a : broadcasts émis à `DsssRate1Mbps` (test de régression (f)) |
| F-07 | `MobilityHelper::AssignStreams` ne couvre pas l'allocateur des positions initiales, car ses variables aléatoires sont consommées dès `Install()`. `RandomWaypointMobilityModel::DoAssignStreams` utilise 2 streams (vitesse, pause) plus ceux de son allocateur de waypoints. | `mobility-helper.h:195-212`, `random-waypoint-mobility-model.cc:121-129` | [FAIT] |
| F-08 | Énergie : `BasicEnergySourceInitialEnergyJ = 10 J`, tension 3,0 V ; `WifiRadioEnergyModel` : Idle et CcaBusy 0,273 A, Tx 0,380 A, Rx 0,313 A, Sleep 0,033 A. À l'épuisement, `WifiRadioEnergyModelHelper` appelle par défaut `WifiPhy::SetOffMode` : la radio s'éteint. | `basic-energy-source.cc`, `wifi-radio-energy-model.cc`, `wifi-radio-energy-model-helper.cc:69-75` | [FAIT] |
| F-09 | Les classes énergie sont dans `ns3::energy` (`ns3::energy::BasicEnergySource`). `ns3::BasicEnergySource` n'est plus qu'un alias déprécié. | `basic-energy-source.cc:29-30` | [FAIT] |
| F-10 | AODV ne déclare **aucune** TraceSource. L'overhead de routage doit donc être mesuré hors d'AODV : à la couche IP, sur les paquets UDP du port 654 (`RoutingProtocol::AODV_PORT`). | `src/aodv/model/aodv-routing-protocol.cc`, `.h:55` | [FAIT] |
| F-11 | Avec `EnableSeqTsSizeHeader = true`, `OnOffApplication` crée des paquets de `PacketSize - taille(en-tête)` octets avant d'ajouter l'en-tête : la charge utile UDP reste de 512 octets. | `onoff-application.cc:253-270` | [FAIT] |
| F-12 | ns-3.48 : `Remote` et `Tx` sont déclarés dans `ns3::SourceApplication`, `Local` et `Rx` dans `ns3::SinkApplication`, qui sont les parents de `OnOffApplication` et `PacketSink`. | `source-application.cc`, `sink-application.cc` | [FAIT] |
| F-13 | Les ACK partent au même débit que les données : `DsssRate11Mbps`. ns-3 choisit le plus haut mode de base ≤ débit des données, et tous les modes DSSS sont « de base » (F-06). En 802.11b réel, les ACK partent souvent à 1 ou 2 Mbit/s. | calibration, colonne `observedAckMode` | [MESURÉ] |
| F-14 | Bruit effectif au récepteur : −93,966 dBm. Cette valeur correspond exactement à k·290 K·**20 MHz** + 7 dB : ns-3 intègre le bruit thermique sur 20 MHz, et non sur les 22 MHz du DSSS (écart 0,4 dB avec l'estimation initiale). | `interference-helper.cc:408-412` ; colonne `meanRxNoise_dBm` | [MESURÉ] + [CALCUL] |
| F-15 | Une trame unicast est émise au plus 7 fois (`attemptsPerOffered = 7` quand rien n'est reçu), soit 6 retransmissions. | calibration, colonne `attemptsPerOffered` | [MESURÉ] |
| F-16 | `RxNoiseFigure` est en écriture seule dans ns-3.48 : le lire provoque `NS_FATAL`. La relecture de la configuration effective le signale `<non lisible>`. | `wifi-phy.cc:188` ; bug trouvé et corrigé à STEP 1a | [FAIT] |

### Conséquence sur le scénario de référence — mesurée à STEP 1a

Bilan de liaison du profil principal, sans évanouissement :

    Prx(d) = 16 − 40.05 − 30·log10(d)   [dBm, d en m, d0 = 1 m]
    bruit  = −93,97 dBm (F-14)

Calibration statique à deux nœuds [MESURÉ] : 1000 trames par point, pas de
5 m, seed 12345, run 1 ; broadcast de 52 octets IP (taille d'un RREQ),
unicast de 540 octets IP (données). « Fiable » = taux de réception ≥ 0,9 à
toutes les distances inférieures ; « mort » = taux ≤ 0,1. Degré moyen
[CALCUL] : 20 nœuds uniformes dans 500 m × 500 m, avec correction des effets
de bord.

| configuration | broadcast fiable / mort | unicast fiable / mort | degré moyen (fiable) broadcast / unicast |
|---|---|---|---|
| A — défauts ns-3 (détecteur −82 dBm, NonUnicastMode non fixé) | 85 / 90 m (1 Mbit/s) | 85 / 90 m | 1,5 / 1,5 |
| A' — détecteur actif, NonUnicastMode = 11 Mbit/s | 85 / 90 m | 85 / 90 m | 1,5 / 1,5 |
| B — détecteur désactivé, NonUnicastMode non fixé | 295 / 350 m (1 Mbit/s) | 105 / 120 m | 11,5 / 2,2 |
| B' — détecteur désactivé, NonUnicastMode = 11 Mbit/s | 105 / 130 m | 105 / 120 m | 2,2 / 2,2 |
| range 150 m (contrôle) | 150 / 155 m | 150 / 155 m | 4,1 / 4,1 |
| range 250 m (contrôle) | 250 / 255 m | 250 / 255 m | 9,2 / 9,2 |

Lecture :

1. Avec les valeurs radio de la spécification, **aucune option de D-05 ou
   D-06 ne porte les données au-delà d'environ 105 m** [MESURÉ]. Le degré
   moyen des liens de données reste donc ≤ 2,2. Pour des disques de rayon r
   dans le plan, la percolation (apparition d'une composante géante)
   n'intervient qu'autour d'un degré moyen de 4,5 [FAIT, résultat classique
   de percolation continue]. Le réseau de référence sera majoritairement
   partitionné, quelles que soient D-05 et D-06 [ESTIMATION, confiance 0,85 ;
   à confirmer par la connectivité mesurée à STEP 1b].
2. L'option B sans NonUnicastMode crée une zone grise d'un facteur 2,8 en
   portée : RREQ et HELLO à 295 m, données à 105 m [MESURÉ].
3. Les deux profils de la spécification décrivent des **réseaux
   différents** : le contrôle `range` à 250 m (degré ≈ 9) n'est pas un
   contrôle positif du régime `logdistance` (degré ≈ 1,5 à 2,2), mais un
   autre scénario. Le contrôle à 150 m, avec un degré ≈ 4,1 proche du seuil
   de percolation, reste le plus proche d'un réseau multi-sauts.

Voir D-05 et D-19.

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
[MESURÉ] Les deux disques sont nets : 150 → 155 m et 250 → 255 m
(`doc/step1a-reference/`). Voir aussi D-19, point 3.
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
pour l'OFDM : −82 dBm est la sensibilité exigée pour le débit minimal en
802.11a/g. Ce seuil limite toutes les liaisons à 85 m [MESURÉ].
Options, toutes paramétrées dans `RadioConfig` / la CLI :

- **A** — défauts ns-3.48 (`--preambleDetection=true --pdMinRssi=-82`).
  Reproductible, sans intervention ; portée 85 m, degré ≈ 1,5.
- **B** — détecteur désactivé (`--preambleDetection=false`). Détection fixée
  par `RxSensitivity` (−101 dBm) et par les modèles d'erreur ; données
  portées à 105 m, degré ≈ 2,2 ; zone grise si D-06 n'est pas fixé.
- **C** — détecteur conservé avec un `--pdMinRssi` tiré d'une fiche technique
  de récepteur 802.11b. Le paramètre devient physique et documenté.

Règle : décider **avant** STEP 1b, sur la seule base de la calibration de
STEP 1a, et consigner la décision ici. Choisir la valeur qui maximise le PDR
serait exactement l'ajustement que la spécification interdit.
[AVIS] B' (B + NonUnicastMode 11 Mbit/s) est l'option la plus propre du
point de vue radio : pas de seuil OFDM appliqué à du DSSS, pas de zone grise.
Mais la mesure montre que D-05 seul ne change pas l'ordre de grandeur de la
connectivité (1,5 → 2,2). La vraie question est D-19.

### D-06 — Débit des trames non-unicast (`NonUnicastMode`) — `À CONFIRMER`
[MESURÉ] Non fixé, les RREQ, HELLO et RERR partent à 1 Mbit/s, et les
données, RREP et ACK à 11 Mbit/s (F-06, F-13). Sous l'option D-05 A, cela
n'a aucun effet (A et A' donnent la même portée de 85 m). Sous B, les
broadcasts portent 2,8 fois plus loin que les données : AODV découvre des
routes par des liens incapables de porter des données unicast (Lundgren et
al., 2002, « communication gray zones »).
[AVIS] Fixer `NonUnicastMode = DsssRate11Mbps`. C'est la lecture la plus
littérale de « débit PHY DsssRate11Mbps », et cela supprime un facteur
confondant qui agit précisément sur le chemin RREQ/RREP exploité par un
blackhole. Asymétrie résiduelle mesurée sous B' : un RREQ de 52 octets reste
reçu à 41 % à 120 m, contre 2,5 % pour une donnée de 540 octets, un effet de
taille de trame qui subsiste à débit égal.

### D-19 — Régime de connectivité visé par la baseline — `À CONFIRMER` (bloquant pour STEP 1b)
Constat [MESURÉ + CALCUL] : avec 16 dBm, n = 3, 40,05 dB et des données à
11 Mbit/s, la portée des données plafonne à environ 105 m. Avec 20 nœuds
dans 500 m × 500 m, le réseau reste sous le seuil de percolation, quelle que
soit la décision D-05. Ce n'est pas un bug : c'est une propriété des
paramètres de la spécification. C'est aussi une incohérence interne de
celle-ci, puisque son contrôle `range` à 250 m suppose un réseau connexe.

Deux voies légitimes, à trancher **par un argument de régime et non par le
PDR** :

1. **Conserver la spécification.** La baseline étudie un MANET clairsemé et
   partitionné. Scientifiquement défendable, mais l'effet d'une attaque sur
   les routes y sera faible et noyé dans les pertes topologiques. Il faudra
   rapporter la connectivité mesurée à côté de chaque PDR.
2. **Fixer d'abord un régime cible**, par exemple « réseau connexe la plupart
   du temps, degré moyen de 6 à 10, routes de 2 à 4 sauts », puis modifier
   **un seul** paramètre, justifié physiquement, pour l'atteindre :
   `areaSize`, `numNodes`, `txPower` (20 dBm = 100 mW, plafond courant en
   2,4 GHz), `pathLossExponent` (2,7 à 3,5 selon l'environnement) ou débit
   de données. La calibration de STEP 1a permet de vérifier le régime obtenu
   avant toute simulation MANET. Le scénario de la spécification peut rester
   une variante « clairsemée » documentée.

[AVIS] Voie 2, en conservant la variante clairsemée. Une baseline destinée à
évaluer des attaques de routage doit placer la plupart des flux sur des
routes multi-sauts existantes. La décision appartient à l'utilisateur et
doit être consignée ici avant STEP 1b.

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
