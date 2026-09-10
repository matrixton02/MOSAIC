# Mathematical Model of the Z-Order Physics Data Store

## 1. Data model

Each detector event is a tuple over d attribute dimensions. For the current
prototype, d = 3:

    e_i = (t_i, E_i, c_i)  ∈  ℝ × ℝ × ℤ

where t_i is timestamp, E_i is energy, c_i is channel. A dataset is a
sequence of N such events, e_1, ..., e_N (not necessarily arriving in any
particular order).

## 2. Quantization (discretization)

Z-order encoding requires integer coordinates. Each real-valued dimension j
is mapped into a fixed number of bits b (b = 21 in the current code) via a
min-max scaling function:

    φ_j : ℝ → {0, 1, ..., 2^b − 1}

    φ_j(x) = floor( (x − min_j) / (max_j − min_j) · (2^b − 1) )

where min_j, max_j are the dataset's observed bounds for dimension j.

This is a **lossy, many-to-one** mapping: the quantization error for a
single value is bounded by

    |x − φ_j⁻¹(φ_j(x))| ≤ (max_j − min_j) / 2^b

For b = 21, this bound is under 1 part in 2 million of the dimension's
range — negligible for physical measurement precision, but worth stating
explicitly as a source of approximation in the pipeline, since it's the
first of two places (the second being the learned index's own prediction
error) where exactness is traded for structure.

