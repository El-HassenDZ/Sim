# STEP 1 — Blackholes multiples câblés au fork AODV

> L'historique de l'étape 0 (baseline) reste dans `docs/RESULTS_STATUS.md`. Ce document
> décrit l'étape 1. La baseline saine y est supposée acquise et validée.

## 1. Numéro de l'étape

**Étape 1** du plan de développement : *Multiple Full Blackhole*. Elle câble la politique
d'attaque — déjà testée isolément à l'étape 0 — dans le protocole de routage forké, et
démontre son effet de bout en bout.

## 2. Objectif scientifique

Établir la **chaîne causale** de l'attaque full Blackhole (A2) et la rendre mesurable :
forge d'un RREP attractif (Éq. 23) → attraction d'au moins une route → abandon silencieux
des données en transit → chute mesurable du PDR par rapport à la baseline appariée. Le
critère n'est pas une valeur de PDR cible mais une **relation** : à seeds appariés, le PDR
sous attaque doit être strictement inférieur à celui sans attaque, avec des compteurs
d'attaque strictement positifs.

## 3. Objectif logiciel

- Brancher `AttackBehavior` dans `ns3::mtcaodv::RoutingProtocol` par deux hooks (A2.3 dans
  `RecvRequest`, A2.4 dans `Forwarding`), sans introduire de logique d'attaque dans le
  protocole lui-même : toute décision est déléguée à la politique.
- Garantir que le fork reste **strictement identique** à l'AODV d'origine pour un nœud
  honnête (sans politique).
- Installer une instance de comportement **distincte** par attaquant (invariant 20.4.3).
- Exposer les paramètres du profil Blackhole en ligne de commande.
- Étendre le CSV (colonnes `forgedRrepCount`, `blackholeDropCount`) et sa validation
  sans casser le schéma de l'étape 0.
- Fournir un test d'intégration ns-3 déterministe (T-08).

## 4. Dépendances

Celles de l'étape 0. Aucune nouvelle dépendance externe. `src/aodv/` reste inchangé
(invariant 20.3.1) ; l'attaque vit exclusivement dans le fork `contrib/mtcaodv/`.

## 5. Fichiers ajoutés

| Fichier | Rôle |
|---|---|
| `contrib/mtcaodv/test/mtcaodv-blackhole-integration-test-suite.cc` | Test système T-08 : fixture causale S–R–D, témoin sain vs Blackhole vs attaque tardive |

## 6. Fichiers modifiés

| Fichier | Modification |
|---|---|
| `model/attack-behavior.{h,cc}` | Ajout à l'interface de `NotifyForgedReplySent` / `NotifyTransitPacketDropped` (virtuelles, corps par défaut vide) pour que le protocole ne dépende que de l'interface |
| `model/blackhole-behavior.h` | Ces deux méthodes marquées `override` |
| `model/mtc-aodv-routing-protocol.{h,cc}` | Pointeur `m_attackBehavior` résolu paresseusement ; `SendForgedReply()` ; hook A2.3 dans `RecvRequest()` ; hook A2.4 dans `Forwarding()` |
| `helper/pilot-configuration.{h,cc}` | Champs et options du profil Blackhole ; garde `advertisedHops ≤ 255` ; entrées dans `Describe()` |
| `examples/mtc-aodv-pilot.cc` | Installation des comportements sur les attaquants ; relecture des compteurs ; colonnes CSV `forgedRrepCount`/`blackholeDropCount` ; manifest étape 1 ; garde stock-AODV-sous-attaque |
| `experiments/schemas/run_record_step0.json` | Colonnes optionnelles étape 1, contrôles X-09 et MF-06 |
| `experiments/validate_step0.py` | Contrôles X-09 et MF-06 |
| `experiments/tests/test_validate_step0.py` | 4 tests supplémentaires (étape 1) |
| `contrib/mtcaodv/CMakeLists.txt` | Déclaration de la suite d'intégration |
| `docs/RESULTS_STATUS.md`, `docs/DIVERGENCES.md` | Résultats mesurés ; divergence D-I12 |

## 7. Classes

Aucune nouvelle classe protocolaire. Les hooks utilisent l'interface `AttackBehavior`
(étape 0) et son implémentation `BlackholeBehavior` (étape 0). La nouveauté est
l'**intégration**, pas de nouvelles structures.

## 8. Méthodes importantes

