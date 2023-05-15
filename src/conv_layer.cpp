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

  /* Halide algorithm */
  const int N = 5, CI = 128, CO = 128, W = 100, H = 80;

  ImageParam input(type_of<int>(), 4, "input");
  ImageParam filter(type_of<int>(), 4, "filter");  
  ImageParam bias(type_of<int>(), 1, "bias");  
  Var x("x"), y("y"), c("c"), n("n");  

  Func conv("conv"), relu("relu");  
  RDom r(0, CI, 0, 3, 0, 3);  
  input.requires(input(_) >= 0);  
  filter.requires(filter(_) >= 0);  
  bias.requires(bias(_) >= 0);  
  
  conv(c, x, y, n) = bias(c);  
  conv.ensures(conv(c, x, y, n) >= 0);  
  conv(c, x, y, n) += filter(c, r.y, r.z, r.x) * input(r.x, x + r.y, y + r.z, n);  
  conv.invariant(conv(c, x, y, n) >= 0);  
  conv.ensures(conv(c, x, y, n) >= 0);  

  // relu(c, x, y, n) = max(0, conv(c, x, y, n));
  relu(c, x, y, n) = conv(c, x, y, n);  
  relu.ensures(relu(c, x, y, n) >= 0);  

  /* Schedule */
  int tile_w = 2;
  int tile_h = 5;
  const int vec = 8;
  Var co, ci, xo, xi, yo, yi, t;
  Var v;
  /* Schedule 0 */
  if(schedule == 0){

  /* Schedule 1 */
  } else if(schedule == 1) { 
    Var xy;
    relu.fuse(x,y, xy)
      .unroll(c, 2, TailStrategy::GuardWithIf)
      .parallel(n)
      ;
    conv.compute_at(relu, xy)
      ;
  /* Schedule 2*/
  } else if(schedule == 2) { 
    relu.split(c, co, ci, tile_w, TailStrategy::GuardWithIf)
        .reorder(ci, x, y, n, co)
        .unroll(ci)
        .parallel(y)
        .parallel(n)
        .parallel(co);
  /* Schedule 3 */
  } else if(schedule == 3) { 
    relu.split(c, co, ci, tile_w, TailStrategy::GuardWithIf)
      .reorder(ci, x, y, n, co)
      .unroll(ci)
      .parallel(n)
      .parallel(co);
    conv.compute_at(relu, x)
      .unroll(c)
      .unroll(x)
      .unroll(y)
      .update()
      .reorder(c, x, y, r.x, r.y, r.z, n)
      .unroll(c)
      .unroll(x)
      .unroll(y)
      .unroll(r.x, 2, TailStrategy::GuardWithIf)
      ;
  }
  /* End Schedule */
  
  // Bounding the dimensions
  set_bounds({{0, CO}, {0, W}, {0, H}, {0, N}}, relu.output_buffer());
  set_bounds({{0, CI}, {0, W + 2}, {0, H + 2}, {0, N}}, input);
  set_bounds({{0, CO}, {0, 3}, {0, 3}, {0, CI}}, filter);
  set_bounds({{0, CO}}, bias);

  Target new_target = standard_target();
  if(front) {
    relu.translate_to_pvl(name + ".pvl", {}, {}); 
  } else {
    relu.compile_to_pvl(name + ".pvl" , {input, filter, bias}, {}, name, new_target, only_memory);
  }
}