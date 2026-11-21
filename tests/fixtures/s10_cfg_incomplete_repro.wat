(module
  ;; S10/K1: two functions whose br_if control metadata is corrupted
  ;; POST-load by the probe script (cross-function target + mid-instruction
  ;; target). wasmhex_add_jump must reject both, and the owning functions
  ;; must be marked cfg-incomplete ('u' at the entry EA) so F5 refuses
  ;; them instead of silently building a half-known CFG. s10_clean must
  ;; stay unmarked and decompilable.
  (memory 1)
  (func (export "s10_cross") (param i32) (result i32)
    (local i32)
    block $b
      local.get 0
      if
        local.get 0
        br_if $b
      end
      i32.const 11
      local.set 1
    end
    local.get 1)
  (func (export "s10_midinsn") (param i32) (result i32)
    (local i32)
    ;; LEB bytes 41 FC 8F 84 07: jumping at the FC byte makes the decoder
    ;; read an out-of-range 0xFC sub-opcode (LEB 8F 84 07) and fail -
    ;; the "target is not an instruction boundary" branch.
    i32.const 147525116
    drop
    block $b
      local.get 0
      if
        local.get 0
        i32.const 1
        i32.add
        br_if $b
      end
      i32.const 22
      local.set 1
    end
    local.get 1)
  (func (export "s10_clean") (result i32)
    i32.const 3)
)
