
/* Altered as an example how we verify code when sequences were present . */

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

void halide_unused(bool e){};

/*@
pure int max(int x, int y) = x > y ? x : y;

pure int min(int x, int y) = x > y ? y : x;

pure float max(float x, float y) = x > y ? x : y;

pure float min(float x, float y) = x > y ? y : x;

pure int abs(int x) = x >= 0 ? x : -x;

pure float abs(float x) = x >= 0 ? x : -x;

// Euclidean division is defined internally in VerCors
pure int euclidean_div(int x, int y);
pure int euclidean_mod(int x, int y);
pure int hdiv(int x, int y) = y == 0 ? 0 : euclidean_div(x, y);
pure int hmod(int x, int y) = y == 0 ? 0 : euclidean_mod(x, y);
@*/

/*@
  requires y != 0;
  ensures \result == euclidean_div(x, y);
@*/
/*inline*/ int /*@ pure @*/ div_eucl(int x, int y)
{
    int q = x/y;
    int r = x%y;
    return r < 0 ? q + (y > 0 ? -1 : 1) : q;
}

/*@
  requires y != 0;
  ensures \result == euclidean_mod(x, y);
@*/
/*inline*/ int /*@ pure @*/ mod_eucl(int x, int y)
{
    int r = x%y;
    return (x >= 0 || r == 0) ? r : r + abs(y);
}

static inline float /*@ pure @*/ fast_inverse_f32(float x) {return 1.0f/x;};
//@ given double eps;
static inline float /*@ pure @*/ sqrt_f32(double x) {return (float)sqrt((double)x)/*@given {eps=eps}@*/;}
static inline float /*@ pure @*/ pow_f32(float x, float y){ return (float) pow((double) x, (double) y);}
static inline float /*@ pure @*/ floor_f32(float x){ return (float) floor((double) x); }
static inline float /*@ pure @*/ ceil_f32(float x){ return (float) ceil((double) x); }
static inline float /*@ pure @*/ round_f32(float x){ return (float) round((double) x); }

//@ given double eps;
static inline double /*@ pure @*/ sqrt_f64(double x) {return sqrt(x)/*@given {eps=eps}@*/;}
static inline double /*@ pure @*/ pow_f64(double x, double y) {return pow(x, y);}
static inline double /*@ pure @*/ floor_f64(double x) {return floor(x);}
static inline double /*@ pure @*/ ceil_f64(double x) {return ceil(x);}
static inline double /*@ pure @*/ round_f64(double x){ return round(x); }



struct halide_dimension_t {
    int32_t min, extent, stride;
};

struct halide_buffer_int32_t {

    /** The dimensionality of the buffer. */
    int32_t dimensions;

    /** The shape of the buffer. Halide does not own this array - you
     * must manage the memory for it yourself. */
    struct halide_dimension_t *dim;

    /** A pointer to the start of the data in main memory. In terms of
     * the Halide coordinate system, this is the address of the min
     * coordinates (defined below). */

    int32_t *host;
};

/*@ 
 requires buf != NULL ** Perm(buf, 1\2);
 requires Perm(buf->host, 1\2);
 @*/
/*@ pure @*/ int32_t *_halide_buffer_get_host_int32_t(struct halide_buffer_int32_t *buf) {
    return buf->host;
}

/*@ 
    requires buf != NULL ** Perm(buf, 1\2);
    requires Perm(buf->dim, 1\2) ** buf->dim != NULL;
    requires 0 <= d && d < \pointer_length(buf->dim);
    requires Perm(&buf->dim[d], 1\2);
    requires Perm(buf->dim[d].min, 1\2);
@*/
/*@ pure @*/ int _halide_buffer_get_min_int32_t(struct halide_buffer_int32_t *buf, int d) {
    return buf->dim[d].min;
}

/*@ 
    requires buf != NULL ** Perm(buf, 1\2);
    requires Perm(buf->dim, 1\2) ** buf->dim != NULL;
    requires 0 <= d && d < \pointer_length(buf->dim);
    requires Perm(&buf->dim[d], 1\2);
    requires Perm(buf->dim[d].min, 1\2) ** Perm(buf->dim[d].extent, 1\2);
@*/
/*@ pure @*/ int _halide_buffer_get_max_int32_t(struct halide_buffer_int32_t *buf, int d) {
    return buf->dim[d].min + buf->dim[d].extent - 1;
}

/*@ 
    requires buf != NULL ** Perm(buf, 1\2);
    requires Perm(buf->dim, 1\2) ** buf->dim != NULL;
    requires 0 <= d && d < \pointer_length(buf->dim);
    requires Perm(&buf->dim[d], 1\2);
    requires Perm(buf->dim[d].extent, 1\2);
@*/
/*@ pure @*/ int _halide_buffer_get_extent_int32_t(struct halide_buffer_int32_t *buf, int d) {
    return buf->dim[d].extent;
}

