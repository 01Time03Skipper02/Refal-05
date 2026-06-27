#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "compact_list.h"
#ifdef R05_COMPACT_HANDLES
#include "cl_iter_table.h"
#else
#define cl_slots_relocate_macro(new_m) ((void)(new_m))
#define cl_slots_shift_in_macro(m, from_index, delta) \
  ((void)(m), (void)(from_index), (void)(delta))
#define cl_slots_move_range(src, from, to, dst, dst_base) \
  ((void)(src), (void)(from), (void)(to), (void)(dst), (void)(dst_base))
#define cl_slots_kill_macro(m) ((void)(m))
#define cl_slots_kill_at(m, index) ((void)(m), (void)(index))
#endif


#ifndef R05_MAX_MACRONODE
//#define R05_MAX_MACRONODE SHRT_MAX
#define R05_MAX_MACRONODE 1000
#endif /* R05_MAX_MACRONODE */


static void macro_weld(cm_macronode_t *left, cm_macronode_t *right) {
  assert(left != NULL && right != NULL);
  left->next_macro = right;
  right->prev_macro = left;
}

static int is_sentinel(const cm_macronode_t *mn) {
  return mn->capacity == 0;
}

static int grow_capacity(int current, int need) {
  int cap = (current > 0) ? current : CM_MIN_BUCKET;
  if (cap < CM_MIN_BUCKET) cap = CM_MIN_BUCKET;
  while (cap < need) {
    cap += CM_MIN_BUCKET;
  }
  return cap;
}

static int is_bracket(const cm_item_t *it) {
  return it->tag == R05_DATATAG_OPEN_BRACKET
      || it->tag == R05_DATATAG_CLOSE_BRACKET;
}

static cm_macronode_t **s_macros = NULL;
static size_t s_macro_count = 0;
static size_t s_macro_capacity = 0;
static unsigned int s_macro_high_water = 1;
static unsigned int *s_macro_free_ids = NULL;
static size_t s_macro_free_count = 0;
static size_t s_macro_free_capacity = 0;

static void macro_registry_grow_to(unsigned int min_capacity) {
  if (min_capacity >= s_macro_capacity) {
    size_t new_capacity = s_macro_capacity == 0 ? 64 : 2 * s_macro_capacity;
    cm_macronode_t **grown;
    while (min_capacity >= new_capacity) {
      new_capacity *= 2;
    }
    grown = (cm_macronode_t **)realloc(
      s_macros, new_capacity * sizeof(s_macros[0])
    );
    assert(grown != NULL);
    memset(
      grown + s_macro_capacity, 0,
      (new_capacity - s_macro_capacity) * sizeof(s_macros[0])
    );
    s_macros = grown;
    s_macro_capacity = new_capacity;
  }
}

static void macro_registry_push_free_id(unsigned int id) {
  if (s_macro_free_count == s_macro_free_capacity) {
    size_t new_capacity = s_macro_free_capacity == 0
      ? 64 : 2 * s_macro_free_capacity;
    unsigned int *grown = (unsigned int *)realloc(
      s_macro_free_ids, new_capacity * sizeof(s_macro_free_ids[0])
    );
    assert(grown != NULL);
    s_macro_free_ids = grown;
    s_macro_free_capacity = new_capacity;
  }
  s_macro_free_ids[s_macro_free_count++] = id;
}

static void macro_registry_add(cm_macronode_t *mn) {
  unsigned int id;
  if (s_macro_free_count != 0) {
    id = s_macro_free_ids[--s_macro_free_count];
  } else {
    id = s_macro_high_water++;
  }
  macro_registry_grow_to(id);
  mn->macro_id = id;
  s_macros[id] = mn;
  s_macro_count++;
}

static void macro_registry_remove(cm_macronode_t *mn) {
  unsigned int id = mn->macro_id;
  if (id != 0 && id < s_macro_high_water && s_macros[id] == mn) {
    s_macros[id] = NULL;
    mn->macro_id = 0;
    macro_registry_push_free_id(id);
    assert(s_macro_count > 0);
    s_macro_count--;
  }
}

cm_macronode_t *cm_macronode_by_id(unsigned int macro_id) {
  if (macro_id == 0 || macro_id >= s_macro_high_water) {
    return NULL;
  }
  return s_macros[macro_id];
}

