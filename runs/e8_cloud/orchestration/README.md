# How the 2026-09-24 cloud numbers were produced

The exact runner scripts used in the Linux cloud session, with the scratchpad path replaced by
`$SP`. Binaries: `booster-core.e8farm` = `7b847eb` (the merge of `e-ladder`; farm only),
`booster-core.e11` = `681acb9` (control arms R2/R3/R3b and every critic arm),
`booster-core.e12` = `77ad3f2` (`--rfly-rollouts-at`; the decomposition arms, R5, R9, T16).
The gates showed each later binary byte-identical to the earlier ones with its new flags absent.

Order of execution:
1. `runs/e8_cand_farm.sh` — the six-seed farm (14:54–18:32 UTC).
2. `ladder.sh` — R3, then was stopped; `ladder2.sh` — R2 (it was then replaced by `queue.sh`
   before reaching R5/R9); `r3b.sh` — R3 re-flown with csv receipts.
3. `chain.sh` — waits for the farm, trains `critic_c0.w`, runs the offline stats, flies De, D, B,
   De1 (18:33–18:49 UTC).
4. `queue.sh` — after the chain: the width decomposition (rich_event, rich_t0event,
   rich_periodic), R5, R9, T16. (`t16.sh` was superseded by `queue.sh` before it ran.)
