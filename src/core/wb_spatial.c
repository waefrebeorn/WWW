#include "wb_spatial.h"
#include <math.h>

/* MinGW/hidden-M_PI portability: define it unconditionally. */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* --- key packing ---
 * bits [63..60] level (4 bits, 0..15)
 * bits [59..0]  morton(ty,tx), 30 bits each (levels up to 15 -> 30-bit axis).
 * We interleave ty and tx bottom-up: morton = sum over bit b of (ty_b<<2b|tx_b<<2b+1).
 */
#define WB_LEVEL_SHIFT 60u

/* --- morton helpers (30-bit axes) --- */

/* spread lower 'bits' of x into even/odd positions (standard trick) */
static inline uint64_t part1by1_30(uint32_t x) {
    uint64_t z = x & 0x3FFFFFFFULL;
    z = (z | (z << 16)) & 0x0000FFFF0000FFFFULL;
    z = (z | (z << 8))  & 0x00FF00FF00FF00FFULL;
    z = (z | (z << 4))  & 0x0F0F0F0F0F0F0F0FULL;
    z = (z | (z << 2))  & 0x3333333333333333ULL;
    z = (z | (z << 1))  & 0x5555555555555555ULL;
    return z;
}
static inline uint32_t compact1(uint64_t z) {
    z &= 0x5555555555555555ULL;
    z = (z | (z >> 1))  & 0x3333333333333333ULL;
    z = (z | (z >> 2))  & 0x0F0F0F0F0F0F0F0FULL;
    z = (z | (z >> 4))  & 0x00FF00FF00FF00FFULL;
    z = (z | (z >> 8))  & 0x0000FFFF0000FFFFULL;
    z = (z | (z >> 16)) & 0x00000000FFFFFFFFULL;
    return (uint32_t)z;
}

wb_qkey wb_qkey_make(uint32_t level, uint32_t tx, uint32_t ty) {
    uint64_t mort = part1by1_30(ty) | (part1by1_30(tx) << 1);
    return ((uint64_t)(level & 0xF) << WB_LEVEL_SHIFT) | mort;
}
uint32_t wb_qkey_level(wb_qkey k) { return (uint32_t)(k >> WB_LEVEL_SHIFT); }
/* mort layout: even bits = ty, odd bits = tx (see wb_qkey_make). compact1
 * extracts even-positioned bits; tx lives in odd positions -> shift first. */
uint32_t wb_qkey_tx(wb_qkey k)    { return compact1((k & 0x0FFFFFFFFFFFFFFFULL) >> 1); }
uint32_t wb_qkey_ty(wb_qkey k)    { return compact1(k & 0x0FFFFFFFFFFFFFFFULL); }

wb_qkey wb_qkey_parent(wb_qkey k) {
    uint32_t lv = wb_qkey_level(k);
    if (lv == 0) return 0;
    uint32_t tx = wb_qkey_tx(k) >> 1, ty = wb_qkey_ty(k) >> 1;
    return wb_qkey_make(lv - 1, tx, ty);
}

wb_qkey wb_qkey_child(wb_qkey k, uint32_t cx, uint32_t cy) {
    uint32_t lv = wb_qkey_level(k);
    if (lv >= WB_MAX_LEVEL) return 0;
    uint32_t tx = (wb_qkey_tx(k) << 1) | (cx & 1);
    uint32_t ty = (wb_qkey_ty(k) << 1) | (cy & 1);
    return wb_qkey_make(lv + 1, tx, ty);
}

/* --- Web Mercator conversion to fractional cell coordinates --- */
double wb_spatial_lon_to_x(double lon, uint32_t level) {
    double n = (double)(1u << level);
    return (lon + 180.0) / 360.0 * n;
}
double wb_spatial_lat_to_y(double lat, uint32_t level) {
    double n = (double)(1u << level);
    double latr = lat * (M_PI / 180.0);
    double ys = (1.0 - log(tan(latr) + 1.0 / cos(latr)) / M_PI) / 2.0;
    if (ys < 0.0) ys = 0.0; else if (ys > 1.0) ys = 1.0;
    return ys * n;
}
double wb_spatial_x_to_lon(double tx, uint32_t level) {
    double n = (double)(1u << level);
    return tx / n * 360.0 - 180.0;
}
double wb_spatial_y_to_lat(double ty, uint32_t level) {
    double n = (double)(1u << level);
    /* exact inverse of ys=(1-ln(tan+sec)/pi)/2  with n in [0,1]:
     *   ln(tan+sec) = pi*(1-2n)  =>  tan = sinh(pi*(1-2n))
     *   lat = atan(sinh(pi*(1 - 2*(ty/n))))                            */
    double latr = atan(sinh(M_PI * (1.0 - 2.0 * (ty / n))));
    return latr * (180.0 / M_PI);
}

wb_qkey wb_qkey_from_lonlat(double lon, double lat, uint32_t level) {
    if (level > WB_MAX_LEVEL) level = WB_MAX_LEVEL;
    double fx = wb_spatial_lon_to_x(lon, level);
    double fy = wb_spatial_lat_to_y(lat, level);
    if (fx < 0.0) fx = 0.0; else if (fx >= (double)(1u<<level)) fx = (double)(1u<<level)-1.0;
    if (fy < 0.0) fy = 0.0; else if (fy >= (double)(1u<<level)) fy = (double)(1u<<level)-1.0;
    return wb_qkey_make(level, (uint32_t)fx, (uint32_t)fy);
}

void wb_qkey_bounds(wb_qkey k, double *lon0, double *lat0, double *lon1, double *lat1) {
    uint32_t lv = wb_qkey_level(k);
    uint32_t tx = wb_qkey_tx(k), ty = wb_qkey_ty(k);
    *lon0 = wb_spatial_x_to_lon((double)tx, lv);
    *lon1 = wb_spatial_x_to_lon((double)(tx + 1), lv);
    /* tile Y grows SOUTH, so ty+1 edge is the lower latitude and ty is north.
     * Emit natural geographic order: lat0 = south (min), lat1 = north (max). */
    *lat0 = wb_spatial_y_to_lat((double)(ty + 1), lv);
    *lat1 = wb_spatial_y_to_lat((double)ty, lv);
}

/* --- neighbors at same level --- */
static uint32_t tile_side(uint32_t level) { return 1u << level; }

wb_qkey wb_qkey_east(wb_qkey k) {
    uint32_t lv = wb_qkey_level(k); uint32_t s = tile_side(lv);
    uint32_t tx = wb_qkey_tx(k);
    if (tx + 1 >= s) return 0;
    return wb_qkey_make(lv, tx + 1, wb_qkey_ty(k));
}
wb_qkey wb_qkey_west(wb_qkey k) {
    uint32_t lv = wb_qkey_level(k); uint32_t tx = wb_qkey_tx(k);
    if (tx == 0) return 0;
    return wb_qkey_make(lv, tx - 1, wb_qkey_ty(k));
}
wb_qkey wb_qkey_north(wb_qkey k) { /* north = 증가 y */
    uint32_t lv = wb_qkey_level(k); uint32_t s = tile_side(lv);
    uint32_t ty = wb_qkey_ty(k);
    if (ty + 1 >= s) return 0;
    return wb_qkey_make(lv, wb_qkey_tx(k), ty + 1);
}
wb_qkey wb_qkey_south(wb_qkey k) {
    uint32_t lv = wb_qkey_level(k); uint32_t ty = wb_qkey_ty(k);
    if (ty == 0) return 0;
    return wb_qkey_make(lv, wb_qkey_tx(k), ty - 1);
}
