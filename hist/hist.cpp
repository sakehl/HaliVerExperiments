#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::ConciseCasts;

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
    if((argc > 2 || schedule < 0 || schedule >4) && !front){
        printf("Invallid argument\n");
        return 1;
    }

    int nx = 1536;
    int ny = 2560;

    ImageParam input(type_of<float>(), 3, "input"); /* LoC 1 */
    input.requires(input(_) >= 0.0f && input(_) <= 255.0f); /* LoA 1 */
    Var x("x"), y("y"), c("c"); /* LoC 2 */
    // Algorithm
    Func Y("Y"); /* LoC 3 */
    Y(x, y) = (0.299f * input(x, y, 0) + /* LoC 4 */
               0.587f * input(x, y, 1) + /* LoC 5 */
               0.114f * input(x, y, 2)); /* LoC 6 */

    Func Cr("Cr"); /* LoC 7 */
    Expr R = input(x, y, 0); /* LoC 8 */
    Cr(x, y) = ((R - Y(x, y)) * 0.713f) + 128; /* LoC 9 */
    Cr.ensures(Cr(x,y) >= 0.0f && Cr(x,y) < 256.0f); /* LoA 2 */

    Func Cb("Cb"); /* LoC 10 */
    Expr B = input(x, y, 2); /* LoC 11 */
    Cb(x, y) = ((B - Y(x, y)) * 0.564f) + 128; /* LoC 12 */
    Cb.ensures(Cb(x,y) >= 0.0f && Cb(x,y) < 256.0f); /* LoA 3 */

    Func hist_rows("hist_rows"); /* LoC 13 */
    hist_rows(x, y) = 0; /* LoC 14 */
    hist_rows.ensures(hist_rows(x, y) == 0); /* LoA 4 */
    RDom rx(0, nx); /* LoC 15 */
    Expr bin = cast<int>(clamp(Y(rx, y), 0, 255)); /* LoC 16 */
    hist_rows(bin, y) += 1; /* LoC 17 */
    hist_rows.invariant(hist_rows(x,y) <= rx); /* LoA 5 */

    Func hist("hist"); /* LoC 18 */
    hist(x) = 0; /* LoC 19 */
    hist.ensures(hist(x) == 0); /* LoA 6 */
    RDom ry(0, ny); /* LoC 20 */
    hist(x) += hist_rows(x, ry); /* LoC 21 */
    hist.invariant(hist(x) <= ry*nx); /* LoA 6 */

    Func cdf("cdf"); /* LoC 22 */
    cdf(x) = hist(0); /* LoC 23 */
    RDom b(1, 255); /* LoC 24 */
    cdf(b.x) = cdf(b.x - 1) + hist(b.x); /* LoC 25 */

    Func cdf_bin("cdf_bin"); /* LoC 26 */
    cdf_bin(x, y) = u8(clamp(Y(x, y), 0, 255)); /* LoC 27 */

    Func eq("equalize"); /* LoC 28 */
    eq(x, y) = clamp((cdf(cdf_bin(x, y)) * 255.0f) / (ny * nx), 0, 255); /* LoC 29 */

    Expr red = clamp((eq(x, y) + (Cr(x, y) - 128) * 1.4f), 0, 255); /* LoC 30 */
    Expr green = clamp((eq(x, y) - 0.343f * (Cb(x, y) - 128) - 0.711f * (Cr(x, y) - 128)), 0, 255); /* LoC 31 */
    Expr blue = clamp((eq(x, y) + 1.765f * (Cb(x, y) - 128))/1000, 0, 255); /* LoC 32 */
    Func output("output"); /* LoC 33 */
    output(x, y, c) = mux(c, {red, green, blue}); /* LoC 34 */
    output.ensures(output(x, y, c) >= 0 && output(x, y, c)<=255); /* LoA 6 */
    // Bounds
    {
        input.dim(0).set_bounds(0, 1536);
        input.dim(1).set_bounds(0, 2560);
        input.dim(2).set_bounds(0, 3);
        input.dim(1).set_stride(1536);
        input.dim(2).set_stride(1536*2560);

        output.output_buffer().dim(0).set_bounds(0, 1536);
        output.output_buffer().dim(1).set_bounds(0, 2560);
        output.output_buffer().dim(2).set_bounds(0, 3);
        output.output_buffer().dim(1).set_stride(1536);
        output.output_buffer().dim(2).set_stride(1536*2560);
    }

    cdf.bound(x, 0, 256);
    output.bound(c, 0, 3);

    // Schedule
    if(schedule == 0){

    } else if(schedule == 1){  /* LoC 4 */

        eq.compute_at(output, y);

        output.reorder(c, x, y)
            .unroll(c)
            .parallel(y)
            ;
    } else if(schedule == 2){  /* LoC 6 */
        Y.compute_root();
        eq.compute_root();
        hist.compute_root();

        output
            .reorder(c, x, y)
            .unroll(c)
            .parallel(y)
            ;
    } else if(schedule == 3){  /* LoC 13 */
        int vec = 4;
        hist_rows
            .compute_root()
            .parallel(y, 4) // Combination of split and parallel
            ;
        hist_rows
            .update()
            .reorder(y, rx)
            ;
        hist.compute_root()
            .update()
            .reorder(x, ry)
            .parallel(x)
            .reorder(ry, x)
            ;

        cdf.compute_root();
        output.reorder(c, x, y)
            .unroll(c)
            .parallel(y, 8) // Combination of split and parallel
            ;
    } else if(schedule == 4){
        const int vec = 4; ///natural_vector_size<float>();
        // Make separate copies of Y to use while
        // histogramming and while computing the output. It's
        // better to redundantly luminance than reload it, but
        // you don't want to inline it into the histogram
        // computation because then it doesn't vectorize.
        RVar rxo("rxo"), rxi("rxi");
        Var  v("v"), yi("yi");
        Y.clone_in(hist_rows)
            .compute_at(hist_rows.in(), y)
            .split(x, x, v, vec)
            // .vectorize(v)
            ;

        hist_rows.in()
            .compute_root()
            .split(x, x, v, vec)
            // .vectorize(v)
            .split(y, y, yi, 4)
            .parallel(y)
            ;
        hist_rows.compute_at(hist_rows.in(), y)
            .split(x, x, v, vec)
            // .vectorize(v)
            .update()
            .reorder(y, rx)
            .unroll(y)
            ;
        hist.compute_root()
            .split(x, x, v, vec)
            // .vectorize(v)
            .update()
            .reorder(x, ry)
            .split(x, x, v, vec)
            // .vectorize(v)
            .unroll(x, 4)
            .parallel(x)
            .reorder(ry, x)
            ;

        cdf.compute_root();
        output.reorder(c, x, y)
            .bound(c, 0, 3)
            .unroll(c)
            .split(y, y, yi, 8)
            .parallel(y)
            .split(x, x, v, vec * 2)
            // .vectorize(v)
            ;

    }

    Target target = Target();
    Target new_target = target
      .with_feature(Target::NoAsserts)
      .with_feature(Target::NoBoundsQuery)
      ;

    std::string name = "hist";

    if(front) {
        output.translate_to_pvl(name + "_front_pvl.pvl", {}, {});
    } else {
        output.compile_to_pvl(name + "_pvl-" + std::to_string(schedule) + ".pvl" , {input}, {}, name, new_target);
    }
}