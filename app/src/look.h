/* PlexusX — Look: the colour-engine parameter block.
 *
 * Deliberately free of <windows.h> so that the colour math in color_math.h
 * (and therefore the unit tests in tests/test_all.c) can be compiled on any
 * host.  common.h includes this header; nothing else defines `Look`.
 */
#ifndef PLEXUSX_LOOK_H
#define PLEXUSX_LOOK_H

typedef struct Look {
    int   enabled;         /* 1 = active, 0 = bypassed/neutral */
    float sat;             /* 0..300 (%)   100 = neutral, 300 = max application boost */
    float vibrance;        /* 0..300 (%)   100 = neutral, smart saturation */
    float bri;             /* 0..200 (%)   100 = neutral (0.0 to 2.0x) */
    float con;             /* 0..200 (%)   100 = neutral (0.0 to 2.0x) */
    float gamma;           /* 0.40..2.50   1.00 = neutral gamma curve */
    float temp;            /* 3000..10000  (Kelvin) 6500 = standard D65 neutral */
    float tint;            /* -100..100    (%) negative = green, positive = magenta */
    float r_gain;          /* 0..200 (%)   100 = neutral */
    float g_gain;          /* 0..200 (%)   100 = neutral */
    float b_gain;          /* 0..200 (%)   100 = neutral */
    float shadows;         /* 0..200 (%)   100 = neutral (toe shadow lift / crush) */
    float highlights;      /* 0..200 (%)   100 = neutral (shoulder compression / boost) */
    float black_level;     /* 0..200 (%)   100 = neutral (floor level) */
    float white_point;     /* 0..200 (%)   100 = neutral (ceiling level) */
    float clarity;         /* 0..200 (%)   100 = neutral (midtone S-curve dehaze) */
    float hue;             /* -180..180    (deg) 0 = neutral */
} Look;

/* A look in which every stage is a no-op (engine enabled, nothing adjusted).
 * Usable as an initializer:  Look l = LOOK_NEUTRAL_INIT;  or  (Look)LOOK_NEUTRAL_INIT */
#define LOOK_NEUTRAL_INIT { 1, 100.0f, 100.0f, 100.0f, 100.0f, 1.00f, 6500.0f, 0.0f, \
                            100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 0.0f }

#endif /* PLEXUSX_LOOK_H */
