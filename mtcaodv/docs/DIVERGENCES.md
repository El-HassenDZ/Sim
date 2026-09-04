# Divergences entre la spécification MTC-AODV v1.0 et l'implémentation

Ce fichier est un livrable de premier rang, pas de la documentation d'accompagnement.
Toute divergence entre le document *MTCAODV Mathematical & Algorithms Specification v1.0*
(2026-09-03) et le code de ce dépôt doit y figurer avec quatre informations :

1. **Prévu** — ce que la spécification demande, avec sa référence exacte.
2. **Implémenté** — ce que le code fait réellement.
3. **Pourquoi** — la contrainte technique ou l'incohérence qui a forcé l'écart.
4. **Impact** — l'effet possible sur l'évaluation et sur la revendication scientifique.

Une divergence non consignée ici est un défaut, pas une décision.

---

## Statut de départ : requalification du §18.1

**Prévu.** Le §18.1 de la spécification déclare `validate_gate1.py` **PASS**,
`mtcaodv-attack-manager` **PASS**, `mtcaodv-blackhole-behavior` **PASS**, et le code
Gate 1B « implémenté — à vérifier ».

**Implémenté.** Aucun de ces éléments n'existait dans le dépôt `El-HassenDZ/Sim`
(commit initial `dee0497` : template Node.js sans rapport). L'auteur a confirmé que ce
code n'existe pas non plus hors du dépôt.

**Pourquoi.** On ne peut pas hériter d'un statut de test pour du code absent.

**Impact.** Tous les statuts « Testé » du §18.1 sont requalifiés en **non vérifiés**.
A2.1 à A2.4 sont réimplémentés depuis la spécification et re-testés dans ce dépôt.
Aucune assertion de validation antérieure ne doit être reprise dans une rédaction
scientifique tant qu'elle n'est pas reproduite ici.

---

## D-I1 — Échelle robuste des anomalies RREP (Éq. 3, 4a, 4b)

**Prévu.** \(z = \dfrac{\max(0,\cdot)}{\operatorname{MAD}+\varepsilon}\), avec \(\varepsilon\)
décrit comme « stabilisateur numérique strictement positif » (C-06).

**Implémenté.** \(z = \dfrac{\max(0,\cdot)}{\max(1{,}4826\cdot\operatorname{MAD},\;s_{min})}\),
avec un plancher d'échelle exprimé dans l'unité de la grandeur :
`sequenceScaleFloor = 4` incréments, `hopScaleFloor = 1` saut,
`timingScaleFloor = 10` ms. \(\varepsilon = 10^{-9}\) redevient un garde numérique pur.

**Pourquoi.** Quand tous les RREP comparables portent la même valeur — cas fréquent
sur le nombre de sauts — \(\operatorname{MAD}=0\) et \(z=\Delta/\varepsilon\). Avec un
\(\varepsilon\) petit, le moindre écart d'un incrément produit \(z\to\infty\) et
\(A_{RREP}\to1\) : le détecteur sature sur du bruit et devient inutilisable. La
spécification demande à \(\varepsilon\) d'être simultanément un garde-fou numérique et
une échelle statistique, deux rôles incompatibles. Le facteur 1,4826 rend en outre la
MAD consistante avec l'écart-type gaussien, ce qui donne un sens interprétable aux
poids \(w_s,w_h,w_t\) et au biais \(b_R\) (« déviation robuste en unités sigma »).

**Impact.** Aucun sur la contribution scientifique : l'Éq. (5) et le rôle de
\(A_{RREP}\) (WATCH uniquement, jamais quarantaine — invariant 20.2.1) sont inchangés.
La correction rend le détecteur numériquement défini. Les trois planchers deviennent
des paramètres à calibrer, ajoutés au périmètre de C-03/C-06.

**Validé par l'auteur** le 2026-09-04.

---

## D-I2 — Les caractéristiques OCEA sont strictement locales à l'observateur (Éq. 6–8)

