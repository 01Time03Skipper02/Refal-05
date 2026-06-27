#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "refal05rts.h"
#include "cl_iter_table.h"

typedef struct cl_slot {
  unsigned int    macro_id;
  unsigned int    next;
  unsigned int    prev;
  unsigned int    frame_next;
  short           index;
  unsigned char   gen;
  unsigned char   frame_id;
  unsigned char   attached;
} cl_slot_t;


static cl_slot_t   *s_slots = NULL;
static unsigned int s_slots_capacity = 0;
static unsigned int s_slots_high_water = 1;
static unsigned int s_freelist_head = 0;
static size_t       s_live_count = 0;
static size_t       s_peak_live_count = 0;
static unsigned char s_current_frame = 0;
static unsigned char s_next_frame = 1;
static unsigned int s_frame_slot_head = 0;

#ifdef R05_COMPACT_COUNTERS
static unsigned long s_count_alloc_calls = 0;
static unsigned long s_count_alloc_search_steps = 0;
static unsigned long s_count_alloc_hits = 0;
static unsigned long s_count_alloc_new = 0;
static unsigned long s_count_frame_end_calls = 0;
static unsigned long s_count_frame_slots = 0;
static unsigned long s_count_detach = 0;
static unsigned long s_count_shift_scans = 0;
static unsigned long s_count_move_scans = 0;
#endif


#define GEN_MASK 0xFFu
#define SLOT_SHIFT 8
#define SLOT_ID_MAX (UINT_MAX >> SLOT_SHIFT)

#define MAKE_HANDLE(slot_id, gen) \
  (((unsigned int)(slot_id) << SLOT_SHIFT) | ((unsigned int)(gen) & GEN_MASK))

#define HANDLE_SLOT(h)  ((unsigned int)(h) >> SLOT_SHIFT)
#define HANDLE_GEN(h)   ((unsigned int)(h) & GEN_MASK)

static unsigned int checked_slot_id(unsigned int slot_id) {
  assert(slot_id <= SLOT_ID_MAX);
  return slot_id;
}

static unsigned int checked_macro_id(unsigned int macro_id) {
  assert(macro_id != 0);
  return macro_id;
}

enum {
  CL_SLOTS_INITIAL = 8
};


static void slots_grow(unsigned int min_needed) {
  unsigned int new_cap;
  cl_slot_t *new_slots;

  assert(min_needed <= SLOT_ID_MAX);

  new_cap = s_slots_capacity == 0 ? CL_SLOTS_INITIAL : s_slots_capacity;
  while (new_cap < min_needed) {
    unsigned int next_cap = new_cap * 2;
    if (next_cap <= new_cap || next_cap > SLOT_ID_MAX) {
      next_cap = SLOT_ID_MAX;
    }
    new_cap = next_cap;
  }
  assert(new_cap >= min_needed);

  new_slots = (cl_slot_t *) realloc(s_slots, new_cap * sizeof(cl_slot_t));
  assert(new_slots != NULL);

  memset(
    new_slots + s_slots_capacity, 0,
    (new_cap - s_slots_capacity) * sizeof(cl_slot_t)
  );

  s_slots = new_slots;
  s_slots_capacity = new_cap;
}

void cl_iter_table_init(void) {
  if (s_slots != NULL) {
    free(s_slots);
  }
  s_slots = NULL;
  s_slots_capacity = 0;
  s_slots_high_water = 1;
  s_freelist_head = 0;
  s_live_count = 0;
  s_peak_live_count = 0;
  s_current_frame = 0;
  s_next_frame = 1;
  s_frame_slot_head = 0;
  slots_grow(CL_SLOTS_INITIAL);
}


void cl_iter_table_free(void) {
  free(s_slots);
  s_slots = NULL;
  s_slots_capacity = 0;
  s_slots_high_water = 1;
  s_freelist_head = 0;
  s_live_count = 0;
  s_peak_live_count = 0;
  s_current_frame = 0;
  s_next_frame = 1;
  s_frame_slot_head = 0;
}

