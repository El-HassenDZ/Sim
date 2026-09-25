# ============================================================
#  AT-AEES-MANET Configuration
#  Adaptive Trust-Aware Energy-Efficient Secure Routing
#  with Attack-Resilient Trust Evaluation
# ============================================================

# ── Simulation Parameters ────────────────────────────────────
N_NODES         = 100
SIM_TIME        = 40.0          # seconds
N_RUNS          = 10
AREA_SIZE       = 1000.0        # metres (square)
SEED            = 42

# ── Energy Model ─────────────────────────────────────────────
E_INITIAL       = 15.1          # Joules per node
E_TX            = 50e-9         # J/bit transmit
E_RX            = 50e-9         # J/bit receive
E_AMP           = 100e-12       # J/bit/m² amplification
PKT_SIZE        = 512           # bytes
DATA_RATE       = 4             # packets/sec

# ── Mobility ─────────────────────────────────────────────────
MIN_SPEED       = 1.0           # m/s
MAX_SPEED       = 10.0          # m/s
PAUSE_TIME      = 5.0           # seconds
TX_RANGE        = 250.0         # metres

# ── Attack Configuration ──────────────────────────────────────
MALICIOUS_RATIO = 0.10          # 10% malicious nodes
# Attack types assigned proportionally across malicious nodes
ATTACK_TYPES    = {
    'blackhole'  : 0.30,        # drops all packets
    'grayhole'   : 0.25,        # drops packets selectively
    'on_off'     : 0.25,        # alternates good/bad behaviour
    'collusion'  : 0.20,        # cooperates with other malicious nodes
}

# ── Sliding Window Trust ──────────────────────────────────────
WINDOW_SIZE     = 10            # interactions per window
N_WINDOWS       = 5             # number of windows kept in history
DECAY_FACTOR    = 0.92          # exponential decay weight for older windows
TRUST_ALPHA     = 0.70          # weight for direct trust vs indirect trust
TRUST_BETA      = 0.30          # weight for indirect trust
TRUST_THRESHOLD_BASE = 0.50     # base threshold (adapts dynamically)

# ── Adaptive Trust Threshold ──────────────────────────────────
THRESH_MOBILITY_W   = 0.30      # mobility contribution to threshold
THRESH_DENSITY_W    = 0.20      # density contribution
THRESH_VARIANCE_W   = 0.25      # trust variance contribution
THRESH_LOSS_W       = 0.25      # packet loss contribution
THRESH_MIN          = 0.35      # floor (never go below this)
THRESH_MAX          = 0.75      # ceiling (never exceed this)

# ── On-Off Attack Detection ───────────────────────────────────
OSC_WINDOW      = 12             # window to detect oscillation
OSC_THRESHOLD   = 0.12          # variance threshold to flag oscillation
ISOLATION_TIME  = 20.0          # seconds a suspicious node stays isolated
RE_EVAL_AFTER   = 15.0          # seconds before re-evaluation

# ── FCMVC Clustering ──────────────────────────────────────────
N_CLUSTERS      = 10
FCM_MAX_ITER    = 100
FCM_EPSILON     = 1e-5
FCM_FUZZINESS   = 2.0           # fuzziness exponent m

# ── AT-EFIAGNN Architecture ───────────────────────────────────
GNN_LAYERS      = 3
GNN_HIDDEN      = 128
GNN_INPUT_DIM   = 9             # extended feature vector (vs 6 in base paper)
# Features: x, y, energy, trust_score, trust_stability,
#           cluster_membership, link_quality, attack_suspicion, mobility_speed

# ── HLOA Optimisation ─────────────────────────────────────────
HLOA_POP        = 30
HLOA_MAX_ITER   = 50
HLOA_WEIGHT_DECAY = 0.9

# ── Routing Metric Weights ────────────────────────────────────
ROUTE_ALPHA     = 0.40          # trust weight
ROUTE_BETA      = 0.35          # residual energy weight
ROUTE_GAMMA     = 0.25          # delay penalty weight
# These adapt dynamically based on network conditions

# ── Evaluation Snapshots ──────────────────────────────────────
EVAL_TIMES      = [10.0, 30.0, 40.0]

# ── Baseline Methods (for comparison) ────────────────────────
BASELINE_METHODS = ['SO-RA-MANET', 'ARP-MANET-GA', 'CEERP-SC-MANET', 'EER-MANET-EFIAGNN']
# EER-MANET-EFIAGNN is the base paper — we compare against it directly
