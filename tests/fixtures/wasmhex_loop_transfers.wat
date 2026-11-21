(module
  (func (export "early_continue") (param $limit i32) (result i32)
    (local $i i32) (local $sum i32)
    i32.const 0 local.set $i
    i32.const 0 local.set $sum
    block $exit
      loop $again
        local.get $i local.get $limit i32.ge_u
        br_if $exit
        local.get $i i32.const 2 i32.and i32.eqz
        if
          local.get $i i32.const 1 i32.add local.set $i
          br $again
        end
        local.get $sum local.get $i i32.add local.set $sum
        local.get $i i32.const 1 i32.add local.set $i
        br $again
      end
    end
    local.get $sum)

  (func (export "outer_continue_from_inner") (param $continue_outer i32)
    (result i32) (local $outer i32) (local $inner i32)
    block $exit
      loop $outer_loop
        local.get $outer i32.const 2 i32.ge_u br_if $exit
        i32.const 0 local.set $inner
        block $inner_exit
          loop $inner_loop
            local.get $continue_outer
            if
              i32.const 0 local.set $continue_outer
              i32.const 1 local.set $outer
              br $outer_loop
            end
            local.get $inner i32.const 1 i32.add local.set $inner
            local.get $inner i32.const 2 i32.ge_u br_if $inner_exit
            br $inner_loop
          end
        end
        local.get $outer i32.const 1 i32.add local.set $outer
        br $outer_loop
      end
    end
    local.get $outer)

  (func (export "nested_block_continue") (param $limit i32) (result i32)
    (local $i i32) (local $sum i32)
    loop $again
      block $nested
        local.get $i i32.const 1 i32.add local.set $i
        local.get $i i32.const 3 i32.eq
        if
          i32.const 7 local.set $sum
          br $again
        end
      end
      local.get $sum local.get $i i32.add local.set $sum
      local.get $i local.get $limit i32.lt_u
      br_if $again
    end
    local.get $sum)

  (func (export "nested_else_branch") (result i32)
    (local $i i32) (local $sum i32)
    block $exit
      loop $again
        block $nested
          local.get $i i32.const 1 i32.add local.set $i
          local.get $i i32.const 1 i32.eq
          if
            i32.const 9 local.set $sum
            br $again
          else
            i32.const 1 local.set $sum
            br $exit
          end
        end
        br $again
      end
    end
    local.get $sum))
