# MOSAIC — Multi-dimensional Ordered Storage with Adaptive Index for Clustering

## What each piece is, in plain terms

**A page** is a fixed 4KB slice of one on-disk file. It's not a separate
file — think of the whole dataset as one long binary file, pre-cut into
equal-size blocks. Each page has a small 16-byte header (page ID, record
size, how many records are currently stored, max capacity) followed by
records packed back-to-back with no gaps. Because your data is
write-once/read-many (detector events are never edited after being
recorded), pages never need delete/compaction logic — records are only
ever appended.

**The page directory** is the "which page has this key range" lookup
table. It's a small, in-memory list — one entry per page, not per record
— of `(min_key, max_key, page_id)` triples, kept sorted. A point lookup
binary-searches this list; a range lookup walks it to find every
overlapping page. Right now it's a flat sorted array. It's designed so
this implementation is swappable: a real B+ tree, and later a trained
model, can replace it without any other file needing to change.

**A table** is the object that ties one page file + one page directory
together under a fixed record schema (key size + payload size). It's
what you actually call `insert()` and `finalize()` on. There's one
`Table` per dataset you're storing — you don't need multiple tables for
this project.

**The Z-order (Morton) key** is not a separate storage concept — it's
just *which number you choose to use as the key* before any of the above
runs. Instead of using raw timestamp as the sort key, you interleave the
bits of (timestamp, energy, channel) into one number, so records that are
close together in the *original 3D space* tend to land close together in
the *1D sorted order* — which means close together on disk, in the same
or nearby pages. Everything downstream (pages, directory, table) treats
this Z-key exactly like it would treat a plain timestamp; it has no idea
Z-order was involved.

## How many actual files this is

One project, one repo, with source files organized as:

| File | Role |
|---|---|
| `page.hpp` | Layout of one 4KB page in memory (header + packed records) |
| `page_file.hpp` | The only file that touches disk — real read/write of pages, with I/O counters |
| `page_directory.hpp` | Sorted key-range → page lookup (today: binary search; later: B+ tree; later still: learned model) |
| `table.hpp` | Glues file + directory together under one schema; `insert()`, `finalize()`, `scan_page()` |
| `zorder.hpp` | `scale_to_bits()` (quantize a real value into the bit budget) + `morton3()` (interleave three quantized values into one key) |
| `data_gen.hpp` | Synthetic detector-event simulator (background + bursty flares) — your stand-in for real Aditya-L1/HEP data during development |
| `build_zorder_table.cpp` | The pipeline: generate → compute Z-key per event → sort → bulk-load into pages → verify round-trip |

That's the entire system as it stands — no SQL parser, no separate query
engine files, no multiple database files. One binary data file on disk
per table, one small set of C++ headers implementing the logic.

## End-to-end workflow, in order

1. **Generate or ingest events** — each event is `(timestamp, energy, channel)`.
2. **Quantize** each dimension into a 21-bit integer bucket, using the dataset's own min/max (`scale_to_bits`).
3. **Interleave** the three quantized values into one 63-bit Z-order key (`morton3`).
4. **Sort** all events by their Z-key.
5. **Bulk-load**: insert sorted events into `Table`, which fills pages in order via `PageFile`, and records each finished page's `(min_key, max_key, page_id)` into the `PageDirectory`.
6. **Finalize**: flush the last partial page, sort the directory for querying.
7. **Query** (point lookup done; range lookup on raw Z-keys done): compute the Z-key(s) for a query, ask the directory/index which page(s) could hold it, read those pages, filter to exact matches.

Steps 1–6 are done and tested (round-trip verified correct). Step 7 is
done only for the trivial case (an exact Z-key or a raw Z-key range) —
translating a *real* multi-dimensional query box (e.g. "energy 40–60 AND
time in this window") into the right Z-key ranges is the next major piece
of work, described below.

## Next steps, in build order

1. **Multi-dimensional range query decomposition (BIGMIN-style).** Take a query box in (time, energy, channel) space, quantize it, and compute the set of disjoint Z-key sub-ranges that cover it. Query the directory once per sub-range, union results, then post-filter (since covering ranges can include false positives outside the true box). This is the piece that makes MOSAIC actually queryable by physical meaning rather than raw Z-key.

2. **Upgrade the flat directory into a real multi-level B+ tree.** Same `lookup()`/`range_lookup()` interface, but proper internal nodes instead of one flat sorted array — this becomes your primary baseline for comparison, and is expected by a DBMS course rubric regardless.

3. **Buffer pool with LRU/clock eviction.** Right now every `read_page`/`write_page` call goes straight to disk. Adding a cache with real hit/miss tracking makes your I/O-count metric honest and is a standard DBMS-course requirement.

4. **Fix the double-write bug.** `allocate_page()` currently writes an empty page, then `flush_current_page()` writes it again once full — every page is written to disk twice. Worth fixing before you trust any I/O-count benchmark numbers.

5. **Single-stage RMI (naive learned index).** Train one small model to predict page/position directly from the Z-key. Expect — and explicitly report — that it struggles on the "sudden jump" segments (flare boundaries). This "expected failure" result is your setup for step 6, not a wasted step.

6. **Two-stage, jump-aware RMI (MOSAIC's actual contribution).** Stage 1 model classifies which segment (background vs. flare/run) a key belongs to; Stage 2 has one small model per segment. This is what directly answers the distributional-jump problem from your motivation section.

7. **Insert handling** (gapped array or delta buffer + periodic retrain) — needed once you want to demonstrate the engine isn't read-only.

8. **Benchmark harness**: run all of {full scan, two-separate-1D-indexes, B+ tree, naive RMI, two-stage RMI} against synthetic + your real HEP/solar data, logging latency (p50/p95/p99), memory, build time, and I/O count per query — exactly the metrics your evaluation plan already specifies.

Recommended immediate next build: **step 1 (range decomposition)**, since it's the piece that turns your storage engine into something actually queryable, and everything after it (B+ tree, RMI) is compared against how well it does that job.
