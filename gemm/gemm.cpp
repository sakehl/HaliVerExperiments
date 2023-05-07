#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char *argv[]) {
  int schedule;
  std::string front_s = "front";
  bool front = false;
  if(argc == 1){
    schedule = 0;
  } else if(argc == 2){
    if(front_s.compare(argv[1]) == 0){
      front = true;
    } else {
      schedule = std::stoi(argv[1]);
    }
  }
  if((argc > 2 || schedule < 0 || schedule >3) && !front){
    printf("Invallid argument\n");
    return 1;
  }

  const int num_rows = 2048;
  const int num_cols = 2048;
  const int sum_size = 1024;
  const int a_ = 2;
  const int b_ = 3;
  const int vec = 4;
  const int s = vec * 2;

  /* Halide algorithm */
  ImageParam A_(type_of<int>(), 2, "A_"); /* LoC 1 */
  ImageParam B_(type_of<int>(), 2, "B_"); /* LoC 2 */
  ImageParam C_(type_of<int>(), 2, "C_"); /* LoC 3 */

  A_.requires(0 <= A_(_0, _1) && A_(_0, _1) < 100); /* LoA 1 */
  B_.requires(0 <= B_(_0, _1) && B_(_0, _1) < 100); /* LoA 2 */
  C_.requires(0 <= C_(_0, _1) && C_(_0, _1) < 100); /* LoA 3 */

  Var i("i"), j("j"), ii("ii"), ji("ji"), jii("jii"), iii("iii"), io("io"), jo("jo"), t; /* LoC 4 */
  Var ti[3], tj[3]; /* LoC 5 */

  // Swizzle A for better memory order in the inner loop.
  Func A("A"), B("B"), Btmp("Btmp"), As("As"), Atmp("Atmp"), result_("_result"); /* LoC 6 */
  Atmp(i, j) = A_(i,j); /* LoC 7 */

  As(i, j, io) = Atmp(io * s + i, j); /* LoC 8 */
  As.ensures(As(i, j, io) == A_(io * s + i, j)); /* LoA 4 */
  As.ensures(0 <= As(i, j, io) && As(i, j, io) < 100); /* LoA 5 */
  A(i, j) = As(i % s, j, i / s); /* LoC 9 */
  A.ensures(implies(i >= 0 && i < num_rows && j >=0 && j < sum_size, A(i, j) == A_(i,j))); /* LoA 6 */
  B(i, j) = B_(i, j); /* LoC 10 */

  Var k("k"); /* LoC 11 */
  Func prod("prod"); /* LoC 12 */
  // Express all the products we need to do a matrix multiply as a 3D Func.
  prod(k, i, j) = A(i, k) * B(k, j); /* LoC 13 */

  // Reduce the products along k.
  Func AB("AB"); /* LoC 14 */
  AB(i, j) = 0; /* LoC 15 */
  AB.ensures(AB(i,j) == 0); /* LoA 7 */
  RDom rv(0, sum_size); /* LoC 16 */
  AB(i, j) += prod(rv, i, j); /* LoC 17 */
  AB.invariant(AB(i,j) >= 0 && AB(i,j) <= rv * 100 * 100); /* LoA 8 */
  AB.ensures(AB(i,j) >= 0 && AB(i,j) <= sum_size * 100 * 100); /* LoA 9 */

  // Do the part that makes it a 'general' matrix multiply.
  result_(i, j) = (a_ * AB(i, j) + b_ * C_(i, j)); /* LoC 18 */
  result_.ensures(result_(i, j) >= 0 && result_(i, j) <= sum_size * 100 * 100 * a_ + b_ * 100); /* LoA 9 */
  
  /* Halide algorithm */
  if(schedule == 0){

  } else if(schedule == 1) { /* LoC: 8 */
    result_
      .tile(i, j, ti[1], tj[1], i, j, 2 * s, 2 * s, TailStrategy::GuardWithIf);
    result_
      .parallel(tj[1])
      .parallel(ti[1])
      ;

    As.compute_root()
        .split(j, jo, ji, s)
        .reorder(i, ji, io, jo)
        .split(jo, jo, jii , 4)
        .parallel(jo)
        ;

  } else if(schedule == 2) { /* LoC: 19 */
    result_.tile(i, j, ti[1], tj[1], i, j, 2 * s, 2 * s, TailStrategy::GuardWithIf);
    result_
        .tile(i, j, ii, ji, s, 4)
        .tile(i, j, ti[0], tj[0], i, j, 1, s / 4);
    result_
      .parallel(tj[1])
      .parallel(ti[1])
      ;

    result_.rename(tj[0], t);
    result_.bound(i, 0, num_rows).bound(j, 0, num_cols);

    As.compute_root()
        .split(j, jo, ji, s)
        .reorder(i, ji, io, jo)
        .unroll(i)
        .split(jo, jo, jii , 4)
        .parallel(jo)
        ;

    Atmp.compute_at(As, io)
        .unroll(j)
        ;

    AB.compute_at(result_, i)
        .bound_extent(j, 4)
        .unroll(j)
        .bound_extent(i, s)
        ;
  } else if(schedule == 3) { /* LoC: 24 */
    result_.tile(i, j, ti[1], tj[1], i, j, 2 * s, 2 * s, TailStrategy::GuardWithIf);
    result_
        .tile(i, j, ii, ji, s, 4)
        .tile(i, j, ti[0], tj[0], i, j, 1, s / 4);
    result_
      .fuse(tj[1], ti[1], t)
      .parallel(t);


    result_.rename(tj[0], t);
    result_.bound(i, 0, num_rows)
      .bound(j, 0, num_cols);

    As.compute_root()
        .split(j, jo, ji, s)
        .reorder(i, ji, io, jo)
        .unroll(i)
        .split(jo, jo, jii , 4)
        .parallel(jo)
        ;

    Atmp.compute_at(As, io)
        .unroll(j)
        ;

    AB.compute_at(result_, i)
        .bound_extent(j, 4)
        .unroll(j)
        .bound_extent(i, s)
        .update()
        .reorder(i, j, rv)
        .unroll(j)
        .unroll(rv, 2)
        ;
  }

  // Bounding the dimensions
  A_.dim(0).set_bounds(0, num_rows);
  A_.dim(1).set_bounds(0, sum_size);
  A_.dim(1).set_stride(num_rows);
  
  B_.dim(0).set_bounds(0, sum_size);
  B_.dim(1).set_bounds(0, num_cols);
  B_.dim(1).set_stride(sum_size);

  C_.dim(0).set_bounds(0, num_rows);
  C_.dim(1).set_bounds(0, num_cols);
  C_.dim(1).set_stride(num_rows);
  
  result_.output_buffer().dim(0).set_bounds(0, num_rows);
  result_.output_buffer().dim(1).set_bounds(0, num_cols);
  result_.output_buffer().dim(1).set_stride(num_rows);

  Target target = Target();
  Target new_target = target
    .with_feature(Target::NoAsserts)
    .with_feature(Target::NoBoundsQuery)
    ;

  std::string name = "gemm";

  if(front) {
      result_.translate_to_pvl(name + "_front_pvl.pvl", {}, {}); 
  } else {
      result_.compile_to_pvl(name + "_pvl-" + std::to_string(schedule) + ".pvl" , {A_, B_, C_}, {}, name, new_target);
  }
}