**Prévu.** Le §5.4 cite comme indicateurs de perte bénigne « rupture/mobilité, canal
occupé, pression de file, congestion ou RERR cohérent », et comme caractéristiques
d'opportunité « fiabilité observateur–voisin, soutien à la réception par \(j\),
persistance de contact prévue, visibilité par écoute passive ». Le document ne précise
pas **chez quel nœud** ces grandeurs sont mesurées.

**Implémenté.** Toutes les caractéristiques \(x_k\) et tous les indicateurs \(y_\ell\)
sont calculés **exclusivement à partir d'observables du nœud observateur \(i\)** :

| Rôle | Caractéristique | Source locale à \(i\) | Poids |
|---|---|---|---|
| \(x_1\) | `linkDeliveryReliability` | fraction de trames unicast \(i\to j\) acquittées au MAC | 0,35 |
| \(x_2\) | `receptionEvidence` | ACK 802.11 reçu par \(i\) pour la trame remise à \(j\) | 0,35 |
| \(x_3\) | `contactPersistence` | durée de contact restante estimée par \(i\) | 0,20 |
| \(x_4\) | `promiscuousVisibility` | fraction de la fenêtre où la radio de \(i\) pouvait écouter | 0,10 |
| \(y_1\) | `linkBreakDetected` | échec MAC après retries max, ou disparition de \(j\) | 0,30 |
| \(y_2\) | `localChannelBusy` | fraction de fenêtre en CCA_BUSY/RX à la PHY de \(i\) | 0,25 |
| \(y_3\) | `localQueuePressure` | occupation de la file d'émission **de \(i\)** | 0,20 |
| \(y_4\) | `consistentRouteError` | RERR reçu de \(j\) couvrant la destination | 0,25 |

Toute grandeur non calculable localement est déclarée indisponible
(\(a_k=0\) / \(a_\ell=0\)), ce que le formalisme OCEA absorbe nativement : couverture
réduite ⇒ \(I_e\) réduit ⇒ masse déplacée vers \(u_e\).

**Pourquoi.** Lire la longueur de file, l'occupation canal ou l'état radio du nœud
évalué \(j\) via les objets ns-3 constituerait un **oracle de simulateur**. Cela
violerait §4.1 (« aucune vue globale instantanée n'est admise »), §2 (séparation
stricte de la vérité expérimentale) et l'invariant 20.2.8. Un mécanisme qui ne
fonctionne que grâce à un accès simulateur n'est pas déployable et invaliderait la
revendication de décentralisation.

**Impact.** Réel et mesurable, à rapporter honnêtement : la couverture diagnostique
\(d_e\) sera souvent partielle, donc \(I_e = O_e d_e\) plus faible, donc l'accumulation
d'évidence décisive plus lente que ne le suggérerait une lecture optimiste du §9.3.
C'est le prix de la validité du modèle d'observation, pas un défaut d'implémentation.
Les compteurs `oceaCoverageHistogram` et `uncertainMassShare` sont exportés pour
quantifier cet effet.

**Validé par l'auteur** le 2026-09-04.

---

## D-I3 — Le score RREP est un `optional`, jamais un zéro (C-16)

**Prévu.** C-16 signale que la version algorithmique récupérée retournait zéro quand
\(|\mathcal R_r| < n_R^{min}\), alors que la sémantique sûre est « score indisponible ».
A3.1 retourne `NO_COMPARATIVE_SCORE`.

**Implémenté.** `RrepAnomalyDetector::ComputeAnomalyScore()` retourne
`std::optional<double>`. Un `nullopt` se propage jusqu'à la machine à états, qui
n'évalue simplement pas la clause `A_RREP >= theta_R` de A4.3.

**Pourquoi.** Confondre « pas de comparaison possible » et « score nul » revient à
traiter une absence d'observation comme une normalité observée, ce que le §21 interdit
explicitement (« faux outlier auto-référent », « absence confondue avec normalité »).