static void transform_bracket_targets(
  cm_macronode_t *anchor, cm_macronode_t *old_macro,
  int from_index, cm_macronode_t *new_macro, int index_delta
) {
  size_t m;

  (void)anchor;
  for (m = 1; m < s_macro_high_water; ++m) {
    cm_macronode_t *cur = s_macros[m];
    int i;
    if (cur == NULL) {
      continue;
    }
    for (i = 0; i < cur->count; ++i) {
      cm_item_t *item = &cur->items[i];
      if (is_bracket(item) && item->info.link == old_macro
          && item->offset >= from_index) {
        item->info.link = new_macro;
        item->offset = (short)(item->offset + index_delta);
      }
    }
  }
}

static void unlink_bracket(cm_item_t *item) {
  if (is_bracket(item) && item->info.link != NULL) {
    cm_item_t *partner = &item->info.link->items[item->offset];
    partner->info.link = NULL;
    partner->offset = 0;
  }
}

#ifdef R05_CHECK_BRACKETS
#include <stdio.h>
static void check_brackets_at(cm_macronode_t *anchor, const char *op) {
  size_t m;
  (void)anchor;
#ifdef R05_CHECK_BRACKETS_REMOVE_ONLY
  if (strcmp(op, "remove") != 0 && strcmp(op, "remove-single") != 0
      && strcmp(op, "link") != 0 && strcmp(op, "split") != 0
      && strcmp(op, "merge") != 0 && strcmp(op, "insert-middle") != 0
      && strcmp(op, "insert-left") != 0 && strcmp(op, "insert-new") != 0
      && strcmp(op, "push-back") != 0 && strcmp(op, "grow") != 0) {
    return;
  }
#endif
  for (m = 1; m < s_macro_high_water; ++m) {
    cm_macronode_t *cur = s_macros[m];
    int i;
    if (cur == NULL) {
      continue;
    }
    for (i = 0; i < cur->count; ++i) {
      cm_item_t *item = &cur->items[i];
      if (is_bracket(item) && item->info.link != NULL) {
        cm_item_t *partner;
        if (!(item->offset >= 0 && item->offset < item->info.link->count)) {
          fprintf(stderr, "bracket check failed after %s: bad offset\n", op);
          abort();
        }
        partner = &item->info.link->items[item->offset];
        if (!is_bracket(partner)) {
          fprintf(
            stderr,
            "bracket check failed after %s: item tag=%d at macro=%p index=%d -> macro=%p offset=%d partner tag=%d count=%d\n",
            op, item->tag, (void *)cur, i, (void *)item->info.link,
            item->offset, partner->tag, item->info.link->count
          );
          abort();
        }
        if (!(partner->info.link == cur && partner->offset == i)) {
          fprintf(stderr, "bracket check failed after %s: asymmetric\n", op);
          abort();
        }
      }
    }
  }
}
#define check_brackets(anchor, op) check_brackets_at((anchor), (op))
#else
#define check_brackets(anchor, op) ((void)(anchor), (void)(op))
#endif

static cm_macronode_t *macro_grow(cm_macronode_t *mn, int new_capacity) {
  cm_macronode_t *grown;
  assert(new_capacity >= mn->count);

  grown = cm_macronode_alloc(new_capacity);
  assert(grown != NULL);
  transform_bracket_targets(mn, mn, 0, grown, 0);
  grown->count = mn->count;
  grown->slot_list_head = mn->slot_list_head;
  memcpy(grown->items, mn->items, (size_t)mn->count * sizeof(cm_item_t));
  grown->prev_macro = mn->prev_macro;
  grown->next_macro = mn->next_macro;
  grown->prev_macro->next_macro = grown;
  grown->next_macro->prev_macro = grown;
  cl_slots_relocate_macro(grown);
  macro_registry_remove(mn);
  free(mn->slot_by_index);
  free(mn);
  check_brackets(grown, "grow");
  return grown;
}

static void macro_insert(
  cm_macronode_t *prev, cm_macronode_t *mn, cm_macronode_t *next
) {
  macro_weld(prev, mn);
  macro_weld(mn, next);
}

static void macro_unlink(cm_macronode_t *mn) {
  macro_weld(mn->prev_macro, mn->next_macro);
}

