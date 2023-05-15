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
  ImageParam inp(type_of<int>(), 2, "inp"); 
  Func blur_x("blur_x"), blur_y("blur_y"); 
  Var x("x"), y("y"), xi("xi"), yi("yi"), yo("yo"), yoo("yoo"), xy("xy"); 

  blur_x(x, y) = (inp(x+2, y)+inp(x, y)+inp(x+1, y)) / 3; 
  blur_x.ensures(blur_x(x, y) == (inp(x+2, y) + inp(x, y) + inp(x+1, y)) / 3); 

  blur_y(x, y) =  (blur_x(x, y+2) + blur_x(x, y) + blur_x(x, y+1)) / 3; 
  blur_y.ensures(blur_y(x, y) == ((inp(x+2, y+2)+inp(x, y+2)+inp(x+1, y+2)) / 3+(inp(x+2, y)+inp(x, y)+inp(x+1, y)) / 3+(inp(x+2, y+1)+inp(x, y+1)+inp(x+1, y+1)) / 3)/3);
  
  /* Schedule 1 */
  if(schedule == 1) { 
    blur_y
      .fuse(x,y, xy)
      .parallel(xy)
      ;
  /* Schedule 2 */
  } else if(schedule == 2) { 
    blur_y
      .split(y, y, yi, 8, TailStrategy::GuardWithIf)
      .split(x, x, xi, 8, TailStrategy::GuardWithIf)
      .reorder(xi, yi, x, y)
      .parallel(x)
      .parallel(y)
      ;
    blur_x
      .compute_at(blur_y, x)
      ;
  /* Schedule 3 */
  } else if(schedule == 3) { 
    blur_y
      .split(y, y, yi, 8, TailStrategy::GuardWithIf)
      .parallel(y)
      .split(x, x, xi, 2, TailStrategy::GuardWithIf)
      .unroll(xi)
      ;
    blur_x
      .store_at(blur_y, y)
      .compute_at(blur_y, yi)
      .split(x, x, xi, 2, TailStrategy::GuardWithIf)
      .unroll(xi)
      ;
  }
  /* End Schedule */

  // Bounding the dimensions
  int n = 1024;
  set_bounds({{0, n}, {0, n}}, blur_y.output_buffer());
  set_bounds({{0, n+2}, {0, n+2}}, inp);

  // No assertions in code
  Target new_target = standard_target();
  if(front) {
    blur_y.translate_to_pvl(name+".pvl", {}, {}); 
  } else {
    blur_y.compile_to_pvl(name+".pvl" , {inp}, {}, name, new_target, only_memory);
  }
}