static void slot_list_attach(cm_macronode_t *m, unsigned int slot_id) {
  cl_slot_t *slot = &s_slots[slot_id];
  assert(! slot->attached);
  slot->prev = 0;
  slot->next = checked_slot_id(m->slot_list_head);
  if (m->slot_list_head != 0) {
    s_slots[m->slot_list_head].prev = checked_slot_id(slot_id);
  }
  m->slot_list_head = checked_slot_id(slot_id);
  slot->attached = 1;
  if (m->slot_by_index != NULL
      && slot->index >= 0 && slot->index < m->capacity) {
    m->slot_by_index[slot->index] = checked_slot_id(slot_id);
  }
}


static void slot_list_detach(cm_macronode_t *m, unsigned int slot_id) {
  cl_slot_t *slot = &s_slots[slot_id];

#ifdef R05_COMPACT_COUNTERS
  s_count_detach++;
#endif

  if (slot->prev == 0) {
    assert(m->slot_list_head == slot_id);
    m->slot_list_head = checked_slot_id(slot->next);
  } else {
    s_slots[slot->prev].next = slot->next;
  }
  if (slot->next != 0) {
    s_slots[slot->next].prev = slot->prev;
  }

  if (m->slot_by_index != NULL
      && slot->index >= 0 && slot->index < m->capacity
      && m->slot_by_index[slot->index] == slot_id) {
    m->slot_by_index[slot->index] = 0;
  }

  slot->next = 0;
  slot->prev = 0;
  slot->attached = 0;
}


static void slot_map_rebuild(cm_macronode_t *m) {
  unsigned int slot_id;
  if (m == NULL || m->slot_by_index == NULL) {
    return;
  }

  memset(m->slot_by_index, 0, (size_t)m->capacity * sizeof(unsigned int));
  for (slot_id = m->slot_list_head; slot_id != 0;
       slot_id = s_slots[slot_id].next) {
    cl_slot_t *slot = &s_slots[slot_id];
    assert(slot->attached);
    if (slot->index >= 0 && slot->index < m->capacity) {
      assert(m->slot_by_index[slot->index] == 0);
      m->slot_by_index[slot->index] = checked_slot_id(slot_id);
    }
  }
}


static cl_handle_t slot_map_lookup(cm_macronode_t *m, int index) {
  cl_handle_t result;
  unsigned int slot_id;
  cl_slot_t *slot;

  result.handle = CL_HANDLE_NULL;
  if (m->slot_by_index == NULL || index < 0 || index >= m->capacity) {
    return result;
  }

  slot_id = m->slot_by_index[index];
  if (slot_id == 0) {
    return result;
  }

  if (slot_id >= s_slots_high_water) {
    m->slot_by_index[index] = 0;
    return result;
  }

  slot = &s_slots[slot_id];
  if (!slot->attached || slot->macro_id != m->macro_id
      || slot->index != index || cm_macronode_by_id(slot->macro_id) != m) {
    m->slot_by_index[index] = 0;
    return result;
  }

#ifdef R05_COMPACT_COUNTERS
  s_count_alloc_hits++;
#endif
  result.handle = MAKE_HANDLE(slot_id, slot->gen);
  return result;
}


static void slot_recycle(unsigned int slot_id) {
  cl_slot_t *slot = &s_slots[slot_id];
  unsigned char frame_id = slot->frame_id;
  unsigned int frame_next = slot->frame_next;

  slot->macro_id = 0;
  slot->index = 0;
  if (frame_id == s_current_frame && frame_id != 0) {
    slot->frame_id = frame_id;
    slot->frame_next = frame_next;
  } else {
    slot->frame_id = 0;
  slot->frame_next = 0;
  }
  slot->prev = 0;
  slot->attached = 0;
  slot->gen++;
  slot->next = checked_slot_id(s_freelist_head);
  s_freelist_head = slot_id;
  if (s_live_count > 0) {
    s_live_count--;
  }
}

