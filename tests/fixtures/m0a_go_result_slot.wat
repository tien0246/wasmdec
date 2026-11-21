;; M0/3.1 (todo 3.1): Go result-slot fixture — the fmt.Sprintf shape.
;;
;;   callee $sprintf_like: 1 string param (2x i64 slots @0..16) + variadic
;;   slice (3x i64 slots @16..40) + one string result (2x i64 slots
;;   @40..56), written at the incoming window past the param window and
;;   feeding the frame epilogue (G5 frame evidence).
;;
;;   caller $go_result_caller: pushes the outgoing-arg window [L, L+40),
;;   runs the full G3 call protocol, refreshes SP and reads the result
;;   string back at [L+40, L+56) — the exact cell the callee stored
;;   ("caller đọc đúng offset", the G5-S2b caller cross-check).
;;
;; Layout (byte-exact, self-consistent):
;;   caller L = SP0 - 48 (frame 48); args at [L+0, L+40); ret push [L-8].
;;   callee entry SP = L-8; prologue frame 24 -> LC = L-32; incoming
;;   window base = LC + 24 + 8 = L; params rel 0..40, results rel 40..56.
;;   callee restores SP by exactly frame_size (24) -> L-8; caller reads at
;;   refresh+48 = L+40 (and refresh+56 = L+48).
(module
  (import "gojs" "runtime.wasmExit" (func $wasmExit (param i32)))
  (global $sp (mut i32) (i32.const 0))
  (global $ctxt (mut i64) (i64.const 0))
  (global $g (mut i64) (i64.const 0))
  (global $ret0 (mut i64) (i64.const 0))
  (global $ret1 (mut i64) (i64.const 0))
  (global $ret2 (mut i64) (i64.const 0))
  (global $ret3 (mut i64) (i64.const 0))
  (global $pause (mut i32) (i32.const 0))
  (global $stack_top i32 (i32.const 65536))
  (memory 1)

  (func $sprintf_like (param $pcb i32) (result i32)
    (local $sp i32)
    (local $t0 i64)
    global.get $sp
    local.set $sp
    block $unwind
      block $c0
        local.get $pcb
        br_table 0 1
      end
      ;; case 0: prologue SP -= 24
      local.get $sp
      i32.const 24
      i32.sub
      local.tee $sp
      global.set $sp
      ;; incoming params: format string {ptr,len} @0..16 (2x i64 slots)
      local.get $sp
      i64.extend_i32_u
      i64.const 24
      i64.add
      i32.wrap_i64
      i64.load
      local.set $t0
      local.get $sp
      i64.extend_i32_u
      i64.const 32
      i64.add
      i32.wrap_i64
      i64.load
      local.set $t0
      ;; variadic []any {ptr,len,cap} @16..40 (3x i64 slots)
      local.get $sp
      i64.extend_i32_u
      i64.const 40
      i64.add
      i32.wrap_i64
      i64.load
      local.set $t0
      local.get $sp
      i64.extend_i32_u
      i64.const 48
      i64.add
      i32.wrap_i64
      i64.load
      local.set $t0
      local.get $sp
      i64.extend_i32_u
      i64.const 56
      i64.add
      i32.wrap_i64
      i64.load
      local.set $t0
      ;; result string {ptr,len} @40..56 (rel past the param window)
      local.get $sp
      i64.extend_i32_u
      i64.const 72
      i64.add
      i32.wrap_i64
      local.get $t0
      i64.store
      local.get $sp
      i64.extend_i32_u
      i64.const 80
      i64.add
      i32.wrap_i64
      local.get $t0
      i64.store
      ;; frame epilogue: restore exactly frame_size (24)
      local.get $sp
      i32.const 24
      i32.add
      local.tee $sp
      global.set $sp
      i32.const 0
      return
    end
    i32.const 1)

  (func (export "go_result_caller") (param $pcb i32) (result i32)
    (local $sp i32)
    (local $t0 i64)
    global.get $sp
    local.set $sp
    block $unwind
      block $c0
        local.get $pcb
        br_table 0 1
      end
      ;; case 0: caller frame SP -= 48, args base L
      local.get $sp
      i32.const 48
      i32.sub
      local.tee $sp
      global.set $sp
      ;; outgoing args: format {111, 9} @0..16
      local.get $sp
      i64.const 111
      i64.store
      local.get $sp
      i64.extend_i32_u
      i64.const 8
      i64.add
      i32.wrap_i64
      i64.const 9
      i64.store
      ;; variadic slice {0, 0, 0} @16..40
      local.get $sp
      i64.extend_i32_u
      i64.const 16
      i64.add
      i32.wrap_i64
      i64.const 0
      i64.store
      local.get $sp
      i64.extend_i32_u
      i64.const 24
      i64.add
      i32.wrap_i64
      i64.const 0
      i64.store
      local.get $sp
      i64.extend_i32_u
      i64.const 32
      i64.add
      i32.wrap_i64
      i64.const 0
      i64.store
      ;; G3 call protocol: SP -= 8, push the resume address
      local.get $sp
      i32.const 8
      i32.sub
      local.tee $sp
      global.set $sp
      local.get $sp
      i64.const 0x10001
      i64.store
      i32.const 0
      call $sprintf_like
      global.get $sp
      local.set $sp
      br_if $unwind
      ;; post-call reads: the result window [L+40, L+56) — refresh SP is
      ;; L-8 (the callee restored by exactly its frame), so +48 / +56.
      local.get $sp
      i64.extend_i32_u
      i64.const 48
      i64.add
      i32.wrap_i64
      i64.load
      local.set $t0
      local.get $sp
      i64.extend_i32_u
      i64.const 56
      i64.add
      i32.wrap_i64
      i64.load
      local.set $t0
      ;; epilogue: undo the call slot then the frame
      local.get $sp
      i32.const 8
      i32.add
      local.tee $sp
      global.set $sp
      local.get $sp
      i32.const 48
      i32.add
      local.tee $sp
      global.set $sp
      local.get $t0
      i32.wrap_i64
      return
    end
    i32.const 1)

)
