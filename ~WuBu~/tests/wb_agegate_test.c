/*
 * wb_agegate_test.c -- LP-54 stone 1 gate: age gating.
 *
 * Devil's-advocate cases: the effective age is the LATER of the tier's age and
 * the highest descriptor floor, never the sum (violence 13 + language 10 is 13,
 * not 23); a tier that already implies a descriptor is not pushed past the tier
 * above it by listing that descriptor again; access is granted AT the boundary
 * age and refused one year below; an unknown descriptor bit is ignored.
 */
#include "wb_agegate.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

/* The US ladder: ESRB-ish. */
static const wb_agegate_descriptor us_descs[] = {
    { WB_AGEGATE_VIOLENCE, "violence", 13 },
    { WB_AGEGATE_LANGUAGE, "language", 10 },
    { WB_AGEGATE_GAMBLING, "gambling", 16 },
    { WB_AGEGATE_HORROR,   "horror",   11 },
};
static const wb_agegate_level us_tiers[] = {
    { "Everyone", 0,  0 },
    { "E10+",      10, 0 },
    { "Teen",      13, WB_AGEGATE_LANGUAGE },
    { "Mature",    17, WB_AGEGATE_VIOLENCE },
    { "Adults",    18, WB_AGEGATE_VIOLENCE | WB_AGEGATE_LANGUAGE },
};
static const wb_agegate_ladder us = { "US", us_tiers, 5, us_descs, 4 };

/* A stricter ladder (PEGI-ish): gambling floors at 18. */
static const wb_agegate_descriptor eu_descs[] = {
    { WB_AGEGATE_VIOLENCE, "violence", 12 },
    { WB_AGEGATE_LANGUAGE, "language", 12 },
    { WB_AGEGATE_GAMBLING, "gambling", 18 },
};
static const wb_agegate_level eu_tiers[] = {
    { "PEGI 3",  3,  0 },
    { "PEGI 7",  7,  0 },
    { "PEGI 12", 12, 0 },
    { "PEGI 16", 16, 0 },
    { "PEGI 18", 18, 0 },
};
static const wb_agegate_ladder eu = { "EU", eu_tiers, 5, eu_descs, 3 };

