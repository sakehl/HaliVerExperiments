#include "Halide.h"
#include "helper.h"
#include <stdio.h>
#include <functional>

using namespace Halide;

int main(int argc, char *argv[]) {
    int schedule; 
    bool only_memory, front;
    std::string name;
    int res = read_args(argc, argv, schedule, only_memory, front, name);
    if(res != 0) return res;

    int nx = 2048;
    int ny = 1024;
    float scale_factor = 0.5;
    bool upsample = scale_factor > 1.0;
    int new_nx = nx * scale_factor;
    int new_ny = ny * scale_factor;

    /* Halide algorithm */
    ImageParam input(type_of<float>(), 3, "input");
    // Common Vars
    Var x("x"), y("y"), c("c"), k("k");
    Var xi("xi"), yi("yi");

    // Intermediate Funcs
    Func as_float("as_float"), clamped("clamped"), resized_x("resized_x"), resized_y("resized_y"),
        unnormalized_kernel_x("unnormalized_kernel_x"), unnormalized_kernel_y("unnormalized_kernel_y"),
        kernel_x("kernel_x"), kernel_y("kernel_y"),
        kernel_sum_x("kernel_sum_x"), kernel_sum_y("kernel_sum_y");
    Func output("output");

    input.requires(input(_) >= 0.0f);
    clamped = Halide::BoundaryConditions::repeat_edge(input);
    // Handle different types by just casting to float
    as_float(x, y, c) = cast<float>(clamped(x, y, c));
    as_float.ensures(as_float(x, y, c) >= 0.0f);

    Expr kernel_scaling = upsample ? Expr(1.0f) : scale_factor;

    Expr kernel_radius = 0.5f / kernel_scaling;

    Expr kernel_taps = cast<int>(ceil(1.0f / kernel_scaling)); 

    // source[xy] are the (non-integer) coordinates inside the source image
    Expr sourcex = (x + 0.5f) / scale_factor - 0.5f;
    Expr sourcey = (y + 0.5f) / scale_factor - 0.5f;

    // Initialize interpolation kernels. Since we allow an arbitrary
    // scaling factor, the filter coefficients are different for each x
    // and y coordinate.
    Expr beginx = cast<int>(ceil(sourcex - kernel_radius));
    Expr beginy = cast<int>(ceil(sourcey - kernel_radius));

    RDom r(0, kernel_taps);

    auto kernel = [](Expr x) -> Expr {
        Expr xx = abs(x);
        return select(abs(x) <= 0.5f, 1.0f, 0.0f);
    };
    unnormalized_kernel_x(x, k) = kernel((k + beginx - sourcex) * kernel_scaling);
    unnormalized_kernel_x.ensures(implies(k == 0, unnormalized_kernel_x(x, k) == 1.0f));
    unnormalized_kernel_x.ensures(unnormalized_kernel_x(x, k) >= 0.0f);
    unnormalized_kernel_y(y, k) = kernel((k + beginy - sourcey) * kernel_scaling);
    unnormalized_kernel_y.ensures(implies(k == 0, unnormalized_kernel_y(y, k) == 1.0f));
    unnormalized_kernel_y.ensures(unnormalized_kernel_y(y, k) >= 0.0f);

    std::function<Expr(Expr)> biggerThanZero = [](Expr f) -> Expr {
        return f >= 0.0f;
    };

    std::function<Expr(Expr)> biggerThanOne = [r](Expr f) -> Expr {
        return implies(r>0, f >= 1.0f);
    };

    kernel_sum_x(x) = sum(unnormalized_kernel_x(x, r), "kernel_sum_x", {biggerThanZero, biggerThanOne});
    kernel_sum_x.ensures(kernel_sum_x(x) >= 1.0f);
    kernel_sum_y(y) = sum(unnormalized_kernel_y(y, r), "kernel_sum_y", {biggerThanZero, biggerThanOne});
    kernel_sum_y.ensures(kernel_sum_y(y) >= 1.0f);

    kernel_x(x, k) = unnormalized_kernel_x(x, k) / kernel_sum_x(x);
    kernel_x.ensures(kernel_x(x, k) >= 0.0f);
    kernel_y(y, k) = unnormalized_kernel_y(y, k) / kernel_sum_y(y);
    kernel_y.ensures(kernel_y(y, k) >= 0.0f);

    // Perform separable resizing. The resize in x vectorizes
    // poorly compared to the resize in y, so do it first if we're
    // upsampling, and do it second if we're downsampling.
    Func resized;
    if (upsample) {
        resized_x(x, y, c) = sum(kernel_x(x, r) * as_float(r + beginx, y, c), "resized_x", {biggerThanZero});
        resized_x.ensures(resized_x(x,y,c) >= 0.0f);
        resized_y(x, y, c) = sum(kernel_y(y, r) * resized_x(x, r + beginy, c), "resized_y", {biggerThanZero});
        resized_y.ensures(resized_y(x,y,c) >= 0.0f);
        resized = resized_y;
    } else {
        resized_y(x, y, c) = sum(kernel_y(y, r) * as_float(x, r + beginy, c), "resized_y", {biggerThanZero});
        resized_y.ensures(resized_y(x,y,c) >= 0.0f);
        resized_x(x, y, c) = sum(kernel_x(x, r) * resized_y(r + beginx, y, c), "resized_x", {biggerThanZero});
        resized_x.ensures(resized_x(x,y,c) >= 0.0f);
        resized = resized_x;
    }

    if (input.type().is_float()) {
        output(x, y, c) = clamp(resized(x, y, c), 0.0f, 1.0f);
    } else {
        output(x, y, c) = saturating_cast(input.type(), resized(x, y, c));
    }

    output.ensures(output(x, y, c) >= 0.0f);

    /* Schedule 1 */
    if (schedule ==  1) {
        // naive: compute_root() everything
        unnormalized_kernel_x
            .compute_root();
        kernel_sum_x
            .compute_root();
        kernel_x
            .compute_root();
        unnormalized_kernel_y
            .compute_root();
        kernel_sum_y
            .compute_root();
        kernel_y
            .compute_root();
        as_float
            .compute_root();
        resized_x
            .compute_root();
        output
            .compute_root();
    /* Schedule 2 */
    } else if (schedule ==  2) {
        // less-naive: add vectorization and parallelism to 'large' realizations;
        // use compute_at for as_float calculation
        unnormalized_kernel_x
            .compute_root();
        kernel_sum_x
            .compute_root();
        kernel_x
            .compute_root();

        unnormalized_kernel_y
            .compute_root();
        kernel_sum_y
            .compute_root();
        kernel_y
            .compute_root();

        as_float
            .compute_at(resized_x, y);
        resized_x
            .compute_root()
            .parallel(y);
        output
            .compute_root()
            .parallel(y)
            // Split plus make inner dim parallel
            .parallel(x,8,TailStrategy::GuardWithIf); //vectorize(x, 8);
    /* Schedule 3 */
    } else if (schedule ==  3) {
        // complex: use compute_at() and tiling intelligently.
        unnormalized_kernel_x
            .compute_at(kernel_x, x)
            .parallel(x);
        kernel_sum_x
            .compute_at(kernel_x, x)
            .parallel(x);
        kernel_x
            .compute_root()
            .reorder(k, x)
            .parallel(x, 8, TailStrategy::GuardWithIf);

        unnormalized_kernel_y
            .compute_at(kernel_y, y)
            .parallel(y, 8, TailStrategy::GuardWithIf);
        kernel_sum_y
            .compute_at(kernel_y, y)
            .parallel(y);
        kernel_y
            .compute_at(output, y)
            .reorder(k, y)
            .parallel(y, 8, TailStrategy::GuardWithIf);

        if (upsample) {
            as_float
                .compute_at(output, y)
                .parallel(x, 8, TailStrategy::GuardWithIf);
            resized_x
                .compute_at(output, x)
                .parallel(x, 8, TailStrategy::GuardWithIf);
            output
                .tile(x, y, xi, yi, 16, 64, TailStrategy::GuardWithIf)
                .parallel(y)
                .parallel(xi);
        } else {
            resized_y
                .compute_at(output, y)
                .parallel(x, 8, TailStrategy::GuardWithIf);
            resized_x
                .compute_at(output, xi);
            output
                .tile(x, y, xi, yi, 32, 8, TailStrategy::GuardWithIf)
                .parallel(y)
                .parallel(xi);
        }
    }
    /* End Schedule */

    // Bounding the dimensions
    set_bounds({{0, nx}, {0, ny}, {0, 3}}, input);
    set_bounds({{0, new_nx}, {0, new_ny}, {0, 3}}, output.output_buffer());

    // No assertions in code
    Target target = standard_target();

    if(front) {
        output.translate_to_pvl(name + ".pvl", {}, {});
    } else {
        output.compile_to_c(name + ".c" , {input}, {}, name, target, only_memory);
    }
}