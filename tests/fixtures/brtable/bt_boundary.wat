;; m_jae boundary witness: selector == case_count (3) and selector ==
;; 0xFFFFFFFF must land on the DEFAULT arm — the switch lowering's
;; unsigned-above-equal bounds check (m_jae) sends them there.
(module
  (func (export "dispatch") (param i32) (result i32)
    (local $r i32)
    (block $out
      (block $a
        (block $b
          (block $c
            local.get 0
            (br_table $c $b $a $out)
          )
          i32.const 10 local.set $r br $out
        )
        i32.const 11 local.set $r br $out
      )
      i32.const 12 local.set $r
    )
    local.get $r))
