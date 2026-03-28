/* EXPECTED: 0 */
/* Test: designated initializers in all contexts */

struct point {
    int x;
    int y;
};

struct xyz {
    int x;
    int y;
    int z;
};

struct outer {
    struct point inner;
    int val;
};

union val {
    int i;
    long l;
};

/* Pattern 1: global struct with designated init */
static struct point g_pt = { .x = 10, .y = 20 };

/* Pattern 3: global array with designated init */
int g_arr[5] = { [2] = 42, [4] = 99 };

/* Pattern 5: nested struct */
static struct outer g_outer = {
    .inner = { .x = 100, .y = 200 }, .val = 3
};

/* Pattern 6: array of structs (positional with nested braces) */
static struct point g_pts[2] = { { 1, 2 }, { 3, 4 } };

/* Pattern 7: union */
static union val g_uval = { .i = 77 };

/* Pattern 9: mixed positional + designated */
static struct xyz g_mix = { 1, 2, .z = 3 };

/* Pattern 10: out-of-order */
static struct point g_ooo = { .y = 55, .x = 33 };

int main(void) {
    /* Check pattern 1: global struct desig */
    if (g_pt.x != 10) return 1;
    if (g_pt.y != 20) return 2;

    /* Check pattern 3: global array desig */
    if (g_arr[0] != 0) return 3;
    if (g_arr[2] != 42) return 4;
    if (g_arr[4] != 99) return 5;

    /* Check pattern 5: nested struct */
    if (g_outer.inner.x != 100) return 6;
    if (g_outer.inner.y != 200) return 7;
    if (g_outer.val != 3) return 8;

    /* Check pattern 6: array of structs */
    if (g_pts[0].x != 1) return 60;
    if (g_pts[0].y != 2) return 61;
    if (g_pts[1].x != 3) return 62;
    if (g_pts[1].y != 4) return 63;

    /* Check pattern 7: union */
    if (g_uval.i != 77) return 9;

    /* Check pattern 9: mixed positional + designated */
    if (g_mix.x != 1) return 12;
    if (g_mix.y != 2) return 13;
    if (g_mix.z != 3) return 130;

    /* Check pattern 10: out-of-order */
    if (g_ooo.x != 33) return 10;
    if (g_ooo.y != 55) return 11;

    /* Pattern 2: local struct desig */
    {
        struct point lp = { .x = 7, .y = 8 };
        if (lp.x != 7) return 14;
        if (lp.y != 8) return 15;
    }

    /* Pattern 4: local array desig */
    {
        int la[4] = { [0] = 11, [3] = 44 };
        if (la[0] != 11) return 16;
        if (la[3] != 44) return 17;
    }

    /* Local out-of-order */
    {
        struct point lp2 = { .y = 99, .x = 88 };
        if (lp2.x != 88) return 18;
        if (lp2.y != 99) return 19;
    }

    /* Local nested struct */
    {
        struct outer lo = { .inner = { .x = 5, .y = 6 }, .val = 9 };
        if (lo.inner.x != 5) return 20;
        if (lo.inner.y != 6) return 21;
        if (lo.val != 9) return 22;
    }

    return 0;
}
