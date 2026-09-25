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
TIME_STEP       = 1.0           # seconds per simulation step
BOOTSTRAP_TIME  = 8.0           # seconds of trust bootstrap, run in [-BOOTSTRAP_TIME, 0)

# ── Energy Model (first-order radio model, per packet and per hop) ──
E_INITIAL       = 15.1          # Joules per node
E_TX            = 50e-9         # J/bit transmit electronics
E_RX            = 50e-9         # J/bit receive electronics
E_AMP           = 100e-12       # J/bit/m² amplification
PKT_SIZE        = 512           # bytes
DATA_RATE       = 4             # packets/sec per flow

# ── Traffic ──────────────────────────────────────────────────
N_FLOWS         = 20            # CBR flows between honest nodes, fixed per run
MAX_HOPS        = 10
HOP_DELAY       = 0.003         # s per hop — delay PROXY (no MAC/queue model)

# ── Mobility ─────────────────────────────────────────────────
MIN_SPEED       = 1.0           # m/s
MAX_SPEED       = 10.0          # m/s
PAUSE_TIME      = 5.0           # seconds
TX_RANGE        = 250.0         # metres

# ── Attack Configuration ──────────────────────────────────────
MALICIOUS_RATIO = 0.10          # 10% malicious nodes
# Attack types assigned proportionally (largest remainder) across malicious nodes
ATTACK_TYPES    = {
    'blackhole'  : 0.30,        # drops all packets
    'grayhole'   : 0.25,        # drops packets selectively
    'on_off'     : 0.25,        # alternates good/bad behaviour
    'collusion'  : 0.20,        # drops packets and sends false recommendations
}
BENIGN_FAILURE  = 0.05          # honest forwarding failure probability
GRAYHOLE_DROP   = 0.60
COLLUSION_DROP  = 0.75
ONOFF_PERIOD    = 20.0          # seconds
ONOFF_DUTY      = 0.5           # fraction of the period spent behaving well

# ── Sliding Window Trust ──────────────────────────────────────
WINDOW_SIZE     = 10            # interactions per window
N_WINDOWS       = 5             # number of windows kept in history
DECAY_FACTOR    = 0.92          # exponential decay weight for older windows
TRUST_ALPHA     = 0.70          # weight for direct trust vs indirect trust
TRUST_BETA      = 0.30          # weight for indirect trust
TRUST_THRESHOLD_BASE = 0.50     # base threshold (adapts dynamically)
TRUST_UPDATE_INTERVAL = 5.0     # seconds between trust updates
COLLUSION_FILTER = 'mad'        # 'mad' (median ± 2·MAD) or 'std' (original rule)

# ── Adaptive Trust Threshold ──────────────────────────────────
THRESH_MOBILITY_W   = 0.30      # mobility contribution to threshold
THRESH_DENSITY_W    = 0.20      # density contribution
THRESH_VARIANCE_W   = 0.25      # trust variance contribution
THRESH_LOSS_W       = 0.25      # packet loss contribution
THRESH_MIN          = 0.35      # floor (never go below this)
THRESH_MAX          = 0.75      # ceiling (never exceed this)
MOBILITY_REF_SPEED  = MAX_SPEED # speed giving the full mobility contribution

# ── On-Off Attack Detection ───────────────────────────────────
OSC_WINDOW      = 12             # window to detect oscillation (trust updates)
OSC_THRESHOLD   = 0.12          # variance threshold to flag oscillation
ISOLATION_TIME  = 20.0          # seconds a suspicious node stays isolated
RE_EVAL_AFTER   = 15.0          # seconds before re-evaluation

# ── FCMVC Clustering ──────────────────────────────────────────
N_CLUSTERS      = 10
FCM_MAX_ITER    = 100
FCM_EPSILON     = 1e-5
FCM_FUZZINESS   = 2.0           # fuzziness exponent m

# ── AT-EFIAGNN Architecture ───────────────────────────────────
GNN_HIDDEN      = 128           # 3 layers (fixed in gnn/at_efiagnn.py)
GNN_INPUT_DIM   = 9             # extended feature vector (vs 6 in base paper)
# Features: x, y, energy, trust_score, trust_stability,
#           cluster_membership, node_degree, attack_suspicion, mobility_speed
GNN_SCORE_W     = 0.30          # weight of the GNN score in the composite metric

# ── HLOA Optimisation ─────────────────────────────────────────
HLOA_POP        = 30
HLOA_MAX_ITER   = 50
HLOA_WEIGHT_DECAY = 0.9
HLOA_FITNESS_PAIRS = 30         # (src, dst) pairs per fitness evaluation
TRAIN_SEED_OFFSET  = 100000     # router trained on seed + offset, tested on seed

# ── Routing Metric Weights ────────────────────────────────────
ROUTE_ALPHA     = 0.40          # trust weight
ROUTE_BETA      = 0.35          # residual energy weight
ROUTE_GAMMA     = 0.25          # suspicion penalty weight
# These adapt dynamically based on network conditions

# ── Evaluation Snapshots ──────────────────────────────────────
EVAL_TIMES      = [10.0, 30.0, 40.0]

# ── Variants compared in the same simulator ──────────────────
# full               : all components
# no_sliding_window  : cumulative trust (decay 1, unbounded history)
# fixed_threshold    : threshold = TRUST_THRESHOLD_BASE
# no_onoff_detector  : oscillation term disabled
# no_collusion_filter: plain trust-weighted mean of recommendations
# no_defense         : trust ignored by routing, nothing flagged
# no_attack          : full defence, malicious nodes behave honestly
VARIANTS = ['full', 'no_sliding_window', 'fixed_threshold', 'no_onoff_detector',
            'no_collusion_filter', 'no_defense', 'no_attack']
