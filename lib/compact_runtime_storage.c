#include <assert.h>
#include <stdlib.h>

#include "compact_runtime_storage.h"
#include "cl_iter_table.h"


static cl_iter_t handle_from_cm(cm_iter_t it) {
  return cl_iter_alloc(it.macro, it.index);
}


static int list_empty(cm_list_t *list) {
  return cm_iter_eq(cm_list_begin(list), cm_list_end(list));
}


void crs_init(compact_runtime_storage_t *storage) {
  storage->view = cm_list_create();
  storage->build = cm_list_create();
  storage->buried = cm_list_create();
  storage->pending_insert_pos = CL_ITER_NULL;
}


void crs_destroy(compact_runtime_storage_t *storage) {
  cm_list_destroy(&storage->build);
  cm_list_destroy(&storage->buried);
  cm_list_destroy(&storage->view);
}


cl_iter_t crs_view_begin(compact_runtime_storage_t *storage) {
  return handle_from_cm(cm_list_begin(&storage->view));
}


cl_iter_t crs_view_end(compact_runtime_storage_t *storage) {
  return handle_from_cm(cm_list_end(&storage->view));
}


cl_iter_t crs_buried_begin(compact_runtime_storage_t *storage) {
  return handle_from_cm(cm_list_begin(&storage->buried));
}


cl_iter_t crs_buried_end(compact_runtime_storage_t *storage) {
  return handle_from_cm(cm_list_end(&storage->buried));
}


void crs_remove_range(cl_iter_t begin, cl_iter_t end) {
  cl_iter_t after = cl_iter_next(end);
  cm_iter_t current = cl_iter_deref(begin);

  while (! cm_iter_eq(current, cl_iter_deref(after))) {
    current = cm_remove(current);
  }
}


void crs_reset_build(compact_runtime_storage_t *storage) {
  storage->pending_insert_pos = CL_ITER_NULL;
  if (! list_empty(&storage->build)) {
    cm_list_destroy(&storage->build);
    storage->build = cm_list_create();
  }
}


void crs_alloc_item(compact_runtime_storage_t *storage, cm_item_t item) {
  if (! cl_iter_is_null(storage->pending_insert_pos)) {
    cl_iter_t pending = storage->pending_insert_pos;
    cm_item_t *target = cm_iter_get(cl_iter_deref(pending));
    *target = item;
    storage->pending_insert_pos = CL_ITER_NULL;
    return;
  }
  cm_push_back(&storage->build, item);
}


void crs_alloc_chars(
  compact_runtime_storage_t *storage, const char buffer[], size_t len
) {
  size_t i;

  if (len == 0) {
    return;
  }
  if (! cl_iter_is_null(storage->pending_insert_pos)) {
    cm_item_t item = { 0 };
    item.tag = R05_DATATAG_CHAR;
    item.info.char_ = buffer[0];
    crs_alloc_item(storage, item);
    buffer++;
    len--;
  }
  if (len == 0) {
    return;
  }
  {
    cm_item_t *items = (cm_item_t *)calloc(len, sizeof(cm_item_t));
    assert(items != NULL);
    for (i = 0; i < len; ++i) {
      items[i].tag = R05_DATATAG_CHAR;
      items[i].info.char_ = buffer[i];
    }
    cm_push_back_bulk(&storage->build, items, (int)len);
    free(items);
  }
}


cl_iter_t crs_alloc_node(
  compact_runtime_storage_t *storage, enum r05_datatag tag
) {
  cm_item_t item = { 0 };
  cm_iter_t inserted;

  item.tag = (short) tag;
  if (! cl_iter_is_null(storage->pending_insert_pos)) {
    cl_iter_t pending = storage->pending_insert_pos;
    cm_item_t *target = cm_iter_get(cl_iter_deref(pending));
    *target = item;
    storage->pending_insert_pos = CL_ITER_NULL;
    return pending;
  }
  inserted = cm_push_back(&storage->build, item);
  return handle_from_cm(inserted);
}


cl_iter_t crs_insert_pos(compact_runtime_storage_t *storage) {
  cm_item_t placeholder = { 0 };
  cm_iter_t inserted;

  if (! cl_iter_is_null(storage->pending_insert_pos)) {
    return storage->pending_insert_pos;
  }
  placeholder.tag = R05_DATATAG_ILLEGAL;
  inserted = cm_push_back(&storage->build, placeholder);
  storage->pending_insert_pos = handle_from_cm(inserted);
  return storage->pending_insert_pos;
}


void crs_splice_before(cl_iter_t dest, cl_iter_t begin, cl_iter_t end) {
  cm_splice(
    cl_iter_deref(dest), cl_iter_deref(begin), cl_iter_deref(end)
  );
}


void crs_splice_from_build(
  compact_runtime_storage_t *storage, cl_iter_t dest
) {
  cm_iter_t begin;
  cm_iter_t end;

  if (list_empty(&storage->build)) {
    return;
  }

  begin = cm_list_begin(&storage->build);
  if (! cl_iter_is_null(storage->pending_insert_pos)) {
    cm_iter_t placeholder = cl_iter_deref(storage->pending_insert_pos);
    if (! cm_iter_eq(begin, placeholder)) {
      end = cm_iter_prev(placeholder);
      cm_splice(cl_iter_deref(dest), begin, end);
    }
    cm_remove(cl_iter_deref(storage->pending_insert_pos));
    storage->pending_insert_pos = CL_ITER_NULL;
  } else {
    end = cm_iter_prev(cm_list_end(&storage->build));
    cm_splice(cl_iter_deref(dest), begin, end);
  }
  assert(list_empty(&storage->build));
}
