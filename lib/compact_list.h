#ifndef COMPACT_LIST_H_
#define COMPACT_LIST_H_

#include <stddef.h>
#include "refal05rts.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CM_MIN_BUCKET 8

typedef struct cm_item {
  short                  tag;
  short                  offset;
  union {
    char                  char_;
    r05_number            number;
    struct r05_function  *function;
    struct cm_macronode  *link;
    unsigned int          handle;
  } info;
} cm_item_t;

typedef struct cm_macronode {
  struct cm_macronode *prev_macro;
  struct cm_macronode *next_macro;
  unsigned short        slot_list_head;
  unsigned short        macro_id;
  unsigned short        capacity;
  unsigned short        count;
  cm_item_t            items[];
} cm_macronode_t;

typedef struct cm_iter {
  cm_macronode_t *macro;
  int             index;
} cm_iter_t;

typedef struct cl_list {
  cm_macronode_t *head;
  cm_macronode_t *tail;
} cm_list_t;
cm_macronode_t *cm_macronode_alloc(int capacity);

void cm_macronode_free(cm_macronode_t *mn);

cm_macronode_t *cm_macronode_by_id(unsigned int macro_id);

cm_list_t cm_list_create(void);

void cm_list_destroy(cm_list_t *list);

cm_iter_t cm_list_begin(cm_list_t *list);

cm_iter_t cm_list_end(cm_list_t *list);

cm_iter_t cm_iter_next(cm_iter_t it);

cm_iter_t cm_iter_prev(cm_iter_t it);

int cm_iter_eq(cm_iter_t a, cm_iter_t b);

cm_item_t *cm_iter_get(cm_iter_t it);

cm_iter_t cm_insert_before(cm_iter_t pos, cm_item_t item);

cm_iter_t cm_remove(cm_iter_t pos);

void cm_split(cm_iter_t *pos);

void cm_splice(cm_iter_t dest, cm_iter_t begin, cm_iter_t end);

void cm_compact_list(cm_list_t *list);

void cm_merge(cm_macronode_t *left, cm_macronode_t *right);

cm_iter_t cm_push_back(cm_list_t *list, cm_item_t item);

cm_iter_t cm_push_back_bulk(cm_list_t *list, const cm_item_t *items, int k);

typedef struct cm_stats {
  int total_elements;
  int total_macronodes;
  size_t compact_bytes;
  size_t classic_bytes;
} cm_stats_t;

cm_stats_t cm_list_stats(cm_list_t *list);

void cm_link_brackets_iter(cm_iter_t open, cm_iter_t close);

static inline cm_item_t cm_item_char(char c) {
  cm_item_t it;
  it.tag = R05_DATATAG_CHAR;
  it.offset = 0;
  it.info.char_ = c;
  return it;
}

static inline cm_item_t cm_item_number(r05_number n) {
  cm_item_t it;
  it.tag = R05_DATATAG_NUMBER;
  it.offset = 0;
  it.info.number = n;
  return it;
}

static inline cm_item_t cm_item_function(struct r05_function *f) {
  cm_item_t it;
  it.tag = R05_DATATAG_FUNCTION;
  it.offset = 0;
  it.info.function = f;
  return it;
}

static inline cm_item_t cm_item_open_bracket(void) {
  cm_item_t it;
  it.tag = R05_DATATAG_OPEN_BRACKET;
  it.offset = 0;
  it.info.link = NULL;
  return it;
}

static inline cm_item_t cm_item_close_bracket(void) {
  cm_item_t it;
  it.tag = R05_DATATAG_CLOSE_BRACKET;
  it.offset = 0;
  it.info.link = NULL;
  return it;
}

cm_iter_t cm_push_pinned_singleton(cm_iter_t dest, cm_item_t item);


#ifdef __cplusplus
}
#endif

#endif