**Impact.** Aucun sur la conception ; résout C-16 dans le sens que la spécification
désigne elle-même comme le plus sûr.

**Validé par l'auteur** le 2026-09-04.

---

## D-I4 — Décomposition de l'énergie : \(E_{overhear}\) (Éq. 30)

**Prévu.** \(E_{total} = E_{tx}+E_{rx}+E_{idle}+E_{overhear}+E_{crypto}\).

**Implémenté.** Le `WifiRadioEnergyModel` de ns-3.48 comptabilise l'énergie par **état
PHY** (TX, RX, IDLE, CCA_BUSY, SLEEP) ; il ne distingue pas une trame reçue et destinée
au nœud d'une trame simplement écoutée. Deux sorties sont donc produites :

- `E_tx`, `E_rx`, `E_idle` : mesurés directement par le modèle d'énergie ;
- `E_overhear` : **attribué** par trame, via les traces `PhyRxBegin`/`PhyRxEnd` et le
  contrôle de l'adresse destination MAC (durée de réception × courant RX). Si
  l'attribution par trame n'est pas activée, la valeur exportée est `NaN`, jamais 0,
  et `E_rx` reste la somme des deux composantes.
- `E_crypto` : comptabilisé par `SimulatedCryptoProvider` (modèle de coût, C-29).

**Pourquoi.** L'Éq. (30) suppose une décomposition qui n'existe pas nativement. Publier
un `E_overhear = 0` serait un zéro fabriqué, interdit par l'invariant 20.4.6 et par la
règle D-22.

