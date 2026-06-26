#include <assert.h>
#include <stdio.h>

#include "compact_runtime_storage.h"
#include "cl_iter_table.h"


int main(void) {
  compact_runtime_storage_t storage;
  cl_iter_t first, second, third;

  cl_iter_table_init();
  crs_init(&storage);
  cl_iter_frame_begin();

  crs_reset_build(&storage);
  first = crs_alloc_node(&storage, R05_DATATAG_NUMBER);
  cl_iter_set_number(first, 10);
  second = crs_alloc_node(&storage, R05_DATATAG_NUMBER);
  cl_iter_set_number(second, 20);
  third = crs_alloc_node(&storage, R05_DATATAG_NUMBER);
  cl_iter_set_number(third, 30);
  {
    cl_iter_t insert_pos = crs_insert_pos(&storage);
    assert(cl_iter_eq(insert_pos, crs_insert_pos(&storage)));
    cl_iter_t filled = crs_alloc_node(&storage, R05_DATATAG_NUMBER);
    assert(cl_iter_eq(insert_pos, filled));
    cl_iter_set_number(filled, 40);
  }
  crs_splice_from_build(&storage, crs_view_end(&storage));

  assert(cl_iter_number(crs_view_begin(&storage)) == 10);
  assert(cl_iter_number(cl_iter_next(crs_view_begin(&storage))) == 20);
  assert(cl_iter_number(cl_iter_prev(crs_view_end(&storage))) == 40);

  crs_splice_before(first, third, third);
  assert(cl_iter_number(crs_view_begin(&storage)) == 30);
  assert(cl_iter_number(cl_iter_next(crs_view_begin(&storage))) == 10);

  crs_remove_range(first, second);
  assert(cl_iter_number(crs_view_begin(&storage)) == 30);
  assert(cl_iter_number(cl_iter_next(crs_view_begin(&storage))) == 40);
  assert(cl_iter_eq(
    cl_iter_next(cl_iter_next(crs_view_begin(&storage))),
    crs_view_end(&storage)
  ));

  crs_reset_build(&storage);
  first = crs_alloc_node(&storage, R05_DATATAG_NUMBER);
  cl_iter_set_number(first, 50);
  crs_insert_pos(&storage);
  crs_splice_from_build(&storage, crs_view_end(&storage));
  assert(cl_iter_number(cl_iter_prev(crs_view_end(&storage))) == 50);

  cl_iter_frame_end();
  crs_destroy(&storage);
  assert(cl_iter_table_live_slots() == 0);
  cl_iter_table_free();

  puts("compact_runtime_storage: OK");
  return 0;
}
