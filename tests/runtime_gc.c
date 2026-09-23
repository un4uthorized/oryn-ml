#include "oryn/runtime.h"

#include <assert.h>

int main(void) {
  ov_gc_init();
  OValue values[] = {ov_string("one"), ov_string("two")};
  OValue root = ov_array(2, values);
  ov_gc_push(&root);
  ov_gc_collect();
  assert(ov_gc_live_objects() == 4);
  assert(ov_gc_live_bytes() > 0);
  ov_gc_pop(1);
  ov_gc_collect();
  assert(ov_gc_live_objects() == 0);
  assert(ov_gc_live_bytes() == 0);
  assert(ov_gc_collection_count() == 2);
  ov_gc_shutdown();
  return 0;
}
