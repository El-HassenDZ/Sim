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
        if isinstance(obj, (np.float32, np.float64, np.floating)): return float(obj)
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
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
    except ImportError:
        print("[Skip] matplotlib not available")
        return
    os.makedirs(output_dir, exist_ok=True)
    times = sorted(aggregated.keys())
    metrics_to_plot = [
        ('energy_efficiency',   'Energy Efficiency',    'EE Score',  True),
        ('delay_ms',            'End-to-End Delay',     'Delay (ms)',False),
        ('throughput_kbps',     'Throughput',           'kbps',      True),
        ('energy_mJ',           'Energy Consumption',   'mJ',        False),
        ('detection_rate',      'Detection Rate',       '(%)',        True),
        ('false_positive_rate', 'False Positive Rate',  '(%)',        False),
    ]
    for key, title, ylabel, _ in metrics_to_plot:
        fig, ax = plt.subplots(figsize=(8, 5))
        means = [aggregated[t].get(key, {}).get('mean', 0)
                 if isinstance(aggregated[t].get(key), dict)
                 else aggregated[t].get(key, 0) for t in times]
        stds  = [aggregated[t].get(key, {}).get('std', 0)
                 if isinstance(aggregated[t].get(key), dict)
                 else 0 for t in times]
        ax.errorbar(times, means, yerr=stds, marker='o', linewidth=2,
                    capsize=5, color='#2196F3', label='AT-AEES-MANET')
        ax.set_xlabel('Simulation Time (s)')
        ax.set_ylabel(ylabel)
        ax.set_title(f'{title} over Time')
        ax.legend(); ax.grid(True, alpha=0.3)
        fig.tight_layout()
        fname = f"{output_dir}/{key}.png"
        fig.savefig(fname, dpi=150); plt.close(fig)
        print(f"[Plot] Saved → {fname}")
    # Adaptive threshold plot
    if all(isinstance(aggregated[t].get('adaptive_threshold'), dict) for t in times):
        fig, ax = plt.subplots(figsize=(8, 5))
        thresh = [aggregated[t]['adaptive_threshold']['mean'] for t in times]
        ax.plot(times, thresh, marker='s', color='#E91E63', linewidth=2, label='Adaptive Threshold')
        ax.axhline(0.50, color='gray', linestyle='--', label='Static 0.50 (base paper)')
        ax.set_xlabel('Simulation Time (s)'); ax.set_ylabel('Trust Threshold')
        ax.set_title('Adaptive vs Static Trust Threshold')
        ax.legend(); ax.grid(True, alpha=0.3); fig.tight_layout()
        fname = f"{output_dir}/adaptive_threshold.png"
        fig.savefig(fname, dpi=150); plt.close(fig)
        print(f"[Plot] Saved → {fname}")
