;; The matching binary uses one recursive group that WABT 1.0.42 cannot assemble.
(module
  (type $left (struct (field (ref null $right))))
  (type $right (struct (field (ref null $left))))
  (import "env" "shared" (table 1 (ref null $left)))
)
