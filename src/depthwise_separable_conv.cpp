#include "Halide.h"
#include "helper.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;

int main(int argc, char *argv[]) {
    std::string name = argv[1];
    /* Halide algorithm */
    // [in_channels, width, height, batch_size]
    ImageParam input(type_of<float>(), 4, "input");

    // [channel_multiplier, in_channels, filter_width, filter_height]
    ImageParam depthwise_filter(type_of<float>(), 4, "depthwise_filter");

    // [out_channels, channel_multiplier * in_channels]
    ImageParam pointwise_filter(type_of<float>(), 2, "pointwise_filter");

    // [out_channels]
    ImageParam bias(type_of<float>(), 1, "bias");

    // [out_channels, width, height, batch_size]
    Func output("output");

    // Bounds
    const int N = 4, CI = 32, CO = 16, CM = 1, W = 112, H = 112;

    // The algorithm. It will be a generic depthwise convolution,
    // with no assumptions about input sizes or shapes. This makes
    // it especially challenging to schedule.

    // Some free variables, where x and y represent the spatial dimensions.
    Var x("x"), y("y"), d("d"), b("b");

    // Pad x and y with 0. Unfortunately the built-in boundary
    // condition helpers cause unwanted loop partitioning.
    Func input_bounded("input_bounded");
    // Expr in_bounds = (x >= 0 && x < input.dim(1).extent() &&
    //                     y >= 0 && y < input.dim(2).extent());
    Expr in_bounds = (x >= 0 && x < W &&
                        y >= 0 && y < H);
    Expr clamped_x = clamp(x, 0, W);
    Expr clamped_y = clamp(y, 0, H);
    input_bounded(d, x, y, b) =
        select(in_bounds, input(d, clamped_x, clamped_y, b), 0.0f);

    // Expr channel_multiplier = depthwise_filter.dim(0).extent();
    Expr channel_multiplier = CI / CO;

    // Convolve the image depthwise -- for each input channel,
    // generate channel_multiplier number of intermediate channels using convolution
    Func depthwise_convolved("depthwise_convolved");
    Expr pad_width = 3 / 2; // depthwise_filter.dim(2).extent() / 2;
    Expr pad_height = 3 / 2; // depthwise_filter.dim(3).extent() / 2;
    // RDom depthwise_filter_dom(0, depthwise_filter.dim(0).extent(),
    //                             0, depthwise_filter.dim(2).extent(),
    //                             0, depthwise_filter.dim(3).extent(), "r");
    RDom depthwise_filter_dom(0, CI / CO,
                                0, 3,
                                0, 3, "r");
    // Give clearer names to the reduction over input channels (depth), x and y.
    RVar rd = depthwise_filter_dom[0];
    RVar rx = depthwise_filter_dom[1];
    RVar ry = depthwise_filter_dom[2];
    depthwise_convolved(d, x, y, b) +=
        depthwise_filter(rd, d, rx, ry) *
        input_bounded(d / channel_multiplier,
                        x + rx - pad_width,
                        y + ry - pad_height,
                        b);

    // Convolve the image point-wise: for each pixel we map from
    // input_channels * channel_multiplier number of channels to output_channels
    Func pointwise_convolved("pointwise_convolved");
    // Reduction over the channels in the depthwise output
    // RDom rc(0, pointwise_filter.dim(1).extent(), "rc");
    RDom rc(0, CI * CM, "rc");
    pointwise_convolved(d, x, y, b) = bias(d);
    pointwise_convolved(d, x, y, b) +=
        pointwise_filter(d, rc) * depthwise_convolved(rc, x, y, b);

    // ReLU
    output(d, x, y, b) = max(pointwise_convolved(d, x, y, b), 0.f);

    /* Schedule */
    {
        // CPU schedule

        // 0.13ms on an Intel i9-9960X using 16 threads pinned to 3.0 GHz,
        // which is only about 20% of peak flops.

        int tile_w = 1;
        int tile_h = 1;
        int tile_d = 1;
        const int vec = 8;// natural_vector_size<float>();

        // Figure out how many registers we have in the register
        // file on this target.
        int num_regs = 16;
        // if (get_target().has_feature(Target::AVX512_Skylake) ||
        //     (get_target().arch == Target::ARM &&
        //         get_target().bits == 64)) {
        //     num_regs = 32;
        // }

        // Pick a tile size designed to fit into the register file.
        if (num_regs == 32 && vec == 16) {
            // 32 vector registers available of size 16. Use 24 of
            // them for accumulators.
            tile_d = 1;
            tile_w = 6;
            tile_h = 4;
            // Using more tiles in the d dimension would be
            // better, but we're tuning for 16 output channels and
            // our vectors are already that wide (on avx512).
        } else if (num_regs == 32 && vec == 4) {
            // 32 vector registers, of size 4. We'll use 24.
            tile_d = 4;
            tile_w = 3;
            tile_h = 2;
        } else if (num_regs == 16 && vec == 8) {
            // 16 registers available of size 8. Use 12 for accumulators.
            tile_d = 2;
            tile_w = 3;
            tile_h = 2;
        } else {
            // Old x86 or 32-bit arm. Assume vectors of size 4,
            // 16 registers. No FMA so we need to reserve a few
            // more registers for things other than the
            // accumulators.
            tile_d = 4;
            tile_w = 2;
            tile_h = 1;
        }
        // Change units from vectors to elements
        tile_d *= vec;

        // This schedule aggressively fuses the depthwise conv into
        // the pointwise conv. We do the depthwise convolution within
        // slices of the channel reduction loop in the pointwise
        // convolution.

        Var di("di"), xi("xi"), yi("yi");
        RVar ro("ro"), ri("ri");

        output
            .tile({d, x, y}, {di, xi, yi}, {tile_d, tile_w, tile_h}, TailStrategy::GuardWithIf)
            // .vectorize(di)
            // .unroll(xi)
            // .unroll(yi)
            // .fuse(y, b, b)
            .parallel(b);

        pointwise_convolved.compute_at(output, d)
            // .vectorize(d)
            // .unroll(x)
            // .unroll(y)
            .update()
            .reorder(d, x, y, rc, b)
            // .vectorize(d)
            // .unroll(x)
            // .unroll(y)
            .split(rc, ro, ri, tile_d)
            ;

        depthwise_convolved
            // .store_in(MemoryType::Stack)
            .bound_extent(d, tile_d)
            .compute_at(pointwise_convolved, ro)
            // .vectorize(d)
            .reorder(x, y, d)
            // .unroll(x)
            // .unroll(y)
            .update()
            // .vectorize(d)
            .reorder(x, y, d, rd, rx, ry, b)
            // .unroll(x)
            // .unroll(y)
            ;

        input_bounded
            // .store_in(MemoryType::Stack)
            .compute_root()// .compute_at(pointwise_convolved, ro)
            .tile(d, x, di, xi, vec, 4, TailStrategy::RoundUp)
            // .vectorize(di)
            // .unroll(xi)
            ;
    }
    /* End Schedule */

    set_bounds({{0, CI}, {0, W}, {0, H}, {0, N}}, input);
    set_bounds({{0, CI / CO}, {0, CI}, {0, 3}, {0, 3}}, depthwise_filter);
    set_bounds({{0, CO}, {0, CI * CM}}, pointwise_filter);
    set_bounds({{0, CO}}, bias);
    set_bounds({{0, CO}, {0, W}, {0, H}, {0, N}}, output.output_buffer());

    Target new_target = standard_target();
    output.compile_to_pvl(name + ".pvl", {input, depthwise_filter, pointwise_filter, bias}, {}, name, new_target, true);
}