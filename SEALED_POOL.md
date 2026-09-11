# THE SEALED POOL — seeds 9200–9209

**Sealed 2026-09-11.** These ten seeds exist for **final claims only**. Nothing may be tuned,
selected, warm-started, hand-probed, or status-checked against them. Flying them is a
**publication event**, not an experiment.

## Why a new pool was needed

Seeds **42 / 7 / 99** were the held-out set for the whole 2026-07 → 2026-09 arc and are now
**burned**:

- they informed a dozen reported conclusions across D-046 … D-054;
- **seed 42 is directly contaminated** — D-047's CEM warm start was chosen *because* a box-ceiling
  θ scored 13/60 on it during hand probing, so selection information crossed the fence through a
  human decision, which no gate catches (D-047 integrity correction (a));
- seed 42 is also, measurably, an **easy** seed: `identity` scores 9/60 there against 5/60 on the
  training pool, and every "reactive baseline" number quoted for seven weeks came from it.

They are hereby **demoted to development seeds**. They remain useful and remain quotable *as
development numbers*; they are no longer evidence for a headline.

## How these ten were chosen

Substring grep is worthless for this — every candidate band scores ~40 "hits" because those digits
appear inside `evals.jsonl` floats and `.bin` tap rows. **The ground truth is the set of seeds ever
actually flown**, recovered from run headers:

```
7  42  99  1001-1006  7400-7402  7500-7502        (from --seed / seed= in runs/)
5000-5005                                          (D-047/D-050 training, via subprocess)
```

**9200–9209 appears nowhere in that set.** Verified 2026-09-11 before first use.

## The rule

| | |
|---|---|
| may be flown | **once per claim**, to verify a headline that is already settled on development seeds |
| may **never** be | a selection criterion, a warm start, a tuning target, a progress check, or a "let me just peek" |
| if peeked | **the pool is burned** — say so in the ledger and mint a new band. Do not quietly reuse it |

Repeatedly evaluating a held-out set and keeping the best-looking result **is model selection on
the test set**, laundered through a loop. That error was caught twice in this project already
(D-047's warm start; D-050's "full pool" re-score that turned out to be its own training set), and
both times it inflated a number that was then published.

## Flights against this pool

| date | what was flown | result | ADR |
|---|---|---|---|
| 2026-09-11 | D-055 — the deployable headline verification | *pending* | D-055 |

## The expectation, adjusted BEFORE the deployable arm returned

Written while the third arm was still flying, because an expectation revised *after* seeing the
number is not an expectation — it is an explanation, and the difference is the whole point of
pre-registration.

**Both control arms came in lower on the sealed pool than on the development pool:**

| arm | dev (42/7/99) | sealed (9200-9209) | shift |
|---|---|---|---|
| `identity` | 28/180 = 15.6% | **68/600 = 11.3%** | −4.3 pp |
| constant θ | 121/180 = 67.2% | **382/600 = 63.7%** | −3.5 pp |

**These seeds are uniformly harder by roughly 3.5–4 points**, on two independent controllers that
were never tuned on either pool. So the original pre-registration ("within ~2σ of 97.2%") was
written against the wrong reference and should be adjusted downward, not defended.

**Adjusted reads, declared now:**

- **~94–96%** — consistent with pool difficulty alone. The headline is **CONFIRMED**; the dev-pool
  97.2% simply came from a slightly easier draw. Quote the sealed figure from here on.
- **below ~92%** — a real shortfall *beyond* pool difficulty. The dev pool was optimistic and
  D-052/D-054's numbers get re-labelled in `SCOREBOARD.md` and `PLAN.md`.
- **at or above 97%** — report it plainly, but do **not** treat it as an improvement. On a pool
  that is harder for both baselines, a held-up rate at the top of the range most likely means the
  ceiling compresses the shift, not that the controller got better.

Note the asymmetry deliberately: the compression argument cuts *toward* accepting a high number, so
it is stated here in advance rather than reached for afterwards.
