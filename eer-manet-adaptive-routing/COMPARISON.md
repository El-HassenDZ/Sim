# Comparaison — code original `eer-manet-adaptive-routing` exécuté seul

Objet : vérifier si le fichier `results/simulation_results.json` livré dans le dépôt
est reproductible, et si l'exécution concurrente (plusieurs simulations en parallèle)
expliquait les écarts. Environnement : Python 3.11.15, numpy 2.4.6, scipy 1.17.1.

Trois sources comparées :
- **JSON dépôt** : `results/simulation_results.json` fourni avec le code, tel quel.
- **Rerun concurrent** : `python main.py` relancé pendant que d'autres simulations tournaient.
- **Rerun solo** : `python main.py` relancé seul (aucun autre calcul), 43,8 min.

Toutes les valeurs à t = 40 s, graine 42, 10 runs, 100 nœuds. Le code n'a **pas** été modifié.

## Moyennes

| Métrique (t=40 s) | JSON dépôt | Rerun concurrent | Rerun solo |
|---|---|---|---|
| PDR | 0.622 | 0.626 | 0.626 |
| Débit (kbps) | 208.773 | 210.125 | 210.125 |
| Délai (ms) | 10.415 | 10.955 | 10.955 |
| Détection (%) | 87.0 | 85.0 | 85.0 |
| Faux positifs (%) | 0.0 | 0.0 | 0.0 |
| Énergie (mJ) | 5007.9 | 5012.9 | 5012.9 |
| Seuil adaptatif | 0.5706 | 0.5701 | 0.5701 |
| Confiance moy. | 0.8970 | 0.8991 | 0.8991 |

## Détection par type d'attaque (%)

| Attaque | JSON dépôt | Rerun solo |
|---|---|---|
| blackhole | 100.0 | 100.0 |
| grayhole | 89.0 | 78.0 |
| on_off | 48.3 | 51.7 |
| collusion | 90.0 | 90.0 |

## Détail par run — PDR à t = 40 s

| Run | JSON dépôt | Rerun concurrent | Rerun solo | solo = concurrent ? | solo = dépôt ? |
|---|---|---|---|---|---|
| 1 | 0.634 | 0.598 | 0.598 | oui | **non** |
| 2 | 0.644 | 0.644 | 0.644 | oui | oui |
| 3 | 0.649 | 0.582 | 0.582 | oui | **non** |
| 4 | 0.628 | 0.598 | 0.598 | oui | **non** |
| 5 | 0.707 | 0.678 | 0.678 | oui | **non** |
| 6 | 0.550 | 0.601 | 0.601 | oui | **non** |
| 7 | 0.498 | 0.511 | 0.511 | oui | **non** |
| 8 | 0.613 | 0.661 | 0.661 | oui | **non** |
| 9 | 0.635 | 0.759 | 0.759 | oui | **non** |
| 10 | 0.657 | 0.626 | 0.626 | oui | **non** |

## Détail par run — détection (%)

| Run | JSON dépôt | Rerun solo |
|---|---|---|
| 1 | 70 | 90 |
| 2 | 90 | 90 |
| 3 | 90 | 90 |
| 4 | 100 | 90 |
| 5 | 100 | 100 |
| 6 | 80 | 80 |
| 7 | 90 | 60 |
| 8 | 70 | 90 |
| 9 | 90 | 60 |
| 10 | 90 | 100 |

## Conclusions

1. **Le code est parfaitement déterministe.** Le rerun solo est identique au rerun
   concurrent sur 10/10 runs (valeurs par run à 1e-9 près, pas seulement en moyenne).
   La charge CPU / l'exécution concurrente **n'a aucun effet** sur les résultats.
2. **Le JSON du dépôt n'est pas reproductible par ce code.** Le rerun solo ne
   coïncide avec le JSON livré que sur 1/10 runs. Comme le code est déterministe,
   cet écart ne peut pas venir du hasard d'exécution : le fichier livré a été produit
   par une **version différente** du code (paramètres ou logique), non par celui du dépôt.
3. **Les moyennes restent proches** (PDR 0,622 contre 0,626 ; détection 87 contre 85 %),
   donc le JSON vient bien de ce projet ou d'une variante voisine — mais il ne doit pas
   être cité comme la sortie exacte du code présent tant que la version d'origine n'est
   pas retrouvée.

_Note : ces chiffres décrivent le comportement du code original ; ils ne valident pas
sa méthodologie. Voir REVIEW.md pour les défauts de fond (attaques on-off/collusion non
appliquées aux données, routage sans destination, résultats du README codés en dur)._