| Méthode | Rôle |
|---|---|
| `RoutingProtocol::ResolveAttackBehavior()` | Résout et met en cache (une fois) la politique agrégée sur le nœud ; renvoie `nullptr` pour un nœud honnête |
| `RoutingProtocol::SendForgedReply()` | Construit et émet un RREP forgé au format AODV standard (A2.3, Éq. 23), incrémente le compteur après émission |
| Hook dans `RecvRequest()` | Après la route inverse, avant la réponse légitime : si la politique décide de forger, émet le RREP forgé et arrête le traitement du RREQ |
| Hook dans `Forwarding()` | À l'entrée : si la politique décide d'abandonner, consomme le paquet sans callback ni RERR |
| `AttackBehavior::Notify*` | Points de comptage virtuels, corps par défaut vide |

## 9. Paramètres

Nouvelles options du pilote (profil full Blackhole, §8.1 ; toutes configurables) :

| Option | Défaut | Unité | Symbole |
|---|---:|---|---|
| `--seqOffset` | 1000 | — | \(\Delta_{seq}\) (Éq. 23) |
| `--advertisedHops` | 1 | sauts | \(h_{fake}\) |
| `--forgedLifetime` | 30 | s | \(T_{fake}\) |
| `--dropTransitData` | true | booléen | — |
| `--preserveControlPlane` | true | booléen | — |

Options d'attaque déjà présentes : `--attackerRatio`, `--attackStart`.

## 10. Équations implémentées

| Équation / règle | Objet | Emplacement |
|---|---|---|
| (23) | \(seq_{fake} = (seq_{observed} + \Delta_{seq}) \bmod 2^{32}\) | `BlackholeBehavior::CreateForgedReplyProfile` (étape 0), émis par `SendForgedReply` |
| (2) | \(N_A = \lfloor r_a N + 0{,}5 \rfloor\) | `AttackManager` ; contrôlé X-04 |
| règle booléenne A2.4 | abandon silencieux du transit | hook `Forwarding()` |

## 11. TraceSources

Les `TraceSource` `ForgedReply` et `BlackholeDrop` de `BlackholeBehavior` (étape 0) sont
désormais **effectivement déclenchées** par le protocole. Aucune nouvelle source.

## 12. RNG streams

Inchangés par rapport à l'étape 0 (placement 70000, mobilité 71000, Wi-Fi 72000, tirage
d'attaquants 73001, trafic 74000, routage 75000). L'attaque n'introduit aucun aléa : le
profil forgé est déterministe.

## 13. Compilation

```bash
cd /home/hassen/res/ns-3.48
# Le programme de scénario est déjà lié depuis l'étape 0 ; sinon :
ln -sfn /home/hassen/res/ns-3.48/contrib/mtcaodv/examples/mtc-aodv-pilot.cc \
        /home/hassen/res/ns-3.48/scratch/mtc-aodv-pilot.cc
./ns3 build
```

## 14. Tests

```bash
cd /home/hassen/res/ns-3.48
./test.py -s mtcaodv-blackhole-integration     # T-08, nouveau
./test.py -s mtcaodv-attack                     # non-régression politique
./test.py -s mtcaodv-metrics                    # non-régression métriques
python3 -m unittest discover -s experiments/tests -p "test_validate_step0.py" -v
```

## 15. Simulation

```bash
cd /home/hassen/res/ns-3.48

# Sans attaque (baseline fork).
./ns3 run "mtc-aodv-pilot --protocol=mtcaodv --nodes=20 --simTime=60 \
  --minSpeed=1 --maxSpeed=20 --attackerRatio=0 --seed=1001 --run=1"

# Avec plusieurs Blackholes (r_a = 0,20 => N_A = 4), attaque à t = 10 s.
./ns3 run "mtc-aodv-pilot --protocol=mtcaodv --nodes=20 --simTime=60 \
  --minSpeed=1 --maxSpeed=20 --attackerRatio=0.20 --attackStart=10 --seed=1001 --run=1"

# Balayage apparié clean vs attaqué sur 6 seeds.
python3 experiments/run_pilot.py --ns3Dir /home/hassen/res/ns-3.48 \
  --seeds 1001-1006 --protocols mtcaodv --outputDir results/step1_clean --tag clean
python3 experiments/run_pilot.py --ns3Dir /home/hassen/res/ns-3.48 \
  --seeds 1001-1006 --protocols mtcaodv --outputDir results/step1_atk --tag atk \
  --set attackerRatio=0.20 --set attackStart=10

python3 experiments/validate_step0.py results/step1_*/*_metrics.csv
python3 experiments/aggregate_step0.py results/step1_*/*_metrics.csv --groupBy attackerRatio
```

