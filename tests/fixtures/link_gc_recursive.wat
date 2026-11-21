;; WABT 1.0.42 cannot assemble recursive GC types; the paired binary uses their core encodings.
(module
  (type $node (struct (field (ref null $node))))
  (table 1 (ref null $node)))
