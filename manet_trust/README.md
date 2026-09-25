# adaptive_trust — version corrigée

- `original/trust/adaptive_trust.py` : fichier reçu, inchangé (référence).
- `trust/adaptive_trust.py` : gestionnaire corrigé ; ne reçoit plus la liste des attaquants.
- `trust/behaviour.py` : modèle d'attaque (vérité terrain), un flux RNG par nœud, fausses recommandations des colluders.
- `trust/evaluation.py` : TPR, FPR, précision, F1, IC 95 % (Student).
- `config.py` : **valeurs supposées** — le `config.py` du projet n'a pas été fourni. À remplacer.
- `run_scenario.py` : scénario abstrait à un saut (pas de routage, pas de ns-3) comparant sans défense / original / corrigé.
- `tests/` : `python3 -m pytest -q tests`

Changement d'API : `record_interaction(observer, target, t)` (qui tirait lui-même le comportement)
devient `record_observation(observer, target, t, forwarded)` ; `get_detection_rate()` passe dans
`evaluation.detection_metrics()`. `update_all(t, positions, energies)` garde sa signature, `energies` est ignoré.
