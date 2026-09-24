"""Write the E4 champion as an MLP weights file with the hidden path silent, plus the champion's
--rfly-policy csv string, so the two modes can be flown on the same seed and compared. They must
produce identical results: theta = b + W.phi(6 features) in both, evaluated in the same order."""
import json, os, sys
sys.path.insert(0, os.path.dirname(__file__))
from e5_es_mlp import NIN, NHID, NOUT, N_WLIN, NPAR, write_weights, CHAMP

c = json.load(open(CHAMP))["vec"]
vec = [0.0] * NPAR
for o in range(NOUT):
    for f in range(6): vec[o * NIN + f] = c[o * 7 + 1 + f]
    vec[N_WLIN + o] = c[o * 7]
out_dir = r"D:/bl_e1_data/es_mlp"
os.makedirs(out_dir, exist_ok=True)
write_weights(vec, os.path.join(out_dir, "champ_equiv.w"))
with open(os.path.join(out_dir, "champ_policy.csv"), "w") as fh:
    fh.write(",".join(f"{v:.6f}" for v in c))
print("wrote champ_equiv.w and champ_policy.csv")
