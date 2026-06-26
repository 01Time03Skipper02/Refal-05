#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "refal05rts.h"
#include "cl_iter_table.h"

typedef struct cl_slot {
  unsigned short  macro_id;
  unsigned short  next;
  short           index;
  unsigned char   gen;
  unsigned char   frame_id;
} cl_slot_t;


static cl_slot_t   *s_slots = NULL;
static unsigned int s_slots_capacity = 0;
static unsigned int s_slots_high_water = 1;
static unsigned int s_freelist_head = 0;
static size_t       s_live_count = 0;
static size_t       s_peak_live_count = 0;
static unsigned char s_current_frame = 0;
static unsigned char s_next_frame = 1;


#define GEN_MASK 0xFFu
#define SLOT_SHIFT 8

#define MAKE_HANDLE(slot_id, gen) \
  (((unsigned int)(slot_id) << SLOT_SHIFT) | ((unsigned int)(gen) & GEN_MASK))

#define HANDLE_SLOT(h)  ((unsigned int)(h) >> SLOT_SHIFT)
#define HANDLE_GEN(h)   ((unsigned int)(h) & GEN_MASK)

static unsigned short checked_slot_id(unsigned int slot_id) {
  assert(slot_id <= USHRT_MAX);
  return (unsigned short)slot_id;
}

static unsigned short checked_macro_id(unsigned int macro_id) {
  assert(macro_id <= USHRT_MAX);
  return (unsigned short)macro_id;
}

enum {
  CL_SLOTS_INITIAL = 8
};


static void slots_grow(unsigned int min_needed) {
  unsigned int new_cap;
  cl_slot_t *new_slots;

  assert(min_needed <= USHRT_MAX);

  new_cap = min_needed < CL_SLOTS_INITIAL ? CL_SLOTS_INITIAL : min_needed;
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
}

static void slot_list_attach(cm_macronode_t *m, unsigned int slot_id) {
  cl_slot_t *slot = &s_slots[slot_id];
  slot->next = checked_slot_id(m->slot_list_head);
  m->slot_list_head = checked_slot_id(slot_id);
}


static void slot_list_detach(cm_macronode_t *m, unsigned int slot_id) {
  cl_slot_t *slot = &s_slots[slot_id];
  unsigned int prev_id = 0;
  unsigned int cur_id = m->slot_list_head;

  while (cur_id != slot_id) {
    assert(cur_id != 0);
    prev_id = cur_id;
    cur_id = s_slots[cur_id].next;
  }

  if (prev_id == 0) {
    m->slot_list_head = checked_slot_id(slot->next);
  } else {
    s_slots[prev_id].next = slot->next;
  }

  slot->next = 0;
}


static void slot_recycle(unsigned int slot_id) {
  cl_slot_t *slot = &s_slots[slot_id];

  slot->macro_id = 0;
  slot->index = 0;
  slot->frame_id = 0;
  slot->gen++;
  slot->next = checked_slot_id(s_freelist_head);
  s_freelist_head = slot_id;
  assert(s_live_count > 0);
  s_live_count--;
}

static void freelist_remove(unsigned int slot_id) {
  unsigned int prev_id = 0;
  unsigned int cur_id = s_freelist_head;

  while (cur_id != 0) {
    unsigned int next_id = s_slots[cur_id].next;
    if (cur_id == slot_id) {
      if (prev_id == 0) {
        s_freelist_head = next_id;
      } else {
        s_slots[prev_id].next = checked_slot_id(next_id);
      }
      return;
    }
    prev_id = cur_id;
    cur_id = next_id;
  }
}

