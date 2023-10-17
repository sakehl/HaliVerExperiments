#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char *argv[]) {

  /* Halide algorithm */
  ImageParam inp(type_of<int>(), 2, "inp"); 
  Func f("f");
  Var x("x"), y("y"), xi("xi"), yi("yi"), xo("xo"), yo("yo"), xy("xy");

  // ****************************************************** 
  // Lesson 1: Simple Function
  // ******************************************************

  // We write a single function, getting some input
  f(x, y) = inp(y,x) + 1;
  // And specialize for values where y==x
  f(x,x) = 0;

  // Now we could prove some things about these functions
  // If we require that the input is odd, we can prove that the output is even
  inp.requires(inp(_0, _1) % 2 == 1);
  // Since the inp parameter is not declared with specific parameters "x" and "y" we use implicit
  // parameters "_0" (zeroth dimension) and "_1" (first dimension)

  // Here we get the dimensions of the output for ease of reference
  const Expr xmin = f.output_buffer().dim(0).min();
  const Expr xmax = f.output_buffer().dim(0).max();
  const Expr xextent = xmax-xmin+1;
  
  const Expr ymin = f.output_buffer().dim(1).min();
  const Expr ymax = f.output_buffer().dim(1).max();
  const Expr yextent = ymax-ymin+1;

  // For front-end verification, we now can prove the following property
  Annotation property = ensures(forall({x,y}, xmin<=x && x<xmax && ymin<=y && y<ymax, f(x,y) % 2 == 0));

  // If we want to check if the front-end adhers to this we can write
  f.translate_to_pvl("tutorial_f_0.pvl", {inp}, {property});
  // The file `tutorial_f_0.pvl` is made and can be checked by Vercors:
  // $ vct build/tutorial_f_0.pvl
  // It gives warnings about triggers, but you can ignore that since these specific triggers are actually inferred in the Viper (on which VerCors relies).

  // For back-end verification, we do not have enough information yet.
  // So the following file will not verify
  f.output_buffer().dim(1).set_stride(xextent);
  f.output_buffer().dim(0).set_min(0);
  f.output_buffer().dim(1).set_min(0);
  inp.dim(1).set_stride(xextent);
  inp.dim(0).set_bounds(0, xextent);
  inp.dim(1).set_bounds(0, yextent);
  f.compile_to_c("tutorial_f_0.c", {inp}, {property}, "f");
  // $ vct build/tutorial_f_0.c

  // This is due to two facts:
  // 1. We need preciser bound information. Since multi-dimensional functions are flattened, we end-up with 


  // Since f is the only definition, it is not inlined.
  // We addd annotations to the pure definition
  f.annotate(ensures(f(x,y) % 2 == 0));
  // And the update definition
  f.update(0).annotate(ensures(f(x,y) % 2 == 0));
  // We could have equally have written 'f(x,x) % 2 == 0' as in the update the following is true x==y.

  // We need to define the annotations with the same parameter values as the definition.
  // Thus the following is not allowed
  // f.annotate(ensures(f(x+1, y) % 2 == 0));

  // One also write f.ensures(..), but then the annotations you are adding are always added to the last defined function definition.
  // This is the standard way we use it, so we could have also have written:
  {
      Func f("f");
      f(x, y) = inp(y,x) + 1;
      f.ensures(f(x,y) % 2 == 0);
      f(x,x) = 0;
      f.ensures(f(x,y) % 2 == 0);
  }




  // f
  //   .split(x, xo, xi, 8, TailStrategy::GuardWithIf)
  //   .vectorize(xi)
  //   ;
  // //
  int nx = 128;
  int ny = 256;
  
  {
    inp.dim(0).set_bounds(0, nx);
    inp.dim(1).set_bounds(0, ny);
    inp.dim(1).set_stride(nx);

    f.output_buffer().dim(0).set_bounds(0, nx);
    f.output_buffer().dim(1).set_bounds(0, ny);
    f.output_buffer().dim(1).set_stride(nx);
  }

  Halide::Target target = Halide::Target()
    .with_feature(Halide::Target::NoAsserts)
    .with_feature(Halide::Target::NoBoundsQuery)
    ;

  f.compile_to_c("tutorial.c" , {inp}, {}, "tutorial", target);
}