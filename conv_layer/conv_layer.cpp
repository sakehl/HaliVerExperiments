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

  const int N = 5, CI = 128, CO = 128, W = 100, H = 80;

  /* Halide algorithm */
  ImageParam input(type_of<int>(), 4, "input");  /* LoC 1 */
  ImageParam filter(type_of<int>(), 4, "filter");  /* LoC 2 */
  ImageParam bias(type_of<int>(), 1, "bias");  /* LoC 3 */
  Var x("x"), y("y"), c("c"), n("n");  /* LoC 4 */

  Func conv("conv"), relu("relu");  /* LoC 5 */
  RDom r(0, CI, 0, 3, 0, 3);  /* LoC 6 */
  input.requires(input(_) >= 0);  /* LoA 1 */
  filter.requires(filter(_) >= 0);  /* LoA 2 */
  bias.requires(bias(_) >= 0);  /* LoA 3 */
  
  conv(c, x, y, n) = bias(c);  /* LoC 7 */
  conv.ensures(conv(c, x, y, n) >= 0);  /* LoA 4 */
  conv(c, x, y, n) += filter(c, r.y, r.z, r.x) * input(r.x, x + r.y, y + r.z, n);  /* LoC 8 */
  conv.invariant(conv(c, x, y, n) >= 0);  /* LoA 5 */
  conv.ensures(conv(c, x, y, n) >= 0);  /* LoA 6 */

  // relu(c, x, y, n) = max(0, conv(c, x, y, n));
  relu(c, x, y, n) = conv(c, x, y, n);  /* LoC 9 */
  relu.ensures(relu(c, x, y, n) >= 0);  /* LoA 7 */


  /* Halide schedule */
  int tile_w = 2;
  int tile_h = 5;
  const int vec = 8;
  Var co, ci, xo, xi, yo, yi, t;
  Var v;
  if(schedule == 0){

  } else if(schedule == 1) { /* LoC: 4 */
    Var xy;
    relu.fuse(x,y, xy)
      .unroll(c, 2)
      .parallel(n)
      ;
    conv.compute_at(relu, xy)
      ;
  } else if(schedule == 2) { /* LoC: 6 */
    relu.split(c, co, ci, tile_w)
        .reorder(ci, x, y, n, co)
        .unroll(ci)
        .parallel(y)
        .parallel(n)
        .parallel(co);
  } else if(schedule == 3) { /* LoC: 15 */
    relu.split(c, co, ci, tile_w)
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
      .unroll(r.x, 2)
      ;
  }
  // Bounding the dimensions
  relu.output_buffer().dim(0).set_bounds(0, CO).set_stride(1);
  relu.output_buffer().dim(1).set_bounds(0, W).set_stride(CO);
  relu.output_buffer().dim(2).set_bounds(0, H).set_stride(CO * W);
  relu.output_buffer().dim(3).set_bounds(0, N).set_stride(CO * H * W);

  input.dim(0).set_bounds(0, CI).set_stride(1);
  input.dim(1).set_bounds(0, W + 2).set_stride(CI);
  input.dim(2).set_bounds(0, H + 2).set_stride(CI * (W + 2));
  input.dim(3).set_bounds(0, N).set_stride(CI * (W + 2) * (H + 2));

  filter.dim(0).set_bounds(0, CO).set_stride(1);
  filter.dim(1).set_bounds(0, 3).set_stride(CO);
  filter.dim(2).set_bounds(0, 3).set_stride(CO * 3);
  filter.dim(3).set_bounds(0, CI).set_stride(CO * 3 * 3);

  bias.dim(0).set_bounds(0, CO).set_stride(1);


  Target target = Target();
  Target new_target = target
    .with_feature(Target::NoAsserts)
    .with_feature(Target::NoBoundsQuery)
    ;

  std::string name = "conv_layer";
  if(front) {
    relu.translate_to_pvl(name + "_front_pvl.pvl", {}, {}); 
  } else {
    relu.compile_to_pvl(name + "_pvl-" + std::to_string(schedule) + ".pvl" , {input, filter, bias}, {}, name, new_target);
    relu.print_loop_nest();
  }
}