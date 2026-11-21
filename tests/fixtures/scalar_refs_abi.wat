(module
  (type $import_type (func (param funcref externref) (result externref)))
  (type $defined_type (func (param externref funcref) (result funcref)))
  (import "env" "ref_roundtrip" (func $import_ref (type $import_type)))
  (func $defined_ref (type $defined_type)
    local.get 1)
  (func (export "call_import_ref") (type $import_type)
    local.get 0
    local.get 1
    call $import_ref)
  (func (export "call_defined_ref") (type $defined_type)
    local.get 0
    local.get 1
    call $defined_ref))
