(module
  (type $payload_i32 (func (param i32)))
  (tag $signal (type $payload_i32))

  (func (export "i64_shr_0") (param i64) (result i64)
    local.get 0
    i64.const 0
    i64.shr_u)
  (func (export "i64_shr_31") (param i64) (result i64)
    local.get 0
    i64.const 31
    i64.shr_u)
  (func (export "i64_shr_32") (param i64) (result i64)
    local.get 0
    i64.const 32
    i64.shr_u)
  (func (export "i64_shr_63") (param i64) (result i64)
    local.get 0
    i64.const 63
    i64.shr_u)
  (func (export "compare_branch") (param i32 i32 i32 i32)
    block
      block
        local.get 3
        if
          local.get 3
          i32.const 0
          i32.gt_s
          br_if 1
          local.get 3
          br_if 2
          local.get 2
          br_if 1
          br 2
          block
            local.get 3
            if
            end
          end
        end
      end
    end)
  (func (export "f32_return") (param f32) (result f32)
    local.get 0)
  (func (export "f64_return") (param f64) (result f64)
    local.get 0)
  (func (export "br_if_function_result") (param i32 i32) (result i32)
    local.get 1
    local.get 0
    br_if 0)
  (func (export "br_function_result") (param i32) (result i32)
    local.get 0
    br 0)
  (func (export "br_table_function_result") (param i32 i32) (result i32)
    block (result i32)
      local.get 1
      local.get 0
      br_table 1 0
    end)
  (func (export "nested_br_if_function_result") (param i32 i32) (result i32)
    block
      block
        block
          local.get 1
          local.get 0
          br_if 3
          drop
        end
      end
    end
    i32.const 7)
  (func (export "br_on_null_extra_stack") (param externref) (result i32)
    block $done (result i32)
      i32.const 99
      i32.const 10
      local.get 0
      br_on_null $done
      drop
      i32.add
    end)
  (func (export "throw_extra_stack") (result i32)
    (try
      (do
        (block
          i32.const 99
          i32.const 42
          throw $signal))
      (catch $signal
        i32.const 1
        i32.add
        return))
    unreachable)
  (func (export "select_ref") (param funcref funcref i32) (result funcref)
    local.get 0
    local.get 1
    local.get 2
    select (result funcref))
  (memory (export "mem") 51)
  (func (export "load64_offset8") (param i32) (result i64)
    local.get 0
    i64.load offset=8 align=8)
  (func $start (export "run_start_stub")
    nop)
  (func (export "_ZN5arrow7compute8internal18GetBinaryLikeCastsEv")
    nop)
  (start $start)
)