cm_macronode_t *cm_macronode_alloc(int capacity) {
  cm_macronode_t *mn;
  assert(capacity >= 0);
  assert(capacity <= SHRT_MAX);
  mn = (cm_macronode_t *)malloc(
    sizeof(cm_macronode_t) + (size_t)capacity * sizeof(cm_item_t)
  );
  assert(mn != NULL);
  mn->prev_macro = NULL;
  mn->next_macro = NULL;
  mn->slot_list_head = 0;
  mn->slot_by_index = capacity > 0
    ? (unsigned int *)calloc((size_t)capacity, sizeof(unsigned int))
    : NULL;
  assert(capacity == 0 || mn->slot_by_index != NULL);
  mn->capacity   = (unsigned short)capacity;
  mn->count      = 0;
  macro_registry_add(mn);
  return mn;
}

static cm_macronode_t *macronode_alloc_with_reserve(int min_capacity) {
  return cm_macronode_alloc(grow_capacity(0, min_capacity));
}

void cm_macronode_free(cm_macronode_t *mn) {
  cl_slots_kill_macro(mn);
  macro_registry_remove(mn);
  free(mn->slot_by_index);
  free(mn);
}

cm_list_t cm_list_create(void) {
  cm_list_t list;
  list.head = cm_macronode_alloc(0);
  list.tail = cm_macronode_alloc(0);
  macro_weld(list.head, list.tail);
  return list;
}

void cm_list_destroy(cm_list_t *list) {
  cm_macronode_t *cur = list->head;
  while (cur != NULL) {
    cm_macronode_t *next = cur->next_macro;
    cm_macronode_free(cur);
    cur = next;
  }
  list->head = NULL;
  list->tail = NULL;
}

cm_iter_t cm_list_begin(cm_list_t *list) {
  cm_iter_t it;
  cm_macronode_t *first = list->head->next_macro;
  while (first != list->tail && first->count == 0) {
    first = first->next_macro;
  }
  it.macro = first;
  it.index = 0;
  return it;
}

cm_iter_t cm_list_end(cm_list_t *list) {
  cm_iter_t it;
  it.macro = list->tail;
  it.index = 0;
  return it;
}

cm_iter_t cm_iter_next(cm_iter_t it) {
  assert(it.macro != NULL);
  if (it.index + 1 < it.macro->count) {
    it.index++;
  } else {
    it.macro = it.macro->next_macro;
    it.index = 0;
    while (it.macro->count == 0 && it.macro->next_macro != NULL) {
      it.macro = it.macro->next_macro;
    }
  }
  return it;
}

cm_iter_t cm_iter_prev(cm_iter_t it) {
  assert(it.macro != NULL);
  if (it.index > 0) {
    it.index--;
  } else {
    it.macro = it.macro->prev_macro;
    assert(it.macro != NULL);
    while (it.macro->count == 0 && it.macro->prev_macro != NULL) {
      it.macro = it.macro->prev_macro;
    }
    it.index = it.macro->count - 1;
    if (it.index < 0) it.index = 0;
  }
  return it;
}

int cm_iter_eq(cm_iter_t a, cm_iter_t b) {
  return a.macro == b.macro && a.index == b.index;
}

cm_item_t *cm_iter_get(cm_iter_t it) {
  assert(it.macro != NULL && it.index >= 0 && it.index < it.macro->count);
  return &it.macro->items[it.index];
}

void cm_link_brackets_iter(cm_iter_t open, cm_iter_t close) {
  cm_item_t *o = &open.macro->items[open.index];
  cm_item_t *c = &close.macro->items[close.index];

  assert(is_bracket(o) && is_bracket(c));

  o->info.link = close.macro;
  o->offset    = (short)close.index;
  c->info.link = open.macro;
  c->offset    = (short)open.index;
  check_brackets(open.macro, "link");
}

void cm_split(cm_iter_t *pos) {
  cm_macronode_t *left = pos->macro;
  cm_macronode_t *right;
  int right_count;

  if (pos->index == 0) {
    return;
  }

  right_count = left->count - pos->index;

  right = cm_macronode_alloc(right_count);
  transform_bracket_targets(left, left, pos->index, right, -pos->index);
  right->count = right_count;
  memcpy(right->items, left->items + pos->index,
         (size_t)right_count * sizeof(cm_item_t));

  cl_slots_move_range(left, pos->index, left->count, right, 0);

  left->count = pos->index;

  macro_insert(left, right, left->next_macro);

  pos->macro = right;
  pos->index = 0;
  check_brackets(right, "split");
}

