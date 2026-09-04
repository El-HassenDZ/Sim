# Architecture d'implémentation MTC-AODV (ÉTAPE 2)

Cartographie normative : **concept de la spécification → classe → fichier → algorithme
→ équation**. Elle complète le §18 de la spécification en y ajoutant la colonne
« responsabilité », l'interface publique et le langage retenu.

## Principe directeur

Une classe = une responsabilité, énonçable en une phrase. Les composants de détection,
de confiance, de certification et de registre sont des objets `ns3::Object` agrégés au
nœud, découplés du protocole de routage par des interfaces étroites. Le protocole
appelle ; il ne calcule pas.

## Cloisonnement de la vérité terrain (invariant 20.2.8)

```
                 ┌─────────────────── barrière de compilation ───────────────────┐
  AttackManager  │                                                               │
  BlackholeBehavior ─── installe ───> RoutingProtocol (hooks A2.3/A2.4)          │
  MetricsCollector (évaluation hors ligne)                                       │
                 │                                                               │
                 │  RrepAnomalyDetector, ForwardingObserver, EvidenceAttributor, │
                 │  MobetaTrustManager, SecurityStateMachine, WitnessAggregator, │
                 │  ValidationCommittee, CertificateValidator, PtmbLedger        │
                 └───────────────────────────────────────────────────────────────┘
```

Aucun fichier de la zone de droite n'inclut `attack-manager.h` ni
`blackhole-behavior.h`. Le test **T-34** vérifie mécaniquement cette propriété sur le
graphe d'inclusion : elle ne dépend pas de la discipline du développeur.

---

## 1. Couche expérimentation (`helper/`)

| Classe | Responsabilité | Algo | Éq. |
|---|---|---|---|
| `AttackManager` | Calculer \(N_A\), tirer les attaquants sans remise sur un flux RNG assigné, produire un `AttackSelectionResult` validé | A2.1, A2.2 | (2) |
| `AttackSelectionResult` | Résultat immuable et auto-validant : IDs triés, uniques, ratio demandé et ratio parmi éligibles | A2.2 | (2) |
| `ExperimentConfiguration` | Charger/valider le JSON de paramètres, allouer les plages de flux RNG disjointes, produire le `scenarioHash` canonique | A7.1 | — |
| `MtcAodvHelper` | Installer le protocole forké, attacher une instance **distincte** de `BlackholeBehavior` par attaquant, agréger les composants de sécurité selon la variante A/B/C0/C/D | A2 | — |

Interface clé :

```
AttackSelectionResult AttackManager::SelectAttackers(
        const NodeContainer& nodes,
        double attackerRatio,
        const std::set<uint32_t>& excludedIds);
```

`ComputeAttackerCount(uint32_t nodeCount, double ratio)` est une fonction libre,
testable sans simulateur, qui lève une exception sur ratio non fini, hors \([0,1]\), ou
sur \(N_A>N\) (D-01, D-02).

---

## 2. Couche routage et attaque (`model/`)

| Classe | Responsabilité | Algo | Éq. |
|---|---|---|---|
| `mtcaodv::RoutingProtocol` | Fork AODV ns-3.48 + points d'ancrage. Ne contient **aucune** logique de confiance : il délègue | A1, A2.3, A2.4, A6 | (19), (23) |
| `AttackBehavior` | Interface abstraite d'un comportement malveillant : `IsActive`, `ShouldForgeRouteReply`, `ShouldDropTransitPacket`, `CreateForgedReplyProfile` | A2 | — |
| `BlackholeBehavior` | Politique full Blackhole. **Aucun accès au simulateur** : décisions pures, testables hors ns-3 | A2.3, A2.4 | (23) |

Points d'ancrage dans le fork, et rien d'autre :

| Fonction amont | Ajout | Algo |
|---|---|---|
| `RecvRequest()` | après création de la route inverse : tentative de RREP forgé | A2.3 |
| `SendForgedReply()` | **nouvelle** : sérialise un `RrepHeader` standard, unicast port 654 | A2.3 |
| `RecvReply()` | alimente `RrepCandidateBuffer` ; traitement AODV inchangé et non retardé (D-I6) | A3.1 |
| `Forwarding()` | drop transit silencieux ; sinon ouverture d'une fenêtre d'observation | A2.4, A3.2 |
| `RouteOutput()` / `RouteInput()` | filtre certificat + départage lexicographique **après** les règles AODV | A6 |

