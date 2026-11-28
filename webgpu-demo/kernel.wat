(module
  ;; HCSUDA browser demo: WASM CPU references for the built-in kernel shapes.
  ;; All pointers are byte offsets into the exported memory; floats are f32.
  (memory (export "memory") 4)

  ;; c[i] = a[i] + alpha*b[i]
  (func (export "hcs_vecadd_ref")
    (param $n i32) (param $a i32) (param $b i32) (param $c i32) (param $alpha f32)
    (local $i i32)
    (block $exit (loop $loop
      (br_if $exit (i32.ge_u (local.get $i) (local.get $n)))
      (f32.store
        (i32.add (local.get $c) (i32.mul (local.get $i) (i32.const 4)))
        (f32.add
          (f32.load (i32.add (local.get $a) (i32.mul (local.get $i) (i32.const 4))))
          (f32.mul
            (f32.load (i32.add (local.get $b) (i32.mul (local.get $i) (i32.const 4))))
            (local.get $alpha))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $loop))))

  ;; y[i] = alpha*x[i]
  (func (export "hcs_scale_ref")
    (param $n i32) (param $x i32) (param $y i32) (param $alpha f32)
    (local $i i32)
    (block $exit (loop $loop
      (br_if $exit (i32.ge_u (local.get $i) (local.get $n)))
      (f32.store
        (i32.add (local.get $y) (i32.mul (local.get $i) (i32.const 4)))
        (f32.mul (f32.load (i32.add (local.get $x) (i32.mul (local.get $i) (i32.const 4))))
                 (local.get $alpha)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $loop))))

  ;; out[g] = sum of chunk g, chunk = ceil(n/groups)
  (func (export "hcs_reduce_ref")
    (param $n i32) (param $x i32) (param $out i32) (param $groups i32)
    (local $g i32) (local $first i32) (local $last i32) (local $chunk i32) (local $limit i32)
    (local $i i32) (local $sum f32)
    (local.set $chunk (i32.div_u (i32.add (i32.add (local.get $n) (local.get $groups))
                                          (i32.const -1))
                                 (local.get $groups)))
    (block $gExit (loop $gLoop
      (br_if $gExit (i32.ge_u (local.get $g) (local.get $groups)))
      (local.set $first (i32.mul (local.get $g) (local.get $chunk)))
      (local.set $limit (i32.add (local.get $first) (local.get $chunk)))
      (local.set $last (select (local.get $n) (local.get $limit)
                               (i32.lt_u (local.get $n) (local.get $limit))))
      (local.set $i (local.get $first))
      (local.set $sum (f32.const 0))
      (block $iExit (loop $iLoop
        (br_if $iExit (i32.ge_u (local.get $i) (local.get $last)))
        (local.set $sum (f32.add (local.get $sum)
          (f32.load (i32.add (local.get $x) (i32.mul (local.get $i) (i32.const 4))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $iLoop)))
      (f32.store (i32.add (local.get $out) (i32.mul (local.get $g) (i32.const 4)))
                 (local.get $sum))
      (local.set $g (i32.add (local.get $g) (i32.const 1)))
      (br $gLoop))))

  ;; y[i] = exclusive prefix within chunk g
  (func (export "hcs_scan_ref")
    (param $n i32) (param $x i32) (param $y i32) (param $groups i32)
    (local $g i32) (local $first i32) (local $last i32) (local $chunk i32) (local $limit i32)
    (local $i i32) (local $prefix f32)
    (local.set $chunk (i32.div_u (i32.add (i32.add (local.get $n) (local.get $groups))
                                          (i32.const -1))
                                 (local.get $groups)))
    (block $gExit (loop $gLoop
      (br_if $gExit (i32.ge_u (local.get $g) (local.get $groups)))
      (local.set $first (i32.mul (local.get $g) (local.get $chunk)))
      (local.set $limit (i32.add (local.get $first) (local.get $chunk)))
      (local.set $last (select (local.get $n) (local.get $limit)
                               (i32.lt_u (local.get $n) (local.get $limit))))
      (local.set $i (local.get $first))
      (local.set $prefix (f32.const 0))
      (block $iExit (loop $iLoop
        (br_if $iExit (i32.ge_u (local.get $i) (local.get $last)))
        (f32.store (i32.add (local.get $y) (i32.mul (local.get $i) (i32.const 4)))
                   (local.get $prefix))
        (local.set $prefix (f32.add (local.get $prefix)
          (f32.load (i32.add (local.get $x) (i32.mul (local.get $i) (i32.const 4))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $iLoop)))
      (local.set $g (i32.add (local.get $g) (i32.const 1)))
      (br $gLoop))))

  ;; hist[value & 255]++ over u32 input
  (func (export "hcs_histogram_ref")
    (param $n i32) (param $x i32) (param $h i32)
    (local $i i32) (local $bin i32) (local $addr i32)
    (block $zeroExit (loop $zeroLoop
      (br_if $zeroExit (i32.ge_u (local.get $i) (i32.const 256)))
      (i32.store (i32.add (local.get $h) (i32.mul (local.get $i) (i32.const 4))) (i32.const 0))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $zeroLoop)))
    (local.set $i (i32.const 0))
    (block $exit (loop $loop
      (br_if $exit (i32.ge_u (local.get $i) (local.get $n)))
      (local.set $bin (i32.and (i32.load (i32.add (local.get $x)
        (i32.mul (local.get $i) (i32.const 4)))) (i32.const 255)))
      (local.set $addr (i32.add (local.get $h) (i32.mul (local.get $bin) (i32.const 4))))
      (i32.store (local.get $addr) (i32.add (i32.load (local.get $addr)) (i32.const 1)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $loop))))

  ;; c[row*n+col] = sum_t a[row*k+t] * b[t*n+col]
  (func (export "hcs_matmul_ref")
    (param $m i32) (param $n i32) (param $k i32)
    (param $a i32) (param $b i32) (param $c i32)
    (local $row i32) (local $col i32) (local $t i32) (local $sum f32)
    (block $rowExit (loop $rowLoop
      (br_if $rowExit (i32.ge_u (local.get $row) (local.get $m)))
      (local.set $col (i32.const 0))
      (block $colExit (loop $colLoop
        (br_if $colExit (i32.ge_u (local.get $col) (local.get $n)))
        (local.set $t (i32.const 0))
        (local.set $sum (f32.const 0))
        (block $tExit (loop $tLoop
          (br_if $tExit (i32.ge_u (local.get $t) (local.get $k)))
          (local.set $sum (f32.add (local.get $sum)
            (f32.mul
              (f32.load (i32.add (local.get $a)
                (i32.mul (i32.add (i32.mul (local.get $row) (local.get $k))
                                  (local.get $t)) (i32.const 4))))
              (f32.load (i32.add (local.get $b)
                (i32.mul (i32.add (i32.mul (local.get $t) (local.get $n))
                                  (local.get $col)) (i32.const 4)))))))
          (local.set $t (i32.add (local.get $t) (i32.const 1)))
          (br $tLoop)))
        (f32.store (i32.add (local.get $c)
          (i32.mul (i32.add (i32.mul (local.get $row) (local.get $n))
                            (local.get $col)) (i32.const 4))) (local.get $sum))
        (local.set $col (i32.add (local.get $col) (i32.const 1)))
        (br $colLoop)))
      (local.set $row (i32.add (local.get $row) (i32.const 1)))
      (br $rowLoop))))

  ;; b[col*rows+row] = a[row*cols+col]
  (func (export "hcs_transpose_ref")
    (param $rows i32) (param $cols i32) (param $a i32) (param $b i32)
    (local $row i32) (local $col i32)
    (block $rowExit (loop $rowLoop
      (br_if $rowExit (i32.ge_u (local.get $row) (local.get $rows)))
      (local.set $col (i32.const 0))
      (block $colExit (loop $colLoop
        (br_if $colExit (i32.ge_u (local.get $col) (local.get $cols)))
        (f32.store
          (i32.add (local.get $b)
            (i32.mul (i32.add (i32.mul (local.get $col) (local.get $rows))
                              (local.get $row)) (i32.const 4)))
          (f32.load (i32.add (local.get $a)
            (i32.mul (i32.add (i32.mul (local.get $row) (local.get $cols))
                              (local.get $col)) (i32.const 4)))))
        (local.set $col (i32.add (local.get $col) (i32.const 1)))
        (br $colLoop)))
      (local.set $row (i32.add (local.get $row) (i32.const 1)))
      (br $rowLoop))))
)