cm_iter_t cm_insert_before(cm_iter_t pos, cm_item_t item) {
  cm_macronode_t *mn;
  cm_iter_t result;

  if (!is_sentinel(pos.macro)) {
    mn = pos.macro;
    if (mn->count >= mn->capacity) {
      mn = macro_grow(mn, grow_capacity(mn->capacity, mn->count + 1));
      pos.macro = mn;
    }
    if (pos.index < mn->count) {
      transform_bracket_targets(mn, mn, pos.index, mn, +1);
      memmove(
        mn->items + pos.index + 1,
        mn->items + pos.index,
        (size_t)(mn->count - pos.index) * sizeof(cm_item_t)
      );
      cl_slots_shift_in_macro(mn, pos.index, +1);
    }
    mn->items[pos.index] = item;
    mn->count++;

    result.macro = mn;
    result.index = pos.index;
    check_brackets(mn, "insert-middle");
    return result;
  }

  {
    cm_macronode_t *left = pos.macro->prev_macro;
    if (!is_sentinel(left) && left->count < left->capacity) {
      left->items[left->count] = item;
      result.macro = left;
      result.index = left->count;
      left->count++;
      check_brackets(left, "insert-left");
      return result;
    }
  }

  mn = macronode_alloc_with_reserve(1);
  mn->count = 1;
  mn->items[0] = item;
  macro_insert(pos.macro->prev_macro, mn, pos.macro);

  result.macro = mn;
  result.index = 0;
  check_brackets(mn, "insert-new");
  return result;
}

cm_iter_t cm_remove(cm_iter_t pos) {
  cm_macronode_t *mn = pos.macro;
  cm_iter_t next_pos;

  assert(mn != NULL && pos.index >= 0 && pos.index < mn->count);

  if (mn->count == 1) {
    cm_macronode_t *left  = mn->prev_macro;
    cm_macronode_t *right = mn->next_macro;
    next_pos.macro = right;
    next_pos.index = 0;
    unlink_bracket(&mn->items[pos.index]);
    cl_slots_kill_at(mn, pos.index);
    macro_unlink(mn);
    cm_macronode_free(mn);

    if (!is_sentinel(left) && !is_sentinel(right)) {
      if (left->count + right->count <= left->capacity) {
        int merge_index = left->count;
        cm_merge(left, right);
        next_pos.macro = left;
        next_pos.index = merge_index;
      }
    }
    check_brackets(next_pos.macro, "remove-single");
    return next_pos;
  }

  {
    int tail_count = mn->count - pos.index - 1;
    unlink_bracket(&mn->items[pos.index]);
    cl_slots_kill_at(mn, pos.index);
    if (tail_count > 0) {
      transform_bracket_targets(mn, mn, pos.index + 1, mn, -1);
      memmove(
        mn->items + pos.index,
        mn->items + pos.index + 1,
        (size_t)tail_count * sizeof(cm_item_t)
      );
      cl_slots_shift_in_macro(mn, pos.index + 1, -1);
    }
    mn->count--;
  }

  if (pos.index >= mn->count) {
    cm_macronode_t *right = mn->next_macro;
    if (!is_sentinel(right)
        && mn->count + right->count <= mn->capacity) {
      int merge_index = mn->count;
      cm_merge(mn, right);
      next_pos.macro = mn;
      next_pos.index = merge_index;
    } else {
      next_pos.macro = right;
      next_pos.index = 0;
    }
  } else {
    next_pos.macro = mn;
    next_pos.index = pos.index;
  }
  check_brackets(next_pos.macro, "remove");
  return next_pos;
}

static cm_macronode_t *cm_merge_growing_left(
  cm_macronode_t *left, cm_macronode_t *right
) {
  int total;
  assert(left->next_macro == right);
  if (is_sentinel(left) || is_sentinel(right)) {
    return left;
  }
  total = left->count + right->count;
  if (total > R05_MAX_MACRONODE) {
    return left;
  }
  if (total > left->capacity) {
    left = macro_grow(left, total);
  }
  cm_merge(left, right);
  return left;
}