## 3. Z-order (Morton) encoding
![Morton Curve](https://thumb.wikimedia.org/wikipedia/commons/thumb/e/e7/Lebesgue-3d-step2-animated.gif/330px-Lebesgue-3d-step2-animated.gif?utm_source=en.wikipedia.org&utm_campaign=parser&utm_content=thumbnail)

<img width="400" height="400" alt="image" src="https://github.com/user-attachments/assets/eebe10b6-9ea4-4825-b47f-04993e1c39da" />

Given quantized integers x, y, z ∈ {0, ..., 2^b − 1}, write each in binary:

    x = Σ_{i=0}^{b−1} x_i · 2^i      (similarly for y, z)

The Z-order key interleaves these bit sequences:

    Z(x, y, z) = Σ_{i=0}^{b−1} ( x_i · 2^(3i) + y_i · 2^(3i+1) + z_i · 2^(3i+2) )

**Properties worth stating formally:**

- **Bijectivity.** For equal bit-width b per dimension and d dimensions,
  Z : {0,...,2^b−1}^d → {0,...,2^(db)−1} is a bijection. No information is
  lost in the interleaving step itself — all approximation happened earlier,
  in quantization (§2).

- **Partial locality preservation.** If two points are close in the
  original d-dimensional grid, they are *usually* close in Z-order, but
  this is not a guarantee — there exist adjacent grid cells whose Z-values
  differ by a large jump (this happens at every "boundary crossing" where a
  high-order bit flips, e.g. between Z-indices 3 and 4 in the earlier
  diagram). This is the formal reason Z-order is described as having weaker
  locality than a Hilbert curve — worth citing explicitly as a known
  limitation rather than glossing over it.

## 4. Physical layout as a mapping

Let π : {1,...,N} → {1,...,P} be the page-assignment function, where P is
the number of pages. Records are sorted by Z-key before assignment:

    Z(e_{σ(1)}) ≤ Z(e_{σ(2)}) ≤ ... ≤ Z(e_{σ(N)})

for some permutation σ, and

    π(i) = floor( (i − 1) / C )

where C = floor((PAGE_SIZE − HEADER_SIZE) / record_size) is the page
capacity (170 in the current build, per the earlier test run).

Because assignment happens **after** sorting, each page's key interval

    I_p = [ min{ Z(e_{σ(i)}) : π(i) = p },  max{ Z(e_{σ(i)}) : π(i) = p } ]

is disjoint from every other page's interval, and the intervals are
ordered: max(I_p) < min(I_{p+1}) for all p. This disjoint-ordered-interval
property is exactly what makes binary search over the directory correct —
it's the same invariant a B+ tree's leaf-level maintains, just without the
tree structure above it yet.

## 5. The directory as an approximation of the CDF — why this connects directly to your next step

This is the key conceptual bridge to the learned-index phase, and it's
worth stating precisely because it's the actual mathematical justification
for the whole learned-index idea, not just a hand-wavy analogy.

Define the **empirical cumulative distribution function** of the stored
Z-keys:

    F(z) = (1/N) · |{ i : Z(e_i) ≤ z }|

The directory's binary search is, mathematically, evaluating a
**piecewise-constant approximation of N·F(z)** — it locates which
"bucket" (page) a query key falls into by comparing against known
boundary points, which is exactly what bisection does when approximating
an inverse CDF.

A **learned index** (your next phase) replaces this piecewise-constant
approximation with a smooth, trained function

    f_θ(z) ≈ N · F(z)

directly predicting the row/page position from the key, then does a small
local correction (bounded search) around the prediction. The entire
learned-index literature (Kraska et al.'s RMI, PGM-index, ALEX) can be
summarized as: **"can we approximate F well enough with a small parametric
model that we save the O(log P) comparisons a binary search needs?"**
That's the one-sentence mathematical framing your proposal should open
its "learned index" section with.

## 6. Range query decomposition (formal statement of what BIGMIN needs to solve)

A multi-dimensional query is an axis-aligned box:

    Q = [t_lo, t_hi] × [E_lo, E_hi] × [c_lo, c_hi]

After quantization this becomes an integer box in {0,...,2^b−1}^3. The set
of Z-keys whose *decoded* coordinates fall inside this box,

    S = { Z(x,y,z) : (x,y,z) ∈ Q_quantized }

is **not a contiguous interval** in Z-order space — it is a union of
disjoint sub-intervals, because the Morton curve exits and re-enters the
query box repeatedly as it snakes through the grid (visible directly in
the earlier Z-order diagram: the box formed by cells {5,6,7,8} is not a
contiguous run of the path).

The decomposition problem is: find a small set of intervals
{[a_1,b_1], ..., [a_k,b_k]} such that

    S ⊆ ⋃_k [a_k, b_k]     (covering property — no false negatives)

while keeping k small and the "wasted" region (points inside the covering
intervals but outside Q) small (efficiency). This is exactly what the
BIGMIN/LITMAX recursive splitting algorithm computes, and it is why every
Z-order range query needs a **post-filter step**: after fetching candidate
records from the covering intervals, you must explicitly re-check each
one against the true box Q, since the covering intervals are a superset.
This is the next thing we'll build — flagging it here so the math is in
place before the code is.

---

# Comparison: why not a vector database?

Your instinct that it would be "computationally more hectic" is correct in
outcome but understates the real issue — a vector database solves a
**different problem** than the one you have, so the comparison isn't
really "slower, same answer" — it's "answers a different question, and
the answer it gives isn't guaranteed complete."

**What vector DBs (HNSW, IVF, product quantization) are built for:**
approximate k-nearest-neighbor search over high-dimensional (typically
50–1500 dimension) learned embedding vectors, under a similarity metric
(cosine or L2 distance). The query is "find the k points most similar to
this vector," and the index is explicitly allowed to be **approximate** —
recall of 90–99% is normal and accepted, because in the typical use case
(semantic search, recommendation) missing a few near-duplicates is fine.

**What your problem actually is:** an *exact* axis-aligned range query
over a small number (2–4) of physically meaningful, independently
interpretable dimensions. "Give me every flare with energy in [40,60] keV"
is a completeness-critical query for a physicist — an approximate answer
that silently drops 5% of matching events is a correctness bug, not an
acceptable tradeoff.

| Property | Your Z-order + (B+/learned) index | Vector DB (HNSW / IVF) |
|---|---|---|
| Query semantics | Exact range / point queries | Approximate k-NN similarity |
| Result guarantee | Exact (with post-filter step) | Probabilistic recall (typically 90–99%) |
| Dimensionality sweet spot | Low (2–5), interpretable axes | High (50–1500+), learned/opaque axes |
| Index memory overhead | O(P), P ≈ N/170 in your data — tiny | O(N·M) graph edges (M ≈ 16–64 per node) — large |
| Build cost | O(N log N) — one sort | O(N log N) with a much larger constant (graph construction, distance computations) |
| What "closeness" means | Euclidean/interleaved closeness in raw physical units | Learned/cosine similarity in embedding space |
| Natural fit for your data | Yes — dimensions are already physically meaningful | No — would require learning an embedding first, discarding the interpretability you actually want |

**The honest bottom line for your proposal:** it's not that a vector DB
is a slower version of what you're building — it's that applying one here
would mean solving the wrong problem well, rather than your problem at
all. This is worth stating explicitly and confidently in your related-work
section; it pre-empts a reviewer asking "why not just use FAISS/HNSW,"
which is a real question people will ask given how ubiquitous vector DBs
have become.

## The comparisons that actually matter (apples to apples)

These are the systems solving the *same* problem as you — exact
multi-dimensional range queries over structured, low-dimensional data —
and are what your evaluation section should benchmark against instead:

| Method | How it works | Strength | Weakness for your case |
|---|---|---|---|
| **Z-order + B+/learned index (yours)** | Linearize via bit-interleaving, index the 1D result | Simple, reuses a 1D index unmodified, small memory | Range queries decompose into multiple sub-ranges; locality only partial |
| **R-tree** | Bounding boxes organized hierarchically | Handles range queries very naturally, no decomposition step needed | More complex to implement/maintain (splits/merges of boxes); harder to make "learned" |
| **KD-tree** | Recursively splits space by alternating dimension | Good for point queries and nearest-neighbor | Rebalancing under inserts is awkward; not naturally page-oriented |
| **Grid file** | Fixed or adaptive grid over the space, one bucket per cell | Very simple, fast for uniform data | Degrades badly under skewed data — exactly the bursty distribution your physics data has |
| **Two separate 1D indexes + intersection** | Index time and energy separately, bitmap-intersect results | Simple, each index individually well-understood | Double the index memory; intersection cost grows with result set size |
| **Hilbert curve + 1D index** | Same idea as Z-order but better locality preservation | Stronger locality guarantee than Z-order | More expensive to compute (no simple bit-interleave trick); harder to invert |

Your strongest, most defensible comparison set for the paper is:
**full scan (today's realistic baseline) → two-separate-1D-index
intersection → Z-order + B-tree → Z-order + learned index**, with R-tree
as an additional reference point since it's the classic textbook answer
to "how do you index multiple dimensions."
