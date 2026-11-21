;; A2 this-param rule: only real member functions get `this`.
;;
;; Three exported functions with Itanium-mangled names:
;;   _ZN2ns4funcEii     ns::func(int, int)   — namespace function: 2 wasm
;;                        params == 2 mangled params -> NO this (a1, a2)
;;   _ZN2ns3Cls4funcEi  ns::Cls::func(int)   — method: 2 wasm params ==
;;                        1 mangled param + implicit this -> arg0 = this
;;   _ZN2ns3ClsC1Ev     ns::Cls::Cls()       — ctor (C1 tail): 1 wasm param
;;                        -> arg0 = this
;;
;; Note the manglings: "Cls" is 3 characters, so the component is 3Cls
;; (4Cls would swallow the following digit and leave the name malformed),
;; and a constructor is spelled C1/C2, not by repeating the class name.
(module
  (type $two_i32 (func (param i32 i32) (result i32)))
  (type $one_i32 (func (param i32) (result i32)))

  ;; ns::func(i32, i32) -> i32 — namespace-level, expects (a1, a2)
  (func (export "_ZN2ns4funcEii") (type $two_i32)
    local.get 0
    local.get 1
    i32.add)

  ;; ns::Cls::func(i32) -> i32 — method (this + i32), expects (this, a2)
  (func (export "_ZN2ns3Cls4funcEi") (type $two_i32)
    local.get 0
    local.get 1
    i32.add)

  ;; ns::Cls::Cls() -> i32 — complete-object ctor, expects (this)
  (func (export "_ZN2ns3ClsC1Ev") (type $one_i32)
    i32.const 7)
)