## 16. Sorties

CSV et manifest de l'étape 0, augmentés :

- CSV : deux colonnes `forgedRrepCount`, `blackholeDropCount` ajoutées **après** les
  colonnes de l'étape 0 (le schéma normatif est préservé).
- Manifest : `"step": 1`, bloc `attack` enrichi de `installed`, `forgedRrepCount`,
  `blackholeDropCount`, et liste triée `attackerNodeIds`.

## 17. Critères PASS/FAIL

| # | Critère | Vérification | État dans cet environnement |
|---|---|---|---|
| P-1 | Module et pilote compilent, sans warning | `./ns3 build` | **PASS** |
| P-2 | T-08 (chaîne causale) passe | `./test.py -s mtcaodv-blackhole-integration` | **PASS** |
| P-3 | Non-régression : `mtcaodv-attack`, `mtcaodv-metrics` | `./test.py` | **PASS** |
| P-4 | Tests Python (24) | `unittest` | **PASS** |
| P-5 | Fork honnête ≡ AODV standard après hooks | 5 seeds appariés, `diff` | **PASS**, 0 divergence |
| P-6 | Attaque active : `forgedRrepCount > 0` et `blackholeDropCount > 0` | 6 seeds | **PASS** partout |
| P-7 | PDR attaqué < PDR sain (appariement) | 6 seeds | **PASS** partout (≈ 0,27 vs ≈ 0,83) |
| P-8 | N_A = floor(r_a·N + 0,5) | X-04 | **PASS** (4 pour r_a=0,20, N=20) |
| P-9 | Attaque inactive avant `attackStart` : rien de forgé/abandonné | T-08 cas 3 | **PASS** |
| P-10 | Reproductibilité de la ligne attaquée | double exécution | **PASS** |
| P-11 | stock AODV + attaque refusé (invariant 20.3.1) | `--protocol=aodv --attackerRatio>0` | **PASS**, code 1 |
| P-12 | Validateur rejette compteurs incohérents | X-09, MF-06 | **PASS** |

## 18. Limitations de cette étape

1. **Pilote homogène.** Tous les nœuds exécutent le fork. La variante A du §16.2 (nœuds
   honnêtes en AODV stock, attaquants forgeurs, piles mixtes) relève de l'étape 10
   (admission sécurisée). Une attaque avec `--protocol=aodv` est donc refusée.
2. **Aucune défense.** Détection, confiance, certificats et PTMB n'existent pas : le PDR
   attaqué est celui d'un AODV sans protection, comme attendu.
3. **Pas de résultat confirmatoire.** 6 seeds < 30 (A7). Chiffres pilotes uniquement.
4. **`blackholeDropCount` compte les paquets consommés au hook de forwarding**, pas les
   paquets applicatifs distincts perdus de bout en bout ; les deux diffèrent en présence
   de retransmissions MAC. C'est un compteur d'événements d'attaque, pas une métrique de
   perte (laquelle reste le PLR).

## 19. Éléments volontairement non implémentés

- Filtrage RREP / dépistage d'anomalie (étape 2) : le RREP forgé est émis mais **aucun
  détecteur ne l'examine**.
- OCEA, MOBeta-Trust, machine à états, témoins, certificats, PTMB, admission sécurisée
  (étapes 3 à 10).
- Coordination entre attaquants (stress test secondaire, §8.2) : les Blackholes sont
  indépendants, conformément au scénario principal.

## 20. Prochaine étape prévue

**Étape 2 — Dépistage d'anomalie RREP.** Introduire `RrepAnomalyDetector` et
`RrepCandidateBuffer` : buffer borné de RREP comparables, médiane et MAD robustes,
anomalies de séquence/sauts/précocité (Éq. 3–5), score logistique, passage en `WATCH`.
Le détecteur ne verra jamais la liste des attaquants (invariant 20.2.8) et `WATCH`
n'entraînera aucune exclusion (invariant 20.2.1). Point ouvert à trancher avant :
C-01/C-02 (fenêtre \(W_R\), minimum \(n_R^{min}\)) et C-16 (représentation du score
indisponible).
