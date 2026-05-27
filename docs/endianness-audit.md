# TLV Endianness Audit — Item 1 Checklist

**Date produced:** 2026-05-27  
**Scope:** `src/whdtlv/core/`, `src/whdtlv/filtering/`, `src/whdtlv/reporting/`, `tools_src/`  
**Item 1 gate:** `make` clean (no code changes) — see [Build Gate](#build-gate) at the bottom.

This file is the cross-session checklist produced by Item 1 of the conversion plan in
[endianness-divergence.md](endianness-divergence.md).  Every multi-byte TLV scalar read
or write site found by the five audit patterns is recorded here.  Items 3–10 (plus the
folded extension to Item 4) must collectively discharge every row marked **OPEN** before
the plan is considered complete.

Rows whose checkbox is ticked are verified complete.

---

## Audit Patterns Run

```
fwrite(&       -- raw fwrite of a local variable (potential implicit-LE write)
fread(&        -- raw fread into a local variable (potential implicit-LE read)
u16_le         -- known LE decode helper
u32_le         -- known LE decode helper
read_u32_le    -- known LE decode helper
```

Searched in: `src/` and `tools_src/`.  
`src_raw/`, `app_src/`, and `tools_src/dat_to_tlv_main.c` had **zero hits** — all writes
there already use `encode_u32_be` / `encode_u16_be` helpers.

**Exclusions:** Single-byte `fwrite(&x, 1, 1, f)` and `fread(&x, 1, 1, f)` calls, raw
string payload writes (`fwrite(buf, 1, n, f)`), and byte-buffer reads are excluded.  Only
multi-byte scalar TLV fields are tracked here.

---

## src/whdtlv/core/tlv_builder.c

### fwrite sites — block framing and value_length (Item 3)

| # | Line | Function | Field | Width | Resolving Item | Status |
|---|------|----------|-------|-------|----------------|--------|
| 1 | 571  | `tlv_write_metadata_map`   | `map_size`                | uint16 | Item 3 | COMPLETE |
| 2 | 640  | `tlv_write_csv_fingerprints` | `payload_size`           | uint16 | Item 3 | COMPLETE |
| 3 | 646  | `tlv_write_csv_fingerprints` | `count`                  | uint16 | Item 3 | COMPLETE |
| 4 | 664  | `tlv_write_csv_fingerprints` | CRC-32 per entry (`crc`) | uint32 | Item 3 | COMPLETE |
| 5 | 1282 | `tlv_write_group_map`      | `payload_size`            | uint16 | Item 3 | COMPLETE |
| 6 | 1285 | `tlv_write_group_map`      | `group_count`             | uint16 | Item 3 | COMPLETE |
| 7 | 777  | `tlv_write_record_to_file` | `entry->length` (value_length) | uint16 | Item 3 | COMPLETE |

### fread sites — builder internal round-trip reads (Item 4)

The first three rows (L689–L716) were in the original Item 4 scope.  The remaining three
(L830, L899, L920) were discovered during the audit and are folded into Item 4 per the
resolution decision on 2026-05-27.  Only multi-byte scalar TLV fields are included;
string loops and byte-buffer reads in these functions are excluded.

| # | Line | Function | Field | Width | Resolving Item | Status |
|---|------|----------|-------|-------|----------------|--------|
| 8  | 689 | `tlv_read_csv_fingerprints`      | `payload_size`               | uint16 | Item 4 | COMPLETE |
| 9  | 692 | `tlv_read_csv_fingerprints`      | `count`                      | uint16 | Item 4 | COMPLETE |
| 10 | 716 | `tlv_read_csv_fingerprints`      | `crc32` per entry            | uint32 | Item 4 | COMPLETE |
| 11 | 830 | `tlv_read_metadata_map`          | `map_size`                   | uint16 | Item 4 | COMPLETE |
| 12 | 899 | `tlv_read_record_with_metadata`  | `map_size` (skip path)       | uint16 | Item 4 | COMPLETE |
| 13 | 920 | `tlv_read_record_with_metadata`  | `length` (value_length)      | uint16 | Item 4 | COMPLETE |

---

## src/whdtlv/filtering/tlv_runtime.c

Helper definitions at L31 (`u16_le`) and L36 (`u32_le`) have been deleted (Item 6
complete).  The local `u16_be` helper (previously used for `group_id` in
`parse_group_map`) was also removed and replaced with `tlv_read_u16_be`.

| # | Line | Block / Context | Field | Helper | Resolving Item | Status |
|---|------|-----------------|-------|--------|----------------|--------|
| 14 | 69  | Block `0x01` parse | `map_size`      | `u16_le` | Item 6 | COMPLETE |
| 15 | 142 | Block `0x04` parse | `payload_size`  | `u16_le` | Item 6 | COMPLETE |
| 16 | 150 | Block `0x04` parse | `count`         | `u16_le` | Item 6 | COMPLETE |
| 17 | 187 | Block `0x04` parse | `crc32`         | `u32_le` | Item 6 | COMPLETE |
| 18 | 228 | Block `0x02` parse | `payload_size`  | `u16_le` | Item 6 | COMPLETE |
| 19 | 236 | Block `0x02` parse | `count`         | `u16_le` | Item 6 | COMPLETE |

---

## src/whdtlv/filtering/tlv_variant.c

Helper definition at L57 (`u16_le`) is to be deleted when L120 is replaced (Item 7 gate
condition).

| # | Line | Function | Field | Helper | Resolving Item | Status |
|---|------|----------|-------|--------|----------------|--------|
| 20 | 120 | record iteration | `value_length` | `u16_le` | Item 7 | COMPLETE |

---

## src/whdtlv/filtering/tlv_select.c

Helper definition at L51 (`read_u32_le`) is to be deleted when all three call sites
below are replaced (Item 10 gate condition).

| # | Line | Context | Field | Helper | Resolving Item | Status |
|---|------|---------|-------|--------|----------------|--------|
| 21 | 146 | field value comparison | token ID | `read_u32_le` | Item 10 | COMPLETE |
| 22 | 285 | field value comparison | token ID | `read_u32_le` | Item 10 | COMPLETE |
| 23 | 458 | field value comparison | token ID | `read_u32_le` | Item 10 | COMPLETE |

---

## src/whdtlv/reporting/whdtlv_report_profile.c

Helper definition at L144 (`read_u32_le_local`) is to be deleted when L354 is replaced
(Item 10 gate condition).

| # | Line | Function | Field | Helper | Resolving Item | Status |
|---|------|----------|-------|--------|----------------|--------|
| 24 | 354 | profile report loop | token ID | `read_u32_le_local` | Item 10 | COMPLETE |

---

## src/whdtlv/core/filename_processor.c — value payload writes (Item 9)

Item 9 covers all multi-byte value payload writes in `filename_processor.c`, split into
two sub-groups.

**Sub-group A — CSV-backed uint32 token IDs.**  All four sites pass a host-native
`uint32_t` via `(const uint8_t *)&id` or `(const uint8_t *)&token_id`.  Each must be
replaced with an explicit BE encode into a local `uint8_t[4]` buffer before passing to
`tlv_record_add_entry`.

**Sub-group B — Non-CSV numeric payloads.**  Two additional sites (formerly G1/G2
unassigned gaps) write multi-byte non-CSV values as native-endian pointers.  Code
inspection on 2026-05-27 confirmed both are live TLV value payload writes in
`filename_processor.c`.  They are not CSV token IDs and were not covered by the original
Item 9 scope; after inspection they are folded into Item 9.

Read-path findings for sub-group B (recorded 2026-05-27):
- `language_bitfield` (uint16, 2 bytes): `variant_index_build` tracks the "language"
  field but only extracts a `csv_id` for entries with `length == 4`; the 2-byte bitfield
  entry always produces `csv_id = 0` and falls back to a hash comparison that does not
  match profile token IDs.  `tlv_select.c` and `whdtlv_report_profile.c`
  `collect_explicit_tokens` both skip entries with `length < 4`.  **No current functional
  read path found** — the write-side conversion is still required.
- `sps_id` (uint32, 4 bytes): the "sps" field is not in `variant_index_build`'s tracked
  list and no profile binds it as a scored or reported column.  **No current read path
  found** — the write-side conversion is still required.

| # | Line | Function / Context | Field | Width | Resolving Item | Status |
|---|------|--------------------|-------|-------|----------------|--------|
| 25 | 624  | contributor lookup       | `contributor_id`  (CSV token, uint32) | 4 | Item 9 | COMPLETE |
| 26 | 827  | prescan CSV field loop   | `id`              (CSV token, uint32) | 4 | Item 9 | COMPLETE |
| 27 | 1302 | CSV multi-part token     | `token_id`        (CSV token, uint32) | 4 | Item 9 | COMPLETE |
| 28 | 1335 | CSV single token         | `token_id`        (CSV token, uint32) | 4 | Item 9 | COMPLETE |
| 29 | 1172 | language token loop      | `language_bitfield` (bitfield, uint16) | 2 | Item 9 | COMPLETE |
| 30 | 1209 | SPS loop                 | `sps_id`          (raw atoi, uint32)  | 4 | Item 9 | COMPLETE |

---

## Unassigned Gaps — Resolved

The two sites formerly listed here (G1 `language_bitfield`, G2 `sps_id`) were inspected
on 2026-05-27 and confirmed as live TLV multi-byte value payload writes in
`filename_processor.c`.  Both are now assigned to **Item 9** (sub-group B) and appear as
rows 29–30 in the `filename_processor.c` section above.  No new Item 9b was required.
The read-path findings are recorded in that section.

---

## Summary

| Source file | Open rows | Resolving item(s) |
|-------------|-----------|-------------------|
| `tlv_builder.c` — fwrite | 0 (7 closed) | Item 3 |
| `tlv_builder.c` — fread  | 0 (6 closed) | Item 4 (incl. 3 folded gaps) |
| `tlv_runtime.c`          | 0 (6 closed) | Item 6 |
| `tlv_variant.c`          | 0 (1 closed) | Item 7 |
| `tlv_select.c`           | 0 (3 closed) | Item 10 |
| `whdtlv_report_profile.c`| 0 (1 closed) | Item 10 |
| `filename_processor.c`   | 6 | Item 9 (4 CSV token IDs + 2 non-CSV numeric payloads) |
| **Total tracked**         | **0 open / 30 closed** | |
| Unassigned gaps          | 0 — resolved 2026-05-27, folded into Item 9 |

---

## Build Gate

Item 1 gate: clean `make` (no code changes made during audit).

| Date | Result | Notes |
|------|--------|-------|
| 2026-05-27 | **PASSED** | `make build/host/dat_to_tlv.exe` — clean build, zero warnings, zero errors |
