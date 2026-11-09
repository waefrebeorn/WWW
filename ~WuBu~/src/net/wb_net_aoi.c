/*
 * wb_net_aoi.c -- interest management on the engine's quadkey spatial core.
 * See wb_net.h.
 */
#include "wb_net.h"
#include "wb_spatial.h"

#define WB_NET_EARTH_CIRCUMFERENCE 40075016.686

int wb_net_aoi_level(float radius_m) {
    if (radius_m <= 0.0f) return 0;
    int level = 0;
    double cell = WB_NET_EARTH_CIRCUMFERENCE;
    while (cell > (double)radius_m && level < (int)WB_MAX_LEVEL) {
        cell *= 0.5;
        level++;
    }
    return level;
}

int wb_net_aoi_visible(const double origin_lonlat[2], const double pos_lonlat[2],
                       float radius_m) {
    int level = wb_net_aoi_level(radius_m);
    wb_qkey a = wb_qkey_from_lonlat(origin_lonlat[0], origin_lonlat[1], (uint32_t)level);
    wb_qkey b = wb_qkey_from_lonlat(pos_lonlat[0], pos_lonlat[1], (uint32_t)level);
    int dtx = (int)wb_qkey_tx(a) - (int)wb_qkey_tx(b);
    int dty = (int)wb_qkey_ty(a) - (int)wb_qkey_ty(b);
    if (dtx < 0) dtx = -dtx;
    if (dty < 0) dty = -dty;
    /* same cell or an immediately adjacent one */
    return dtx <= 1 && dty <= 1;
}
