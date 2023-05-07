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

  /* Halide algorithm */
  ImageParam inp(type_of<int>(), 2, "inp"); /* LoC 1 */
  Func blur_x("blur_x"), blur_y("blur_y"); /* LoC 2 */
  Var x("x"), y("y"), xi("xi"), yi("yi"), yo("yo"), yoo("yoo"), xy("xy"); /* LoC 3 */

  blur_x(x, y) = (inp(x +2, y) + inp(x, y) + inp(x + 1, y)) / 3; /* LoC 4 */
  blur_x.ensures(blur_x(x, y) == (inp(x +2, y) + inp(x, y) + inp(x + 1, y)) / 3); /* LoA 1 */

  blur_y(x, y) =  (blur_x(x, y +2) + blur_x(x, y) + blur_x(x, y + 1)) / 3; /* LoC 5 */
  blur_y.ensures(blur_y(x, y) ==  /* LoA 2 */
        ((inp(x + 2, y + 2) + inp(x, y + 2) + inp(x + 1, y + 2)) / 3 /* LoA 3*/
      + (inp(x + 2, y) + inp(x, y) + inp(x + 1, y)) / 3 /* LoA 4 */
      + (inp(x + 2, y + 1) + inp(x, y + 1) + inp(x + 1, y + 1)) / 3)/3 /* LoA 5 */
    );
  
  /* Halide schedule */
  if(schedule == 0){

  } else if(schedule == 1) { /* LoC: 2*/
    blur_y
      .fuse(x,y, xy)
      .parallel(xy)
      ;
  } else if(schedule == 2) { /* LoC: 6*/
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
  } else if(schedule == 3) { /* LoC: 6*/
    blur_y
      .split(y, y, yi, 8, TailStrategy::GuardWithIf)
      .parallel(y)
      .split(x, x, xi, 8, TailStrategy::GuardWithIf)
      ;
    blur_x
      .store_at(blur_y, y)
      .compute_at(blur_y, yi)
      .split(x, x, xi, 8, TailStrategy::GuardWithIf)
      ;
  }

  // Bounding the dimensions
  int n = 1024;
  blur_y.output_buffer().dim(0).set_bounds(0,n); 
  blur_y.output_buffer().dim(1).set_bounds(0,n);
  blur_y.output_buffer().dim(1).set_stride(n);
  blur_y.bound(x, 0, n);
  blur_y.bound(y, 0, n);
  
  inp.dim(0).set_bounds(0,n+2);
  inp.dim(1).set_bounds(0,n+2);
  inp.dim(1).set_stride(n+2);

  // No assertions in code
  Target target = Target();
  Target new_target = target
    .with_feature(Target::NoAsserts)
    .with_feature(Target::NoBoundsQuery)
    ;

  std::string name = "blur";
  if(front) {
    blur_y.translate_to_pvl(name +"_front_pvl.pvl", {}, {}); 
  } else {
    blur_y.compile_to_pvl(name + "_pvl-" + std::to_string(schedule) + ".pvl" , {inp}, {}, name, new_target);
    blur_y.print_loop_nest();
  }
}