# Licences — répartition par répertoire

Ce dépôt agrège des sources de provenances et de licences différentes. Le fichier
`LICENSE` à la racine (MIT, Microsoft) couvre le template Node.js d'origine ; **il ne
couvre pas** le contenu de `mtcaodv/`.

| Chemin | Origine | Licence |
|---|---|---|
| `/` (hors `mtcaodv/`) | template *Haikus for Codespaces* | MIT (Microsoft Corporation) |
| `mtcaodv/contrib/mtcaodv/model/mtc-aodv-{dpd,id-cache,neighbor,packet,routing-protocol,rqueue,rtable}.{h,cc}` | **fork** de `src/aodv` de ns-3.48 | **GPL-2.0-only** — Copyright (c) 2009 IITP RAS |
| `mtcaodv/contrib/mtcaodv/helper/mtc-aodv-helper.{h,cc}` | **fork** de `src/aodv/helper` de ns-3.48 | **GPL-2.0-only** — Copyright (c) 2009 IITP RAS |
| `mtcaodv/contrib/mtcaodv/**` (autres fichiers) | code original MTC-AODV | GPL-2.0-only (voir ci-dessous) |
| `mtcaodv/experiments/**`, `mtcaodv/scripts/**`, `mtcaodv/docs/**` | code et documentation originaux | GPL-2.0-only |

## Pourquoi GPL-2.0-only pour le code original du module

Le module `contrib/mtcaodv` est lié statiquement au fork AODV, lui-même sous
GPL-2.0-only. Publier les composants originaux sous une licence permissive tout en les
liant à du code GPL créerait une ambiguïté juridique inutile. Le module entier est donc
distribué sous GPL-2.0-only, ce qui est également la licence de ns-3 lui-même.

## Marquage des modifications (obligation GPL §2a)

Le fichier `contrib/mtcaodv/model/.fork-provenance`, généré par `scripts/fork_aodv.sh`,
enregistre :

- la version amont exacte (`ns-3.48 src/aodv`) ;
- l'empreinte SHA-256 de l'arbre source amont au moment du fork ;
- la date du fork ;
- la nature de la transformation appliquée.

Le fork initial est un **renommage mécanique seul** : aucune logique de routage n'est
modifiée par `fork_aodv.sh`. Les modifications fonctionnelles (hooks A2.3, A2.4, A3.1,
A3.2, A6) sont introduites par des commits distincts et ultérieurs, de sorte que
`git log` distingue sans ambiguïté « fork » et « modification ».

## Ce qui n'est pas modifié

Conformément à l'invariant 20.3.1 de la spécification, le module standard
`src/aodv/` de l'arbre ns-3.48 **n'est jamais modifié**. Il sert de baseline A.
L'arbre ns-3.48 lui-même n'est pas versionné dans ce dépôt ; `scripts/fetch_ns3.sh` le
reconstruit à l'identique depuis le tag officiel.
