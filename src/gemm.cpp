#include "Halide.h"
#include "helper.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char *argv[]) {
  int schedule; 
  bool only_memory, front;
  std::string name;
  int res = read_args(argc, argv, schedule, only_memory, front, name);
  if(res != 0) return res;

  const int num_rows = 2048;
  const int num_cols = 2048;
  const int sum_size = 1024;
  const int a_ = 2;
  const int b_ = 3;
  const int vec = 4;
  const int s = vec * 2;

  /* Halide algorithm */
  ImageParam A_(type_of<int>(), 2, "A_"); 
  ImageParam B_(type_of<int>(), 2, "B_"); 
  ImageParam C_(type_of<int>(), 2, "C_"); 

  Var i("i"), j("j"), ii("ii"), ji("ji"), jii("jii"), iii("iii"), io("io"), jo("jo"), t; 
  Var ti[3], tj[3];  

  // Swizzle A for better memory order in the inner loop.
  Func A("A"), B("B"), Btmp("Btmp"), As("As"), Atmp("Atmp"), result_("_result"); 
  // Gemm
  A_.requires(0 <= A_(_0, _1) && A_(_0, _1) < 100); 
  B_.requires(0 <= B_(_0, _1) && B_(_0, _1) < 100); 
  C_.requires(0 <= C_(_0, _1) && C_(_0, _1) < 100);

  Atmp(i, j) = A_(i,j); 
  Atmp.ensures(0 <= Atmp(i, j) && Atmp(i, j) < 100); 

  As(i, j, io) = Atmp(io * s + i, j); 
  As.ensures(0 <= As(i, j, io) && As(i, j, io) < 100); 
  A(i, j) = As(i % s, j, i / s); 
  A.ensures(implies(i >= 0 && i < num_rows && j >=0 && j < sum_size, 
    A(i, j) == A_(i,j))); 
  B(i, j) = B_(i, j); 
  B.ensures(0 <= B(i, j) && B(i, j) < 100); 

  Var k("k"); 
  Func prod("prod"); 
  // Express all the products we need to do a matrix multiply as a 3D Func.
  prod(k, i, j) = A(i, k) * B(k, j); 
  prod.ensures(0 <= prod(k, i, j) && prod(k, i, j) < 100*100); 

  // Reduce the products along k.
  Func AB("AB"); 
  AB(i, j) = 0; 
  AB.ensures(AB(i,j) == 0); 
  RDom rv(0, sum_size); 
  AB(i, j) += prod(rv, i, j); 
  AB.invariant(AB(i,j) >= 0 && AB(i,j) <= rv * 100 * 100); 
  AB.ensures(AB(i,j) >= 0 && AB(i,j) <= sum_size * 100 * 100); 

  // Do the part that makes it a 'general' matrix multiply.
  result_(i, j) = (a_ * AB(i, j) + b_ * C_(i, j)); 
  result_.ensures(result_(i, j) >= 0 && 
    result_(i, j) <= sum_size * 100 * 100 * a_ + b_ * 100); 
  
  // Bounding the dimensions
  set_bounds({{0, num_rows}, {0, sum_size}}, A_);
  set_bounds({{0, sum_size}, {0, num_cols}}, B_);
  set_bounds({{0, num_rows}, {0, num_cols}}, C_);
  set_bounds({{0, num_rows}, {0, num_cols}}, result_.output_buffer());

  /* Schedule 0 */
  if(schedule == 0){
  /* Schedule 1 */
  } else if(schedule == 1) { 
    result_
      .tile(i, j, ti[1], tj[1], i, j, 2 * s, 2 * s, TailStrategy::GuardWithIf);
    result_
      .parallel(tj[1])
      .parallel(ti[1])
      ;

    As.compute_root()
        .split(j, jo, ji, s, TailStrategy::GuardWithIf)
        .reorder(i, ji, io, jo)
        .split(jo, jo, jii , 4, TailStrategy::GuardWithIf)
        .parallel(jo)
        ;
  /* Schedule 2 */
  } else if(schedule == 2) { 
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
        .split(j, jo, ji, s, TailStrategy::GuardWithIf)
        .reorder(i, ji, io, jo)
        // .unroll(i)
        .split(jo, jo, jii , 4, TailStrategy::GuardWithIf)
        .parallel(jo)
        ;

    Atmp.compute_at(As, io)
        // .unroll(j)
        ;

    AB.compute_at(result_, i)
        .bound_extent(j, 4)
        // .unroll(j)
        .bound_extent(i, s)
        ;
  /* Schedule 3 */
  } else if(schedule == 3) { 
    result_.tile(i, j, ti[1], tj[1], i, j, 2 * s, 2 * s, TailStrategy::GuardWithIf);
    result_
        .tile(i, j, ii, ji, s, 4)
        .tile(i, j, ti[0], tj[0], i, j, 1, s / 4);
    result_
      .fuse(tj[1], ti[1], t)
      .parallel(t)
      ;


    result_.rename(tj[0], t);
    result_.bound(i, 0, num_rows)
      .bound(j, 0, num_cols);

    As.compute_root()
        .split(j, jo, ji, s, TailStrategy::GuardWithIf)
        .reorder(i, ji, io, jo)
        .unroll(i)
        .split(jo, jo, jii , 4, TailStrategy::GuardWithIf)
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
  /* End Schedule */

  Target new_target = standard_target();

  if(front) {
      result_.translate_to_pvl(name + ".pvl", {}, {}); 
  } else {
      result_.compile_to_c(name + ".c" , {A_, B_, C_}, {}, name, new_target, only_memory);
  }
}