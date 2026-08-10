(module
  (func $add_simd
    (param $a0 i32) (param $a1 i32) (param $a2 i32) (param $a3 i32)
    (param $b0 i32) (param $b1 i32) (param $b2 i32) (param $b3 i32)
    (result v128)

    (local $lhs v128)
    (local $rhs v128)

    local.get $a0
    i32x4.splat
    local.set $lhs

    local.get $lhs
    local.get $a1
    i32x4.replace_lane 1
    local.set $lhs

    local.get $lhs
    local.get $a2
    i32x4.replace_lane 2
    local.set $lhs

    local.get $lhs
    local.get $a3
    i32x4.replace_lane 3
    local.set $lhs

    local.get $b0
    i32x4.splat
    local.set $rhs

    local.get $rhs
    local.get $b1
    i32x4.replace_lane 1
    local.set $rhs

    local.get $rhs
    local.get $b2
    i32x4.replace_lane 2
    local.set $rhs

    local.get $rhs
    local.get $b3
    i32x4.replace_lane 3
    local.set $rhs

    local.get $lhs
    local.get $rhs
    i32x4.add
  )
  (export "add_simd" (func $add_simd))
)