void cl_iter_frame_begin(void) {
  assert(s_current_frame == 0);
  if (s_slots == NULL) {
    cl_iter_table_init();
  }

  s_current_frame = s_next_frame++;
  if (s_next_frame == 0) {
    s_next_frame = 1;
  }
  s_frame_slot_head = 0;
}


static void frame_slots_shift_in_macro(
  cm_macronode_t *m, int from_index, int delta
) {
  unsigned int slot_id;
  for (slot_id = s_frame_slot_head; slot_id != 0;
       slot_id = s_slots[slot_id].frame_next) {
    cl_slot_t *s = &s_slots[slot_id];
    if (! s->attached && s->macro_id == m->macro_id
        && s->index >= from_index) {
      s->index += delta;
    }
  }
}


static void frame_slots_move_range(
  cm_macronode_t *src, int from, int to,
  cm_macronode_t *dst, int dst_base
) {
  unsigned int slot_id;
  for (slot_id = s_frame_slot_head; slot_id != 0;
       slot_id = s_slots[slot_id].frame_next) {
    cl_slot_t *s = &s_slots[slot_id];
    if (! s->attached && s->macro_id == src->macro_id
        && s->index >= from && s->index < to) {
      s->macro_id = checked_macro_id(dst->macro_id);
      s->index = s->index - from + dst_base;
    }
  }
}


static void frame_slots_kill_macro(cm_macronode_t *m) {
  unsigned int slot_id;
  for (slot_id = s_frame_slot_head; slot_id != 0;
       slot_id = s_slots[slot_id].frame_next) {
    cl_slot_t *s = &s_slots[slot_id];
    if (! s->attached && s->macro_id == m->macro_id) {
      slot_recycle(slot_id);
    }
  }
}


static void frame_slots_kill_at(cm_macronode_t *m, int index) {
  unsigned int slot_id;
  for (slot_id = s_frame_slot_head; slot_id != 0;
       slot_id = s_slots[slot_id].frame_next) {
    cl_slot_t *s = &s_slots[slot_id];
    if (! s->attached && s->macro_id == m->macro_id && s->index == index) {
      slot_recycle(slot_id);
    }
  }
}


void cl_iter_frame_end(void) {
  unsigned int slot_id = s_frame_slot_head;
  unsigned char frame_id = s_current_frame;

#ifdef R05_COMPACT_COUNTERS
  s_count_frame_end_calls++;
#endif

  assert(frame_id != 0);
  s_current_frame = 0;
  s_frame_slot_head = 0;

  while (slot_id != 0) {
    cl_slot_t *slot = &s_slots[slot_id];
    unsigned int next_id = slot->frame_next;

#ifdef R05_COMPACT_COUNTERS
    s_count_frame_slots++;
#endif

    if (slot->macro_id != 0 && slot->frame_id == frame_id) {
      if (slot->attached) {
        cm_macronode_t *macro = cm_macronode_by_id(slot->macro_id);
        assert(macro != NULL);
        slot_list_detach(macro, slot_id);
      }
      slot_recycle(slot_id);
    } else if (slot->macro_id != 0) {
      slot->frame_next = 0;
    } else if (slot->frame_id == frame_id) {
      slot->frame_id = 0;
      slot->frame_next = 0;
    }
    slot_id = next_id;
  }
}


void cl_iter_escape(cl_handle_t it) {
  unsigned int slot_id;
  cl_slot_t *slot;

  if (it.handle == CL_HANDLE_NULL) {
    return;
  }

  slot_id = HANDLE_SLOT(it.handle);
  assert(slot_id < s_slots_high_water);
  slot = &s_slots[slot_id];
  assert((slot->gen & GEN_MASK) == HANDLE_GEN(it.handle));
  assert(slot->macro_id != 0);
  if (! slot->attached) {
    cm_macronode_t *macro = cm_macronode_by_id(slot->macro_id);
    assert(macro != NULL);
    slot_list_attach(macro, slot_id);
  }
  slot->frame_id = 0;
}