---

## 3. Couche détection (`model/`)

| Classe | Responsabilité | Algo | Éq. |
|---|---|---|---|
| `RrepCandidateBuffer` | Conserver, par découverte et pendant \(W_R\), les RREP protocolairement valides ; capacité et nombre de découvertes bornés | A3.1 | — |
| `RrepAnomalyDetector` | Médiane/MAD, comparaison **modulaire** de séquence, score logistique. Retourne `std::optional<double>` (D-I3) | A3.1 | (3)–(5) |
| `PacketFingerprint` | Empreinte bornée d'un paquet à partir de champs immuables localement disponibles | A3.2 | — |
| `ForwardingObserver` | Ouvrir/fermer les fenêtres, collecter les caractéristiques **locales** \(x_k,y_\ell\) et leurs drapeaux de disponibilité | A3.2 | entrées (6)–(10) |
| `EvidenceAttributor` | Fonction pure : observation close → \((g,m,b,u)\) | A3.3 | (6)–(10) |
| `EvidenceMass` | Agrégat de valeur, invariants de somme et de bornes vérifiés à la construction | A3.3 | (10c) |

Méthodes exigées par le §18 de la spécification, conservées telles quelles :
`ComputeSequenceAnomaly()`, `ComputeHopAnomaly()`, `ComputeTimingAnomaly()`,
`ComputeAnomalyScore()`, `ComputeOpportunityCoverage()`,
`ComputeForwardingOpportunity()`, `ComputeBenignLossAssessment()`,
`AttributeObservedForwarding()`, `AttributeMissingForwarding()`.

---

## 4. Couche confiance et décision (`model/`)

| Classe | Responsabilité | Algo | Éq. |
|---|---|---|---|
| `BetaDistributionMath` | CDF Beta régularisée et quantile, itérations bornées, sans dépendance externe | A4.2 | (13), (14a) |
| `TrustRecord` | État par couple \((i,j)\) ; **score numérique et état de sécurité restent des champs distincts** (§9.1) | A4.1 | (11), (12) |
| `MobetaTrustManager` | `ApplyPositiveEvidence()`, `ApplyMaliciousEvidence()`, `ComputePosteriorSummary()`, maintenance périodique | A4.1, A4.2, A4.4 | (11)–(14a) |
| `SecurityStateMachine` | Transitions légales uniquement ; `ShouldAccuse()` implémente la conjonction (14b) ; assertion : `QUARANTINED` inatteignable sans certificat valide | A4.3, A4.4 | (5), (13), (14), (16) |

---

## 5. Couche certification (`model/`)

| Classe | Responsabilité | Algo | Éq. |
|---|---|---|---|
| `EvidenceRecord` | Enregistrement canonique signé, sérialisation déterministe, digest | A5.1 | — |
| `EvidenceManager` | Cycle de vie `OBSERVED→PROPOSED→PENDING→LOCALLY_CERTIFIED/REJECTED→CONTESTED→RECONCILED→EXPIRED`, pool borné, anti-duplication | A5.1, A5.5 | §10.3 |
| `WitnessAggregator` | Filtrage, déduplication canonique, caps par identité et par contexte, `ComputeWeightedSupport()` → \(\Phi_j, h_e\) | A5.2 | (15) |
| `ValidationCommittee` | Sélection déterministe par `HASH(checkpointDigest ‖ evidenceRoot ‖ epoch ‖ identity)`, gel de l'appartenance, `PENDING` si \(|C_e|<3f_c+1\) | A5.3 | (16a) |
| `QuarantineCertificate` | Certificat expirant, portée, votes liés | A5.4 | (16b) |
| `CertificateValidator` | `TryCreateCertificate()`, vérification de votes uniques et non contradictoires, `ValidContext` | A5.4 | (16b) |
| `SimulatedCryptoProvider` | **Modèle de coût** : taille de signature, délai, énergie, compteur de vérifications. Jamais présenté comme de la cryptographie | A5.1 | — |

