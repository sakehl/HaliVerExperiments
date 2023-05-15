#include "Halide.h"
#include "helper.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::ConciseCasts;


int main(int argc, char *argv[]) {
    int schedule; 
    bool only_memory, front;
    std::string name;
    int res = read_args(argc, argv, schedule, only_memory, front, name);
    if(res != 0) return res;

    /* Halide algorithm */
    int nx = 1536;
    int ny = 2560;

    ImageParam input(type_of<float>(), 3, "input"); 
    input.requires(input(_) >= 0.0f && input(_) <= 255.0f);
    Var x("x"), y("y"), c("c");
    Func Y("Y"); 
    Y(x, y) = (0.299f * input(x, y, 0) + 
               0.587f * input(x, y, 1) + 
               0.114f * input(x, y, 2)); 

    Func Cr("Cr"); 
    Expr R = input(x, y, 0); 
    Cr(x, y) = ((R - Y(x, y)) * 0.713f) + 128; 
    Cr.ensures(Cr(x,y) >= 0.0f && Cr(x,y) < 256.0f);

    Func Cb("Cb"); 
    Expr B = input(x, y, 2); 
    Cb(x, y) = ((B - Y(x, y)) * 0.564f) + 128; 
    Cb.ensures(Cb(x,y) >= 0.0f && Cb(x,y) < 256.0f);

    Func hist_rows("hist_rows"); 
    hist_rows(x, y) = 0; 
    hist_rows.ensures(hist_rows(x, y) == 0);
    RDom rx(0, nx); 
    Expr bin = cast<int>(clamp(Y(rx, y), 0, 255)); 
    hist_rows(bin, y) += 1; 
    hist_rows.invariant(0 <= hist_rows(x,y) && hist_rows(x,y) <= rx);
    hist_rows.ensures(0 <= hist_rows(x,y) && hist_rows(x,y) <= nx);

    Func hist("hist"); 
    hist(x) = 0; 
    hist.ensures(hist(x) == 0);
    RDom ry(0, ny); 
    hist(x) += hist_rows(x, ry); 
    hist.invariant(0 <= hist(x) && hist(x) <= ry*nx);
    hist.ensures(0 <= hist(x) && hist(x) <= ny*nx);

    Func cdf("cdf"); 
    cdf(x) = hist(0); 
    RDom b(1, 255); 
    cdf(b.x) = cdf(b.x - 1) + hist(b.x); 

    Func cdf_bin("cdf_bin"); 
    cdf_bin(x, y) = u8(clamp(Y(x, y), 0, 255)); 

    Func eq("equalize"); 
    eq(x, y) = clamp((cdf(cdf_bin(x, y)) * 255.0f) / (ny * nx), 0, 255); 

    Expr red = clamp((eq(x, y) + (Cr(x, y) - 128) * 1.4f), 0, 255); 
    Expr green = clamp((eq(x, y) - 0.343f * (Cb(x, y) - 128) - 0.711f * (Cr(x, y) - 128)), 0, 255); 
    Expr blue = clamp((eq(x, y) + 1.765f * (Cb(x, y) - 128))/1000, 0, 255); 
    Func output("output"); 
    output(x, y, c) = mux(c, {red, green, blue}); 
    output.ensures(output(x, y, c) >= 0 && output(x, y, c)<=255);

    /* Schedule 0 */
    // Bounds
    {
        int nx = 1536;
        int ny = 2560;
        set_bounds({{0, nx}, {0, ny}, {0, 3}}, input);
        set_bounds({{0, nx}, {0, ny}, {0, 3}}, output.output_buffer());
    }

    cdf.bound(x, 0, 256);
    output.bound(c, 0, 3);
    if(schedule == 0){
    /* Schedule 1 */
    } else if(schedule == 1){  

        eq.compute_at(output, y);

        output.reorder(c, x, y)
            .unroll(c)
            .parallel(y)
            ;
    /* Schedule 2 */
    } else if(schedule == 2){  
        Y.compute_root();
        eq.compute_root();
        hist.compute_root();

        output
            .reorder(c, x, y)
            .unroll(c)
            .parallel(y)
            ;
    /* Schedule 3 */
    } else if(schedule == 3){  
        int vec = 4;
        hist_rows
            .compute_root()
            .parallel(y, 4, TailStrategy::GuardWithIf) // Combination of split and parallel
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
            .parallel(y, 8, TailStrategy::GuardWithIf) // Combination of split and parallel
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
    /* End Schedule */

    Target new_target = standard_target();

    if(front) {
        output.translate_to_pvl(name + ".pvl", {}, {});
    } else {
        output.compile_to_pvl(name + ".pvl" , {input}, {}, name, new_target, only_memory);
    }
}