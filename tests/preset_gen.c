/* PlexusX — native preset seed generator (host tool, not shipped).
 *
 * Prints the built-in preset seed (app/src/presets/preset_store.h,
 * PxPre_SeedBuiltins) as the same JSON the app exports, so
 * scripts/preset_parity.js can prove the website's preset definitions
 * (site/assets/presets.js) are field-for-field identical to the native ones.
 *
 * CI usage:
 *   gcc -std=gnu11 -Wall -Wextra -Werror -O2 -Iapp/src \
 *       tests/preset_gen.c app/src/games/game_profile.c -o /tmp/preset_gen -lm
 *   /tmp/preset_gen > /tmp/preset_seed.json
 *   node scripts/preset_parity.js --golden /tmp/preset_seed.json
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../app/src/presets/preset_store.h"

int main(void)
{
    static PxPresetLib lib;
    /* PxPre_ToJson writes at most a few tens of KB for 20 presets; 512 KB is
     * far beyond any possible seed and keeps the buffer static (no malloc
     * failure path to test). */
    static char buf[512 * 1024];

    memset(&lib, 0, sizeof lib);
    PxPre_SeedBuiltins(&lib);
    if (PxPre_ToJson(&lib, buf, sizeof buf) < 0) {
        fprintf(stderr, "preset_gen: serialisation failed\n");
        return 1;
    }
    fputs(buf, stdout);
    return 0;
}
