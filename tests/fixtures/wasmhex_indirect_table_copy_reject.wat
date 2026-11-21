(module
  (type $unary_i32 (func (param i32) (result i32)))
  (import "env" "imported" (table $imported 3 3 funcref))
  (table $exported 3 3 funcref)
  (table $growing 3 5 funcref)
  (table $cross_dest 3 3 funcref)
  (table $cross_src 3 3 funcref)
  (table $unknown_index 3 3 funcref)
  (table $unknown_reference 3 3 funcref)
  (table $ambiguous 3 3 funcref)
  (table $loop 3 3 funcref)
  (table $unknown_length 3 3 funcref)
  (table $out_of_bounds 3 3 funcref)
  (table $active_cross_dest 3 3 funcref)
  (table $active_cross_src 3 3 funcref)
  (table $active_unknown 3 3 funcref)
  (table $active_loop 3 3 funcref)
  (table $active_oob 3 3 funcref)
  (table $active_overwrite 3 3 funcref)
  (elem declare func $target0 $target1)
  (elem (table $active_cross_src) (i32.const 0) func $target1)
  (elem (table $active_unknown) (i32.const 0) func $target1)
  (elem (table $active_loop) (i32.const 0) func $target1)
  (elem (table $active_oob) (i32.const 0) func $target1)
  (elem (table $active_overwrite) (i32.const 0) func $target1)
  (func $target0 (type $unary_i32) (param i32) (result i32)
    local.get 0
    i32.const 10
    i32.add)
  (func $target1 (type $unary_i32) (param i32) (result i32)
    local.get 0
    i32.const 20
    i32.add)
  (func $copy_cross_table (type $unary_i32) (param i32) (result i32)
    i32.const 0
    ref.func $target1
    table.set $cross_src
    i32.const 1
    i32.const 0
    i32.const 1
    table.copy $cross_dest $cross_src
    local.get 0
    i32.const 1
    call_indirect $cross_dest (type $unary_i32))
  (func $copy_unknown_index (param i32 i32) (result i32)
    i32.const 0
    ref.func $target1
    table.set $unknown_index
    local.get 1
    i32.const 0
    i32.const 1
    table.copy $unknown_index $unknown_index
    local.get 0
    i32.const 1
    call_indirect $unknown_index (type $unary_i32))
  (func $copy_unknown_reference (param i32 funcref) (result i32)
    i32.const 0
    local.get 1
    table.set $unknown_reference
    i32.const 1
    i32.const 0
    i32.const 1
    table.copy $unknown_reference $unknown_reference
    local.get 0
    i32.const 1
    call_indirect $unknown_reference (type $unary_i32))
  (func $copy_unknown_length (param i32 i32) (result i32)
    i32.const 0
    ref.func $target1
    table.set $unknown_length
    i32.const 1
    i32.const 0
    local.get 0
    table.copy $unknown_length $unknown_length
    local.get 1
    i32.const 1
    call_indirect $unknown_length (type $unary_i32))
  (func $copy_out_of_bounds (type $unary_i32) (param i32) (result i32)
    i32.const 1
    i32.const 2
    i32.const 2
    table.copy $out_of_bounds $out_of_bounds
    i32.const 0
    ref.func $target1
    table.set $out_of_bounds
    local.get 0
    i32.const 1
    call_indirect $out_of_bounds (type $unary_i32))
  (func $copy_active_cross_table (type $unary_i32) (param i32) (result i32)
    i32.const 1
    i32.const 0
    i32.const 1
    table.copy $active_cross_dest $active_cross_src
    local.get 0
    i32.const 1
    call_indirect $active_cross_dest (type $unary_i32))
  (func $copy_active_unknown_index (param i32 i32) (result i32)
    local.get 1
    i32.const 0
    i32.const 1
    table.copy $active_unknown $active_unknown
    local.get 0
    i32.const 1
    call_indirect $active_unknown (type $unary_i32))
  (func $copy_active_in_loop (type $unary_i32) (param i32) (result i32)
    loop $again
      i32.const 1
      i32.const 0
      i32.const 1
      table.copy $active_loop $active_loop
      local.get 0
      br_if $again
    end
    local.get 0
    i32.const 1
    call_indirect $active_loop (type $unary_i32))
  (func $copy_active_oob (type $unary_i32) (param i32) (result i32)
    i32.const 0
    i32.const 2
    i32.const 2
    table.copy $active_oob $active_oob
    local.get 0
    i32.const 0
    call_indirect $active_oob (type $unary_i32))
  (func $copy_active_overwrites_initial_slot (type $unary_i32) (param i32) (result i32)
    i32.const 0
    i32.const 0
    i32.const 1
    table.copy $active_overwrite $active_overwrite
    local.get 0
    i32.const 0
    call_indirect $active_overwrite (type $unary_i32))
  (func $copy_ambiguous_join (param i32 i32) (result i32)
    local.get 0
    if
      i32.const 0
      ref.func $target0
      table.set $ambiguous
    else
      i32.const 0
      ref.func $target1
      table.set $ambiguous
    end
    i32.const 1
    i32.const 0
    i32.const 1
    table.copy $ambiguous $ambiguous
    local.get 1
    i32.const 1
    call_indirect $ambiguous (type $unary_i32))
  (func $copy_in_loop (type $unary_i32) (param i32) (result i32)
    i32.const 0
    ref.func $target1
    table.set $loop
    loop $again
      i32.const 1
      i32.const 0
      i32.const 1
      table.copy $loop $loop
      local.get 0
      br_if $again
    end
    local.get 0
    i32.const 1
    call_indirect $loop (type $unary_i32))
  (func $copy_imported (type $unary_i32) (param i32) (result i32)
    i32.const 0
    ref.func $target1
    table.set $imported
    i32.const 1
    i32.const 0
    i32.const 1
    table.copy $imported $imported
    local.get 0
    i32.const 1
    call_indirect $imported (type $unary_i32))
  (func $copy_exported (type $unary_i32) (param i32) (result i32)
    i32.const 0
    ref.func $target1
    table.set $exported
    i32.const 1
    i32.const 0
    i32.const 1
    table.copy $exported $exported
    local.get 0
    i32.const 1
    call_indirect $exported (type $unary_i32))
  (func $copy_after_growth (type $unary_i32) (param i32) (result i32)
    ref.null func
    i32.const 1
    table.grow $growing
    drop
    i32.const 0
    ref.func $target1
    table.set $growing
    i32.const 1
    i32.const 0
    i32.const 1
    table.copy $growing $growing
    local.get 0
    i32.const 1
    call_indirect $growing (type $unary_i32))
  (export "table" (table $exported))
  (export "copy_active_oob" (func $copy_active_oob)))
