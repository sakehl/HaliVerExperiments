#include "Halide.h"
#include <stdio.h>

using namespace Halide;

void set_bounds(std::vector<std::tuple<int, int>> dims, Halide::OutputImageParam p);

int main(int argc, char *argv[]) {

  ImageParam inp(type_of<int>(), 2, "inp"); 
  Func f("f");
  Var x("x"), y("y"), xi("xi"), yi("yi"), xo("xo"), yo("yo"), xy("xy");

  // NOTE: To understand Halide better, please look at the tutorial of Halide first: https://halide-lang.org/tutorials/tutorial_introduction.html
  // To understand VerCors better look here: https://vercors.ewi.utwente.nl/wiki/

  // ****************************************************** 
  // Lesson 1: Simple Function
  // ******************************************************

  // We write a single function, getting some input
  f(x, y) = inp(y,x) + 1;
  // And specialize for values where y==0
  f(x, 0) = 0;

  // Now we could prove some things about these functions
  // If we require that the input is odd, we can prove that the output is even
  inp.requires(inp(_0, _1) % 2 == 1);
  // Since the inp parameter is not declared with specific parameters "x" and "y" we use implicit
  // parameters "_0" and "_1" (zeroth and first dimension)

  // Here we get the dimensions of the output for ease of reference
  const Expr f_min_x = f.output_buffer().dim(0).min();
  const Expr f_max_x = f.output_buffer().dim(0).max();
  const Expr f_x_extent = f_max_x-f_min_x+1; // Extent is the number of elements in a dimension
  
  const Expr f_min_y = f.output_buffer().dim(1).min();
  const Expr f_max_y = f.output_buffer().dim(1).max();
  const Expr f_y_extent = f_max_y-f_min_y+1;

  // For front-end verification, we now can prove the following property
  Annotation property = ensures(forall({x,y}, f_min_x<=x && x<=f_max_x && f_min_y<=y && y<=f_max_y, f(x,y) % 2 == 0));

  // If we want to check if the front-end adheres to this we can write
  f.translate_to_pvl("lesson1_f_front.pvl", {inp}, {property});
  // The file `lesson1_f_front.pvl` is made and can be checked by VerCors:
  // $ vct build/lesson1_f_front.pvl
  // It gives warnings about triggers, but you can ignore that since these specific triggers are actually inferred in the underlying tools.

  // Let's move to the back-end verification approach

  // Normally Halide allows buffers to have any dimensions checks them at runtime to be actually correct: 
  f.compile_to_c("lesson1_f_0.c", {inp}, {}, "f");
  // If we try to simply check the backend we will fail/timeout:
  // $ vct build/lesson1_f_0.c --dev-total-timeout 60
  // This is because the buffers dimensions are not specified enough.
  // In general, the underlying VerCors tools has a lot of trouble verifying flattened multidimensional arrays with non-concrete bounds.
  // Thus let's set concrete bounds
  int nx = 12;
  int ny = 42;
  f.output_buffer().dim(0).set_bounds(0, nx);
  f.output_buffer().dim(1).set_bounds(0, ny);
  f.output_buffer().dim(1).set_stride(nx);

  // The input is accessed with y and x swapped
  inp.dim(0).set_bounds(0, ny);
  inp.dim(1).set_bounds(0, nx);
  inp.dim(1).set_stride(ny);

  f.compile_to_c("lesson1_f_1.c", {inp}, {}, "f");
  // You can run this with
  // $ vct build/lesson1_f_1.c
  // And it will check that there are no memory errors
  
  // To also verify the property we add it as _pipeline annotation_.
  f.compile_to_c("lesson1_f_2.c", {inp}, {property}, "f");
  // $ vct build/lesson1_f_2.c
  // But this fails. For back-end verification. More intermediate information is needed for the back-end to verify.

  // Any non-inlined function, needs _intermediate_ annotations for the back-end.
  // We add intermediate annotations to the pure definition
  f.annotate(ensures(f(x,y) % 2 == 0));
  // And the update definition
  f.update(0).annotate(ensures(f(x,y) % 2 == 0));
  // Note with this we annotate that after the update this property holds for all values, not only for the update where y==0.

  f.compile_to_c("lesson1_f_3.c", {inp}, {property}, "f");
  // $ vct build/lesson1_f_3.c
  // And this does verify

  // You can also write f.ensures(..), but then the annotations you are adding are always added to the last defined function definition.
  // This is the standard way we use it, so we could have also have written:
  Func f2("f2");
  f2(x, y) = inp(y,x) + 1;
  f2.ensures(f2(x,y) % 2 == 0);
  f2(x,x) = 0;
  f2.ensures(f2(x,y) % 2 == 0);
  set_bounds({{0,nx}, {0,ny}}, f2.output_buffer()); // This is a helper function to easier set the bounds
  f2.compile_to_c("lesson1_f_4.c", {inp}, {}, "f2");
  // $ vct build/lesson1_f_4.c
  // If you inspect `build/lesson1_f_4.c` you will see that even when did not add the `property` pipeline annotation here, the property is still added on line 182.
  // This is because the _intermediate_ annotations of the last function in the pipeline (here `f2`) is automatically added as
  // _pipeline_ annotation quantified over its dimensions (here f2's dimensions)

  // We can also add scheduling, and still verify the files. For instance
  f2.parallel(y)
    .split(x, xo, xi, 2, Halide::TailStrategy::GuardWithIf)
    .unroll(xi);
  f2.update().parallel(x);
  f2.compile_to_c("lesson1_f_5.c", {inp}, {}, "f2");
  // $ vct build/lesson1_f_5.c
}

void set_bounds(std::vector<std::tuple<int, int>> dims, Halide::OutputImageParam p){
    int stride = 1;
    for(int i = 0; i < dims.size(); i++){
        p.dim(i).set_bounds(std::get<0>(dims[i]), std::get<1>(dims[i]));
        p.dim(i).set_stride(stride);
        stride *= std::get<1>(dims[i]);
    }
}