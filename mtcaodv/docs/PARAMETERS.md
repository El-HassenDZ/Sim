# Paramètres MTC-AODV : valeurs, statuts et justifications

Source de vérité machine : `experiments/configs/default_parameters.json`.
Ce fichier-ci justifie chaque défaut ; il ne le redéfinit pas.

## Discipline de calibration

La spécification (§16.1, A7) interdit toute optimisation post hoc :

> « Les dimensions de zone, PHY/rate, propagation, file, énergie initiale, nombres de
> flux, débit, taille de paquet, warm-up, temps d'attaque et paramètres de sécurité
> doivent être obtenus par pilote/sensibilité/calibration, gelés, puis utilisés sans
> optimisation post hoc dans la confirmation. »

Trois statuts, appliqués sans exception :

| Statut | Signification | Modifiable ? |
|---|---|---|
| `FIXED` | imposé par la spécification (§16.1, §16.3, Annexe C) | non |
| `PROVISIONAL` | défaut motivé proposé ici pour un point C-xx ouvert | oui, en phase de calibration |
| `FROZEN` | gelé après calibration, avec date et empreinte | non |

Les seeds de calibration (`1–200`) et de confirmation (`1000–1999`) sont **disjoints**.
Aucun paramètre ne peut être modifié après consultation d'un résultat confirmatoire ;
le cas échéant, la phase confirmatoire est intégralement rejouée.

Au 2026-09-04, **aucun paramètre n'est `FROZEN`** : la calibration n'a pas eu lieu.
Toute valeur ci-dessous est un point de départ exécutable, pas un résultat.

---

## Détecteur RREP — C-01 à C-04, C-06

| Paramètre | Valeur | Justification |
|---|---:|---|
| \(W_R\) `comparisonWindow_s` | 0,5 s | Doit couvrir l'étalement des RREP d'une même découverte sur quelques sauts en 802.11b, tout en restant très inférieur au `NET_TRAVERSAL_TIME` d'AODV (2 × 40 ms × 35 ≈ 2,8 s dans ns-3), afin qu'une fenêtre ne chevauche pas la retransmission du RREQ. |
| \(n_R^{min}\) | 3 | La spécification autorise ≥ 2, mais à \(n=2\) la médiane est le milieu et \(\operatorname{MAD}=|x_1-x_2|/2\) : la statistique n'a aucun pouvoir discriminant et son point de rupture est nul. \(n=3\) est le plus petit effectif où la médiane est une vraie valeur d'ordre. Coût assumé : moins de découvertes produisent un score. Compteur `discoveriesWithoutComparativeScore` exporté. |
| `candidateBufferCapacity` | 8 | Borne mémoire par découverte (§11.4 : « structures bornées »). |
| \(w_s\) | 1,0 | La fraîcheur est la grandeur directement falsifiée par l'attaque (\(\Delta_{seq}=1000\)). |
| \(w_h\) | 1,0 | Le nombre de sauts est la seconde grandeur falsifiée (\(h_{fake}=1\)). |
| \(w_t\) | 0,5 | La précocité est un signal plus faible et davantage confondu par la mobilité et la charge : demi-poids. |
| \(b_R\) | 3,0 | Après la correction D-I1, les \(z\) sont en unités de sigma robuste. \(b_R=3\) place le point où \(\sigma=0{,}5\) à une déviation robuste pondérée de 3σ, seuil conventionnel d'aberrance. |
| \(\theta_R\) | 0,5 | Point de croisement du logistique : « WATCH au-delà de 3σ ». N'a aucun effet de routage (invariant 20.2.1). |
| `sequenceScaleFloor` | 4 incréments | Un nœud intermédiaire honnête peut légitimement annoncer une séquence de quelques incréments plus fraîche ; le plancher évite de traiter cet écart normal comme une aberration. Avec \(w_s=1\), un écart de 4 contribue 1,0 au logit, soit \(\sigma(1-3)=0{,}12 < \theta_R\). |
| `hopScaleFloor` | 1 saut | Unité naturelle et minimale de la grandeur. |
| `timingScaleFloor_s` | 10 ms | Ordre de grandeur d'un `NodeTraversalTime` ns-3 (40 ms) divisé par 4 ; en deçà, l'écart relève de la gigue MAC. |

---

## Observation du transfert — C-17

