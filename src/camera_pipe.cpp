#include "Halide.h"
#include "helper.h"
#include <stdint.h>

using std::vector;

using namespace Halide;
using namespace Halide::ConciseCasts;

/* Halide algorithm */
bool auto_schedule = false;
Type result_type = UInt(8);

float color_temp = 3700;
float contrast = 50;
float sharpen_strength = 1.0;
int blackLevel = 25;
int whiteLevel = 1023;

// Shared variables
Var x, y, c, yi, yo, yii, xi;

// Average two positive values rounding up
Expr avg(Expr a, Expr b) {
    Type wider = a.type().with_bits(a.type().bits() * 2);
    return cast(a.type(), (cast(wider, a) + b + 1) / 2);
}

Expr blur121(Expr a, Expr b, Expr c) {
    return avg(avg(a, c), b);
}

Func interleave_x(Func a, Func b) {
    Func out;
    out(x, y) = select((x % 2) == 0, a(x / 2, y), b(x / 2, y));
    return out;
}

Func interleave_y(Func a, Func b) {
    Func out;
    out(x, y) = select((y % 2) == 0, a(x, y / 2), b(x, y / 2));
    return out;
}


class Demosaic {
public:
    LoopLevel intermed_compute_at;
    LoopLevel intermed_store_at;
    LoopLevel output_compute_at;

    // ImageParam deinterleaved(type_of<int>(), 2, "deinterleaved");
    Func deinterleaved; //Input<Func> deinterleaved{"deinterleaved", Int(16), 3};
    Func output; // Output<Func> output{"output", Int(16), 3};
    // Output<Func> output{"output", Int(16), 3};

    Demosaic(LoopLevel intermed_compute_at = LoopLevel::inlined(),
        LoopLevel intermed_store_at = LoopLevel::inlined(),
        LoopLevel output_compute_at = LoopLevel::inlined()) 
    :   intermed_compute_at(intermed_compute_at),
        intermed_store_at(intermed_store_at),
        output_compute_at(output_compute_at) { };

