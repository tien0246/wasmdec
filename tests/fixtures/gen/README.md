# Fixture generators (K2 phase-1: C++ port)

The artifacts produced by the generator are **committed**; regeneration is
optional. Since K2 phase-1 the six `make_*.py` scripts are replaced by one
C++ tool — build and run:

```
cmake -S tests/fixtures -B <build-dir>
cmake --build <build-dir> --target fixtures-gen
```

| Subcommand (former script) | Produces | Needs |
|---|---|---|
| `component-multi` (make_component_multi.py) | `component_multi_core.wasm` | — |
| `validator-states` (make_validator_states.py) | `a5_* a6_* a7_* a8_* s5_*` | — |
| `pclntab-names` (make_pclntab_names.py) | `g6_pclntab_names.wasm` | — |
| `brtable-wat` (make_brtable_matrix.py) | the `bt_*.wat` sources (`.wasm` via the `fixtures` target) | wat2wasm (for .wasm) |
| `differential` (make_differential.py) | `differential_o0/o2.wasm` | clang (wasm32) |
| `simd-lane-bounds` (make_simd_lane_bounds.py) | `simd_lane_bounds_invalid.wasm` | — |
| `typed-ref-unsupported-gc` | `wasmhex_typed_ref_unsupported_gc.wasm` | — |

Note (P4 rule): `dwarf_{full,noname,ext,ext.debug}.wasm` stay as committed `.wasm` fixtures in `tests/fixtures/`.

Every subcommand is idempotent: byte-identical outputs are not rewritten
(and the clang-based groups keep the python mtime skip). The tool was
verified byte-identical against the python generators before they were
removed (sha256, 9 crafted modules + forced brtable regen + scratch
clang builds).
