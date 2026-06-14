/*
 * Host unit tests for the style preset table.
 */
#include <stdio.h>

#include "styles.h"
#include "scales.h"

extern int g_run, g_fail;
#define CHECK(cond) do { \
    g_run++; \
    if (!(cond)) { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

void run_styles_tests(void);

void run_styles_tests(void) {
  printf("test_styles_table\n");
  CHECK(STYLE_COUNT >= 1);

  for (uint8_t i = 0; i < STYLE_COUNT; i++) {
    const Style *s = &styles[i];
    CHECK(s->name != 0);
    CHECK(s->scale < SCALE_COUNT);
    CHECK(s->root < 12);
    CHECK(s->length >= 2 && s->length <= 16);
    CHECK(s->time_range >= 1 && s->time_range <= 4);
    CHECK(s->time_level <= 4095);
    /* degrees should stay within a sane range so root+degree maps on-scale */
    for (uint8_t k = 0; k < s->length; k++) {
      CHECK(s->degree[k] >= -12 && s->degree[k] <= 60);
    }
  }
}
