(module
  ;; HCSUDA browser demo: WASM CPU reference for the documented vecadd shape.
  ;; Memory layout: a at 0, b at n*4, c at 2*n*4 (f32 each).
  (memory (export "memory") 1)
  (func (export "hcs_vecadd_ref")
    (param $n i32) (param $a i32) (param $b i32) (param $c i32) (param $alpha f32)
    (local $i i32)
    (block $exit
      (loop $loop
        (br_if $exit (i32.ge_u (local.get $i) (local.get $n)))
        (f32.store
          (i32.add (local.get $c) (i32.mul (local.get $i) (i32.const 4)))
          (f32.add
            (f32.load (i32.add (local.get $a) (i32.mul (local.get $i) (i32.const 4))))
            (f32.mul
              (f32.load (i32.add (local.get $b) (i32.mul (local.get $i) (i32.const 4))))
              (local.get $alpha))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $loop)))))