static void slots_trim_tail(void) {
  unsigned int new_high_water = s_slots_high_water;
  unsigned int new_capacity;
  cl_slot_t *trimmed;

  while (new_high_water > 1 && s_slots[new_high_water - 1].macro_id == 0) {
    freelist_remove(new_high_water - 1);
    new_high_water--;
  }

  new_capacity = new_high_water < CL_SLOTS_INITIAL
    ? CL_SLOTS_INITIAL : new_high_water;
  if (new_capacity >= s_slots_capacity) {
    s_slots_high_water = new_high_water;
    return;
  }

  trimmed = (cl_slot_t *)realloc(s_slots, new_capacity * sizeof(cl_slot_t));
  assert(trimmed != NULL);
  s_slots = trimmed;
  s_slots_high_water = new_high_water;
  s_slots_capacity = new_capacity;
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
}


void cl_iter_frame_end(void) {
  unsigned int slot_id;
  unsigned char frame_id = s_current_frame;

  assert(frame_id != 0);
  s_current_frame = 0;

  for (slot_id = 1; slot_id < s_slots_high_water; ++slot_id) {
    cl_slot_t *slot = &s_slots[slot_id];
    if (slot->macro_id != 0) {
      cm_macronode_t *macro = cm_macronode_by_id(slot->macro_id);
      assert(macro != NULL);
      macro->slot_list_head = 0;
    }
  }

  for (slot_id = 1; slot_id < s_slots_high_water; ++slot_id) {
    cl_slot_t *slot = &s_slots[slot_id];
    if (slot->macro_id != 0) {
      cm_macronode_t *macro = cm_macronode_by_id(slot->macro_id);
      assert(macro != NULL);
      if (slot->frame_id == frame_id) {
        slot_recycle(slot_id);
      } else {
        slot->next = checked_slot_id(macro->slot_list_head);
        macro->slot_list_head = checked_slot_id(slot_id);
      }
    }
  }
  slots_trim_tail();
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
  slot->frame_id = 0;
}

cl_handle_t cl_iter_alloc(cm_macronode_t *m, int i) {
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

  for (slot_id = m->slot_list_head; slot_id != 0;
       slot_id = s_slots[slot_id].next) {
    slot = &s_slots[slot_id];
    if (slot->index == i) {
      result.handle = MAKE_HANDLE(slot_id, slot->gen);
      return result;
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

  slot = &s_slots[slot_id];
  slot->macro_id = checked_macro_id(m->macro_id);
  slot->index = i;
  slot->next = 0;
  slot->frame_id = s_current_frame;

  slot_list_attach(m, slot_id);

  s_live_count++;
  if (s_live_count > s_peak_live_count) {
    s_peak_live_count = s_live_count;
  }
  result.handle = MAKE_HANDLE(slot_id, slot->gen);
  return result;
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
}


void cl_slots_shift_in_macro(cm_macronode_t *m, int from_index, int delta) {
  unsigned int slot_id = m->slot_list_head;
  while (slot_id != 0) {
    cl_slot_t *s = &s_slots[slot_id];
    if (s->index >= from_index) {
      s->index += delta;
    }
    slot_id = s->next;
  }
}


void cl_slots_move_range(
  cm_macronode_t *src, int from, int to,
  cm_macronode_t *dst, int dst_base
) {
  unsigned int slot_id = src->slot_list_head;
  while (slot_id != 0) {
    cl_slot_t *s = &s_slots[slot_id];
    unsigned int next_id = s->next;

    if (s->index >= from && s->index < to) {
      slot_list_detach(src, slot_id);
      s->macro_id = checked_macro_id(dst->macro_id);
      s->index = s->index - from + dst_base;
      slot_list_attach(dst, slot_id);
    }
    slot_id = next_id;
  }
}


void cl_slots_kill_macro(cm_macronode_t *m) {
  unsigned int slot_id = m->slot_list_head;
  while (slot_id != 0) {
    cl_slot_t *s = &s_slots[slot_id];
    unsigned int next_id = s->next;
    slot_recycle(slot_id);
    slot_id = next_id;
  }
  m->slot_list_head = 0;
}


void cl_slots_kill_at(cm_macronode_t *m, int index) {
  unsigned int slot_id = m->slot_list_head;
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