static cl_handle_t cl_iter_alloc_impl(cm_macronode_t *m, int i, int tracked) {
  unsigned int slot_id;
  cl_slot_t *slot;
  cl_handle_t result;

  if (m == NULL) {
    result.handle = CL_HANDLE_NULL;
    return result;
  }

  if (s_slots == NULL) {
    cl_iter_table_init();
  }

#ifdef R05_COMPACT_COUNTERS
  s_count_alloc_calls++;
#endif

  if (tracked) {
    result = slot_map_lookup(m, i);
    if (result.handle != CL_HANDLE_NULL) {
      return result;
    }
    if (m->slot_by_index == NULL || i < 0 || i >= m->capacity) {
      for (slot_id = m->slot_list_head; slot_id != 0;
           slot_id = s_slots[slot_id].next) {
        slot = &s_slots[slot_id];
#ifdef R05_COMPACT_COUNTERS
        s_count_alloc_search_steps++;
#endif
        if (slot->index == i) {
#ifdef R05_COMPACT_COUNTERS
          s_count_alloc_hits++;
#endif
          result.handle = MAKE_HANDLE(slot_id, slot->gen);
          return result;
        }
      }
    }
  }

  if (s_freelist_head != 0) {
    slot_id = s_freelist_head;
    s_freelist_head = s_slots[slot_id].next;
  } else {
    if (s_slots_high_water >= s_slots_capacity) {
      slots_grow(s_slots_high_water + 1);
    }
    slot_id = s_slots_high_water++;
  }

#ifdef R05_COMPACT_COUNTERS
  s_count_alloc_new++;
#endif

  slot = &s_slots[slot_id];
  {
    int already_in_frame =
      s_current_frame != 0 && slot->frame_id == s_current_frame;

    slot->macro_id = checked_macro_id(m->macro_id);
    slot->index = i;
    slot->next = 0;
    slot->prev = 0;
    slot->attached = 0;
    if (s_current_frame != 0) {
      slot->frame_id = s_current_frame;
      if (! already_in_frame) {
        slot->frame_next = checked_slot_id(s_frame_slot_head);
        s_frame_slot_head = checked_slot_id(slot_id);
      }
    } else {
      slot->frame_id = 0;
      slot->frame_next = 0;
    }
  }

  if (tracked) {
    slot_list_attach(m, slot_id);
  }

  s_live_count++;
  if (s_live_count > s_peak_live_count) {
    s_peak_live_count = s_live_count;
  }
  result.handle = MAKE_HANDLE(slot_id, slot->gen);
  return result;
}


cl_handle_t cl_iter_alloc(cm_macronode_t *m, int i) {
  return cl_iter_alloc_impl(m, i, 1);
}


cl_handle_t cl_iter_alloc_temp(cm_macronode_t *m, int i) {
  return cl_iter_alloc_impl(m, i, 0);
}


cm_iter_t cl_iter_deref(cl_handle_t it) {
  cm_iter_t result;
  unsigned int slot_id = HANDLE_SLOT(it.handle);
  cl_slot_t *slot;

  if (it.handle == CL_HANDLE_NULL) {
    result.macro = NULL;
    result.index = 0;
    return result;
  }

  assert(slot_id < s_slots_high_water);
  slot = &s_slots[slot_id];

  assert((slot->gen & GEN_MASK) == HANDLE_GEN(it.handle));

  result.macro = cm_macronode_by_id(slot->macro_id);
  assert(result.macro != NULL);
  result.index = slot->index;
  return result;
}


int cl_iter_handle_valid(cl_handle_t it) {
  unsigned int slot_id;
  cl_slot_t *slot;

  if (it.handle == CL_HANDLE_NULL) {
    return 1;
  }
  slot_id = HANDLE_SLOT(it.handle);
  if (slot_id >= s_slots_high_water) {
    return 0;
  }
  slot = &s_slots[slot_id];
  return (slot->gen & GEN_MASK) == HANDLE_GEN(it.handle)
      && slot->macro_id != 0
      && cm_macronode_by_id(slot->macro_id) != NULL;
}