| Paramètre | Valeur | Justification |
|---|---:|---|
| `observationWindow_s` | 0,30 s | À 802.11b 11 Mbit/s, un paquet de 512 octets occupe ~0,4 ms ; en incluant contention, jusqu'à 7 retransmissions MAC et une file modérée, 0,3 s couvre une retransmission honnête avec une marge large. Une fenêtre trop courte fabriquerait de la masse malveillante à partir de la congestion — exactement ce qu'OCEA doit éviter (§9.2). |
| `observationSamplingRate` | 1,0 | Aucun échantillonnage au pilote : maximiser l'évidence disponible pour évaluer les mécanismes. Sera réduit si le coût mémoire l'impose, avec effet mesuré sur \(T_{detect}\). |
| `maximumObservationWindows` | 256 | Borne dure par nœud (§11.4, invariant 20.4.2). |
| `linkReliabilityMinimumSamples` | 4 | En deçà, `linkDeliveryReliability` est déclarée indisponible (\(a_1=0\)) plutôt qu'estimée sur du bruit. |

---

## OCEA — C-05

Poids d'opportunité (somme 1) : 0,35 / 0,35 / 0,20 / 0,10 pour
`linkDeliveryReliability`, `receptionEvidence`, `contactPersistence`,
`promiscuousVisibility`.

`receptionEvidence` (ACK 802.11 reçu par \(i\)) est l'évidence locale la plus forte que
\(j\) a **effectivement reçu** le paquet : sans elle, une non-retransmission peut
s'expliquer entièrement par une perte sur le lien \(i\to j\). `linkDeliveryReliability`
en est le complément statistique. Ces deux caractéristiques reçoivent donc le poids
dominant. `contactPersistence` conditionne la possibilité même d'observer pendant la
fenêtre. `promiscuousVisibility` est nécessaire mais peu discriminante, d'où 0,10.

Poids diagnostiques (somme 1) : 0,30 `linkBreakDetected`, 0,25 `localChannelBusy`,
0,20 `localQueuePressure`, 0,25 `consistentRouteError`.

Une rupture de lien avérée et un RERR cohérent sont les deux explications bénignes les
plus concluantes, d'où les poids les plus élevés. L'occupation canal et la pression de
file locales sont des **proxys** de la congestion chez \(j\) (correction D-I2) : réelles
mais indirectes, d'où des poids moindres.

> Ces huit poids sont le point C-05 et sont **PROVISOIRES**. Ils commandent directement
> \(c_e\), \(d_e\) et donc \(I_e\) : ils doivent faire l'objet d'une analyse de
> sensibilité avant gel.

---

## MOBeta-Trust — C-07 à C-09, C-15, C-18, C-26

