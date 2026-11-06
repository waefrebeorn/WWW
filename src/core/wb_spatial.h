/*
 * wb_spatial.h -- Planet-scale spatial keying (quadkeys) for WuBu Engine.
 *
 * The foundation of the 1:1-Earth metaverse. Everything spatial hangs off
 * this: geodata ingestion, ambient culling, chunk streaming, experience
 * spawning (arenas are just quadkey-addressed cells), and agentic link.
 *
 * Scheme: Web-Mercator quadkeys, morton-encoded into ONE uint64.
 *   - level 0 = whole Earth (1 cell). Each level subdivides by 2 on each axis,
 *     so level N has 2^N x 2^N cells over the [-180..180, merc(y)..] range.
 *   - key layout (MSB first, 4 bits per level; 16 levels -> 3.3e9 cells/axis,
 *     ~39mm resolution at the equator -- finer than needed for tile streaming):
 *       bits [63..60] = level
 *       bits [59..0]  = morton(ty, tx) over up to 15 levels (30 bits each)
 *   - 2-bit cell selector: 0=x0,y0  1=x1,y0  2=x0,y1  3=x1,y1
 *
 * Web Mercator is chosen because real geodata tiles (Google/Bing/Mapbox,
 * OpenStreetMap raster, common DEM pyramid tiles) are already split exactly
 * this way -- so "1:1 Earth with real geo data overlaid" is a direct fit, not
 * a re-engineering.
 */
#ifndef WB_SPATIAL_H
#define WB_SPATIAL_H

#include <stdint.h>
#include <stddef.h>

#define WB_MAX_LEVEL 15u
#define WB_MAX_TILE   (1u << WB_MAX_LEVEL)   /* 32768 cells per axis at max level */

typedef uint64_t wb_qkey;

/* Build / inspect a key. tx,ty are cell indices in [0, 2^level). */
wb_qkey wb_qkey_make(uint32_t level, uint32_t tx, uint32_t ty);
uint32_t wb_qkey_level(wb_qkey k);
uint32_t wb_qkey_tx(wb_qkey k);
uint32_t wb_qkey_ty(wb_qkey k);

/* Return the parent cell (level-1) and a child selector (0..3) of a cell. */
wb_qkey wb_qkey_parent(wb_qkey k);

/* Return child at (child_x, child_y) in {0,1}^2 of this cell at level+1. */
wb_qkey wb_qkey_child(wb_qkey k, uint32_t cx, uint32_t cy);

/* Geometric conversion (Web Mercator, standard spheroid R = 6378137 m). */
double wb_spatial_lon_to_x(double lon, uint32_t level);   /* cell x (float, may be fractional) */
double wb_spatial_lat_to_y(double lat, uint32_t level);   /* cell y (float, may be fractional) */
double wb_spatial_x_to_lon(double tx, uint32_t level);
double wb_spatial_y_to_lat(double ty, uint32_t level);

/* Quantize a geographic point to the covering cell at a level. */
wb_qkey wb_qkey_from_lonlat(double lon, double lat, uint32_t level);

/* Geographic bounds of a cell, in degrees / merc-x / merc-y. Preserve
 * fractional cell space so callers can map to world units directly. */
void wb_qkey_bounds(wb_qkey k, double *lon0, double *lat0, double *lon1, double *lat1);

/* East/West neighbors at the same level (null result if out of range,
 * returning 0 because key 0 is level 0). */
wb_qkey wb_qkey_east(wb_qkey k);
wb_qkey wb_qkey_west(wb_qkey k);
wb_qkey wb_qkey_north(wb_qkey k);
wb_qkey wb_qkey_south(wb_qkey k);

#endif /* WB_SPATIAL_H */