    // Defines outputs using inputs
    void apply(Func input){
        deinterleaved = input;
        // These are the values we already know from the input
        // x_y = the value of channel x at a site in the input of channel y
        // gb refers to green sites in the blue rows
        // gr refers to green sites in the red rows

        // Give more convenient names to the four channels we know
        Func r_r("r_r"), g_gr("g_gr"), g_gb("g_gb"), b_b("b_b");

        g_gr(x, y) = deinterleaved(x, y, 0);
        r_r(x, y) = deinterleaved(x, y, 1);
        b_b(x, y) = deinterleaved(x, y, 2);
        g_gb(x, y) = deinterleaved(x, y, 3);

        // These are the ones we need to interpolate
        Func b_r, g_r, b_gr, r_gr, b_gb, r_gb, r_b, g_b;

        // First calculate green at the red and blue sites

        // Try interpolating vertically and horizontally. Also compute
        // differences vertically and horizontally. Use interpolation in
        // whichever direction had the smallest difference.
        Expr gv_r = avg(g_gb(x, y - 1), g_gb(x, y));
        Expr gvd_r = absd(g_gb(x, y - 1), g_gb(x, y));
        Expr gh_r = avg(g_gr(x + 1, y), g_gr(x, y));
        Expr ghd_r = absd(g_gr(x + 1, y), g_gr(x, y));

        g_r(x, y) = select(ghd_r < gvd_r, gh_r, gv_r);

        Expr gv_b = avg(g_gr(x, y + 1), g_gr(x, y));
        Expr gvd_b = absd(g_gr(x, y + 1), g_gr(x, y));
        Expr gh_b = avg(g_gb(x - 1, y), g_gb(x, y));
        Expr ghd_b = absd(g_gb(x - 1, y), g_gb(x, y));

        g_b(x, y) = select(ghd_b < gvd_b, gh_b, gv_b);

        // Next interpolate red at gr by first interpolating, then
        // correcting using the error green would have had if we had
        // interpolated it in the same way (i.e. add the second derivative
        // of the green channel at the same place).
        Expr correction;
        correction = g_gr(x, y) - avg(g_r(x, y), g_r(x - 1, y));
        r_gr(x, y) = correction + avg(r_r(x - 1, y), r_r(x, y));

        // Do the same for other reds and blues at green sites
        correction = g_gr(x, y) - avg(g_b(x, y), g_b(x, y - 1));
        b_gr(x, y) = correction + avg(b_b(x, y), b_b(x, y - 1));

        correction = g_gb(x, y) - avg(g_r(x, y), g_r(x, y + 1));
        r_gb(x, y) = correction + avg(r_r(x, y), r_r(x, y + 1));

        correction = g_gb(x, y) - avg(g_b(x, y), g_b(x + 1, y));
        b_gb(x, y) = correction + avg(b_b(x, y), b_b(x + 1, y));

        // Now interpolate diagonally to get red at blue and blue at
        // red. Hold onto your hats; this gets really fancy. We do the
        // same thing as for interpolating green where we try both
        // directions (in this case the positive and negative diagonals),
        // and use the one with the lowest absolute difference. But we
        // also use the same trick as interpolating red and blue at green
        // sites - we correct our interpolations using the second
        // derivative of green at the same sites.

        correction = g_b(x, y) - avg(g_r(x, y), g_r(x - 1, y + 1));
        Expr rp_b = correction + avg(r_r(x, y), r_r(x - 1, y + 1));
        Expr rpd_b = absd(r_r(x, y), r_r(x - 1, y + 1));

        correction = g_b(x, y) - avg(g_r(x - 1, y), g_r(x, y + 1));
        Expr rn_b = correction + avg(r_r(x - 1, y), r_r(x, y + 1));
        Expr rnd_b = absd(r_r(x - 1, y), r_r(x, y + 1));

        r_b(x, y) = select(rpd_b < rnd_b, rp_b, rn_b);

        // Same thing for blue at red
        correction = g_r(x, y) - avg(g_b(x, y), g_b(x + 1, y - 1));
        Expr bp_r = correction + avg(b_b(x, y), b_b(x + 1, y - 1));
        Expr bpd_r = absd(b_b(x, y), b_b(x + 1, y - 1));

        correction = g_r(x, y) - avg(g_b(x + 1, y), g_b(x, y - 1));
        Expr bn_r = correction + avg(b_b(x + 1, y), b_b(x, y - 1));
        Expr bnd_r = absd(b_b(x + 1, y), b_b(x, y - 1));

        b_r(x, y) = select(bpd_r < bnd_r, bp_r, bn_r);

        // Resulting color channels
        Func r, g, b;

        // Interleave the resulting channels
        r = interleave_y(interleave_x(r_gr, r_r),
                         interleave_x(r_b, r_gb));
        g = interleave_y(interleave_x(g_gr, g_r),
                         interleave_x(g_b, g_gb));
        b = interleave_y(interleave_x(b_gr, b_r),
                         interleave_x(b_b, b_gb));

        output = Func ("output");
        output(x, y, c) = mux(c, {r(x, y), g(x, y), b(x, y)});

        // These are the stencil stages we want to schedule
        // separately. Everything else we'll just inline.
        intermediates.push_back(g_r);
        intermediates.push_back(g_b);

        // Schedule
    }

    /* Schedule */
    void schedule() {
        Pipeline p(output);

        // int vec = get_target().natural_vector_size(UInt(16));
        int vec = 8;

        for (Func f : intermediates) {
            f.compute_at(intermed_compute_at)
                .store_at(intermed_store_at)
                // .vectorize(x, 2 * vec, TailStrategy::RoundUp)
                // .fold_storage(y, 4)
                ;
        }
        // intermediates[1].compute_with(
        //     intermediates[0], x,
        //     {{x, LoopAlignStrategy::AlignStart}, {y, LoopAlignStrategy::AlignStart}});
        output.compute_at(output_compute_at)
            // .vectorize(x)
            // .unroll(y)
            .reorder(c, x, y)
            // .unroll(c)
            ;
    }
    /* End Schedule */

private:
    // Intermediate stencil stages to schedule
    vector<Func> intermediates;
};

Func hot_pixel_suppression(Func input) {

    Expr a = max(input(x - 2, y), input(x + 2, y),
                 input(x, y - 2), input(x, y + 2));

    Func denoised;
    denoised(x, y) = clamp(input(x, y), 0, a);

    return denoised;
}

Func deinterleave(Func raw) {
    // Deinterleave the color channels
    Func deinterleaved("deinterleaved");

    deinterleaved(x, y, c) = mux(c,
                                 {raw(2 * x, 2 * y),
                                  raw(2 * x + 1, 2 * y),
                                  raw(2 * x, 2 * y + 1),
                                  raw(2 * x + 1, 2 * y + 1)});
    return deinterleaved;
}

