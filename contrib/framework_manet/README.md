# framework_manet — baseline AODV instrumentée pour ns-3.48

Module `contrib/` de ns-3.48 destiné à établir une **baseline AODV
reproductible, instrumentée et testable**. Elle sert de référence
expérimentale aux futures extensions de sécurité : blackhole, confiance,
blockchain…, qui ne font **pas** partie de cette phase.

- AODV officiel (`src/aodv/`) utilisé tel quel, **sans modification**. Son
  intégrité est vérifiable (étape 2 de la procédure).
- Le développement est incrémental : une étape n'est construite que sur une
  étape validée.
- Les constats et décisions sont consignés dans
  [`doc/DECISIONS.md`](doc/DECISIONS.md) (identifiants F-xx et D-xx).

## Statut des étapes

| Étape | Contenu | Statut |
|-------|---------|--------|
| STEP 0 | Structure du module, contrat d'API ns-3.48, empreinte d'environnement, test RNG | `TESTÉ` (machine cible, 2026-09-22 : 4/4 tests, 62/62 API, empreinte RNG identique au conteneur de référence) |
| STEP 1a | Helper radio commun, sonde de liaison, calibration radio statique | `IMPLÉMENTÉ — À VÉRIFIER` (compilé, testé et exécuté dans le conteneur de référence) |
| STEP 1b | Scénario AODV + Wi-Fi 802.11b ad hoc + RWP + UDP | `PROPOSÉ` — bloqué par D-05, D-06 et D-19 (`À CONFIRMER`) |
| STEP 2 | Instrumentation : `traffic_log.csv`, `run_metrics.csv`, `mobility.csv` | `PROPOSÉ` |
| STEP 3 | Énergie | `PROPOSÉ` |
| STEP 4 | `routing_table.txt` + NetAnim `framework_manet.xml` | `PROPOSÉ` |
| STEP 5 | Validation Python et tests de cohérence | `PROPOSÉ` |

## Arborescence

```text
contrib/framework_manet/
├── CMakeLists.txt
├── README.md
├── doc/
│   ├── DECISIONS.md                          registre d'audit et de décisions
│   ├── MANIFEST.sha256                       empreinte des fichiers livrés
│   ├── aodv-stock-ns-3.48.sha256             empreinte de src/aodv officiel
│   ├── step0-env-check.reference.txt         sortie de référence de STEP 0
│   └── step1a-reference/                     sorties de référence de STEP 1a
├── helper/
│   ├── framework-manet-radio-helper.{h,cc}   radio unique de la baseline (STEP 1a, 1b)
│   └── framework-manet-link-probe.{h,cc}     sonde de liaison à deux nœuds
├── model/
│   ├── framework-manet-api-contract.{h,cc}   contrat d'API + empreinte de build
│   └── framework-manet-statistics.{h,cc}     IC de Wilson, graphe géométrique aléatoire
├── examples/
│   ├── CMakeLists.txt
│   ├── framework-manet-env-check.cc          validation d'environnement (STEP 0)
│   └── framework-manet-link-calibration.cc   calibration radio (STEP 1a)
└── test/
    ├── framework-manet-test-suite.cc         suite "framework-manet" (STEP 0)
    └── framework-manet-radio-test-suite.cc   suite "framework-manet-radio" (STEP 1a)
```

## Procédure (STEP 0 + STEP 1a)

Toutes les commandes s'exécutent depuis la racine de ns-3.48 :

```bash
cd /home/hassen/res/ns-3.48
```

### 1. Installation et provenance

`-d` extrait l'archive dans la racine ns-3, où qu'elle ait été téléchargée :

```bash
unzip -o /chemin/vers/framework_manet_step1a.zip -d /home/hassen/res/ns-3.48
sha256sum -c contrib/framework_manet/doc/MANIFEST.sha256
```

Chaque fichier doit afficher `OK`. Cela prouve que l'arborescence locale
correspond exactement à la livraison.

### 2. Intégrité de l'AODV officiel

```bash
sha256sum -c contrib/framework_manet/doc/aodv-stock-ns-3.48.sha256
```

Chaque ligne doit afficher `OK`.

### 3. Configuration et compilation

```bash
./ns3 configure --build-profile=default --enable-examples --enable-tests
./ns3 build
```

`framework_manet` doit figurer dans *Modules configured to be built*.

### 4. Tests

```bash
./test.py --no-build --suite=framework-manet --verbose-failed
./test.py --no-build --suite=framework-manet-radio --verbose-failed
./ns3 run --no-build "test-runner --suite=framework-manet-radio --verbose"
```

Le message `Deprecation warning for name ns3::BasicEnergySource` est attendu :
il vient du contrôle négatif de la suite `framework-manet`.

### 5. Validation de l'environnement (régression STEP 0)

`--no-build` évite que les messages de ninja se mêlent à la sortie comparée.