/*@ 
    requires buf != NULL ** Perm(buf, 1\2);
    requires Perm(buf->dim, 1\2) ** buf->dim != NULL;
    requires 0 <= d && d < \pointer_length(buf->dim);
    requires Perm(&buf->dim[d], 1\2);
    requires Perm(buf->dim[d].stride, 1\2);
@*/
/*@ pure @*/ int _halide_buffer_get_stride_int32_t(struct halide_buffer_int32_t *buf, int d) {
    return buf->dim[d].stride;
}
//@ pure int pure_inp(int x);
//@ pure int pure_count(int x);

/*@
 //decreases n;
 requires n >= 0;
 ensures |\result| == n;
 ensures (\forall int i; 0 <= i && i < n; \result[i] == pure_inp(x + 42*i));
pure seq<int> inp_to_row(int x, int n) = n == 0 ? [t: int ] : inp_to_row(x, n-1) + [pure_inp(x + 42*(n-1))];

 decreases |xs|;
 ensures |xs| > 0 ==> \result == (xs[0] > 0 ? 1 : 0) + count_row(xs.tail);
pure int count_row(seq<int> xs) = |xs| ==0 ? 0 : (xs[0] > 0 ? 1 : 0) + count_row(xs.tail);
@*/



#ifdef __cplusplus
extern "C" {
#endif

/*@
 context _inp_buffer != NULL ** Perm(_inp_buffer, 1\2);
 context Perm(_inp_buffer->dim, 1\2) ** _inp_buffer->dim != NULL;
 context \pointer_length(_inp_buffer->dim) == 2;
 context Perm(_inp_buffer->host, 1\2) ** _inp_buffer->host != NULL;
 context Perm(&_inp_buffer->dim[0], 1\2);
 context Perm(_inp_buffer->dim[0].min, 1\2) ** Perm(_inp_buffer->dim[0].stride, 1\2) ** Perm(_inp_buffer->dim[0].extent, 1\2);
 context Perm(&_inp_buffer->dim[1], 1\2);
 context Perm(_inp_buffer->dim[1].min, 1\2) ** Perm(_inp_buffer->dim[1].stride, 1\2) ** Perm(_inp_buffer->dim[1].extent, 1\2);
 context \pointer_length(_inp_buffer->host) == 1 + abs(_inp_buffer->dim[0].stride) * (_inp_buffer->dim[0].extent - 1) + abs(_inp_buffer->dim[1].stride) * (_inp_buffer->dim[1].extent - 1);
 context _count_buffer != NULL ** Perm(_count_buffer, 1\2);
 context Perm(_count_buffer->dim, 1\2) ** _count_buffer->dim != NULL;
 context \pointer_length(_count_buffer->dim) == 1;
 context Perm(_count_buffer->host, 1\2) ** _count_buffer->host != NULL;
 context Perm(&_count_buffer->dim[0], 1\2);
 context Perm(_count_buffer->dim[0].min, 1\2) ** Perm(_count_buffer->dim[0].stride, 1\2) ** Perm(_count_buffer->dim[0].extent, 1\2);
 context \pointer_length(_count_buffer->host) == 1 + abs(_count_buffer->dim[0].stride) * (_count_buffer->dim[0].extent - 1);
 context _count_buffer->host != _inp_buffer->host;
 context ((_inp_buffer->dim[0].min == 0) && (_inp_buffer->dim[0].extent == 42)) && (_inp_buffer->dim[0].stride == 1);
 context ((_inp_buffer->dim[1].min == 0) && (_inp_buffer->dim[1].extent == 10)) && (_inp_buffer->dim[1].stride == 42);
 context (\forall* int _0, int _1; (((0 <= _0) && (_0 < 42)) && (0 <= _1)) && (_1 < 10); Perm(&_inp_buffer->host[(_1*42) + _0], 1\2));
 context (\forall int _0, int _1; (((0 <= _0) && (_0 < 42)) && (0 <= _1)) && (_1 < 10); _inp_buffer->host[(_1*42) + _0] == pure_inp((_1*42) + _0));
 context ((_count_buffer->dim[0].min == 0) && (_count_buffer->dim[0].extent == 42)) && (_count_buffer->dim[0].stride == 1);
 context (\forall* int _0; (0 <= _0) && (_0 < 42); Perm(&_count_buffer->host[_0], 1\1));
 ensures (\forall int _0; (0 <= _0) && (_0 < 42); (0 <= _count_buffer->host[_0]) && (_count_buffer->host[_0] <= 10));
 ensures (\forall int _0; (0 <= _0) && (_0 < 42); (0 <= _count_buffer->host[_0]) && (_count_buffer->host[_0] <= 10));
 ensures (\forall int x; (0 <= x) && (x < 42); 
    _count_buffer->host[x] == count_row(inp_to_row(x, 10)));
@*/
int count(struct halide_buffer_int32_t *_inp_buffer, struct halide_buffer_int32_t *_count_buffer) {
 int32_t* _count = _halide_buffer_get_host_int32_t(_count_buffer);
 int32_t _count_min_0 = _halide_buffer_get_min_int32_t(_count_buffer, 0);
 int32_t _count_extent_0 = _halide_buffer_get_extent_int32_t(_count_buffer, 0);
 int32_t _count_stride_0 = _halide_buffer_get_stride_int32_t(_count_buffer, 0);
 int32_t* _inp = _halide_buffer_get_host_int32_t(_inp_buffer);
 int32_t _inp_min_0 = _halide_buffer_get_min_int32_t(_inp_buffer, 0);
 int32_t _inp_extent_0 = _halide_buffer_get_extent_int32_t(_inp_buffer, 0);
 int32_t _inp_stride_0 = _halide_buffer_get_stride_int32_t(_inp_buffer, 0);
 int32_t _inp_min_1 = _halide_buffer_get_min_int32_t(_inp_buffer, 1);
 int32_t _inp_extent_1 = _halide_buffer_get_extent_int32_t(_inp_buffer, 1);
 int32_t _inp_stride_1 = _halide_buffer_get_stride_int32_t(_inp_buffer, 1);
 halide_unused((_count_stride_0 == 1));
 halide_unused((_count_min_0 == 0));
 halide_unused((_count_extent_0 == 42));
 halide_unused((_inp_stride_0 == 1));
 halide_unused((_inp_min_0 == 0));
 halide_unused((_inp_extent_0 == 42));
 halide_unused((_inp_stride_1 == 42));
 halide_unused((_inp_min_1 == 0));
 halide_unused((_inp_extent_1 == 10));
 // produce count
 /*@
  loop_invariant 0 <= _count_s0_x && _count_s0_x <= 0 + 42;
  loop_invariant (\forall* int _count_s0_x_forall; (0 <= _count_s0_x_forall) && (_count_s0_x_forall < 42); Perm(&_count[_count_s0_x_forall], 1\1));
  loop_invariant (\forall int _count_s0_x_forall; (0 <= _count_s0_x_forall) && (_count_s0_x_forall < _count_s0_x); _count[_count_s0_x_forall] == 0);
 @*/
 for (int _count_s0_x = 0; _count_s0_x < 0 + 42; _count_s0_x++)
 {
  _count[_count_s0_x] = 0;
 } // for _count_s0_x
 /*@
  loop_invariant 0 <= _count_s1_x && _count_s1_x <= 0 + 42;
  loop_invariant (\forall* int _0, int _1; ((0 <= _0) && (_0 < 42)) && ((0 <= _1) && (_1 < 10)); Perm(&_inp[(_1*42) + _0], 1\2));
  loop_invariant (\forall int _0, int _1; (((0 <= _0) && (_0 < 42)) && (0 <= _1)) && (_1 < 10); _inp[(_1*42) + _0] == pure_inp((_1*42) + _0));
  loop_invariant (\forall* int _count_s1_x_forall; (0 <= _count_s1_x_forall) && (_count_s1_x_forall < 42); Perm(&_count[_count_s1_x_forall], 1\1));
  loop_invariant (\forall int _count_s1_x_forall; (_count_s1_x <= _count_s1_x_forall) && (_count_s1_x_forall < 42); (0 <= _count[_count_s1_x_forall]) && (_count[_count_s1_x_forall] <= 0));
  loop_invariant (\forall int _count_s1_x_forall; (0 <= _count_s1_x_forall) && (_count_s1_x_forall < _count_s1_x); (0 <= _count[_count_s1_x_forall]) && (_count[_count_s1_x_forall] <= 10));
  loop_invariant (\forall int _count_s1_x_forall; (0 <= _count_s1_x_forall) && (_count_s1_x_forall < _count_s1_x); 
    _count[_count_s1_x_forall] == count_row(inp_to_row(_count_s1_x_forall, 10)));
 @*/
 for (int _count_s1_x = 0; _count_s1_x < 0 + 42; _count_s1_x++)
 {
  /*@
   loop_invariant 0 <= _count_s1_r6__x && _count_s1_r6__x <= 0 + 10;
   loop_invariant (\forall* int _0, int _1; ((0 <= _0) && (_0 < 42)) && ((0 <= _1) && (_1 < 10)); Perm(&_inp[(_1*42) + _0], 1\2));
   loop_invariant (\forall int _0, int _1; (((0 <= _0) && (_0 < 42)) && (0 <= _1)) && (_1 < 10); _inp[(_1*42) + _0] == pure_inp((_1*42) + _0));
   loop_invariant Perm(&_count[_count_s1_x], 1\1);
   loop_invariant (0 <= _count[_count_s1_x]) && (_count[_count_s1_x] <= _count_s1_r6__x);
   loop_invariant _count[_count_s1_x] == count_row(inp_to_row(_count_s1_x, _count_s1_r6__x));
  @*/
  for (int _count_s1_r6__x = 0; _count_s1_r6__x < 0 + 10; _count_s1_r6__x++)
  {
   _count[_count_s1_x] = (_count[_count_s1_x] + ((0 < _inp[((_count_s1_r6__x * 42) + _count_s1_x)]) ? 1 : 0));
  } // for _count_s1_r6__x
 } // for _count_s1_x
 return 0;
}

#ifdef __cplusplus
}  // extern "C"
#endif