Func color_correct(Func input, Func matrix_3200, Func matrix_7000) {
    // Get a color matrix by linearly interpolating between two
    // calibrated matrices using inverse kelvin.
    Expr kelvin = color_temp;

    Func matrix;
    Expr alpha = (1.0f / kelvin - 1.0f / 3200) / (1.0f / 7000 - 1.0f / 3200);
    Expr val = (matrix_3200(x, y) * alpha + matrix_7000(x, y) * (1 - alpha));
    matrix(x, y) = cast<int16_t>(val * 256.0f);  // Q8.8 fixed point

    /* Schedule */
    matrix.compute_root();
    /* End Schedule */

    Func corrected;
    Expr ir = cast<int32_t>(input(x, y, 0));
    Expr ig = cast<int32_t>(input(x, y, 1));
    Expr ib = cast<int32_t>(input(x, y, 2));

    Expr r = matrix(3, 0) + matrix(0, 0) * ir + matrix(1, 0) * ig + matrix(2, 0) * ib;
    Expr g = matrix(3, 1) + matrix(0, 1) * ir + matrix(1, 1) * ig + matrix(2, 1) * ib;
    Expr b = matrix(3, 2) + matrix(0, 2) * ir + matrix(1, 2) * ig + matrix(2, 2) * ib;

    r = cast<int16_t>(r / 256);
    g = cast<int16_t>(g / 256);
    b = cast<int16_t>(b / 256);
    corrected(x, y, c) = mux(c, {r, g, b});

    return corrected;
}

Func apply_curve(Func input) {
    // copied from FCam
    Func curve("curve");

    Expr minRaw = 0 + blackLevel;
    Expr maxRaw = whiteLevel;

    // How much to upsample the LUT by when sampling it.
    int lutResample = 1;

    minRaw /= lutResample;
    maxRaw /= lutResample;

    Expr invRange = 1.0f / (maxRaw - minRaw);
    Expr b = 2.0f - Halide::pow(2.0f, contrast / 100.0f);
    Expr a = 2.0f - 2.0f * b;

    // Get a linear luminance in the range 0-1
    Expr xf = clamp(cast<float>(x - minRaw) * invRange, 0.0f, 1.0f);
    // Gamma correct it
    float gamma = 2.0;
    Expr g = pow(xf, 1.0f / gamma);
    // Apply a piecewise quadratic contrast curve
    Expr z = select(g > 0.5f,
                    1.0f - (a * (1.0f - g) * (1.0f - g) + b * (1.0f - g)),
                    a * g * g + b * g);

    // Convert to 8 bit and save
    Expr val = cast(result_type, clamp(z * 255.0f + 0.5f, 0.0f, 255.0f));
    // makeLUT add guard band outside of (minRaw, maxRaw]:
    curve(x) = select(x <= minRaw, 0, select(x > maxRaw, 255, val));

    // It's a LUT, compute it once ahead of time.
    /* Schedule */
    curve.compute_root();
    /* End Schedule */

    Func curved("curved");

    if (lutResample == 1) {
        // Use clamp to restrict size of LUT as allocated by compute_root
        curved(x, y, c) = curve(clamp(input(x, y, c), 0, 1023));
    } else {
        // Use linear interpolation to sample the LUT.
        Expr in = input(x, y, c);
        Expr u0 = in / lutResample;
        Expr u = in % lutResample;
        Expr y0 = curve(clamp(u0, 0, 127));
        Expr y1 = curve(clamp(u0 + 1, 0, 127));
        curved(x, y, c) = cast<uint8_t>((cast<uint16_t>(y0) * lutResample + (y1 - y0) * u) / lutResample);
    }

    return curved;
}

Func sharpen(Func input) {
    // Convert the sharpening strength to 2.5 fixed point. This allows sharpening in the range [0, 4].
    Func sharpen_strength_x32("sharpen_strength_x32");
    sharpen_strength_x32() = u8_sat(sharpen_strength * 32);
    /* Schedule */
    sharpen_strength_x32.compute_root();
    /* End Schedule */
    

    // Make an unsharp mask by blurring in y, then in x.
    Func unsharp_y("unsharp_y");
    unsharp_y(x, y, c) = blur121(input(x, y - 1, c), input(x, y, c), input(x, y + 1, c));

    Func unsharp("unsharp");
    unsharp(x, y, c) = blur121(unsharp_y(x - 1, y, c), unsharp_y(x, y, c), unsharp_y(x + 1, y, c));

    Func mask("mask");
    mask(x, y, c) = cast<int16_t>(input(x, y, c)) - cast<int16_t>(unsharp(x, y, c));

    // Weight the mask with the sharpening strength, and add it to the
    // input to get the sharpened result.
    Func sharpened("sharpened");
    sharpened(x, y, c) = u8_sat(input(x, y, c) + (mask(x, y, c) * sharpen_strength_x32()) / 32);

    return sharpened;
}

