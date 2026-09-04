# Compilation et exécution — MTC-AODV sur ns-3.48

Instructions pour un poste où ns-3.48 est déjà installé, par exemple
`/home/hassen/res/ns-3.48`.

## 1. Prérequis

- ns-3.48 compilable (g++ ≥ 11, CMake ≥ 3.13, ninja ou make)
- Python 3.9 ou plus récent

```bash
python3 -m pip install -r experiments/requirements.txt
```

## 2. Rattacher le module à votre arbre ns-3

Le module vit dans ce dépôt ; ns-3 le voit par lien symbolique. Aucune copie n'est
faite, il n'y a donc jamais deux versions du code.

```bash
cd /chemin/vers/mtcaodv                 # racine de ce paquet
export NS3_DIR=/home/hassen/res/ns-3.48 # votre arbre ns-3.48

scripts/fetch_ns3.sh
```

Le script détecte que l'arbre existe déjà, ne télécharge rien, et se contente de créer :

- `$NS3_DIR/contrib/mtcaodv` → `<paquet>/contrib/mtcaodv`
- `$NS3_DIR/scratch/mtcaodv-manet-scenario.cc` → l'exemple correspondant
- `$NS3_DIR/scratch/mtcaodv-range-probe.cc` → la sonde de portée

> Les programmes de scénario passent par `scratch/` plutôt que par `--enable-examples`,
> qui compilerait aussi la centaine d'exemples des modules wifi et internet.

**Le fork AODV est déjà présent dans le paquet.** Ne relancez `scripts/fork_aodv.sh` que
si vous voulez le régénérer depuis vos sources (`FORCE=1 scripts/fork_aodv.sh`).

## 3. Compiler

```bash
scripts/build.sh
```

Équivaut à :

```bash
cd $NS3_DIR
./ns3 configure --build-profile=default --enable-tests --disable-examples \
  --enable-modules=mtcaodv,aodv,internet,wifi,mobility,applications,energy,flow-monitor,stats,propagation,internet-apps
./ns3 build
```

Le profil `default` conserve les `NS_ASSERT` : les invariants numériques de la
spécification (§20.1) sont donc vérifiés à l'exécution. Le profil `optimized`
(`PROFILE=optimized scripts/build.sh`) les désactive et ne doit servir qu'aux campagnes,
après validation.

## 4. Vérifier l'installation

```bash
scripts/run_tests.sh
```

Attendu : suite ns-3 `mtcaodv-attack` **10/10 PASS**, puis **20/20** tests Python et
`CONFIGURATION VALIDE`.

## 5. Première simulation (≈ 30 s)

Profil grille, 25 nœuds, 60 s — le PDR de référence sans attaque vaut **1,000** :

```bash
cd $NS3_DIR
./ns3 run "mtcaodv-manet-scenario \
  --protocolVariant=A --seed=1001 --nodeCount=25 \
  --areaWidth=100 --areaHeight=100 \
  --mobilityProfile=grid --propagationModel=range --radioRange=36 \
  --flowCount=4 --packetSize=512 --packetRate=4 \
  --trafficStart=2 --warmupEnd=5 --trafficStop=60 --drainTime=5 \
  --attackerRatio=0.0 --outputDir=/tmp/mtc --runLabel=grid25"
```

Sortie attendue :

```
variante=A seed=1001 r_a=0 N_A=0
  appTx=880 appRx=880 PDR=1
  par flux : 0:220/220(1.00) 1:220/220(1.00) 2:220/220(1.00) 3:220/220(1.00)
  topologie : degré moyen=5.76 graphe connexe=1.00
  connectivité par flux : 0:1.00/4.0sauts 1:1.00/4.0sauts 2:1.00/4.0sauts 3:1.00/4.0sauts
```

Deux fichiers sont produits dans `--outputDir` : `<runLabel>_metrics.csv` et
`<runLabel>_manifest.json`.

## 6. Variante MANET mobile

```bash
./ns3 run "mtcaodv-manet-scenario \
  --protocolVariant=A --seed=1001 --nodeCount=25 \
  --areaWidth=700 --areaHeight=700 --mobilityProfile=rwp \
  --propagationModel=logdistance --pathLossExponent=2.2 \
  --flowCount=4 --trafficStart=10 --warmupEnd=15 --trafficStop=60 --drainTime=5 \
  --attackerRatio=0.0 --outputDir=/tmp/mtc --runLabel=mobile25"
```

## 7. Sonde de portée radio

À relancer si vous changez la puissance ou le modèle de propagation : elle donne la
portée utile réelle, dont dépend tout le dimensionnement.

```bash
./ns3 run "mtcaodv-range-probe --pathLossExponent=2.2 --maximumDistance=400 --distanceStep=25"
```

## 8. Campagne

```bash
cd /chemin/vers/mtcaodv/experiments
python3 run_campaign.py --config configs/grid25.json --outputDir ../results/grid25 --jobs 4
```

`--dryRun` affiche les commandes sans rien exécuter. En fin de campagne, le script
vérifie que les variantes d'un même bloc partagent le même `scenarioHash` : c'est le
contrôle mécanique de l'appariement (invariant 20.4.4). Une campagne dont un bloc est
mal apparié est déclarée **INVALIDE** et ne doit pas être agrégée.

## Options principales

| Option | Effet |
|---|---|
| `--protocolVariant` | `A` (AODV standard) ou `B`/`C0`/`C`/`D` (fork) |
| `--mobilityProfile` | `rwp` (RWP stationnaire) ou `grid` (grille immobile) |
| `--propagationModel` | `logdistance` (liens marginaux) ou `range` (disque dur) |
| `--radioRange` | portée du disque dur, en m |
| `--pathLossExponent` | exposant de perte, modèle `logdistance` |
| `--enableHello` / `--helloInterval` | détection de rupture de lien par AODV |
| `--attackerRatio` | ratio d'attaquants ; \(N_A=\lfloor r_a N + 0{,}5\rfloor\) |
| `--nodeCount`, `--areaWidth`, `--flowCount`, `--packetRate` | dimensionnement |
| `--seed`, `--run` | reproductibilité |

`./ns3 run "mtcaodv-manet-scenario --PrintHelp"` liste tout.

## En cas d'erreur

Renvoyez-moi la sortie complète de la commande qui échoue, y compris les lignes
précédant le premier `error:`. Pour une erreur de compilation, les 40 premières lignes
suffisent en général.
