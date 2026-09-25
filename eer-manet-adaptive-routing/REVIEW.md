# Revue — eer-manet-adaptive-routing (AT-AEES-MANET)

Les numéros de ligne renvoient à la version reçue (commit « Import eer-manet-adaptive-routing as received »).

## Méthode

- Lecture de tous les fichiers : README, config, main, `src/*`, `results/simulation_results.json`.
- Exécution sous Python 3.11.15, numpy, scipy.
  - Commande documentée `python main.py --quick`.
  - Mode quick avec `PYTHONPATH=src`, lancé deux fois pour vérifier le déterminisme.
  - Mode complet (10 runs, 100 nœuds), pour comparaison avec le JSON fourni.
  - Sondes instrumentées : oscillation, délai en fonction des sauts, énergie, bruit de la fitness.
- Non fourni : spécification mathématique, article de base, `results/figures/`.
- Ni ns-3 ni protocole : le README l'annonce (« No NS-3 dependency »). L'axe « fidélité protocolaire » se limite donc à constater l'absence de protocole.

## Verdict : non fiable

Les chiffres mis en avant par le README ne sont pas produits par le code : ils sont écrits en dur dans `main.py`. Ils sont aussi incompatibles avec les bornes du modèle et contredits par le fichier de résultats du dépôt. Les tests statistiques sont faits contre des échantillons synthétiques. Et le simulateur n'applique pas aux paquets de données deux des quatre attaques qu'il déclare.

## Bloquants