int main(int argc, char *argv[]) {
    std::string name = argv[1];
    ImageParam input(type_of<uint16_t>(), 2, "input");
    ImageParam matrix_3200(type_of<float>(), 2, "matrix_3200");
    ImageParam matrix_7000(type_of<float>(), 2, "matrix_7000");

    // OutputImageParam processed;
    Func processed("processed");
    // shift things inwards to give us enough padding on the
    // boundaries so that we don't need to check bounds. We're going
    // to make a 2560x1920 output image, just like the FCam pipe, so
    // shift by 16, 12. We also convert it to be signed, so we can deal
    // with values that fall below 0 during processing.
    Func shifted("shifted");
    shifted(x, y) = cast<int16_t>(input(x + 16, y + 12));

    Func denoised = hot_pixel_suppression(shifted);

    Func deinterleaved = deinterleave(denoised);

    auto demosaiced = Demosaic();
    demosaiced.apply(deinterleaved);

    Func corrected = color_correct(demosaiced.output, matrix_3200, matrix_7000);

    Func curved = apply_curve(corrected);

    processed(x, y, c) = sharpen(curved)(x, y, c);

    int nx = 2592;
    int ny = 1968;
    int nc = 3;

    int output_nx = ((nx-32)/32)*32;
    int output_ny = ((ny-24)/32)*32;
    /* Schedule */
    // ESTIMATES
    // (This can be useful in conjunction with RunGen and benchmarks as well
    // as auto-schedule, so we do it in all cases.)
    set_bounds({{0, nx}, {0, ny}}, input);
    set_bounds({{0, 4}, {0, 3}}, matrix_3200);
    set_bounds({{0, 4}, {0, 3}}, matrix_7000);
    set_bounds({{0, output_nx}, {0, output_ny}, {0, 3}}, processed.output_buffer());
    {
        Expr out_width = output_nx; //processed.width();
        Expr out_height = output_ny; //processed.height();

        // Depending on the HVX generation, we need 2 or 4 threads
        // to saturate HVX with work. For simplicity, we'll just
        // stick to 4 threads. On balance, the overhead should
        // not be much for the 2 extra threads that we create
        // on cores that have only two HVX contexts.
        Expr strip_size = 32;
        strip_size = (strip_size / 2) * 2;

        int vec = 8; //int vec = get_target().natural_vector_size(UInt(16));
        processed
            .compute_root()
            .reorder(c, x, y)
            .split(y, yi, yii, 2, TailStrategy::RoundUp)
            .split(yi, yo, yi, strip_size / 2, TailStrategy::GuardWithIf)
            // .vectorize(x, 2 * vec, TailStrategy::RoundUp)
            // .unroll(c)
            .parallel(yo);

        denoised
            .compute_at(processed, yi)
            .store_at(processed, yo)
            // .prefetch(input, y, 2)
            // .fold_storage(y, 16)
            .tile(x, y, x, y, xi, yi, 2 * vec, 2, TailStrategy::GuardWithIf)
            // .vectorize(xi)
            // .unroll(yi)
            ;

        deinterleaved
            .compute_at(processed, yi)
            .store_at(processed, yo)
            // .fold_storage(y, 8)
            .reorder(c, x, y)
            // .vectorize(x, 2 * vec, TailStrategy::RoundUp)
            // .unroll(c)
            ;

        curved
            .compute_at(processed, yi)
            .store_at(processed, yo)
            .reorder(c, x, y)
            .tile(x, y, x, y, xi, yi, 2 * vec, 2, TailStrategy::RoundUp)
            // .vectorize(xi)
            // .unroll(yi)
            // .unroll(c)
            ;

        corrected
            .compute_at(curved, x)
            .reorder(c, x, y)
            // .vectorize(x)
            // .unroll(c)
            ;

        demosaiced.intermed_compute_at.set({processed, yi});
        demosaiced.intermed_store_at.set({processed, yo});
        demosaiced.output_compute_at.set({curved, x});

        // We can generate slightly better code if we know the splits divide the extent.
        processed
            .bound(c, 0, 3)
            .bound(x, 0, ((out_width) / (2 * vec)) * (2 * vec))
            .bound(y, 0, (out_height / strip_size) * strip_size);
    }
    /* End Schedule */

    Target new_target = standard_target();
    processed.compile_to_c(name + ".c", {input, matrix_3200, matrix_7000}, {}, name, new_target, true);
    processed.compile_to_pvl(name + ".pvl", {input, matrix_3200, matrix_7000}, {}, name, new_target, true);

    return 0;
};