int main(void) {
    /* ---- tiers ---- */
    CHECK(wb_agegate_tier(&us, 0) != NULL && strcmp(wb_agegate_tier(&us, 0)->name, "Everyone") == 0,
          "tier 0 is Everyone");
    CHECK(wb_agegate_tier(&us, 4) != NULL && strcmp(wb_agegate_tier(&us, 4)->name, "Adults") == 0,
          "tier 4 is Adults");
    CHECK(wb_agegate_tier(&us, 5) == NULL, "an out-of-range rating has no tier");
    CHECK(wb_agegate_tier(NULL, 0) == NULL, "a NULL ladder has no tiers");

    /* ---- the base ages ---- */
    wb_agegate_entry e = { 0, 0 };
    CHECK(wb_agegate_min_age(&us, &e) == 0, "Everyone floors at 0");
    e.rating = 1;
    CHECK(wb_agegate_min_age(&us, &e) == 10, "E10+ floors at 10");
    e.rating = 3;
    CHECK(wb_agegate_min_age(&us, &e) == 17, "Mature floors at 17");
    e.rating = 4;
    CHECK(wb_agegate_min_age(&us, &e) == 18, "Adults floors at 18");

    /* ---- a descriptor raises the floor to its own age ---- */
    e.rating = 1; e.flags = WB_AGEGATE_VIOLENCE;
    CHECK(wb_agegate_min_age(&us, &e) == 13, "violence lifts E10+ from 10 to 13");
    e.flags = WB_AGEGATE_GAMBLING;
    CHECK(wb_agegate_min_age(&us, &e) == 16, "gambling lifts E10+ from 10 to 16");
    e.flags = WB_AGEGATE_LANGUAGE;
    CHECK(wb_agegate_min_age(&us, &e) == 10, "language floors at 10, so E10+ stays 10");
    e.flags = WB_AGEGATE_HORROR;
    CHECK(wb_agegate_min_age(&us, &e) == 11, "horror lifts E10+ from 10 to 11");

    /* ---- two descriptors do not compound: the later floor wins, not the sum ---- */
    e.rating = 1; e.flags = WB_AGEGATE_VIOLENCE | WB_AGEGATE_LANGUAGE;
    CHECK(wb_agegate_min_age(&us, &e) == 13, "violence 13 + language 10 is 13, not 23");
    e.flags = WB_AGEGATE_VIOLENCE | WB_AGEGATE_GAMBLING;
    CHECK(wb_agegate_min_age(&us, &e) == 16, "violence 13 + gambling 16 is 16");
    e.flags = WB_AGEGATE_VIOLENCE | WB_AGEGATE_LANGUAGE | WB_AGEGATE_HORROR;
    CHECK(wb_agegate_min_age(&us, &e) == 13, "three descriptors still take the later floor");

    /* ---- a tier that already implies a descriptor is not pushed past it ---- */
    e.rating = 3; e.flags = 0;
    CHECK(wb_agegate_min_age(&us, &e) == 17, "Mature stays 17");
    e.flags = WB_AGEGATE_VIOLENCE;                       /* Mature already implies it */
    CHECK(wb_agegate_min_age(&us, &e) == 17, "listing an implied descriptor does not raise the tier");
    e.rating = 4; e.flags = WB_AGEGATE_VIOLENCE | WB_AGEGATE_LANGUAGE;
    CHECK(wb_agegate_min_age(&us, &e) == 18, "Adults stays 18 with both implied descriptors");

    /* ---- an unknown descriptor bit is ignored ---- */
    e.rating = 1; e.flags = 1u << 31;
    CHECK(wb_agegate_min_age(&us, &e) == 10, "an unknown descriptor bit is ignored");

    /* ---- the boundary: allowed AT the age, refused one year below ---- */
    e.rating = 1; e.flags = 0;
    CHECK(wb_agegate_allowed(&us, &e, 10) == 1, "access is granted at the boundary age");
    CHECK(wb_agegate_allowed(&us, &e, 9) == 0, "access is refused one year below");
    CHECK(wb_agegate_allowed(&us, &e, 0) == 0, "access is refused well below");
    e.flags = WB_AGEGATE_GAMBLING;
    CHECK(wb_agegate_allowed(&us, &e, 16) == 1, "a gambling title is granted at 16");
    CHECK(wb_agegate_allowed(&us, &e, 15) == 0, "a gambling title is refused at 15");

    /* ---- an out-of-range rating is refused at any age ---- */
    e.rating = 9; e.flags = 0;
    CHECK(wb_agegate_min_age(&us, &e) == 0, "an out-of-range rating floors at 0");
    CHECK(wb_agegate_allowed(&us, &e, 99) == 0, "an out-of-range rating is refused even at 99");

    /* ---- the same game is a different age in a different region ---- */
    wb_agegate_entry g = { 2, WB_AGEGATE_VIOLENCE };       /* PEGI 12 + violence */
    CHECK(wb_agegate_min_age(&us, &g) == 13, "the US floors a teen title with violence at 13");
    CHECK(wb_agegate_min_age(&eu, &g) == 12, "the EU floors the same title at 12");
    wb_agegate_entry h = { 2, WB_AGEGATE_GAMBLING };
    CHECK(wb_agegate_min_age(&us, &h) == 16, "the US floors gambling at 16");
    CHECK(wb_agegate_min_age(&eu, &h) == 18, "the EU floors gambling at 18");

    /* ---- the descriptor list ---- */
    const char *names[WB_AGEGATE_MAX_DESCRIPTORS];
    e.rating = 1; e.flags = WB_AGEGATE_VIOLENCE | WB_AGEGATE_HORROR;
    uint32_t nd = wb_agegate_descriptors(&us, &e, names, WB_AGEGATE_MAX_DESCRIPTORS);
    CHECK(nd == 2, "two descriptors are listed");
    CHECK(strcmp(names[0], "violence") == 0 && strcmp(names[1], "horror") == 0,
          "the descriptor codes are listed in table order");
    e.flags = 0;
    CHECK(wb_agegate_descriptors(&us, &e, names, WB_AGEGATE_MAX_DESCRIPTORS) == 0, "no descriptors lists none");
    CHECK(wb_agegate_descriptors(&us, &e, names, 1) == 0, "a one-slot buffer lists none for an empty set");
    e.flags = WB_AGEGATE_VIOLENCE | WB_AGEGATE_HORROR | WB_AGEGATE_GAMBLING | WB_AGEGATE_LANGUAGE;
    CHECK(wb_agegate_descriptors(&us, &e, names, 2) == 2, "a small buffer truncates the descriptor list");

    /* ---- determinism ---- */
    wb_agegate_entry a = { 2, WB_AGEGATE_VIOLENCE | WB_AGEGATE_LANGUAGE };
    wb_agegate_entry b = { 2, WB_AGEGATE_VIOLENCE | WB_AGEGATE_LANGUAGE };
    CHECK(wb_agegate_min_age(&us, &a) == wb_agegate_min_age(&us, &b), "the floor is deterministic");
    CHECK(wb_agegate_allowed(&us, &a, 12) == wb_agegate_allowed(&us, &b, 12), "the decision is deterministic");
    CHECK(wb_agegate_descriptors(&us, &a, names, 8) == wb_agegate_descriptors(&us, &b, names, 8),
          "the descriptor list is deterministic");

    /* ---- degenerate input ---- */
    CHECK(wb_agegate_min_age(NULL, &a) == 0, "a NULL ladder floors at 0");
    CHECK(wb_agegate_min_age(&us, NULL) == 0, "a NULL entry floors at 0");
    CHECK(wb_agegate_allowed(NULL, &a, 30) == 0, "a NULL ladder refuses");
    CHECK(wb_agegate_allowed(&us, NULL, 30) == 0, "a NULL entry refuses");
    CHECK(wb_agegate_descriptors(NULL, &a, names, 8) == 0, "a NULL ladder lists nothing");
    CHECK(wb_agegate_descriptors(&us, &a, NULL, 8) == 0, "a NULL output is refused");
    CHECK(wb_agegate_descriptors(&us, &a, names, 0) == 0, "a zero capacity is refused");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