| Paramètre | Valeur | Justification |
|---|---:|---|
| \(\alpha_0=\beta_0\) | 1,0 | Prior uniforme sur \([0,1]\), prior neutre symétrique du *Beta Reputation System* (Jøsang & Ismail, réf. 8 de la spécification). Il rend en outre \(n^{dec}=(\alpha-\alpha_0)+(\beta-\beta_0)\) exactement égal à la masse d'évidence accumulée, ce qui est la définition de l'Éq. (14b). Alternatives de sensibilité : Jeffreys (0,5 ; 0,5) et (2 ; 2). |
| \(\rho\) | 0,90 | Voir ci-dessous. |
| \(\tau_d\) | 60 s | Avec \(\lambda_d(\Delta t)=\rho^{\Delta t/\tau_d}\), la demi-vie de l'évidence au-dessus du prior vaut \(\tau_d\ln(0{,}5)/\ln(0{,}9)\approx 395\) s. Sur un run de 600 s, l'évidence persiste sur l'essentiel de la fenêtre d'évaluation mais s'oublie assez vite pour que la réhabilitation (§9.5, Éq. 34) soit observable. Une demi-vie beaucoup plus courte empêcherait toute accumulation ; beaucoup plus longue rendrait la réhabilitation non mesurable. |
| \(\delta_C\) | 0,05 | Intervalle crédible à 95 %, aligné sur le niveau d'IC déjà fixé en Annexe C. |
| `coverageEwmaHalfLife_s` | 30 s | **Résolution proposée de C-15.** \(\bar c_{ij}\) est une EWMA de \(c_e\) : \(\bar c \leftarrow \kappa\bar c + (1-\kappa)c_e\), \(\kappa=\exp(-\Delta t\ln 2/30)\). Choix : suffisamment court pour refléter la couverture *récente* (le terme employé par l'Éq. 14b), suffisamment long pour ne pas osciller entre deux événements. |
| \(\rho_B=\rho_U\) | 0,90 | **C-18 : aucune raison scientifique de les distinguer de \(\rho\) n'existe dans la spécification.** Égalité par défaut, arbitraire assumé, à ablater si un effet est suspecté. |
| Tolérance / itérations Beta | 1e-9 / 200 | A4.2 exige un coût borné et une comparaison à une grille SciPy indépendante (T-19). |

---

## Machine à états — C-10, C-11, C-19

| Paramètre | Valeur | Justification |
|---|---:|---|
| \(\theta_S\) | 0,50 | Limite de fiabilité « acceptable » : un nœud qui ne retransmet pas la moitié de l'évidence interprétable est mauvais. Symétrique du prior neutre. |
| \(\gamma_S\) | 0,95 | Risque posterior minimal, aligné sur le niveau crédible de 95 %. |
| \(n_{min}\) | 5,0 | Masse décisive hors prior. Avec des masses fractionnaires de l'ordre de 0,2–0,5 par événement (cf. l'oracle §9.3 : \(m_e=0{,}27\)), cela exige 10 à 25 événements interprétables — assez pour que le posterior se concentre, atteignable en quelques dizaines de secondes de trafic actif. |
| \(C_{min}\) | 0,50 | L'intervalle crédible à 95 % doit être plus étroit que 0,5. Effet vérifié analytiquement : Beta(1 ; 6) → largeur ≈ 0,39 ⇒ \(C\approx0{,}61\) (passe) ; Beta(3,5 ; 3,5) → largeur ≈ 0,68 ⇒ \(C\approx0{,}32\) (bloque). C'est exactement le comportement voulu : accuser seulement quand le posterior est concentré **et** bas, jamais quand il est simplement bas et incertain (§9.2, cas limite « peu d'évidence décisive »). |
| \(c_{min}\) | 0,30 | Un événement observé uniquement par les caractéristiques faibles (`contactPersistence` seule : \(c_e=0{,}20\)) ne suffit pas ; les deux caractéristiques fortes réunies donnent \(c_e=0{,}70\). |
| \(\theta_W\) | 0,60 | Au-dessus du prior 0,5, de sorte qu'une évidence négative précoce déclenche la surveillance bien avant que l'accusation soit possible. |
| \(\theta_{release}\) | 0,70 | Strictement supérieur à \(\theta_W\), comme l'exige le §5.3 (« supérieur au seuil d'entrée ») : hystérésis, testée par T-21. |
| Sortie de `WATCH` (C-19) | μ ≥ 0,70 **et** aucun score RREP ≥ θ_R depuis 30 s **et** ≥ 3 nouveaux événements interprétables | **Proposition.** La spécification laisse cette règle ouverte. Trois conditions conjointes évitent qu'un nœud sorte de WATCH par simple oubli temporel sans nouvelle preuve. |
| Réhabilitation (C-19) | ≥ 5 nouveaux événements **postérieurs à l'expiration** | A4.4 exige explicitement « uniquement de l'évidence fraîche post-expiry ». |
| Routage en `CONTESTED` (C-19) | `ADMIT` | Un conflit de certificats ne vaut pas exclusion (D-19, invariant 20.2.4 : un certificat conflictuel ne peut exclure). La politique prudente est donc d'admettre la route et de redemander une corroboration. |

---

## Certification — C-12, C-20 à C-22, C-30

| Paramètre | Valeur | Justification |
|---|---:|---|
| \(m_c\) | 4 | **Minimum de l'Éq. (16a)** avec \(f_c=1\). À 802.11 et à la densité du scénario, un nœud a typiquement 5 à 15 voisins bidirectionnels ; exiger \(m_c>4\) rendrait `PENDING` l'issue dominante et viderait la variante D de son contenu. Ce choix est un compromis de faisabilité, pas une garantie de sûreté. |
| \(f_c\) | 1 | Borne byzantine la plus grande réalistement satisfiable localement. **À \(r_a=0{,}30\), la probabilité qu'un comité de 4 contienne ≥ 2 attaquants est de l'ordre de 35 % :** l'hypothèse §4.4 est alors violée pour une fraction substantielle des rounds. Avec \(q_c=3\), deux attaquants ne peuvent pas **forger** un certificat, mais peuvent **bloquer** la certification. Ce déni est mesuré par `NoQuorumRate` (Éq. 38) et doit être rapporté par ratio. |
| \(q_c\) | 3 | Calculé : \(m_c-f_c\). |
| \(q_{min}^{ctx}\) | 2 | Au moins deux contextes d'observation distincts, pour qu'un même épisode vu sous un seul angle ne suffise pas. |
| \(\theta_C\) | 0,50 | Support témoin pondéré majoritaire. |
| \(D_{ik}\) (C-20) | \(1/(1+n_{shared})\) | **Proposition.** \(n_{shared}\) = nombre d'autres rapporteurs admis partageant le même digest de contexte (même accusé, même flux, même seau de 10 s). Caps : 1 rapport par (rapporteur, événement canonique), 3 rapports par seau de contexte. Des identités distinctes ne prouvant pas l'indépendance (C-20), l'influence décroît avec la corrélation de contexte. |
| Sortition (C-21) | déterministe, sans VRF | Conforme à A5.3. **Aucune revendication de non-manipulabilité n'est faite** : un accusé qui connaît `checkpointDigest` et `evidenceRoot` peut anticiper la composition. Limitation documentée, non corrigée. |
| Borne byzantine locale (C-22) | jamais établie en ligne | `byzantineBoundEstablishable = false`, statut `FIXED`. Le protocole traite \(f_c\) comme une **hypothèse**, jamais comme un fait vérifié. Le simulateur vérifie a posteriori avec la vérité terrain, hors ligne uniquement (§4.4, invariant 20.2.8). |