```bash
./ns3 run --no-build framework-manet-env-check > step0_env_check.txt 2>&1
diff <(grep -v -E "executable|compiler" step0_env_check.txt) \
     <(grep -v -E "executable|compiler" contrib/framework_manet/doc/step0-env-check.reference.txt)
```

Attendu : aucune différence, en particulier sur les lignes `[rng]`. Le
contrat compte désormais 74 exigences : 12 ont été ajoutées pour STEP 1a.

### 6. Calibration radio (STEP 1a)

Six configurations, environ 30 s chacune en profil `default`. Chacune
écrit `resulls_framwork/link_calibration_<config>.csv` (D-03) :

```bash
C=framework-manet-link-calibration
./ns3 run --no-build "$C" > step1a_A_pd-on_nu-default.txt 2>&1
./ns3 run --no-build "$C --nonUnicastMode=DsssRate11Mbps" > step1a_A_pd-on_nu-11.txt 2>&1
./ns3 run --no-build "$C --preambleDetection=false" > step1a_B_pd-off_nu-default.txt 2>&1
./ns3 run --no-build "$C --preambleDetection=false --nonUnicastMode=DsssRate11Mbps" > step1a_B_pd-off_nu-11.txt 2>&1
./ns3 run --no-build "$C --propagationModel=range --maxRange=150" > step1a_R150.txt 2>&1
./ns3 run --no-build "$C --propagationModel=range --maxRange=250" > step1a_R250.txt 2>&1
grep -h VERDICT step1a_*.txt
for f in step1a_*.txt; do diff -q "$f" "contrib/framework_manet/doc/step1a-reference/$f"; done
(cd resulls_framwork && sha256sum -c ../contrib/framework_manet/doc/step1a-reference/link_calibration.sha256)
```

Les sorties de référence du conteneur sont dans `doc/step1a-reference/`.
L'environnement de la machine cible étant identique au conteneur (gcc
13.3.0, GSL OFF, int128, empreinte RNG identique), les six CSV doivent être
**identiques au bit près**. Un écart signalerait une non-reproductibilité
inter-machines à diagnostiquer avant STEP 1b.

### Colonnes de `link_calibration_<config>.csv`

| colonne | unité | définition |
|---------|-------|------------|
| `profile`, `maxRange_m`, `pathLossExponent`, `referenceLoss_dB`, `txPower_dBm` | — | configuration de propagation et de puissance |
| `preambleDetection`, `preambleMinRssi_dBm`, `nonUnicastMode`, `dataMode` | — | points D-05 et D-06 |
| `seed`, `run` | — | graine et réplication ns-3 |
| `frameClass` | — | `broadcast` (sans ACK) ou `unicast` (ACK et retransmissions) |
| `payload_bytes` | octet | charge utile au-dessus de LLC : 52 (RREQ IP) ou 540 (donnée IP) |
| `distance_m` | m | distance émetteur–récepteur |
| `predictedRx_dBm` | dBm | puissance reçue prédite par le modèle configuré |
| `meanRxSignal_dBm`, `meanRxNoise_dBm` | dBm | signal et bruit observés par le PHY sur les trames reçues ; `NaN` si aucune |
| `offered`, `delivered` | trame | trames remises au socket / reçues par l'application |
| `prr` | — | `delivered / offered` ; `NaN` si `offered = 0` |
| `prrCi95Low`, `prrCi95High` | — | intervalle de Wilson à 95 % |
| `senderDataTx`, `attemptsPerOffered` | trame, — | émissions PHY de données (retransmissions incluses) et leur ratio à `offered` |
| `receiverAckTx` | trame | ACK émis par le récepteur |
| `observedDataMode`, `observedAckMode` | — | débits PHY réellement observés (`-` si aucun) |

### Critères PASS / FAIL

| Test | PASS | FAIL |
|------|------|------|
| Provenance | `MANIFEST.sha256` : tout `OK` | un fichier `FAILED` |
| Intégrité AODV | tout `OK` | un `FAILED` sur `.cc`/`.h` |
| Compilation | aucune erreur | erreur de compilation, de liaison ou `static_assert` |
| Suites | `framework-manet` 4/4, `framework-manet-radio` 5/5 | un cas `FAIL` |
| Env-check | aucune différence avec la référence | différence hors `GSL` |
| Calibration | 6 × `VERDICT : PASS` ; CSV identiques à la référence | un `FAIL` ; un CSV différent |

## Conventions

- Espace de noms C++ : `ns3::fmanet`.
- Nom des fichiers : `framework-manet-*.{h,cc}` (convention ns-3).
- Unités indiquées dans chaque nom de colonne CSV qui en a une.
- Une métrique indéfinie (division par zéro, aucune observation) vaut `NaN`,
  jamais 0.
- Streams RNG : bloc Wi-Fi à partir de 20 000 (plan D-07).
