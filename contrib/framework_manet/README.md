# framework_manet — baseline AODV instrumentée pour ns-3.48

Module `contrib/` de ns-3.48 destiné à établir une **baseline AODV
reproductible, instrumentée et testable**. Elle sert de référence
expérimentale aux futures extensions de sécurité : blackhole, confiance,
blockchain…, qui ne font **pas** partie de cette phase.

- AODV officiel (`src/aodv/`) utilisé tel quel, **sans modification**. Son
  intégrité est vérifiable, voir plus bas.
- Le développement est incrémental : une étape n'est construite que sur une
  étape validée.
- Les décisions et constats techniques sont consignés dans
  [`doc/DECISIONS.md`](doc/DECISIONS.md) (identifiants F-xx et D-xx).

## Statut des étapes

| Étape | Contenu | Statut |
|-------|---------|--------|
| STEP 0 | Structure du module, contrat d'API ns-3.48, empreinte d'environnement, test de reproductibilité RNG | `IMPLÉMENTÉ — À VÉRIFIER` (compilé et exécuté dans un conteneur de référence ; pas encore sur la machine cible) |
| STEP 1a | Calibration radio statique (portée effective par débit et type de trame) | `PROPOSÉ` |
| STEP 1b | Scénario AODV + Wi-Fi 802.11b ad hoc + RWP + UDP | `PROPOSÉ` — bloqué par D-05 et D-06 (`À CONFIRMER`) |
| STEP 2 | Instrumentation : `traffic_log.csv`, `run_metrics.csv`, `mobility.csv` | `PROPOSÉ` |
| STEP 3 | Énergie | `PROPOSÉ` |
| STEP 4 | `routing_table.txt` + NetAnim `framework_manet.xml` | `PROPOSÉ` |
| STEP 5 | Validation Python et tests de cohérence | `PROPOSÉ` |

## Arborescence (STEP 0)

```text
contrib/framework_manet/
├── CMakeLists.txt                         bibliothèque + dépendances + tests
├── README.md
├── doc/
│   ├── DECISIONS.md                       registre d'audit et de décisions
│   ├── aodv-stock-ns-3.48.sha256          empreinte de src/aodv officiel
│   └── step0-env-check.reference.txt      sortie de référence de STEP 0
├── model/
│   ├── framework-manet-api-contract.h     contrat d'API + empreinte de build
│   └── framework-manet-api-contract.cc
├── examples/
│   ├── CMakeLists.txt
│   └── framework-manet-env-check.cc       programme de validation d'environnement
└── test/
    └── framework-manet-test-suite.cc      suite "framework-manet"
```

Les dossiers `helper/` et `experiments/` n'existent pas encore : ils seront
créés à l'étape qui en aura besoin (voir D-01).

## STEP 0 — procédure

Toutes les commandes s'exécutent depuis la racine de ns-3.48 :

```bash
cd /home/hassen/res/ns-3.48
```

### 1. Installation

Extraire l'archive de livraison **depuis la racine ns-3** : elle contient le
chemin `contrib/framework_manet/`.

```bash
unzip -o framework_manet_step0.zip
ls contrib/framework_manet
```

### 2. Intégrité de l'AODV officiel

```bash
sha256sum -c contrib/framework_manet/doc/aodv-stock-ns-3.48.sha256
```

Chaque ligne doit afficher `OK`. L'empreinte a été calculée sur l'archive du
tag `ns-3.48` de `gitlab.com/nsnam/ns-3-dev`. Un écart sur un fichier `.cc`
ou `.h` signifie que `src/aodv/` a été modifié localement : c'est à traiter
avant toute expérience. Un écart limité à la documentation (`doc/`)
relèverait plutôt d'une différence d'empaquetage entre l'archive GitLab et
l'archive de publication.

### 3. Configuration

Conserver d'abord la configuration actuelle, pour pouvoir la restaurer :

```bash
./ns3 show config > step0_config_before.txt 2>&1
```

Puis :

```bash
./ns3 configure --build-profile=default --enable-examples --enable-tests
```

Dans la sortie, `framework_manet` doit figurer dans *Modules configured to be
built*. Si une configuration antérieure utilisait `--enable-modules=…`,
ajoutez-y `framework_manet` ; `netanim` a alors aussi besoin de
`point-to-point-layout` pour compiler ses propres tests. Notez la ligne
`GNU Scientific Library (GSL)` (voir F-05).

### 4. Compilation

```bash
./ns3 build
```

### 5. Tests

```bash
./test.py --suite=framework-manet --verbose-failed
./ns3 run "test-runner --suite=framework-manet --verbose"
```

Le contrôle négatif affiche volontairement
`Deprecation warning for name ns3::BasicEnergySource; use ns3::energy::BasicEnergySource instead`.

### 6. Validation de l'environnement

```bash
./ns3 run framework-manet-env-check 2>&1 | tee step0_env_check.txt
```

### Comparaison avec le conteneur de référence

`doc/step0-env-check.reference.txt` est la sortie obtenue dans le conteneur
de référence : ns-3.48 compilé depuis l'archive GitLab, gcc 13.3.0, profil
`default`, **GSL OFF**. Pour comparer :

```bash
diff <(grep -v -E "executable|compiler" step0_env_check.txt) \
     <(grep -v -E "executable|compiler" contrib/framework_manet/doc/step0-env-check.reference.txt)
```

Hors lignes `executable` et `compiler`, un écart ne peut venir que de trois
endroits :

- la ligne `GSL` : écart attendu si GSL est installé ; à consigner, voir F-05 ;
- `__cplusplus` : standard C++ du compilateur ;
- les empreintes `[rng]` : elles doivent être **identiques** au bit près.
  Sinon, la reproductibilité inter-machines n'est pas acquise et il faut
  enquêter avant STEP 1.

### Critères PASS / FAIL

| Test | PASS | FAIL |
|------|------|------|
| Intégrité AODV | toutes les lignes `OK` | au moins un `FAILED` sur `.cc`/`.h` |
| Configuration | `framework_manet` listé | module absent ou désactivé |
| Compilation | `./ns3 build` sans erreur | erreur de compilation, de liaison ou `static_assert` |
| Suite `framework-manet` | 4 cas `PASS` | au moins un cas `FAIL` |
| `framework-manet-env-check` | `violations : 0`, `VERDICT : PASS`, code de retour 0 | toute autre sortie |

## Conventions

- Espace de noms C++ : `ns3::fmanet`.
- Nom des fichiers : `framework-manet-*.{h,cc}` (convention ns-3).
- Unités : secondes, mètres, m/s, dBm, dB, joules, bit/s ; l'unité est
  indiquée dans chaque nom de colonne CSV qui en a une.
- Une métrique indéfinie (division par zéro) vaut `NaN`, jamais 0.