void cl_slots_relocate_macro(cm_macronode_t *new_m) {
  unsigned int slot_id;
  for (slot_id = new_m->slot_list_head; slot_id != 0;
       slot_id = s_slots[slot_id].next) {
    s_slots[slot_id].macro_id = checked_macro_id(new_m->macro_id);
  }
  slot_map_rebuild(new_m);
}


void cl_slots_shift_in_macro(cm_macronode_t *m, int from_index, int delta) {
  unsigned int slot_id = m->slot_list_head;
  frame_slots_shift_in_macro(m, from_index, delta);
  while (slot_id != 0) {
    cl_slot_t *s = &s_slots[slot_id];
#ifdef R05_COMPACT_COUNTERS
    s_count_shift_scans++;
#endif
    if (s->index >= from_index) {
      s->index += delta;
    }
    slot_id = s->next;
  }
  slot_map_rebuild(m);
}


void cl_slots_move_range(
  cm_macronode_t *src, int from, int to,
  cm_macronode_t *dst, int dst_base
) {
  unsigned int slot_id = src->slot_list_head;
  frame_slots_move_range(src, from, to, dst, dst_base);
  while (slot_id != 0) {
    cl_slot_t *s = &s_slots[slot_id];
    unsigned int next_id = s->next;

#ifdef R05_COMPACT_COUNTERS
    s_count_move_scans++;
#endif

    if (s->index >= from && s->index < to) {
      slot_list_detach(src, slot_id);
      s->macro_id = checked_macro_id(dst->macro_id);
      s->index = s->index - from + dst_base;
      slot_list_attach(dst, slot_id);
    }
    slot_id = next_id;
  }
  slot_map_rebuild(src);
  slot_map_rebuild(dst);
}


void cl_slots_kill_macro(cm_macronode_t *m) {
  unsigned int slot_id = m->slot_list_head;
  frame_slots_kill_macro(m);
  while (slot_id != 0) {
    cl_slot_t *s = &s_slots[slot_id];
    unsigned int next_id = s->next;
    slot_recycle(slot_id);
    slot_id = next_id;
  }
  m->slot_list_head = 0;
  if (m->slot_by_index != NULL) {
    memset(m->slot_by_index, 0, (size_t)m->capacity * sizeof(unsigned int));
  }
}


void cl_slots_kill_at(cm_macronode_t *m, int index) {
  unsigned int slot_id = m->slot_list_head;
  frame_slots_kill_at(m, index);
  while (slot_id != 0) {
    cl_slot_t *s = &s_slots[slot_id];
    unsigned int next_id = s->next;

    if (s->index == index) {
      slot_list_detach(m, slot_id);
      slot_recycle(slot_id);
    }
    slot_id = next_id;
  }
}


size_t cl_iter_table_live_slots(void) {
  return s_live_count;
}


size_t cl_iter_table_peak_slots(void) {
  return s_peak_live_count;
}


size_t cl_iter_table_slot_size(void) {
  return sizeof(cl_slot_t);
}


size_t cl_iter_table_capacity_bytes(void) {
  return (size_t) s_slots_capacity * sizeof(cl_slot_t);
}


void cl_iter_table_dump_counters(void) {
#ifdef R05_COMPACT_COUNTERS
  fprintf(
    stderr,
    "Compact handle counters:\n"
    "  alloc calls        : %lu\n"
    "  alloc search steps : %lu\n"
    "  alloc hits         : %lu\n"
    "  alloc new/reused   : %lu\n"
    "  frame end calls    : %lu\n"
    "  frame slots seen   : %lu\n"
    "  slot detaches      : %lu\n"
    "  shift scans        : %lu\n"
    "  move scans         : %lu\n",
    s_count_alloc_calls,
    s_count_alloc_search_steps,
    s_count_alloc_hits,
    s_count_alloc_new,
    s_count_frame_end_calls,
    s_count_frame_slots,
    s_count_detach,
    s_count_shift_scans,
    s_count_move_scans
  );
#endif
}
