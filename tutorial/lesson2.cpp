#include "Halide.h"
#include <stdio.h>

using namespace Halide;

void set_bounds(std::vector<std::tuple<int, int>> dims, Halide::OutputImageParam p);

int main(int argc, char *argv[]) {

  ImageParam inp(type_of<int>(), 2, "inp"); 
  Func g("g"), h("h");
  Var x("x"), y("y"), xi("xi"), yi("yi"), xo("xo"), yo("yo"), xy("xy");

  // ****************************************************** 
  // Lesson 2: Multiple functions
  // ******************************************************
  // We define two functions, first g, then h, which uses g.
  g(x,y) = inp(x,y)*inp(x,y)+1;
  h(x,y) = 2*x;
  h.ensures(h(x,y) == 2*x);
  h(x,x) += g(x,x)*2;
  h.ensures(implies(y==x, h(x,y) == 2*x + 2*inp(x,y)*inp(x,y)+2));
  h.ensures(implies(y!=x, h(x,y) == 2*x));

  const Expr h_min_x = h.output_buffer().dim(0).min();
  const Expr h_max_x = h.output_buffer().dim(0).max();
  const Expr h_min_y = h.output_buffer().dim(1).min();
  const Expr h_max_y = h.output_buffer().dim(1).max();
  
  Annotation p1 = ensures(forall({x,y}, h_min_x<=x && x<h_max_x && h_min_y<=y && y<h_max_y, h(x,y) % 2 == 0));
  Annotation p2 = ensures(forall({x,y}, h_min_x<=x && x<h_max_x && h_min_y<=y && y<h_max_y, h(x,y) >= 0));

  // First verify the front-end approach, where we want to prove all sorts of correctness properties.
  h.translate_to_pvl("lesson2_h_front_0.pvl", {inp}, {p1, p2});
  // $ vct build/lesson2_h_front_0.pvl
  // This actually does not verify
  // Looking at the output VerCors says this is wrong
  //                [-------------------------------------------------------------------------------------------------------------------------------------------------------------------
  //    92   ensures (\forall int x,  int y; (((h_min_0() <= x) && (x < ((h_min_0() + h_extent_0()) - 1))) && (h_min_1() <= y)) && (y < ((h_min_1() + h_extent_1()) - 1)); h(x, y) >= 0);
  //                 -------------------------------------------------------------------------------------------------------------------------------------------------------------------]
  // Which makes sense, since for x<0 this is indeed not true and we did not say anything about h_min_x (h_min_0).
  // So we can add the following pre-condition
  Annotation r1 = requires(h_min_x>=0);
  h.translate_to_pvl("lesson2_h_front_1.pvl", {inp}, {r1, p1, p2});
  // $ vct build/lesson2_h_front_1.pvl
  // Which verifies.
  
  // For the back-end approach we set concrete values again
  int nx = 42;
  int ny = 10;
  set_bounds({{0,nx}, {0,ny}}, h.output_buffer());
  set_bounds({{0,nx}, {0,ny}}, inp);
  // it also possible to just check on memory safety first, which does not need any annotations (and discards them actually) so let's do that.
  h.compile_to_c("lesson2_h_0.c", {inp}, {}, "h", Target(), true /*Check only memory safety*/);
  // $ vct build/lesson2_h_0.c
  // Hey this doesn't verify! What happened? VerCors says
  //       [---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  // 212    loop_invariant (\forall* int _h_s0_x_forall, int _h_s0_y_forall; (((0 <= _h_s0_y_forall) && (_h_s0_y_forall < 42)) && (0 <= _h_s0_x_forall)) && (_h_s0_x_forall < 42); Perm(&_h[(_h_s0_y_forall*42) + _h_s0_x_forall], 1\1));
  //        ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------]
  // The offset to the pointer may be outside the bounds of the allocated memory area that the pointer is in. (https://utwente.nl/vercors#ptrBlock)
  
  // So this is harder to understand, but it seems that we try to access h for x in [0,42) and y in [0,42).
  // But wait, didn't we just define that for h, y is in [0,10)?
  // That is true. But also more subtle, Halide also sees that we want to have
  //    `h(x,x) += g(x,x)*2;`
  // for the second update.
  // Now we asked it for x between [0,42), so the Halide compiler inferred that we need h defined for y in [0,42). We gave it impossible bounds! It will never verify when ny<nx.

  // So let's try again.
  nx = 42;
  ny = 100;
  set_bounds({{0,nx}, {0,ny}}, h.output_buffer());
  set_bounds({{0,nx}, {0,ny}}, inp);
  h.compile_to_c("lesson2_h_1.c", {inp}, {}, "h", Target(), true /*Check only memory safety*/);
  // $ vct build/lesson2_h_1.c
  // Which does verifies

  // Now with properties
  h.compile_to_c("lesson2_h_2.c", {inp}, {p1, p2}, "h");
  // $ vct build/lesson2_h_2.c
  // Which also verifies

  // However, if g was not inlined, we need intermediate annotations for g
  g.compute_root();
  h.compile_to_c("lesson2_h_3.c", {inp}, {p1, p2}, "h");
  // $ vct build/lesson2_h_3.c
  // This fails verification, since it lacks information about g. We add
  g.ensures(g(x,y) == inp(x,y)*inp(x,y)+1);
  h.compile_to_c("lesson2_h_4.c", {inp}, {p1, p2}, "h");
  // $ vct build/lesson2_h_4.c
  // Which verifies

  // We can also add scheduling, and still verify the files. For instance
  g.reorder(y, x);
  h.fuse(x, y, xy);
  h.update().parallel(x);
  h.compile_to_c("lesson2_h_5.c", {inp}, {p1, p2}, "h");
  // $ vct build/lesson2_h_5.c
}

void set_bounds(std::vector<std::tuple<int, int>> dims, Halide::OutputImageParam p){
    int stride = 1;
    for(int i = 0; i < dims.size(); i++){
        p.dim(i).set_bounds(std::get<0>(dims[i]), std::get<1>(dims[i]));
        p.dim(i).set_stride(stride);
        stride *= std::get<1>(dims[i]);
    }
}