| # | Constat | Preuve | Effet | Correction |
|---|---|---|---|---|
| B1 | Les chiffres « résultats » et « ablation » du README sont des constantes | `main.py:138-158` ; `README.md:3,11-18,26-32` | TD 88,50 %, TFP 2,8 %, 1298,7 kbps, 0,09 ms, on-off 85,3 %, collusion 83,7 % : aucune exécution ne les produit. Le JSON du dépôt donne, à t = 40 s : TD 87,0 %, TFP 0,0 %, 208,8 kbps, 10,41 ms, on-off 48,3 %, collusion 90,0 %. Borne : débit ≤ 20 flux × 4 paquets/s × 4096 bits × 41/40 = 335,9 kbps, et délai ≥ 3 ms dès qu'il y a un saut. 1298,7 kbps et 0,09 ms sont hors du domaine atteignable. | Tableau calculé depuis des exécutions de variantes |
| B2 | Test t « apparié » contre des échantillons tirés de N(valeur publiée, 5 %) avec la graine 42 ; p = 0,05 par défaut si les données manquent | `metrics.py:123-166` (`:155`, `:139`, `:159`) | Les p-values (« p < 0,001 ») ne mesurent rien | Tests appariés entre variantes, sur les mêmes graines |
| B3 | Pourcentages d'amélioration calculés contre des valeurs publiées, issues d'autres simulateurs et dans d'autres unités | `metrics.py:17-39, 86-120` | Le mode quick affiche « Energy Eff +11174 % », « Energy Cons −141738 % » | Supprimé ; références `no_defense` / `no_attack` exécutées dans le même simulateur |
| B4 | Sur les données, seuls le blackhole et le grayhole jettent des paquets : `hops % 20 >= 10` n'est jamais vrai (hops ≤ 9), et la collusion n'a aucune branche | `manet_env.py:323-330` (vérifié à l'exécution) | Le PDR, le débit et l'énergie ne reflètent ni l'on-off ni la collusion. La confiance observe un autre modèle de comportement (`adaptive_trust.py:371-401`) | Un modèle unique (`simulation/behaviour.py`) |
| B5 | Le prochain saut est le voisin de meilleur score, sans tenir compte de la destination. Le « succès » est déclaré dès qu'on arrive à moins de 250 m de la destination | `manet_env.py:321, 336-341` | Le PDR mesure la probabilité qu'une marche biaisée atteigne un disque, pas du routage | Glouton géographique (progrès obligatoire), livraison par un saut réel vers la destination |
| B6 | IDT = DT propre du voisin, pas son avis sur la cible ; le gestionnaire de confiance reçoit la liste des attaquants et génère lui-même le comportement | `adaptive_trust.py:217`, `:60-65`, `:371-401` ; `manet_env.py:55-56` | Un blackhole garde RT ≈ β × l'honnêteté de ses voisins | Gestionnaire qui ne consomme que des observations (voir la revue précédente) |

## Majeurs

| # | Constat | Preuve | Effet | Correction |
|---|---|---|---|---|
| M1 | La commande documentée échoue : `ModuleNotFoundError: simulation` | `main.py:1,27` (exécuté) | Le JSON du dépôt n'a pas pu être produit par cette commande | Ajouter `src/` au chemin |
| M2 | Énergie découplée du routage : chaque nœud paie 4 paquets/s d'émission ; `E_AMP` n'est jamais utilisé | `manet_env.py:230-239` ; `config.py:18` | 0,31 % de l'énergie est consommée en 41 s, identiquement quelle que soit la route : l'« efficacité énergétique » n'est pas évaluable | Modèle radio de premier ordre, par saut |
| M3 | Le délai vaut exactement 3 ms × nombre de sauts | `manet_env.py:298, 339` ; mesuré : `delay_ms` = 3 × sauts moyens sur 6 graines | Ce n'est pas un délai | Rebaptisé `delay_proxy_ms`, paramètre `HOP_DELAY` |
| M4 | Débit = PDR × charge fixe, et divisé par t au lieu de t + 1 s | `manet_env.py:285-298, 392` | Débit redondant avec le PDR, biaisé de +2,5 à +10 % | Durée écoulée exacte |
| M5 | HLOA optimise les poids sur le scénario de test ; fitness bruitée (mêmes poids → 0,675 à 0,714) ; termes TD/TFP calculés sur la vérité terrain | `manet_env.py:137, 344-381` | Biais optimiste (entraînement = test), sélection sur le bruit | Scénario d'entraînement séparé, fitness déterministe |
| M6 | Les nœuds marqués ne servent plus ni de source ni de destination | `manet_env.py:283-289` | Un faux positif fait disparaître ses paquets du dénominateur du PDR | Flux fixes entre nœuds honnêtes |
| M7 | L'amorçage court de 0 à 8 s, puis la simulation repart à t = 0 | `manet_env.py:86, 94, 149` | Les nœuds on-off sont toujours en phase « bonne » pendant l'amorçage (t < 10 s) | Amorçage sur [−8, 0) |
| M8 | Détecteur d'oscillation quasi inerte : `OSC_THRESHOLD` = 0,12 pour un score maximal observé de 0,127 | `config.py:56` ; sondes : 2 nœuds sur 19 signalés `on_off` | La détection des on-off vient de la RT basse, pas du détecteur | Signalé ; l'ablation le montre |
| M9 | Types d'attaque tirés au hasard (de 1 à 6 blackholes sur 10) alors que la config dit « proportionnellement » ; TD par attaque = 0 quand le type est absent | `manet_env.py:44` ; `config.py:30` ; `manet_env.py:409` | Moyennes par attaque biaisées vers le bas | Plus fort reste ; NaN quand le type est absent |
| M10 | Le JSON fourni n'est pas reproductible à l'identique ; générateur aléatoire partagé entre mobilité, confiance, FCM, HLOA et routage | Réexécution : run 2 identique ; runs 1, 3, 4 : même graine, PDR différent (0,598 contre 0,634 ; 0,582 contre 0,649) | Cause non établie : le code est déterministe sur cette machine (quick lancé ×4), donc le JSON vient probablement d'une autre version | Un flux aléatoire par composant |
| M11 | Aucune référence sans attaque ni sans défense ; l'écart-type est donné sans intervalle de confiance ; 10 attaquants seulement, donc un TD par pas de 10 points | `metrics.py:51-83` | Aucune conclusion causale possible | Variantes `no_attack`, `no_defense`, ablations ; IC 95 % |

## Mineurs

- Le docstring de la formule de routage (`at_efiagnn.py:166-168`) ne correspond pas au code (`:186-189`).
- `composite = 0` pour les nœuds suspects (`:192`), alors que les scores peuvent être négatifs.
- Les poids initiaux × 0,1 donnent une sortie du GNN entièrement nulle (ReLU morte) ; sans effet, puisque HLOA réinitialise les poids.
- HLOA évalue deux fois le meilleur candidat (`hloa.py:64, 87`).
- Les têtes de cluster sont calculées mais inutilisées (`manet_env.py:115`) ; FCM n'est ajusté qu'une fois à t = 0 ; `trusted_mask` n'est pas utilisé (`fcmvc.py:13`).
- `GNN_LAYERS` est déclaré mais inutilisé (`config.py:67`) ; la « link quality » est en fait le degré / 10.
- Poids de routage « adaptatifs » quasi constants : α entre 0,4034 et 0,4041 dans le JSON.
- Le README promet `results/figures/` (7 figures) : dossier absent. Le code écrit dans `outputs/`.
- La composante « False Positive Reduction » (README:31) n'existe pas dans le code.
- La « collusion » ne produit aucune fausse recommandation.
- Durée : le README annonce « ~20 min » ; la version reçue mesure environ 4 à 5 min par run, soit environ 45 min, en concurrence avec d'autres processus.

## Projet corrigé : résultats

Voir le README (tableau à t = 40 s, 10 runs, IC 95 %). Points saillants, en différences appariées par rapport à `full` :

- `no_defense` : PDR −0,319 ± 0,046 (p = 8×10⁻⁸). La défense restaure le PDR sans attaque (`no_attack` : 0,813 ± 0,029).
- `fixed_threshold` : TD −9,0 ± 5,3 points (p = 0,004), entièrement sur les nœuds on-off.
- `no_sliding_window`, `no_onoff_detector`, `no_collusion_filter` : aucune différence mesurable.
- TD = 100 % et TFP = 0 % : effet plafond. Ce scénario ne discrimine pas les variantes de confiance.
