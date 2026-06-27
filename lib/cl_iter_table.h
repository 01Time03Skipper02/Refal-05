#ifndef CL_ITER_TABLE_H_
#define CL_ITER_TABLE_H_

#include <stddef.h>
#include "cl_handle.h"

#define R05_TYPES_DECLARED
#include "compact_list.h"


#ifdef __cplusplus
extern "C" {
#endif

void cl_iter_table_init(void);
void cl_iter_table_free(void);

void cl_iter_frame_begin(void);
void cl_iter_frame_end(void);
void cl_iter_escape(cl_handle_t it);

cl_handle_t cl_iter_alloc(cm_macronode_t *m, int i);
cl_handle_t cl_iter_alloc_temp(cm_macronode_t *m, int i);

cm_iter_t cl_iter_deref(cl_handle_t it);

int cl_iter_handle_valid(cl_handle_t it);

void cl_slots_relocate_macro(cm_macronode_t *new_m);
void cl_slots_shift_in_macro(cm_macronode_t *m, int from_index, int delta);
void cl_slots_move_range(
  cm_macronode_t *src, int from, int to,
  cm_macronode_t *dst, int dst_base
);
void cl_slots_kill_macro(cm_macronode_t *m);
void cl_slots_kill_at(cm_macronode_t *m, int index);

size_t cl_iter_table_live_slots(void);
size_t cl_iter_table_peak_slots(void);
size_t cl_iter_table_slot_size(void);
size_t cl_iter_table_capacity_bytes(void);
void cl_iter_table_dump_counters(void);


#ifdef __cplusplus
}
#endif

#endif  /* CL_ITER_TABLE_H_ */
