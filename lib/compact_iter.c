#include <assert.h>

#include "refal05rts.h"
#include "cl_iter_table.h"


static cm_item_t *iter_item(cl_iter_t it) {
  cm_iter_t pos = cl_iter_deref(it);
  return cm_iter_get(pos);
}

static int is_round_bracket_tag(enum r05_datatag tag) {
  return tag == R05_DATATAG_OPEN_BRACKET
      || tag == R05_DATATAG_CLOSE_BRACKET;
}


cl_iter_t cl_iter_next(cl_iter_t it) {
  cm_iter_t next = cm_iter_next(cl_iter_deref(it));
  return cl_iter_alloc(next.macro, next.index);
}


cl_iter_t cl_iter_prev(cl_iter_t it) {
  cm_iter_t prev = cm_iter_prev(cl_iter_deref(it));
  return cl_iter_alloc(prev.macro, prev.index);
}


int cl_iter_eq(cl_iter_t a, cl_iter_t b) {
  if (a.handle == b.handle) {
    return 1;
  }
  return cm_iter_eq(cl_iter_deref(a), cl_iter_deref(b));
}


enum r05_datatag cl_iter_tag(cl_iter_t it) {
  cm_iter_t pos = cl_iter_deref(it);
  if (pos.macro->count == 0) {
    return R05_DATATAG_ILLEGAL;
  }
  return (enum r05_datatag) cm_iter_get(pos)->tag;
}


void cl_iter_set_tag(cl_iter_t it, enum r05_datatag tag) {
  cm_item_t *item = iter_item(it);
  enum r05_datatag old_tag = (enum r05_datatag)item->tag;

  if (is_round_bracket_tag(old_tag) && item->info.link != NULL) {
    cm_item_t *partner = &item->info.link->items[item->offset];
    if (is_round_bracket_tag((enum r05_datatag)partner->tag)
        && partner->info.link == cl_iter_deref(it).macro
        && partner->offset == cl_iter_deref(it).index) {
      partner->info.link = NULL;
      partner->offset = 0;
    }
  }

  item->tag = (short) tag;
  if (is_round_bracket_tag(tag)) {
    item->info.link = NULL;
    item->offset = 0;
  }
}


char cl_iter_char(cl_iter_t it) {
  return iter_item(it)->info.char_;
}


r05_number cl_iter_number(cl_iter_t it) {
  return iter_item(it)->info.number;
}


struct r05_function *cl_iter_function(cl_iter_t it) {
  return iter_item(it)->info.function;
}


cl_iter_t cl_iter_link(cl_iter_t it) {
  cm_item_t *item = iter_item(it);
  if (item->info.link == NULL) {
    return CL_ITER_NULL;
  }
  return cl_iter_alloc(item->info.link, item->offset);
}


void cl_iter_set_char(cl_iter_t it, char ch) {
  iter_item(it)->info.char_ = ch;
}


void cl_iter_set_number(cl_iter_t it, r05_number number) {
  iter_item(it)->info.number = number;
}


void cl_iter_set_function(cl_iter_t it, struct r05_function *function) {
  iter_item(it)->info.function = function;
}


void cl_iter_set_link(cl_iter_t it, cl_iter_t link) {
  cm_item_t *item = iter_item(it);
  if (link.handle == CL_HANDLE_NULL) {
    item->info.link = NULL;
    item->offset = 0;
    return;
  }
  {
    cm_iter_t target = cl_iter_deref(link);
    item->info.link = target.macro;
    item->offset = (short)target.index;
  }
}