---

## 6. Couche PTMB (`model/`)

| Classe | Responsabilité | Algo | Éq. |
|---|---|---|---|
| `MicroBlock` | Structure canonique (17), `SerializeCanonical()`, `ComputeDigest()`, racine de Merkle des enregistrements | A5.5 | (17) |
| `PtmbLedger` | `Append()`, index par digest, index d'expiration, compteur d'octets retenus, `PruneExpired()`, `Checkpoint()` ; bornes dures (18) | A5.5, A5.7 | (17), (18) |
| `ReconciliationManager` | Échange d'en-têtes, union idempotente par digest, conservation des branches conflictuelles, bloc de fusion multi-parents | A5.6 | (17), (18) |

Le registre est un **DAG**, pas une chaîne : `parents[]` est une liste bornée et aucune
règle de plus longue chaîne n'existe (§5.7, §22).

---

## 7. Couche transport de sécurité (`model/`) — ajout D-I5

| Classe | Responsabilité |
|---|---|
| `SecurityControlProtocol` | Socket UDP 655, sérialisation canonique, diffusion à fanout borné, retries, **comptabilisation intégrale des octets émis** pour l'Éq. (22) |
| `SecurityHeaders` | Cinq en-têtes typés : `NEIGHBOR_ADVERTISEMENT`, `EVIDENCE_REPORT`, `BOUND_VOTE`, `QUARANTINE_CERTIFICATE`, `RECONCILIATION_HEADER` |

Ce composant est un **ajout de conception** ; voir `DIVERGENCES.md` § D-I5.

---

## 8. Couche mesure (`model/`)

| Classe | Responsabilité | Éq. |
|---|---|---|
| `MetricsCollector` | Compteurs réseau, sécurité, registre, énergie ; export CSV + manifest JSON ; **seule** classe autorisée à lire la vérité terrain, et uniquement pour l'évaluation hors ligne | (20)–(39) |

Règle de type appliquée dans le code : toute métrique dont le dénominateur peut être nul
est représentée par un type qui rend `NaN`/`N/A` **exprimable**, jamais par un `double`
initialisé à 0 (D-22, invariant 20.4.6).

---

## 9. Couche Python (`experiments/`)

| Script | Responsabilité | Algo |
|---|---|---|
| `run_campaign.py` | Générer les configurations appariées, exécuter, paralléliser | A7.1 |
| `validate_runs.py` | Validation **fail-closed** : schéma, versions, hashes appariés, complétude ; aucune imputation | A7.2 |
| `aggregate_results.py` | Agrégation des seuls runs `VALID`, préservation de l'appariement | A7 |
| `statistical_analysis.py` | Différences appariées, bootstrap, test préenregistré, Vargha–Delaney, Holm | A7.3 |
| `plot_results.py` | Figures issues **exclusivement** des données produites | A7 |

Frontière stricte (§2.2) : **aucune décision de protocole en Python, aucune statistique
inférentielle en C++.**

---

## 10. Correspondance variantes → composants agrégés

| Variante | RoutingProtocol | Détection A3 | Confiance | Certification + PTMB | Gate route A6 |
|---|---|---|---|---|---|
| **A** | `ns3::aodv` **stock** | — | — | — | — |
| **B** | fork | oui | — | — | — |
| **C0** | fork | oui | Beta binaire (succès/échec entier) | — | — |
| **C** | fork | oui | MOBeta (masses fractionnaires) | — | — |
| **D** | fork | oui | MOBeta | oui | oui |

Conséquence structurelle, à assumer dans l'analyse : **seule la variante D peut modifier
une décision de routage.** L'Éq. (19) place la fraîcheur AODV en premier critère et la
confiance en dernier départage ; seul le filtre dur par certificat évince un prochain
saut. B, C0 et C sont donc des variantes de *détection et de coût*, pas de performance.
L'égalité PDR(A) ≈ PDR(B) ≈ PDR(C0) ≈ PDR(C), aux octets de contrôle près, devient un
**test de validation** (T-33) : un écart significatif signalerait un couplage non voulu
ou un défaut d'appariement.
