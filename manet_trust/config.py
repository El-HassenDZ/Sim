"""
PLACEHOLDER — the project's real config.py was not provided with
adaptive_trust.py. These values are ASSUMPTIONS used only to run the tests
and run_scenario.py. Replace this file with the real one; any number
produced with it is not a result of the original project.
"""
N_WINDOWS = 5
WINDOW_SIZE = 10
DECAY_FACTOR = 0.8
TRUST_ALPHA = 0.6
TRUST_BETA = 0.4
TRUST_THRESHOLD_BASE = 0.5
THRESH_MIN = 0.3
THRESH_MAX = 0.8
THRESH_MOBILITY_W = 0.1
THRESH_DENSITY_W = 0.1
THRESH_VARIANCE_W = 0.1
THRESH_LOSS_W = 0.1
TX_RANGE = 250.0
E_INITIAL = 100.0
PKT_SIZE = 512
OSC_WINDOW = 10
OSC_THRESHOLD = 0.01
ISOLATION_TIME = 30.0
RE_EVAL_AFTER = 15.0
# New, optional (defaults in adaptive_trust.py if absent)
MOBILITY_REF_SPEED = 20.0
COLLUSION_FILTER = 'mad'
