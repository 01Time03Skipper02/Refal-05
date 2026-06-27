#ifndef COMPACT_RUNTIME_STORAGE_H_
#define COMPACT_RUNTIME_STORAGE_H_


#include "refal05rts.h"
#include "compact_list.h"


typedef struct compact_runtime_storage {
  cm_list_t view;
  cm_list_t build;
  cm_list_t buried;
  cl_iter_t pending_insert_pos;
} compact_runtime_storage_t;


void crs_init(compact_runtime_storage_t *storage);
void crs_destroy(compact_runtime_storage_t *storage);

cl_iter_t crs_view_begin(compact_runtime_storage_t *storage);
cl_iter_t crs_view_end(compact_runtime_storage_t *storage);
cl_iter_t crs_buried_begin(compact_runtime_storage_t *storage);
cl_iter_t crs_buried_end(compact_runtime_storage_t *storage);

void crs_reset_build(compact_runtime_storage_t *storage);
void crs_alloc_item(compact_runtime_storage_t *storage, cm_item_t item);
void crs_alloc_chars(
  compact_runtime_storage_t *storage, const char buffer[], size_t len
);
cl_iter_t crs_alloc_node(
  compact_runtime_storage_t *storage, enum r05_datatag tag
);
cl_iter_t crs_insert_pos(compact_runtime_storage_t *storage);

void crs_splice_before(cl_iter_t dest, cl_iter_t begin, cl_iter_t end);
void crs_splice_from_build(
  compact_runtime_storage_t *storage, cl_iter_t dest
);
void crs_remove_range(cl_iter_t begin, cl_iter_t end);


#endif  /* COMPACT_RUNTIME_STORAGE_H_ */