---

## PTMB — C-13, C-14, C-24, C-31

| Paramètre | Valeur | Justification |
|---|---:|---|
| \(S_{max}\) | 1024 o | Tient dans une trame 802.11 avec en-têtes IP/UDP, sans fragmentation IP — une fragmentation multiplierait les pertes et fausserait la mesure de surcharge. |
| \(B_{max}\) | 128 blocs | \(M_i \le B_{max}S_{max} = 128\) KiB par nœud (Éq. 18), budget compatible avec un nœud MANET contraint. |
| `maximumRecordsPerBlock` | 8 | Avec des enregistrements signés de ~120 o, 8 records + en-tête restent sous \(S_{max}\). |
| `maximumPendingPool` | 256 | Borne du pool `PENDING` exigée par A5.7. |
| \(TTL_{evidence}\) | 120 s | L'évidence doit survivre à la formation d'un comité (timeout 5 s) et à plusieurs tentatives, sans devenir un stock permanent. |
| \(TTL_{certificate}\) | 180 s | **Choisi pour rendre la réhabilitation mesurable** : sur un run de 600 s avec attaque à 50 s, un certificat émis vers 100–200 s expire avant la fin, ce qui permet d'observer `RehabilitationTime` (Éq. 34). Un TTL plus long transformerait la quarantaine en blacklist de fait, ce que le §21 interdit. |
| `digestRetentionPeriod` | 600 s | L'anti-rejeu ne doit jamais oublier à l'intérieur d'un run (A5.7 : « préserver les condensats de protection anti-rejeu »). |
| `fanout` / `retries` / `batch` | 3 / 2 / 16 | Diffusion bornée (C-31). Fanout 3 ≈ moitié d'un voisinage typique : redondance sans inondation. |
| Genesis (C-24) | `SHA256("MTC-AODV/PTMB/genesis/v1" ‖ scenarioHash)` | Constante provisionnée hors ligne, au même titre que les identités et clés (§4.2). **Aucune autorité centrale en ligne** n'est introduite : le digest est connu avant déploiement. |

---

## Scénario — C-25, C-27, C-28

Toutes les valeurs de la section `scenario` sauf `nodeCount = 100` sont **PROVISOIRES**
et relèvent de C-28. Elles définissent un point de fonctionnement plausible
(1500 × 1500 m, 100 nœuds, 802.11b, LogDistance n=3, 10 flux CBR UDP de 512 o à
4 paquets/s, 600 s, warm-up 30 s, attaque à 50 s) et **ne constituent pas** le plan
confirmatoire tant que la calibration n'a pas eu lieu.

`lifetimeCriterion = FIRST_NODE_DEPLETED` résout C-25 par le critère le plus simple et
le plus reproductible ; les deux alternatives citées par la spécification (fraction de
nœuds actifs, perte de connectivité applicative) restent calculables a posteriori à
partir des traces d'énergie.

C-27 est tranché : profil **piéton/général** (1–5 m/s en bande principale), cohérent
avec les trois bandes provisoires du §16.1.

---

## Analyse statistique — C-35

`familywiseAlpha = 0,05`, `bootstrapReplicates = 10000`, `calibrationBinCount = 10`
(bins équi-larges). Valeurs conventionnelles, à préenregistrer formellement avant la
phase confirmatoire comme l'exige A7.3.
