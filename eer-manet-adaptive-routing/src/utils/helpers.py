import json, logging, time, os
import numpy as np
from typing import Dict

def get_logger(name):
    logger = logging.getLogger(name)
    if not logger.handlers:
        h = logging.StreamHandler()
        h.setFormatter(logging.Formatter('[%(asctime)s] [%(levelname)-5s] %(message)s', datefmt='%H:%M:%S'))
        logger.addHandler(h)
        logger.setLevel(logging.INFO)
    return logger

class Timer:
    def __init__(self):
        self._start = time.time()
    def elapsed(self):
        return time.time() - self._start
    def elapsed_str(self):
        e = self.elapsed()
        return f"{e:.3f}s" if e < 60 else f"{e/60:.1f}min"

def save_results(aggregated, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    def convert(obj):
        if isinstance(obj, np.ndarray): return obj.tolist()
        if isinstance(obj, (np.float32, np.float64, np.floating)): obj = float(obj)
        if isinstance(obj, float) and obj != obj: return None   # NaN -> null (valid JSON)
        if isinstance(obj, (np.int32, np.int64, np.integer)): return int(obj)
        return obj
    def deep_convert(d):
        if isinstance(d, dict): return {str(k): deep_convert(v) for k, v in d.items()}
        if isinstance(d, list): return [deep_convert(i) for i in d]
        return convert(d)
    with open(path, 'w') as f:
        json.dump(deep_convert(aggregated), f, indent=2)
    print(f"[Saved] Results → {path}")

def plot_results(aggregated, output_dir):
    """One figure per metric: mean ± 95 % CI per variant over eval times."""
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
    except ImportError:
        print("[Skip] matplotlib not available")
        return
    os.makedirs(output_dir, exist_ok=True)
    metrics_to_plot = [
        ('pdr',                   'Packet Delivery Ratio',       'PDR'),
        ('throughput_kbps',       'Throughput',                  'kbps'),
        ('delay_proxy_ms',        'Delay proxy (hops x HOP_DELAY)', 'ms'),
        ('energy_J',              'Energy consumed (data packets)', 'J'),
        ('energy_eff_kbit_per_J', 'Energy efficiency',           'kbit/J'),
        ('detection_rate',        'Detection Rate',              '%'),
        ('false_positive_rate',   'False Positive Rate',         '%'),
        ('adaptive_threshold',    'Trust threshold',             'threshold'),
    ]
    for key, title, ylabel in metrics_to_plot:
        fig, ax = plt.subplots(figsize=(8, 5))
        for variant, by_t in aggregated.items():
            times = sorted(by_t)
            means = [by_t[t][key]['mean'] for t in times]
            cis = [0.0 if np.isnan(by_t[t][key]['ci95']) else by_t[t][key]['ci95']
                   for t in times]
            if all(np.isnan(means)):
                continue
            ax.errorbar(times, means, yerr=cis, marker='o', capsize=4, label=variant)
        ax.set_xlabel('Simulation Time (s)')
        ax.set_ylabel(ylabel)
        ax.set_title(f'{title} (mean ± 95 % CI)')
        ax.legend(fontsize=8); ax.grid(True, alpha=0.3)
        fig.tight_layout()
        fname = f"{output_dir}/{key}.png"
        fig.savefig(fname, dpi=150); plt.close(fig)
        print(f"[Plot] Saved → {fname}")