void cm_splice(cm_iter_t dest, cm_iter_t begin, cm_iter_t end) {
  cm_macronode_t *b_macro, *e_macro;
  cm_macronode_t *before_b, *after_e;
  cm_macronode_t *before_dest;
  int same_macro_before_split = (begin.macro == end.macro);
  int end_offset_from_begin   = end.index - begin.index;

  cm_split(&begin);

  if (same_macro_before_split) {
    end.macro = begin.macro;
    end.index = end_offset_from_begin;
  }

  {
    cm_iter_t after_end;
    after_end.macro = end.macro;
    after_end.index = end.index + 1;
    if (after_end.index < end.macro->count) {
      cm_split(&after_end);
    }
  }

  b_macro = begin.macro;
  e_macro = end.macro;

  cm_split(&dest);

  before_b    = b_macro->prev_macro;
  after_e     = e_macro->next_macro;
  before_dest = dest.macro->prev_macro;

  macro_weld(before_b, after_e);
  if (before_b != dest.macro && after_e != dest.macro
      && !is_sentinel(before_b) && !is_sentinel(after_e)) {
    cm_merge_growing_left(before_b, after_e);
  }

  macro_weld(before_dest, b_macro);
  macro_weld(e_macro, dest.macro);
  if (!is_sentinel(e_macro) && !is_sentinel(dest.macro)) {
    cm_macronode_t *merged = cm_merge_growing_left(e_macro, dest.macro);
    if (b_macro == e_macro) {
      b_macro = merged;
    }
    e_macro = merged;
  }
  if (!is_sentinel(before_dest) && !is_sentinel(b_macro)) {
    cm_merge_growing_left(before_dest, b_macro);
  }
}

void cm_compact_list(cm_list_t *list) {
  cm_macronode_t *cur = list->head->next_macro;
  while (cur != list->tail) {
    cm_macronode_t *next = cur->next_macro;
    if (next == list->tail) {
      if (cur->count > 0 && cur->capacity > cur->count) {
        cur = macro_grow(cur, cur->count);
      }
      cur = next;
    } else {
      cur = cm_merge_growing_left(cur, next);
      next = cur->next_macro;
      if (cur->count + next->count > R05_MAX_MACRONODE) {
        cur = next;
      }
    }
  }
}

void cm_merge(cm_macronode_t *left, cm_macronode_t *right) {
  int merge_at;
  assert(left->next_macro == right);

  if (left->count + right->count > left->capacity) {
    return;
  }

  merge_at = left->count;
  transform_bracket_targets(right, right, 0, left, merge_at);
  memcpy(
    left->items + left->count,
    right->items,
    (size_t)right->count * sizeof(cm_item_t)
  );
  cl_slots_move_range(right, 0, right->count, left, left->count);
  left->count += right->count;

  macro_weld(left, right->next_macro);
  cm_macronode_free(right);
  check_brackets(left, "merge");
}

cm_iter_t cm_push_back(cm_list_t *list, cm_item_t item) {
  cm_macronode_t *last = list->tail->prev_macro;
  cm_iter_t result;

  if (last != list->head && last->count < last->capacity) {
    last->items[last->count] = item;
    result.macro = last;
    result.index = last->count;
    last->count++;
    check_brackets(last, "push-back");
    return result;
  }

  return cm_insert_before(cm_list_end(list), item);
}

cm_iter_t cm_push_back_bulk(cm_list_t *list, const cm_item_t *items, int k) {
  cm_macronode_t *mn;
  cm_iter_t result;

  assert(k > 0);
  mn = cm_macronode_alloc(k);
  mn->count = k;
  memcpy(mn->items, items, (size_t)k * sizeof(cm_item_t));

  macro_insert(list->tail->prev_macro, mn, list->tail);

  result.macro = mn;
  result.index = 0;
  check_brackets(mn, "push-back-bulk");
  return result;
}

cm_iter_t cm_push_pinned_singleton(cm_iter_t dest, cm_item_t item) {
  cm_macronode_t *mn = cm_macronode_alloc(1);
  cm_iter_t result;

  cm_split(&dest);

  mn->items[0] = item;
  mn->count = 1;

  macro_insert(dest.macro->prev_macro, mn, dest.macro);

  result.macro = mn;
  result.index = 0;
  check_brackets(mn, "push-pinned");
  return result;
}

cm_stats_t cm_list_stats(cm_list_t *list) {
  cm_stats_t s;
  cm_macronode_t *cur;

  s.total_elements  = 0;
  s.total_macronodes = 0;
  s.compact_bytes   = 0;
  s.classic_bytes   = 0;

  for (cur = list->head->next_macro; cur != list->tail; cur = cur->next_macro) {
    s.total_elements  += cur->count;
    s.total_macronodes++;
    s.compact_bytes   += sizeof(cm_macronode_t)
                       + (size_t)cur->capacity * sizeof(cm_item_t);
  }

  s.compact_bytes += 2 * sizeof(cm_macronode_t);

  s.classic_bytes = (size_t)s.total_elements * sizeof(struct r05_node)
                  + 2 * sizeof(struct r05_node);

  return s;
}
