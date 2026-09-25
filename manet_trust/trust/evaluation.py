"""
trust/evaluation.py
===================
Metrics that need ground truth. Moved out of the trust manager
(get_detection_rate), and completed with the false-positive rate: a
detection rate alone is maximised by flagging every node.
"""

import math
from typing import Dict, Iterable, Sequence

import numpy as np

# Two-sided 95 % Student t quantiles, df = 1..30
_T95 = [12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262,
        2.228, 2.201, 2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101,
        2.093, 2.086, 2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052,
        2.048, 2.045, 2.042]


def detection_metrics(flagged: Iterable[int], malicious: Iterable[int],
                      n_nodes: int) -> Dict[str, float]:
    """Confusion counts and rates; the positive class is 'malicious'."""
    flagged, malicious = set(flagged), set(malicious)
    honest = set(range(n_nodes)) - malicious
    tp = len(flagged & malicious)
    fp = len(flagged & honest)
    fn = len(malicious - flagged)
    tn = len(honest - flagged)
    tpr = tp / len(malicious) if malicious else float('nan')
    fpr = fp / len(honest) if honest else float('nan')
    precision = tp / (tp + fp) if (tp + fp) else float('nan')
    f1 = (2 * precision * tpr / (precision + tpr)
          if tp and not math.isnan(precision) else 0.0)
    return dict(tp=tp, fp=fp, fn=fn, tn=tn, tpr=tpr, fpr=fpr,
                precision=precision, f1=f1)


def mean_ci95(values: Sequence[float]) -> Dict[str, float]:
    """Mean and 95 % Student-t half-width over independent runs."""
    x = np.asarray(values, dtype=float)
    n = x.size
    if n < 2:
        return dict(mean=float(x.mean()) if n else float('nan'),
                    half_width=float('nan'), n=n)
    t = _T95[n - 2] if n - 1 <= len(_T95) else 1.96
    return dict(mean=float(x.mean()),
                half_width=float(t * x.std(ddof=1) / math.sqrt(n)), n=n)
