(module
  (func (export "count") (param $n i32) (result i32)
    (local $i i32)
    i32.const 0
    local.set $i
    block
      loop
        local.get $i
        local.get $n
        i32.ge_u
        br_if 1
        local.get $i
        i32.const 1
        i32.add
        local.set $i
        br 0
      end
    end
    local.get $i))
