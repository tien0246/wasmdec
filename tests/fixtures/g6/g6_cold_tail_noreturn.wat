(module
  ;; G6: Structural terminal runtime callee proof — cold-tail fallthrough chain
  ;; severance reproducer + near-miss backward-jump helper.
  ;;
  ;; Defines:
  ;;   1. `$terminal_helper` (defined body 0): valid Go IR (`present`,
  ;;      `unwind_exit`, `pc_b_register_proof`), ZERO `return` (0x0F)
  ;;      instructions in its body (`ir.ret.empty()`), NO backward jump in
  ;;      `ir.jmps`, and not `runtime.morestack[_noctxt]`. Qualifies
  ;;      structurally as a terminal noreturn helper.
  ;;   2. `$loop_helper` (defined body 1): near-miss helper with ZERO `return`
  ;;      (0x0F) instructions in its body (`ir.ret.empty()`), but WITH a
  ;;      backward const-JMP edge (`i32.const 1; local.set 0; br $entry` to an
  ;;      earlier target offset). Must NOT qualify as a terminal helper.
  ;;   3. `cold_tail_severed` (defined body 2): contains 3 consecutive cold
  ;;      error-check blocks at the function tail (`$b1`, `$b2`, `$b3`), each
  ;;      ending with a standard Go call to `$terminal_helper` (`SP -= 8;
  ;;      i64.store ret_pc; i32.const 0; call $terminal_helper; global.get SP;
  ;;      local.set 1; br_if $unwind`). Because `$terminal_helper` is proven
  ;;      terminal, consuming each post-call `br_if $unwind` emits a trap
  ;;      terminator (`__builtin_trap`), severing the fallthrough chain so each
  ;;      cold block becomes a leaf and no `goto` chain remains in the
  ;;      decompiled output.
  ;;   4. `cold_tail_near_miss_loop` (defined body 3): contains 3 consecutive
  ;;      cold blocks calling `$loop_helper` (which has a backward jump).
  ;;      Because `$loop_helper` does NOT qualify, the cold blocks are not
  ;;      terminated at `br_if $unwind` and keep their fallthrough chain.
  (type $pcb_fn (func (param i32) (result i32)))
  (import "gojs" "runtime.wasmExit" (func $wasmExit (param i32)))
  (global $sp (mut i32) (i32.const 0))
  (global $ctxt (mut i64) (i64.const 0))
  (global $g (mut i64) (i64.const 0))
  (global $ret0 (mut i64) (i64.const 0))
  (global $ret1 (mut i64) (i64.const 0))
  (global $ret2 (mut i64) (i64.const 0))
  (global $ret3 (mut i64) (i64.const 0))
  (global $pause (mut i32) (i32.const 0))
  (memory 1)

  ;; Defined body 0 (fn_index = 1): terminal helper (no 0x0F return, no backward jump)
  (func $terminal_helper (type $pcb_fn) (param $pcb i32) (result i32)
    (local $sp i32)
    global.get $sp
    local.set 1
    block $unwind
      loop $entry
        block $b1
          block $b0
            local.get 0
            br_table $b0 $b1 $unwind
          end
          local.get 1
          i32.const 16
          i32.sub
          local.tee 1
          global.set $sp
          local.get 1
          i64.load offset=24
          local.get 1
          i64.load offset=32
          i64.add
          drop
          ;; Forward const-JMP to $b1 (target_offset > br_offset)
          i32.const 1
          local.set 0
          br $b1
        end
        i32.const 2
        call $wasmExit
        unreachable
      end
    end
    i32.const 1)

  ;; Defined body 1 (fn_index = 2): near-miss helper (no 0x0F return, WITH backward jump)
  (func $loop_helper (type $pcb_fn) (param $pcb i32) (result i32)
    (local $sp i32)
    global.get $sp
    local.set 1
    block $unwind
      loop $entry
        block $b1
          block $b0
            local.get 0
            br_table $b0 $b1 $unwind
          end
          local.get 1
          i32.const 16
          i32.sub
          local.tee 1
          global.set $sp
          local.get 1
          i64.load offset=24
          drop
        end
        ;; Backward const-JMP: case 1 targets `end $b1` which is BEFORE this `br $entry`!
        i32.const 1
        local.set 0
        br $entry
      end
    end
    i32.const 1)

  ;; Defined body 2 (fn_index = 3): 3 consecutive cold blocks calling $terminal_helper
  (func (export "cold_tail_severed") (type $pcb_fn) (param $pcb i32) (result i32)
    (local $sp i32)
    (local $idx0 i64)
    (local $idx1 i64)
    (local $idx2 i64)
    global.get $sp
    local.set 1
    block $unwind
      loop $entry
        block $b3
          block $b2
            block $b1
              block $b0
                local.get 0
                br_table $b0 $b1 $b2 $b3 $unwind
              end
              local.get 1
              i32.const 32
              i32.sub
              local.tee 1
              global.set $sp
              local.get 1
              i64.load offset=40
              local.tee 2
              i64.const 10
              i64.ge_u
              if
                i32.const 1
                local.set 0
                br $b1
              end
              local.get 1
              i64.load offset=48
              local.tee 3
              i64.const 20
              i64.ge_u
              if
                i32.const 2
                local.set 0
                br $b2
              end
              local.get 1
              i64.load offset=56
              local.tee 4
              i64.const 30
              i64.ge_u
              if
                i32.const 3
                local.set 0
                br $b3
              end
              local.get 1
              i32.const 32
              i32.add
              local.tee 1
              global.set $sp
              i32.const 0
              return
            end
            ;; Cold block 1 ($b1): calls $terminal_helper(idx0, 10)
            local.get 1
            local.get 2
            i64.store offset=0
            local.get 1
            i64.const 10
            i64.store offset=8
            local.get 1
            i32.const 8
            i32.sub
            local.tee 1
            global.set $sp
            local.get 1
            i64.const 0x10001
            i64.store offset=0
            i32.const 0
            call $terminal_helper
            global.get $sp
            local.set 1
            br_if $unwind
          end
          ;; Cold block 2 ($b2): calls $terminal_helper(idx1, 20)
          local.get 1
          local.get 3
          i64.store offset=0
          local.get 1
          i64.const 20
          i64.store offset=8
          local.get 1
          i32.const 8
          i32.sub
          local.tee 1
          global.set $sp
          local.get 1
          i64.const 0x10002
          i64.store offset=0
          i32.const 0
          call $terminal_helper
          global.get $sp
          local.set 1
          br_if $unwind
        end
        ;; Cold block 3 ($b3): calls $terminal_helper(idx2, 30)
        local.get 1
        local.get 4
        i64.store offset=0
        local.get 1
        i64.const 30
        i64.store offset=8
        local.get 1
        i32.const 8
        i32.sub
        local.tee 1
        global.set $sp
        local.get 1
        i64.const 0x10003
        i64.store offset=0
        i32.const 0
        call $terminal_helper
        global.get $sp
        local.set 1
        br_if $unwind
        unreachable
      end
    end
    i32.const 1)

  ;; Defined body 3 (fn_index = 4): near-miss with 3 cold blocks calling $loop_helper
  (func (export "cold_tail_near_miss_loop") (type $pcb_fn) (param $pcb i32) (result i32)
    (local $sp i32)
    (local $idx0 i64)
    (local $idx1 i64)
    (local $idx2 i64)
    global.get $sp
    local.set 1
    block $unwind
      loop $entry
        block $b3
          block $b2
            block $b1
              block $b0
                local.get 0
                br_table $b0 $b1 $b2 $b3 $unwind
              end
              local.get 1
              i32.const 32
              i32.sub
              local.tee 1
              global.set $sp
              local.get 1
              i64.load offset=40
              local.tee 2
              i64.const 10
              i64.ge_u
              if
                i32.const 1
                local.set 0
                br $b1
              end
              local.get 1
              i64.load offset=48
              local.tee 3
              i64.const 20
              i64.ge_u
              if
                i32.const 2
                local.set 0
                br $b2
              end
              local.get 1
              i64.load offset=56
              local.tee 4
              i64.const 30
              i64.ge_u
              if
                i32.const 3
                local.set 0
                br $b3
              end
              local.get 1
              i32.const 32
              i32.add
              local.tee 1
              global.set $sp
              i32.const 0
              return
            end
            local.get 1
            local.get 2
            i64.store offset=0
            local.get 1
            i32.const 8
            i32.sub
            local.tee 1
            global.set $sp
            local.get 1
            i64.const 0x10001
            i64.store offset=0
            i32.const 0
            call $loop_helper
            global.get $sp
            local.set 1
            br_if $unwind
          end
          local.get 1
          local.get 3
          i64.store offset=0
          local.get 1
          i32.const 8
          i32.sub
          local.tee 1
          global.set $sp
          local.get 1
          i64.const 0x10002
          i64.store offset=0
          i32.const 0
          call $loop_helper
          global.get $sp
          local.set 1
          br_if $unwind
        end
        local.get 1
        local.get 4
        i64.store offset=0
        local.get 1
        i32.const 8
        i32.sub
        local.tee 1
        global.set $sp
        local.get 1
        i64.const 0x10003
        i64.store offset=0
        i32.const 0
        call $loop_helper
        global.get $sp
        local.set 1
        br_if $unwind
        unreachable
      end
    end
    i32.const 1)

  (data (i32.const 57056)
    "\f1\ff\ff\ff\00\00\01\08\04\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00"
    "\48\00\00\00\00\00\00\00\a8\00\00\00\00\00\00\00\a8\00\00\00\00\00\00\00\a8\00\00\00\00\00\00\00"
    "\a8\00\00\00\00\00\00\00\72\75\6e\74\69\6d\65\2e\67\6f\50\61\6e\69\63\49\6e\64\65\78\00\72\75\6e"
    "\74\69\6d\65\2e\62\67\73\77\65\65\70\00\6d\61\69\6e\2e\63\6f\6c\64\5f\74\61\69\6c\5f\73\65\76\65"
    "\72\65\64\00\6d\61\69\6e\2e\63\6f\6c\64\5f\74\61\69\6c\5f\6e\65\61\72\5f\6d\69\73\73\00\00\00\00"
    "\00\00\00\00\00\00\00\00\00\10\00\00\28\00\00\00\01\10\00\00\30\00\00\00\02\10\00\00\38\00\00\00"
    "\03\10\00\00\40\00\00\00\04\10\00\00\00\00\00\00\00\10\00\00\00\00\00\00\01\10\00\00\15\00\00\00"
    "\02\10\00\00\25\00\00\00\03\10\00\00\3c\00\00\00")
)