**Impact.** Le total \(E_{total}\) reste exact (il est mesuré comme
\(\sum_i E_i^{initial}-E_i^{remaining}\), première égalité de l'Éq. 30). Seule la
ventilation TX/RX/overhear est conditionnelle.

**Validé par l'auteur** le 2026-09-04.

---

## D-I5 — Ajout du `SecurityControlProtocol` (hors spécification)

**Prévu.** Le §2, le §2.2 et le §11.2 listent un composant `SecurityControlProtocol`
et des « en-têtes typés UDP » ; le §11.2 impose « un plan UDP séparé et borné » dont
« tous les octets comptent dans la surcharge de sécurité » (Éq. 22). **Aucun format de
message, timer, politique de retransmission, procédure de découverte de comité ou
mécanisme d'acheminement des votes n'est spécifié.**

**Implémenté.** Un protocole de contrôle est **conçu et ajouté**, sur le port UDP 655,
avec cinq types de messages sérialisés canoniquement :

| Type | Rôle | Algorithme |
|---|---|---|
| `NEIGHBOR_ADVERTISEMENT` | construction de la vue d'appartenance \(V_e\) | A5.3 |
| `EVIDENCE_REPORT` | rapport signé d'accusation | A5.1 |
| `BOUND_VOTE` | vote lié (accusé, époque, comité, racine d'évidence) | A5.4 |
| `QUARANTINE_CERTIFICATE` | certificat expirant | A5.4 |
| `RECONCILIATION_HEADER` | en-têtes/digests de branche PTMB | A5.6 |

**Pourquoi.** Sans ce composant, A5 n'est pas exécutable : rapports, votes et
certificats n'ont aucun moyen d'atteindre leurs destinataires.

**Impact.** C'est un **ajout de conception**, pas une transcription. Il doit être
présenté comme tel dans toute rédaction. Ses octets sont intégralement comptés dans
\(B_{security}^{tx}\) (Éq. 22), de sorte que la surcharge mesurée reflète bien le coût
réel du mécanisme et non une version idéalisée sans transport.

**Risque scientifique associé (non résolu par cet ajout).** La formation de comité
(A5.3) suppose une vue d'appartenance \(V_e\) et un `checkpointDigest` communs, ce que
la spécification ne construit nulle part. Si la convergence des vues échoue en
mobilité, `NoQuorumRate` tendra vers 1 et la variante D dégénérera en « C + surcharge ».
Ce risque est mesuré explicitement par `NoQuorumRate` (Éq. 38) et rapporté quel que
soit son résultat.

**Validé par l'auteur** le 2026-09-04.

---

## D-I6 — Le filtrage RREP est rétrospectif et ne retarde pas AODV (A3.1)

**Prévu.** A3.1 place un hook « après décodage de `RecvReply()` et avant engagement de
route à la source », et A1 calcule `anomalyScore` (ligne 2) avant
`PROCESS_RREP_USING_AODV_RULES` (ligne 4).

**Implémenté.** Le RREP est traité **immédiatement** par les règles AODV standard. Le
buffer de candidats accumule les RREP protocolairement valides de la découverte
courante pendant \(W_R\) ; les scores sont calculés à la fermeture de la fenêtre et ne
produisent qu'une transition `WATCH` rétrospective.

**Pourquoi.** Attendre \(W_R = 0{,}5\) s avant d'engager une route retarderait
l'établissement de route de tous les nœuds et modifierait le comportement d'AODV,
ce qu'interdisent l'invariant 20.3.2 et la consigne « ne pas modifier inutilement
AODV ». De plus, au moment du **premier** RREP, \(|\mathcal R_r| = 1 < n_R^{min}\) :
aucun score comparatif n'est calculable, donc l'ordre du pseudocode A1 ne peut de
toute façon pas produire de décision utile à cet instant.

**Impact.** `WATCH` arrive avec un retard borné par \(W_R\). Comme `WATCH` n'a aucun
effet de routage (invariant 20.2.1, D-08), ce retard n'affecte ni le PDR ni la
sélection de route ; il n'affecte que la date d'intensification de l'observation, et
donc marginalement \(T_{detect}\) (Éq. 33a). Effet mesuré et rapporté.

---

## D-I7 — Ratio effectif d'attaquants en présence d'exclusions (Éq. 2)

**Prévu.** \(N_A=\lfloor r_a N+0{,}5\rfloor\) où « \(N\) est la population totale,
**y compris les éventuels endpoints exclus** de l'échantillonnage » (§5.2), le tirage se
faisant ensuite parmi les nœuds admissibles (A2.2).

**Implémenté.** Exactement cela. Mais le manifest exporte **deux** grandeurs :
`attackerRatioRequested` (\(r_a\)) et `attackerRatioAmongEligible`
(\(N_A/|eligible|\)).

**Pourquoi.** Avec `excludeTrafficEndpoints=true` et 20 endpoints exclus sur 100 nœuds,
\(r_a=0{,}30\) donne 30 attaquants parmi 80 éligibles, soit 37,5 % des candidats. Un
lecteur qui n'a que « 30 % » en tête surestime la marge de sûreté des comités.

**Impact.** Aucun sur le comportement ; correction d'une ambiguïté de rapport qui
aurait pu induire en erreur sur l'hypothèse conditionnelle du §4.4.

---

## Divergences à venir

Toute étape ultérieure ajoute ses divergences ici avant d'être déclarée validée.

---

## D-I8 — Le schéma CSV normatif renomme des colonnes de l'étape précédente

**Prévu.** La spécification ne fixe pas de noms de colonnes CSV ; le plan de
développement de l'étape 0 en impose dix-neuf, dans un ordre précis :
`protocol, nodes, simTime, minSpeed, maxSpeed, seed, run, attackerRatio, attackerCount,
attackStart, appTxPackets, appRxPackets, appTxBytes, appRxBytes, pdr, plr,
throughput_bps, goodput_bps, meanDelay_s`.

**Implémenté.** `RunRecord` produit exactement ce schéma. Le programme hérité
`mtcaodv-manet-scenario` conserve son ancien schéma
(`appTxPayloadBytes`, `throughputBps`, `meanDelaySeconds`, colonnes de contexte triées
alphabétiquement par `std::map`), inchangé.

**Pourquoi.** Le nouveau schéma est normatif pour la séquence d'étapes en cours ;
l'ancien appartient à des mesures déjà produites. Les renommer aurait invalidé les
fichiers existants sans bénéfice. Les deux programmes écrivent donc des schémas
différents, ce qui est assumé et documenté plutôt que masqué.

**Impact.** Aucun sur les grandeurs mesurées : ce sont les mêmes équations, calculées par
le même code depuis cette étape. Les résultats issus des deux programmes ne doivent pas
être concaténés sans renommage explicite des colonnes.

---

## D-I9 — Convention JSON pour une grandeur non applicable : `null`

**Prévu.** L'invariant 20.4.6 et la décision D-22 imposent `NaN` ou `N/A` pour toute
grandeur non applicable, jamais un zéro fabriqué.

**Implémenté.** `NaN` dans les CSV ; **`null`** dans les manifests JSON.

**Pourquoi.** JSON ne possède pas de littéral pour « non un nombre ». La première version
du manifest écrivait la valeur brute, donc `nan` — un fichier que tout analyseur JSON
conforme rejette. Le défaut a été trouvé par `validate_step0.py` sur le balayage à faible
mobilité (seed 1004), où un flux dont les extrémités n'ont jamais été connectées n'a pas
de nombre de sauts moyen. Une grandeur légitimement absente invalidait alors l'exécution
entière. Correction dans `FormatJsonNumber()`, régression couverte par le test M-13.

**Impact.** Aucun sur la sémantique : `null` et `NaN` disent la même chose, et aucun des
deux n'est zéro. Les lecteurs de manifest doivent traiter `null` comme non applicable.

---

## D-I10 — Random Waypoint classique par défaut, régime stationnaire en option

**Prévu.** Le §16.1 propose « Random Waypoint en état stationnaire » comme mobilité
principale, au statut **Proposé** (non figé). Le plan de développement de l'étape 0
prescrit « RandomWaypoint », vitesses 1 à 20 m/s.

**Implémenté.** `ns3::RandomWaypointMobilityModel` par défaut ;
`ns3::SteadyStateRandomWaypointMobilityModel` disponible via `--mobility=ssrwp`.

**Pourquoi.** Le plan de développement nomme le modèle classique, et le statut du §16.1
est « Proposé », donc non contraignant. Le transitoire de densité et de vitesse propre au
RWP classique (réf. 22, Yoon et al.) est traité par le warm-up de 10 s, exclu de la
fenêtre d'évaluation. Conserver les deux modèles permet en outre de mesurer l'effet de ce
choix au lieu de le postuler.

**Impact.** Le choix du modèle de mobilité modifie l'empreinte de scénario, donc les
exécutions appariées doivent toutes utiliser le même. À trancher avant le gel des
paramètres (étape 12).

---

## D-I11 — Deux classes de configuration coexistent

**Prévu.** Une configuration unique par exécution.

**Implémenté.** `ExperimentConfiguration` (plan confirmatoire du §16.1, N = 100, 600 s,
variantes A/B/C0/C/D ; options `--nodeCount`, `--speedMin`, `--speedMax`,
`--attackStartTime`) et `PilotConfiguration` (pilote normatif, N = 20, 60 s ; options
`--nodes`, `--simTime`, `--minSpeed`, `--maxSpeed`, `--attackStart`).

**Pourquoi.** Le plan de développement impose des noms d'options que la classe héritée
n'utilise pas. Les renommer aurait invalidé les commandes et les mesures déjà produites
avec `mtcaodv-manet-scenario`. La duplication est bornée — environ 200 lignes — et
explicite.

**Impact.** Risque de dérive entre les deux si une étape ultérieure n'en met à jour
qu'une. Atténuation : **toutes les nouvelles étapes utilisent `PilotConfiguration` et
`mtc-aodv-pilot`** ; le couple hérité n'est plus étendu. Convergence à programmer à
l'étape 12, lorsque les paramètres physiques seront gelés.
