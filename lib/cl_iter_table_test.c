#include <assert.h>
#include <stdio.h>

#include "cl_iter_table.h"


static cm_item_t number_item(r05_number number) {
  cm_item_t item = { 0 };
  item.tag = R05_DATATAG_NUMBER;
  item.info.number = number;
  return item;
}


static r05_number handle_number(cl_handle_t handle) {
  cm_item_t *item = cm_iter_get(cl_iter_deref(handle));
  assert(item != NULL);
  assert(item->tag == R05_DATATAG_NUMBER);
  return item->info.number;
}


int main(void) {
  cm_list_t list;
  cm_iter_t first, second, third, inserted, split_pos;
  cl_handle_t h_first, h_second, h_third, h_inserted, h_reused, h_temporary;
  int i;

  cl_iter_table_init();
  assert(cl_iter_table_slot_size() <= 24);
  list = cm_list_create();

  cm_push_back(&list, number_item(10));
  cm_push_back(&list, number_item(20));
  cm_push_back(&list, number_item(30));

  first = cm_list_begin(&list);
  second = cm_iter_next(first);
  third = cm_iter_next(second);
  cl_iter_frame_begin();
  h_first = cl_iter_alloc(first.macro, first.index);
  h_second = cl_iter_alloc(second.macro, second.index);
  h_third = cl_iter_alloc(third.macro, third.index);
  assert(cl_iter_alloc(first.macro, first.index).handle == h_first.handle);
  assert(cl_iter_number(h_first) == 10);
  assert(cl_iter_eq(cl_iter_next(h_first), h_second));
  assert(cl_iter_eq(cl_iter_prev(h_third), h_second));

  inserted = cm_insert_before(second, number_item(15));
  h_inserted = cl_iter_alloc(inserted.macro, inserted.index);
  assert(handle_number(h_first) == 10);
  assert(handle_number(h_inserted) == 15);
  assert(handle_number(h_second) == 20);
  assert(handle_number(h_third) == 30);

  cm_remove(cl_iter_deref(h_inserted));
  assert(! cl_iter_handle_valid(h_inserted));
  assert(handle_number(h_first) == 10);
  assert(handle_number(h_second) == 20);
  assert(handle_number(h_third) == 30);

  inserted = cm_insert_before(cl_iter_deref(h_second), number_item(17));
  h_reused = cl_iter_alloc(inserted.macro, inserted.index);
  assert(h_reused.handle != h_inserted.handle);
  assert(handle_number(h_reused) == 17);
  assert(! cl_iter_handle_valid(h_inserted));
  cm_remove(cl_iter_deref(h_reused));
  assert(! cl_iter_handle_valid(h_reused));

  split_pos = cl_iter_deref(h_second);
  cm_split(&split_pos);
  assert(handle_number(h_first) == 10);
  assert(handle_number(h_second) == 20);
  assert(handle_number(h_third) == 30);

  cm_merge(split_pos.macro->prev_macro, split_pos.macro);
  assert(handle_number(h_first) == 10);
  assert(handle_number(h_second) == 20);
  assert(handle_number(h_third) == 30);

  for (i = 0; i < 100; ++i) {
    cm_push_back(&list, number_item((r05_number) (100 + i)));
  }
  assert(handle_number(h_first) == 10);
  assert(handle_number(h_second) == 20);
  assert(handle_number(h_third) == 30);

  split_pos = cm_iter_prev(cm_list_end(&list));
  h_temporary = cl_iter_alloc(split_pos.macro, split_pos.index);
  cl_iter_escape(h_first);
  cl_iter_escape(h_second);
  cl_iter_escape(h_third);
  cl_iter_frame_end();
  assert(cl_iter_handle_valid(h_first));
  assert(cl_iter_handle_valid(h_second));
  assert(cl_iter_handle_valid(h_third));
  assert(! cl_iter_handle_valid(h_temporary));

  cm_list_destroy(&list);
  assert(! cl_iter_handle_valid(h_first));
  assert(! cl_iter_handle_valid(h_second));
  assert(! cl_iter_handle_valid(h_third));
  assert(cl_iter_table_live_slots() == 0);
  printf(
    "cl_iter_table: OK (slot=%lu B, peak=%lu, capacity=%lu B)\n",
    (unsigned long) cl_iter_table_slot_size(),
    (unsigned long) cl_iter_table_peak_slots(),
    (unsigned long) cl_iter_table_capacity_bytes()
  );
  cl_iter_table_free();
  return 0;
}
