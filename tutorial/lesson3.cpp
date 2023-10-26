#include "Halide.h"
#include <stdio.h>

using namespace Halide;

void set_bounds(std::vector<std::tuple<int, int>> dims, Halide::OutputImageParam p);

int main(int argc, char *argv[]) {

  ImageParam inp(type_of<int>(), 2, "inp"); 
  Func count("count");
  Var x("x"); RDom r(0, 10);

  // ****************************************************** 
  // Lesson 3: Reductions
  // ******************************************************
  // We will use the same count example as the paper, which counts the number of positive numbers 
  // of the first 10 elements of a row of matrix inp. (Listing 2)
  count(x) = 0;
  count.ensures(count(x) == 0);
  count(x) = select(inp(x, r)>0, count(x)+1, count(x));
  // We add a _reduction invariant_ here
  // Similar to a loop invariant, three things are true for a reduction invariant:
  //   1. The annotation of the previous definition (ensures(count(x) == 0)) should imply the
  //      invariant for r==r_min (r==0)
  //   2. For each step of the reduction, it should remain true
  //   3. Afterwards, we know that the invariant holds for r==r_max, which can be used as 
  //      post-condition (ensures(0<=count(x) && count(x)<=10))
  count.invariant(0<=count(x) && count(x)<=r);
  count.ensures(0<=count(x) && count(x)<=10);

  // First verify with the front-end approach
  count.translate_to_pvl("lesson3_count_front_0.pvl", {inp}, {});
  // vct build/lesson3_count_front_0.pvl

  // For the back-end approach we set concrete values again
  int nx = 42;
  int ny = 10;
  set_bounds({{0,nx}}, count.output_buffer());
  set_bounds({{0,nx}, {0,ny}}, inp);
  // ny should be atleast 10, since we look at 10 values of a row of inp. (r_max == 10)
  count.compile_to_c("lesson3_count_0.c", {inp}, {}, "count");
  // vct build/lesson3_count_0.c
  // Which verifies.

  // Now, if we know more about the input, for instances that if: x>y then inp(x,y)>0 else inp(x,y)<0
  inp.requires(select(_0 > _1, inp(_0, _1) > 0, inp(_0, _1) <= 0));
  // We can be more specific.
  count.invariant(count(x) == min(max(x, 0), r));
  count.ensures(count(x) == min(max(x, 0), 10));
  count.translate_to_pvl("lesson3_count_front_1.pvl", {inp}, {});
  // vct build/lesson3_count_front_1.pvl

  count.compile_to_c("lesson3_count_1.c", {inp}, {}, "count");
  // vct build/lesson3_count_1.c

  // In general, it would be nice to be specific for _any_ input. For this we would need sequences from VerCors:
  // https://vercors.ewi.utwente.nl/wiki/#sequence
  // And being able to use pure, user defined function, from VerCors
  // For example, if we could have a function `inp_to_row(int x, int n)`, that converts the first n elements of row x 
  // of the input towards a sequence, and the following function:
  // pure count_row(seq<int> xs) = |xs| == 0 ? 0 : (xs[0] > 0 ? 1 : 0) + count_row(xs.tail);
  // and use this to annotate count like:
  //   count.invariant(count(x) == count_row(inp_to_row(inp, x), r)))
  //   count.ensures(count(x) == count_row(inp_to_row(inp, x), 10)))
  // This would look like
  // `tutorial/lesson3_limits.pvl` for the front end and
  // `tutorial/lesson3_limits_0.c` for the back-end, which both verify with VerCors

  // However, for now this is not yet directly supported and planned as future work for HaliVer.
}

void set_bounds(std::vector<std::tuple<int, int>> dims, Halide::OutputImageParam p){
    int stride = 1;
    for(int i = 0; i < dims.size(); i++){
        p.dim(i).set_bounds(std::get<0>(dims[i]), std::get<1>(dims[i]));
        p.dim(i).set_stride(stride);
        stride *= std::get<1>(dims[i]);
    }
}