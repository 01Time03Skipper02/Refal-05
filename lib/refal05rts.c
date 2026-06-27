#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "refal05rts.h"

#ifdef R05_COMPACT_LIST
#include "compact_list.h"
#endif  /* R05_COMPACT_LIST */

#ifdef R05_COMPACT_HANDLES
#include "cl_iter_table.h"
#include "compact_runtime_storage.h"
#endif  /* R05_COMPACT_HANDLES */

#ifndef R05_SHOW_DEBUG
#define R05_SHOW_DEBUG 0
#endif  /* ifdef R05_SHOW_DEBUG */


#define EXIT_CODE_RECOGNITION_IMPOSSIBLE 201
#define EXIT_CODE_NO_MEMORY 202
#define EXIT_CODE_BUILTIN_ERROR 203


#ifdef R05_CLOCK_SKIP

static clock_t fast_clock(void) {
  static clock_t prev = 0;
  static int skip = 0;

  if (skip++ % R05_CLOCK_SKIP == 0) {
    prev = clock();
  }

  return prev;
}

#define clock() fast_clock()

#endif  /* ifdef R05_CLOCK_SKIP */


#define STATIC_ASSERT(message, expr) \
  int message : ((expr) ? +1 : -1)

struct static_asserts {
  STATIC_ASSERT(r05_number_is_32bit, sizeof(r05_number) * CHAR_BIT == 32);
};


/*==============================================================================
   Операции сопоставления с образцом
==============================================================================*/


#define equal_functions(left, right) \
  (strcmp((left)->name, (right)->name) == 0)


#define match_symbol_func(type, side, advance, param_type, tag_suf, reader) \
  int r05_ ## type ## _ ## side( \
    cl_iter_t *res, cl_iter_t left, cl_iter_t right, \
    param_type value \
  ) { \
    *res = side = advance(side); \
    \
    return ! cl_iter_eq(left, right) \
      && R05_DATATAG_ ## tag_suf == cl_iter_tag(side) \
      && equal_ ## type ## s(reader(side), value); \
  }


#define match_symbol_funcs(type, param_type, tag_suf, reader) \
  match_symbol_func(type, left,  cl_iter_next, param_type, tag_suf, reader) \
  match_symbol_func(type, right, cl_iter_prev, param_type, tag_suf, reader)


#define equal_chars(x, y) ((x) == (y))
#define equal_numbers(x, y) ((x) == (y))


match_symbol_funcs(function, struct r05_function *, FUNCTION, cl_iter_function);
match_symbol_funcs(char,     char,                  CHAR,     cl_iter_char);
match_symbol_funcs(number,   r05_number,            NUMBER,   cl_iter_number);


int r05_brackets_left(
  cl_iter_t *brackets, cl_iter_t left, cl_iter_t right
) {
  left = cl_iter_next(left);

  if (! cl_iter_eq(left, right) && R05_DATATAG_OPEN_BRACKET == cl_iter_tag(left)) {
    brackets[0] = left;
    brackets[1] = cl_iter_link(left);

    return 1;
  } else {
    return 0;
  }
}


int r05_brackets_right(
  cl_iter_t *brackets, cl_iter_t left, cl_iter_t right
) {
  right = cl_iter_prev(right);

  if (! cl_iter_eq(left, right)
      && R05_DATATAG_CLOSE_BRACKET == cl_iter_tag(right)) {
    brackets[0] = cl_iter_link(right);
    brackets[1] = right;

    return 1;
  } else {
    return 0;
  }
}


#define is_open_bracket(node) \
  (R05_DATATAG_OPEN_BRACKET == cl_iter_tag(node))
#define is_close_bracket(node) \
  (R05_DATATAG_CLOSE_BRACKET == cl_iter_tag(node))

int r05_svar_left(
  cl_iter_t *svar, cl_iter_t left, cl_iter_t right
) {
  *svar = left = cl_iter_next(left);

  return ! cl_iter_eq(left, right) && ! is_open_bracket(left);
}


int r05_svar_right(
  cl_iter_t *svar, cl_iter_t left, cl_iter_t right
) {
  *svar = right = cl_iter_prev(right);

  return ! cl_iter_eq(left, right) && ! is_close_bracket(right);
}


int r05_tvar_left(
  cl_iter_t *tvar, cl_iter_t left, cl_iter_t right
) {
  left = cl_iter_next(left);

  if (cl_iter_eq(left, right)) {
    return 0;
  } else {
    tvar[0] = left;
    tvar[1] = is_open_bracket(left) ? cl_iter_link(left) : left;
    return 1;
  }
}


int r05_tvar_right(
  cl_iter_t *tvar, cl_iter_t left, cl_iter_t right
) {
  right = cl_iter_prev(right);

  if (cl_iter_eq(left, right)) {
    return 0;
  } else {
    tvar[0] = is_close_bracket(right) ? cl_iter_link(right) : right;
    tvar[1] = right;
    return 1;
  }
}


static int equal_nodes(cl_iter_t node1, cl_iter_t node2) {
  if (cl_iter_tag(node1) != cl_iter_tag(node2)) {
    return 0;
  } else {
    switch (cl_iter_tag(node1)) {
      case R05_DATATAG_CHAR:
        return cl_iter_char(node1) == cl_iter_char(node2);

      case R05_DATATAG_NUMBER:
        return cl_iter_number(node1) == cl_iter_number(node2);

      case R05_DATATAG_FUNCTION:
        return equal_functions(cl_iter_function(node1), cl_iter_function(node2));

      case R05_DATATAG_OPEN_BRACKET:
      case R05_DATATAG_CLOSE_BRACKET:
        return 1;

      default:
        r05_switch_default_violation(cl_iter_tag(node1));
#ifndef R05_NORETURN_DEFINED
        return 0;
#endif
    }
  }
}


int r05_repeated_svar_left(
  cl_iter_t *svar, cl_iter_t left, cl_iter_t right,
  cl_iter_t *svar_sample
) {
  *svar = left = cl_iter_next(left);

  return ! cl_iter_eq(left, right) && equal_nodes(left, *svar_sample);
}

int r05_repeated_svar_right(
  cl_iter_t *svar, cl_iter_t left, cl_iter_t right,
  cl_iter_t *svar_sample
) {
  *svar = right = cl_iter_prev(right);

  return ! cl_iter_eq(left, right) && equal_nodes(right, *svar_sample);
}


static void add_match_repeated_tvar_time(clock_t duration);
static void add_match_repeated_evar_time(clock_t duration);

int r05_repeated_tevar_left(
  cl_iter_t *tevar, cl_iter_t left, cl_iter_t right,
  cl_iter_t *tevar_sample, char type
) {
#ifdef R05_SHOW_STAT
  clock_t start_match = clock();
#endif
  cl_iter_t current = cl_iter_next(left);
  cl_iter_t limit = right;
  cl_iter_t cur_sample = tevar_sample[0];
  cl_iter_t limit_sample = cl_iter_next(tevar_sample[1]);

  while (
    ! cl_iter_eq(current, limit) && ! cl_iter_eq(cur_sample, limit_sample)
      && equal_nodes(current, cur_sample)
  ) {
    cur_sample = cl_iter_next(cur_sample);
    current = cl_iter_next(current);
  }

#ifdef R05_SHOW_STAT
  (type == 't' ? add_match_repeated_tvar_time : add_match_repeated_evar_time)(
    clock() - start_match
  );
#else
  (void)type;
#endif

  if (cl_iter_eq(cur_sample, limit_sample)) {
    tevar[0] = cl_iter_next(left);
    tevar[1] = cl_iter_prev(current);
    return 1;
  } else {
    return 0;
  }
}


int r05_repeated_tevar_right(
  cl_iter_t *tevar, cl_iter_t left, cl_iter_t right,
  cl_iter_t *tevar_sample, char type
) {
#ifdef R05_SHOW_STAT
  clock_t start_match = clock();
#endif
  cl_iter_t current = cl_iter_prev(right);
  cl_iter_t limit = left;
  cl_iter_t cur_sample = tevar_sample[1];
  cl_iter_t limit_sample = cl_iter_prev(tevar_sample[0]);

  while (
    ! cl_iter_eq(current, limit) && ! cl_iter_eq(cur_sample, limit_sample)
      && equal_nodes(current, cur_sample)
  ) {
    current = cl_iter_prev(current);
    cur_sample = cl_iter_prev(cur_sample);
  }

#ifdef R05_SHOW_STAT
  (type == 't' ? add_match_repeated_tvar_time : add_match_repeated_evar_time)(
    clock() - start_match
  );
#else
  (void)type;
#endif

  if (cl_iter_eq(cur_sample, limit_sample)) {
    tevar[0] = cl_iter_next(current);
    tevar[1] = cl_iter_prev(right);
    return 1;
  } else {
    return 0;
  }
}


int r05_open_evar_advance(cl_iter_t *evar, cl_iter_t right) {
  cl_iter_t term[2];

  if (r05_tvar_left(term, evar[1], right)) {
    evar[1] = term[1];
    return 1;
  } else {
    return 0;
  }
}


size_t r05_read_chars(
  cl_iter_t *char_interval, char buffer[], size_t buflen,
  cl_iter_t left, cl_iter_t right
) {
  size_t nread = 0;
  cl_iter_t cur = char_interval[0] = cl_iter_next(left);
  while (nread < buflen && ! cl_iter_eq(cur, right)
         && R05_DATATAG_CHAR == cl_iter_tag(cur)) {
    buffer[nread] = cl_iter_char(cur);
    ++nread;
    cur = cl_iter_next(cur);
  }

  char_interval[1] = cl_iter_prev(cur);
  return nread;
}


/*==============================================================================
   Распределитель памяти
==============================================================================*/

static void make_dump(void);

#ifndef R05_COMPACT_HANDLES

static struct r05_node s_end_free_list;

static struct r05_node s_begin_free_list = {
  0, &s_end_free_list, R05_DATATAG_ILLEGAL, { '\0' }
};
static struct r05_node s_end_free_list = {
  &s_begin_free_list, 0, R05_DATATAG_ILLEGAL, { '\0' }
};

static struct r05_node *s_free_ptr = &s_end_free_list;

static size_t s_memory_use = 0;


enum { CHUNK_SIZE = 251 };

struct memory_chunk {
  struct r05_node elems[CHUNK_SIZE];
  struct memory_chunk *next;
};

static struct memory_chunk *s_pool = NULL;


static void weld(struct r05_node *left, struct r05_node *right) {
  assert(left != 0 && right != 0);

  left->next = right;
  right->prev = left;
}


static int create_nodes(void) {
  size_t i;
  struct memory_chunk *chunk;

#ifdef R05_MEMORY_LIMIT
  if (s_memory_use >= R05_MEMORY_LIMIT) {
    return 0;
  }
#endif  /* ifdef R05_MEMORY_LIMIT */

  chunk = malloc(sizeof(*chunk));

  if (chunk == 0) {
    return 0;
  }

  chunk->next = s_pool;
  s_pool = chunk;

  for (i = 0; i < CHUNK_SIZE - 1; ++i) {
    chunk->elems[i].next = &chunk->elems[i + 1];
    chunk->elems[i + 1].prev = &chunk->elems[i];
    chunk->elems[i].tag = R05_DATATAG_ILLEGAL;
  }
  chunk->elems[CHUNK_SIZE - 1].tag = R05_DATATAG_ILLEGAL;

  weld(s_end_free_list.prev, &chunk->elems[0]);
  weld(&chunk->elems[CHUNK_SIZE - 1], &s_end_free_list);

  s_free_ptr = &chunk->elems[0];
  s_memory_use += CHUNK_SIZE;

  return 1;
}


static void ensure_memory(void) {
  if ((s_free_ptr == &s_end_free_list) && ! create_nodes()) {
    fprintf(stderr, "\nNO MEMORY\n\n");
    make_dump();

    r05_exit(EXIT_CODE_NO_MEMORY);
  }
}


static void free_memory(void) {
  while (s_pool != 0) {
    struct memory_chunk *next = s_pool->next;
    free(s_pool);
    s_pool = next;
  }

#ifdef R05_SHOW_STAT
  fprintf(
    stderr,
    "Memory used %lu nodes, %lu * %lu = %lu bytes\n",
    (unsigned long int) s_memory_use,
    (unsigned long int) s_memory_use,
    (unsigned long int) sizeof(struct r05_node),
    (unsigned long int) (s_memory_use * sizeof(struct r05_node))
  );
#endif  /* R05_SHOW_STAT */
}

#else

static compact_runtime_storage_t s_compact_storage;
static int s_compact_storage_initialized = 0;
static cl_iter_t *s_call_stack = NULL;
static size_t s_call_stack_size = 0;
static size_t s_call_stack_capacity = 0;


static void ensure_compact_storage(void) {
  if (! s_compact_storage_initialized) {
    cl_iter_table_init();
    crs_init(&s_compact_storage);
    s_compact_storage_initialized = 1;
  }
}


static void free_memory(void) {
  if (s_compact_storage_initialized) {
    crs_destroy(&s_compact_storage);
    cl_iter_table_free();
    s_compact_storage_initialized = 0;
  }
  free(s_call_stack);
  s_call_stack = NULL;
  s_call_stack_size = 0;
  s_call_stack_capacity = 0;
}

#endif  /* R05_COMPACT_HANDLES */


/*==============================================================================
   Операции построения результата
==============================================================================*/

static void start_building_result(void);

void r05_reset_allocator(void) {
  start_building_result();
#ifdef R05_COMPACT_HANDLES
  ensure_compact_storage();
  crs_reset_build(&s_compact_storage);
#else
  s_free_ptr = s_begin_free_list.next;
#endif
}


cl_iter_t r05_alloc_node(enum r05_datatag tag) {
#ifdef R05_COMPACT_HANDLES
  return crs_alloc_node(&s_compact_storage, tag);
#else
  struct r05_node *node;

  ensure_memory();
  node = s_free_ptr;
  s_free_ptr = s_free_ptr->next;
  node->tag = tag;
  return node;
#endif
}


cl_iter_t r05_insert_pos(void) {
#ifdef R05_COMPACT_HANDLES
  return crs_insert_pos(&s_compact_storage);
#else
  ensure_memory();
  return s_free_ptr;
#endif
}


#ifdef R05_COMPACT_HANDLES
void r05_alloc_char(char ch) {
  cm_item_t item = { 0 };
  item.tag = R05_DATATAG_CHAR;
  item.info.char_ = ch;
  crs_alloc_item(&s_compact_storage, item);
}


void r05_alloc_number(r05_number num) {
  cm_item_t item = { 0 };
  item.tag = R05_DATATAG_NUMBER;
  item.info.number = num;
  crs_alloc_item(&s_compact_storage, item);
}


void r05_alloc_function(struct r05_function *func) {
  cm_item_t item = { 0 };
  item.tag = R05_DATATAG_FUNCTION;
  item.info.function = func;
  crs_alloc_item(&s_compact_storage, item);
}
#endif


#ifndef R05_COMPACT_HANDLES
static void list_splice(
  struct r05_node *res, struct r05_node *begin, struct r05_node *end
) {
  assert ((begin == 0) == (end == 0));

  if (begin != 0) {
    struct r05_node *prev_res = res->prev;
    struct r05_node *prev_begin = begin->prev;
    struct r05_node *next_end = end->next;

    weld(prev_res, begin);
    weld(end, res);
    weld(prev_begin, next_end);
  }
}
#endif


static void add_copy_tevar_time(clock_t duration);

void r05_alloc_tevar(cl_iter_t *sample) {
  cl_iter_t p, limit;
#ifdef R05_SHOW_STAT
  clock_t start_copy_time = clock();
#endif

#ifdef R05_COMPACT_HANDLES
  cl_iter_t *bracket_stack = NULL;
  size_t bracket_stack_size = 0;
  size_t bracket_stack_capacity = 0;
#else
  cl_iter_t bracket_stack = CL_ITER_NULL;
#endif

  for (p = sample[0], limit = cl_iter_next(sample[1]);
       ! cl_iter_eq(p, limit);
       p = cl_iter_next(p)) {
    cl_iter_t copy = r05_alloc_node(cl_iter_tag(p));

    if (is_open_bracket(copy)) {
#ifdef R05_COMPACT_HANDLES
      if (bracket_stack_size == bracket_stack_capacity) {
        size_t new_capacity = bracket_stack_capacity == 0
          ? 16 : 2 * bracket_stack_capacity;
        cl_iter_t *grown = realloc(
          bracket_stack, new_capacity * sizeof(bracket_stack[0])
        );
        if (grown == NULL) {
          r05_builtin_error("bracket stack allocation failed");
        }
        bracket_stack = grown;
        bracket_stack_capacity = new_capacity;
      }
      bracket_stack[bracket_stack_size++] = copy;
#else
      cl_iter_set_link(copy, bracket_stack);
      bracket_stack = copy;
#endif
    } else if (is_close_bracket(copy)) {
#ifdef R05_COMPACT_HANDLES
      cl_iter_t open_cobracket;
      assert(bracket_stack_size > 0);
      open_cobracket = bracket_stack[--bracket_stack_size];
#else
      cl_iter_t open_cobracket = bracket_stack;

      assert(! cl_iter_is_null(bracket_stack));
      bracket_stack = cl_iter_link(bracket_stack);
#endif
      r05_link_brackets(open_cobracket, copy);
    } else {
      switch (cl_iter_tag(p)) {
        case R05_DATATAG_CHAR:
          cl_iter_set_char(copy, cl_iter_char(p));
          break;

        case R05_DATATAG_NUMBER:
          cl_iter_set_number(copy, cl_iter_number(p));
          break;

        case R05_DATATAG_FUNCTION:
          cl_iter_set_function(copy, cl_iter_function(p));
          break;

        default:
          break;
      }
    }
  }

#ifdef R05_COMPACT_HANDLES
  assert(bracket_stack_size == 0);
  free(bracket_stack);
#else
  assert(cl_iter_is_null(bracket_stack));
#endif

#ifdef R05_SHOW_STAT
  add_copy_tevar_time(clock() - start_copy_time);
#endif
}


void r05_alloc_chars(const char buffer[], size_t len) {
#ifdef R05_COMPACT_HANDLES
  crs_alloc_chars(&s_compact_storage, buffer, len);
#else
  size_t i;
  for (i = 0; i < len; ++i) {
    r05_alloc_char(buffer[i]);
  }
#endif
}


void r05_alloc_string(const char *string) {
  for (/* пусто */; *string != '\0'; ++string) {
    r05_alloc_char(*string);
  }
}


#ifndef R05_COMPACT_HANDLES
static cl_iter_t s_stack_ptr;
#endif

#ifdef R05_COMPACT_HANDLES
static void compact_trim_call_stack(void) {
  size_t new_capacity;
  cl_iter_t *trimmed;

  if (s_call_stack_size == 0) {
    free(s_call_stack);
    s_call_stack = NULL;
    s_call_stack_capacity = 0;
    return;
  }

  new_capacity = s_call_stack_size < 8 ? 8 : s_call_stack_size;
  if (new_capacity >= s_call_stack_capacity) {
    return;
  }

  trimmed = (cl_iter_t *)realloc(
    s_call_stack, new_capacity * sizeof(s_call_stack[0])
  );
  if (trimmed != NULL) {
    s_call_stack = trimmed;
    s_call_stack_capacity = new_capacity;
  }
}
#endif

void r05_push_stack(cl_iter_t call_bracket) {
#ifdef R05_COMPACT_HANDLES
  if (s_call_stack_size == s_call_stack_capacity) {
    size_t new_capacity = s_call_stack_capacity == 0
      ? 8 : 2 * s_call_stack_capacity;
    cl_iter_t *grown = realloc(
      s_call_stack, new_capacity * sizeof(s_call_stack[0])
    );
    if (grown == NULL) {
      r05_builtin_error("call stack allocation failed");
    }
    s_call_stack = grown;
    s_call_stack_capacity = new_capacity;
  }
  cl_iter_escape(call_bracket);
  s_call_stack[s_call_stack_size++] = call_bracket;
#ifdef R05_TRACE_STACK
  fprintf(
    stderr, "STACK push %u tag=%d depth=%lu\n",
    call_bracket.handle, (int) cl_iter_tag(call_bracket),
    (unsigned long) s_call_stack_size
  );
#endif
#else
  cl_iter_set_link(call_bracket, s_stack_ptr);
  s_stack_ptr = call_bracket;
#endif
}


void r05_link_brackets(cl_iter_t left, cl_iter_t right) {
  cl_iter_set_link(left, right);
  cl_iter_set_link(right, left);
}


void r05_correct_evar(cl_iter_t *evar) {
  if (cl_iter_eq(cl_iter_next(evar[1]), evar[0])) {
    evar[0] = CL_ITER_NULL;
    evar[1] = CL_ITER_NULL;
  }
}


void r05_splice_tevar(cl_iter_t res, cl_iter_t *tevar) {
#ifdef R05_COMPACT_HANDLES
  if (! cl_iter_is_null(tevar[0])) {
    crs_splice_before(res, tevar[0], tevar[1]);
  }
#else
  list_splice(res, tevar[0], tevar[1]);
#endif
}


void r05_splice_to_freelist(cl_iter_t begin, cl_iter_t end) {
#ifdef R05_COMPACT_HANDLES
  crs_remove_range(begin, end);
#else
  list_splice(s_free_ptr, begin, end);
#endif
}


void r05_splice_from_freelist(cl_iter_t pos) {
#ifdef R05_COMPACT_HANDLES
  crs_splice_from_build(&s_compact_storage, pos);
#else
  if (s_free_ptr != s_begin_free_list.next) {
    list_splice(pos, s_begin_free_list.next, s_free_ptr->prev);
  }
#endif
}


void r05_enum_function_code(cl_iter_t begin, cl_iter_t end) {
  (void) begin;
  (void) end;
  r05_recognition_impossible();
}


/*==============================================================================
   Внутренний профилировщик
==============================================================================*/

static clock_t s_start_program_time;
static clock_t s_start_pattern_match_time;
static clock_t s_total_pattern_match_time;
static clock_t s_start_building_result_time;
static clock_t s_total_building_result_time;
static clock_t s_total_copy_tevar_time;
static clock_t s_total_match_repeated_tvar_time;
static clock_t s_total_match_repeated_evar_time;
static clock_t s_start_e_loop;
static clock_t s_total_e_loop;
static clock_t s_total_match_repeated_tvar_time_outside_e;
static clock_t s_total_match_repeated_evar_time_outside_e;


static int s_in_generated;
static int s_in_e_loop;
static unsigned long s_step_counter = 0;

#ifdef R05_COMPACT_LIST
static size_t s_compact_peak_nodes  = 0;
static size_t s_compact_total_nodes = 0;
static size_t s_compact_step_count  = 0;

static void compact_track_step(void);
#endif  /* R05_COMPACT_LIST */

#ifndef R05_STAT_SAMPLE_PERIOD
#define R05_STAT_SAMPLE_PERIOD 1000
#endif  /* R05_STAT_SAMPLE_PERIOD */


#ifdef R05_COMPACT_HANDLES
static size_t s_compact_peak_total_bytes = 0;
static size_t s_compact_peak_lists_bytes = 0;
static size_t s_compact_peak_classic_bytes = 0;
static size_t s_compact_peak_elements = 0;

static void compact_track_actual_step(void) {
  cm_stats_t view = cm_list_stats(&s_compact_storage.view);
  cm_stats_t build = cm_list_stats(&s_compact_storage.build);
  cm_stats_t buried = cm_list_stats(&s_compact_storage.buried);
  size_t lists_bytes = view.compact_bytes
    + build.compact_bytes + buried.compact_bytes;
  size_t total_bytes = lists_bytes
    + cl_iter_table_capacity_bytes()
    + s_call_stack_capacity * sizeof(s_call_stack[0]);

  if (total_bytes > s_compact_peak_total_bytes) {
    s_compact_peak_total_bytes = total_bytes;
    s_compact_peak_lists_bytes = lists_bytes;
    s_compact_peak_classic_bytes = view.classic_bytes
      + build.classic_bytes + buried.classic_bytes;
    s_compact_peak_elements = (size_t) view.total_elements
      + (size_t) build.total_elements + (size_t) buried.total_elements;
  }
}
#endif  /* R05_COMPACT_HANDLES */


#ifdef R05_PROFILER
static struct r05_function *s_profiled_functions;
#endif  /* R05_PROFILER */


static void start_profiler(void) {
  s_start_program_time = clock();
#ifdef R05_SHOW_STAT
  s_in_generated = 0;
#endif
}


static void start_building_result(void) {
#ifdef R05_SHOW_STAT
  if (s_in_generated) {
    clock_t pattern_match;

    s_start_building_result_time = clock();
    pattern_match = s_start_building_result_time - s_start_pattern_match_time;
    s_total_pattern_match_time += pattern_match;

    if (s_in_e_loop > 0) {
      s_total_e_loop += (s_start_building_result_time - s_start_e_loop);
      s_in_e_loop = 0;
    }
  }
#endif
}


static void after_step(void) {
#ifdef R05_SHOW_STAT
  if (s_in_generated) {
    clock_t building_result = clock() - s_start_building_result_time;
    s_total_building_result_time += building_result;
  }

  assert(s_in_e_loop == 0);

  s_in_generated = 0;
  s_in_e_loop = 0;
#endif

#if defined(R05_COMPACT_LIST) && defined(R05_SHOW_STAT)
  if (s_step_counter % R05_STAT_SAMPLE_PERIOD == 0) {
    compact_track_step();
  }
#endif  /* R05_COMPACT_LIST && R05_SHOW_STAT */
#if defined(R05_COMPACT_HANDLES) && defined(R05_COMPACT_AUTO_COMPACT)
  static int compact_skip = 0;
  if (++compact_skip % 10 == 0) {
    cm_compact_list(&s_compact_storage.view);
    cm_compact_list(&s_compact_storage.build);
    cm_compact_list(&s_compact_storage.buried);
    compact_trim_call_stack();
  }
#endif  /* R05_COMPACT_HANDLES && R05_COMPACT_AUTO_COMPACT */
#if defined(R05_COMPACT_HANDLES) && defined(R05_SHOW_STAT)
  {
    static int compact_stat_skip = 0;
    if (++compact_stat_skip % R05_STAT_SAMPLE_PERIOD == 0) {
    compact_track_actual_step();
    }
  }
#endif  /* R05_COMPACT_HANDLES && R05_SHOW_STAT */
}


static void add_copy_tevar_time(clock_t duration) {
  s_total_copy_tevar_time += duration;
}

static void add_match_repeated_tvar_time(clock_t duration) {
  if (s_in_e_loop) {
    s_total_match_repeated_tvar_time += duration;
  } else {
    s_total_match_repeated_tvar_time_outside_e += duration;
  }
}

static void add_match_repeated_evar_time(clock_t duration) {
  if (s_in_e_loop) {
    s_total_match_repeated_evar_time += duration;
  } else {
    s_total_match_repeated_evar_time_outside_e += duration;
  }
}

#ifdef R05_SHOW_STAT
struct time_item {
  const char *name;
  clock_t counter;
};

static int reverse_compare(const void *left_void, const void *right_void) {
  const struct time_item *left = left_void;
  const struct time_item *right = right_void;

  if (left->counter > right->counter) {
    return -1;
  } else if (left->counter < right->counter) {
    return +1;
  } else {
    return 0;
  }
}

#ifdef R05_PROFILER
static void print_functions_profile(double full_time_sec);
#endif  /* R05_PROFILER */

static void print_profile(void) {
  const double cfSECS_PER_CLOCK = 1.0 / CLOCKS_PER_SEC;

  clock_t full_time;
  clock_t refal_time;
  clock_t repeated_time;
  clock_t eloop_time;
  clock_t repeated_time_outside_e;

  enum { nItems = 11 };
  struct time_item items[nItems];

  size_t i;

  full_time = clock() - s_start_program_time;
  refal_time = s_total_pattern_match_time + s_total_building_result_time;
  repeated_time =
    s_total_match_repeated_tvar_time + s_total_match_repeated_evar_time;
  eloop_time = s_total_e_loop - repeated_time;
  repeated_time_outside_e =
    s_total_match_repeated_tvar_time_outside_e
    + s_total_match_repeated_evar_time_outside_e;

  /*
    Ложное предупреждение BCC 5.5:
    компилятор не допускает инициализацию структур и массивов переменными.
  */
  items[0].name = "\nTotal program time";
  items[0].counter = full_time;
  items[1].name = "Builtin time";
  items[1].counter = full_time - refal_time;
  items[2].name = "(Total refal time)";
  items[2].counter = refal_time;
  items[3].name = "Linear pattern time";
  items[3].counter = s_total_pattern_match_time
    - (eloop_time + repeated_time + repeated_time_outside_e);
  items[4].name = "Linear result time";
  items[4].counter = s_total_building_result_time - s_total_copy_tevar_time;
  items[5].name = "Open e-loop time (clear)";
  items[5].counter = eloop_time;
  items[6].name = "Repeated e-var match time (inside e-loops)";
  items[6].counter = s_total_match_repeated_evar_time;
  items[7].name = "Repeated e-var match time (outside e-loops)";
  items[7].counter = s_total_match_repeated_tvar_time_outside_e;
  items[8].name = "Repeated t-var match time (inside e-loops)";
  items[8].counter = s_total_match_repeated_tvar_time;
  items[9].name = "Repeated t-var match time (outside e-loops)";
  items[9].counter = s_total_match_repeated_tvar_time_outside_e;
  items[10].name = "t- and e-var copy time";
  items[10].counter = s_total_copy_tevar_time;

  qsort(items, nItems, sizeof(items[0]), reverse_compare);

  for (i = 0; i < nItems; ++i) {
    unsigned long value = items[i].counter;

    if (value > 0) {
      double percent = (full_time != 0) ? 100.0 * value / full_time : 0.0;
      fprintf(
        stderr, "%s: %.3f seconds (%.1f %%).\n",
        items[i].name, value * cfSECS_PER_CLOCK, percent
      );
    }
  }

#ifdef R05_PROFILER
  print_functions_profile(full_time * cfSECS_PER_CLOCK);
#endif  /* R05_PROFILER */
}

#ifdef R05_PROFILER
static unsigned long s_step_counter;

static void print_functions_profile(double full_time_sec) {
  double mean_step_time = full_time_sec / s_step_counter;
  struct r05_function *func, *sorted = NULL;
  FILE *profile;
  double increment = 0;
  int no = 1;

  while (s_profiled_functions != NULL) {
    struct r05_function **parent;

    func = s_profiled_functions;
    s_profiled_functions = func->next;
    parent = &sorted;
    while (*parent != NULL && (*parent)->seconds > func->seconds) {
      parent = &(*parent)->next;
    }
    func->next = *parent;
    *parent = func;
  }

  s_profiled_functions = sorted;

  profile = fopen("__profile-05.txt", "w");
  if (profile == NULL) {
    fprintf(stderr, "Can't open '__profile-05.txt' for writting.\n");
    fprintf(stderr, "Profile will be written to stderr.\n\n");
    profile = stderr;
  }

  fprintf(profile, "Total steps: %lu\n", s_step_counter);
  fprintf(profile, "Total time: %.3f secs\n", full_time_sec);
  fprintf(profile, "Mean step time: %.3f us\n\n", mean_step_time * 1e6);
  for (
    func = s_profiled_functions;
    func != NULL && func->seconds > 0;
    func = func->next, no++
  ) {
    double percent = func->seconds / full_time_sec * 100.0;
    increment += percent;
    fprintf(
      profile,
      "%3d. %-45s %10.3f ms (%6.2f %% += %6.2f %%), %10lu calls, %10.3f steps\n",
      no, func->name, func->seconds * 1e3, percent, increment,
      func->calls, func->seconds / func->calls / mean_step_time
    );
  }
  if (profile != stderr) {
    fclose(profile);
  }
}
#endif  /* R05_PROFILER */

#endif  /* R05_SHOW_STAT */

static void end_profiler(void) {
#ifdef R05_SHOW_STAT
  after_step();
  print_profile();
#endif  /* R05_SHOW_STAT */
}


void r05_start_e_loop(void) {
#ifdef R05_SHOW_STAT
  assert(s_in_generated);

  if (s_in_e_loop++ == 0) {
    s_start_e_loop = clock();
  }
#endif
}


void r05_this_is_generated_function(void) {
#ifdef R05_SHOW_STAT
  s_start_pattern_match_time = clock();
  s_in_generated = 1;
#endif
}


void r05_stop_e_loop(void) {
#ifdef R05_SHOW_STAT
  assert(s_in_generated);

  if (--s_in_e_loop == 0) {
    s_total_e_loop += (clock() - s_start_e_loop);
  }
#endif
}


double r05_time_elapsed(void) {
  return (clock() - s_start_program_time) / (double) CLOCKS_PER_SEC;
}


/*==============================================================================
   Рефал-машина
==============================================================================*/


#ifndef R05_COMPACT_HANDLES
static struct r05_node s_end_view_field;

static struct r05_node s_begin_view_field = {
  0, &s_end_view_field, R05_DATATAG_ILLEGAL, { '\0' }
};
static struct r05_node s_end_view_field = {
  &s_begin_view_field, 0, R05_DATATAG_ILLEGAL, { '\0' }
};
#endif

#ifdef R05_COMPACT_LIST
static void compact_track_step(void) {
  size_t n = 0;
#ifdef R05_COMPACT_HANDLES
  cm_iter_t p;
  cm_iter_t end = cm_list_end(&s_compact_storage.view);
  for (p = cm_list_begin(&s_compact_storage.view);
       ! cm_iter_eq(p, end);
       p = cm_iter_next(p)) {
    n++;
  }
#else
  cl_iter_t p;
  for (p = cl_iter_next(&s_begin_view_field);
       ! cl_iter_eq(p, &s_end_view_field);
       p = cl_iter_next(p)) {
    n++;
  }
#endif
  if (n > s_compact_peak_nodes) {
    s_compact_peak_nodes = n;
  }
  s_compact_total_nodes += n;
  s_compact_step_count++;
}
#endif  /* R05_COMPACT_LIST */


static cl_iter_t pop_stack(void) {
#ifdef R05_COMPACT_HANDLES
  cl_iter_t result;
  assert(s_call_stack_size > 0);
  result = s_call_stack[--s_call_stack_size];
#ifdef R05_TRACE_STACK
  fprintf(
    stderr, "STACK pop  %u tag=%d depth=%lu\n",
    result.handle, (int) cl_iter_tag(result),
    (unsigned long) s_call_stack_size
  );
#endif
  return result;
#else
  cl_iter_t res = s_stack_ptr;
  s_stack_ptr = cl_iter_link(s_stack_ptr);
  return res;
#endif
}

static int empty_stack(void) {
#ifdef R05_COMPACT_HANDLES
  return s_call_stack_size == 0;
#else
  return cl_iter_is_null(s_stack_ptr);
#endif
}


extern struct r05_function r05f_GO;

static void init_view_field(void) {
  cl_iter_t open, close;

  r05_reset_allocator();
  r05_alloc_open_call(&open);
  r05_alloc_function(&r05f_GO);
  r05_alloc_close_call(&close);
  r05_push_stack(close);
  r05_push_stack(open);
#ifdef R05_COMPACT_HANDLES
  r05_splice_from_freelist(crs_view_end(&s_compact_storage));
#else
  r05_splice_from_freelist(s_begin_view_field.next);
#endif
}

static cl_iter_t s_arg_begin;
static cl_iter_t s_arg_end;

static void main_loop(void) {
#ifdef R05_PROFILER
  clock_t start_step = clock(), now;
#endif  /* R05_PROFILER */

  while (! empty_stack()) {
    cl_iter_t function;
    struct r05_function *callee;

    s_arg_begin = pop_stack();
    assert(! empty_stack());
    s_arg_end = pop_stack();

#if R05_SHOW_DEBUG
    if (s_step_counter >= (unsigned long) R05_SHOW_DEBUG) {
      make_dump();
    }
#endif  /* R05_SHOW_DEBUG */

    function = cl_iter_next(s_arg_begin);
    if (R05_DATATAG_FUNCTION == cl_iter_tag(function)) {
      callee = cl_iter_function(function);
#ifdef R05_TRACE_PROGRESS
      fprintf(stderr, "step %lu enter %s\n", s_step_counter, callee->name);
#endif
#ifdef R05_TRACE_STACK
      fprintf(stderr, "STACK call %s\n", callee->name);
#endif
#ifdef R05_COMPACT_HANDLES
      cl_iter_frame_begin();
#endif
      (callee->ptr)(s_arg_begin, s_arg_end);
#ifdef R05_TRACE_PROGRESS
      fprintf(stderr, "step %lu leave %s\n", s_step_counter, callee->name);
#endif
#ifdef R05_COMPACT_HANDLES
      cl_iter_frame_end();
#endif
    } else {
      r05_recognition_impossible();
    }
    after_step();

#ifdef R05_PROFILER
    now = clock();
    if (callee->next == 0) {
      callee->next = s_profiled_functions;
      s_profiled_functions = callee;
    }
    callee->seconds += (now - start_step) / (double) CLOCKS_PER_SEC;
    callee->calls += 1;
    start_step = now;
#endif  /* R05_PROFILER */

    ++ s_step_counter;
#ifdef R05_TRACE_PROGRESS
    if (s_step_counter % 1000 == 0) {
      fprintf(stderr, "progress: %lu steps\n", s_step_counter);
    }
#endif
  }
}


static void print_indent(int level) {
  enum { cPERIOD = 4 };
  int i;

  putc('\n', stderr);

  if (level < 0) {
    putc('!', stderr);
    return;
  }

  for (i = 0; i < level; ++i) {
    /* Каждые cPERIOD позиций вместо пробела ставим точку. */
    int put_marker = ((i % cPERIOD) == (cPERIOD - 1));

    const char cSpace =  ' ';
    const char cMarker = '.';

    putc((put_marker ? cMarker : cSpace), stderr);
  }
}


static int is_ident_name(const char *name) {
#define in_range(min, c, max) ((min) <= (c) && (c) <= (max))

  if (! in_range('a', *name, 'z') && ! in_range('A', *name, 'Z')) {
    return 0;
  }

  while (
    *name != 0
    && (
      in_range('a', *name, 'z') || in_range('A', *name, 'Z')
      || in_range('0', *name, '9') || *name == '_' || *name == '-'
    )
  ) {
    ++name;
  }

  return *name == 0;
#undef in_range
}

static const char escapes[][2] = {
  { '\t', 't' }, { '\n', 'n' }, { '\r', 'r' }, { '"', '"' },
  { '\'', '\'' }, { '(', '(' }, { ')', ')' }, { '<', '<' },
  { '>', '>' }, { '\\', '\\' }, { '\0', '\0' },
};


static void print_seq(cl_iter_t begin, cl_iter_t end) {
  enum {
    cStateView = 100,
    cStateString,
    cStateFinish
  } state = cStateView;

  int indent = 0;
  int after_bracket = 0;
  int reset_after_bracket = 1;

  while ((state != cStateFinish) && ! cl_iter_eq(cl_iter_next(end), begin)) {
    if (reset_after_bracket) {
      after_bracket = 0;
      reset_after_bracket = 0;
    }

    if (after_bracket) {
      reset_after_bracket = 1;
    }

    switch (state) {
      case cStateView:
        switch (cl_iter_tag(begin)) {
          case R05_DATATAG_ILLEGAL:
            if (cl_iter_is_null(cl_iter_prev(begin))) {
              fprintf(stderr, "[FIRST] ");
            } else if (cl_iter_is_null(cl_iter_next(begin))) {
              fprintf(stderr, "\n[LAST]");
              state = cStateFinish;
            } else {
              fprintf(stderr, "\n[NONE]");
            }
            begin = cl_iter_next(begin);
            continue;

          case R05_DATATAG_CHAR:
            state = cStateString;
            fprintf(stderr, "\'");
            continue;

          case R05_DATATAG_NUMBER:
            fprintf(stderr, "%u ", cl_iter_number(begin));
            begin = cl_iter_next(begin);
            continue;

          case R05_DATATAG_FUNCTION:
            if (is_ident_name(cl_iter_function(begin)->name)) {
              fprintf(stderr, "%s ", cl_iter_function(begin)->name);
            } else {
              const char *p;
              fprintf(stderr, "\"");
              for (p = cl_iter_function(begin)->name; *p != 0; ++p) {
                size_t i = 0;
                while (escapes[i][0] != '\0' && escapes[i][0] != *p) {
                  ++i;
                }
                if (escapes[i][0] != '\0') {
                  fprintf(stderr, "\\%c", escapes[i][1]);
                } else if (' ' <= *p && *p < 127) {
                  fprintf(stderr, "%c", *p);
                } else {
                  fprintf(stderr, "\\x%02X", *p);
                }
              }
              fprintf(stderr, "\" ");
            }
            begin = cl_iter_next(begin);
            continue;

          case R05_DATATAG_OPEN_BRACKET:
            if (! after_bracket) {
              print_indent(indent);
            }
            ++indent;
            after_bracket = 1;
            reset_after_bracket = 0;
            fprintf(stderr, "(");
            begin = cl_iter_next(begin);
            continue;

          case R05_DATATAG_CLOSE_BRACKET:
            --indent;
            fprintf(stderr, ")");
            begin = cl_iter_next(begin);
            continue;

          case R05_DATATAG_OPEN_CALL:
            if (! after_bracket) {
              print_indent(indent);
            }
            ++indent;
            after_bracket = 1;
            reset_after_bracket = 0;
            fprintf(stderr, "<");
            begin = cl_iter_next(begin);
            continue;

          case R05_DATATAG_CLOSE_CALL:
            --indent;
            fprintf(stderr, ">");
            begin = cl_iter_next(begin);
            continue;

          default:
            r05_switch_default_violation(cl_iter_tag(begin));
        }

      case cStateString:
        switch (cl_iter_tag(begin)) {
          case R05_DATATAG_CHAR: {
            unsigned char ch = cl_iter_char(begin);
            switch (ch) {
              case '(': case ')':
              case '<': case '>':
                fprintf(stderr, "\\%c", ch);
                break;

              case '\n':
                fprintf(stderr, "\\n");
                break;

              case '\t':
                fprintf(stderr, "\\t");
                break;

              case '\\':
                fprintf(stderr, "\\\\");
                break;

              case '\'':
                fprintf(stderr, "\\\'");
                break;

              default:
                if (ch < '\x20') {
                  fprintf(stderr, "\\x%02x", ch);
                } else {
                  fprintf(stderr, "%c", ch);
                }
                break;
              }
              begin = cl_iter_next(begin);
              continue;
            }

          default:
            state = cStateView;
            fprintf(stderr, "\' ");
            continue;
        }

      case cStateFinish:
        continue;

      default:
        r05_switch_default_violation(state);
    }
  }

  if (cStateString == state) {
    fprintf(stderr, "\'");
  }
}


static void dump_buried(void);

static void make_dump(void) {
  fprintf(stderr, "\nSTEP NUMBER %lu\n", s_step_counter);
  fprintf(stderr, "\nPRIMARY ACTIVE EXPRESSION:\n");
  print_seq(s_arg_begin, s_arg_end);
  fprintf(stderr, "\nVIEW FIELD:\n");
#ifdef R05_COMPACT_HANDLES
  {
    cl_iter_t begin = crs_view_begin(&s_compact_storage);
    cl_iter_t tail = crs_view_end(&s_compact_storage);
    if (! cl_iter_eq(begin, tail)) {
      print_seq(begin, cl_iter_prev(tail));
    }
  }
#else
  print_seq(&s_begin_view_field, &s_end_view_field);
#endif

  dump_buried();

#ifdef R05_DUMP_FREE_LIST
  fprintf(stderr, "\nFREE LIST:\n");
  print_seq(&s_begin_free_list, &s_end_free_list);
#endif  /* ifdef R05_DUMP_FREE_LIST */

  fprintf(stderr,"\nEnd dump\n");
  fflush(stderr);
}


#ifdef R05_COMPACT_LIST
static size_t compact_realistic_bytes(size_t n) {
  size_t cap;
  if (n == 0) {
    return 2 * sizeof(cm_macronode_t);
  }
  cap = CM_MIN_BUCKET;
  while (cap < n) cap *= 2;
  return sizeof(cm_macronode_t) + cap * sizeof(cm_item_t)
       + 2 * sizeof(cm_macronode_t);
}

static void r05_compact_view_field_stats(void) {
  size_t peak   = s_compact_peak_nodes;
  size_t steps  = s_compact_step_count;
  size_t avg    = (steps > 0) ? (s_compact_total_nodes / steps) : 0;

  size_t classic_peak  = peak * sizeof(struct r05_node)
                       + 2 * sizeof(struct r05_node);
  size_t ideal_peak    = sizeof(cm_macronode_t) + peak * sizeof(cm_item_t)
                       + 2 * sizeof(cm_macronode_t);
  size_t realistic_peak = compact_realistic_bytes(peak);

  size_t classic_avg   = avg * sizeof(struct r05_node)
                       + 2 * sizeof(struct r05_node);
  size_t ideal_avg     = sizeof(cm_macronode_t) + avg * sizeof(cm_item_t)
                       + 2 * sizeof(cm_macronode_t);
  size_t realistic_avg = compact_realistic_bytes(avg);

  fprintf(stderr,
    "Compact list representation stats:\n"
    "  sizeof(r05_node)       = %lu bytes  (prev+next+tag+info)\n"
    "  sizeof(cm_item_t)      = %lu bytes  (tag+info, no pointers)\n"
    "  sizeof(cm_macronode_t) = %lu bytes  (header overhead per macronode)\n"
    "  CM_MIN_BUCKET          = %d  (reserve policy)\n"
    "  Steps measured         : %lu\n"
    "  Peak view field        : %lu nodes\n"
    "    classic              : %lu B\n"
    "    compact (ideal)      : %lu B  (saved %+ld B, %d%%)\n"
    "    compact (realistic)  : %lu B  (saved %+ld B, %d%%)\n"
    "  Avg  view field        : %lu nodes\n"
    "    classic              : %lu B\n"
    "    compact (ideal)      : %lu B  (saved %+ld B, %d%%)\n"
    "    compact (realistic)  : %lu B  (saved %+ld B, %d%%)\n",
    (unsigned long)sizeof(struct r05_node),
    (unsigned long)sizeof(cm_item_t),
    (unsigned long)sizeof(cm_macronode_t),
    CM_MIN_BUCKET,
    (unsigned long)steps,
    (unsigned long)peak,
    (unsigned long)classic_peak,
    (unsigned long)ideal_peak,
    (long)classic_peak - (long)ideal_peak,
    classic_peak > 0 ? (int)(100 - 100 * ideal_peak / classic_peak) : 0,
    (unsigned long)realistic_peak,
    (long)classic_peak - (long)realistic_peak,
    classic_peak > 0 ? (int)(100 - 100 * realistic_peak / classic_peak) : 0,
    (unsigned long)avg,
    (unsigned long)classic_avg,
    (unsigned long)ideal_avg,
    (long)classic_avg - (long)ideal_avg,
    classic_avg > 0 ? (int)(100 - 100 * ideal_avg / classic_avg) : 0,
    (unsigned long)realistic_avg,
    (long)classic_avg - (long)realistic_avg,
    classic_avg > 0 ? (int)(100 - 100 * realistic_avg / classic_avg) : 0
  );
}
#endif  /* R05_COMPACT_LIST */


#ifdef R05_COMPACT_HANDLES
static void r05_compact_storage_stats(void) {
  cm_stats_t view = cm_list_stats(&s_compact_storage.view);
  cm_stats_t build = cm_list_stats(&s_compact_storage.build);
  cm_stats_t buried = cm_list_stats(&s_compact_storage.buried);
  size_t lists_bytes = view.compact_bytes
    + build.compact_bytes + buried.compact_bytes;
  size_t handles_bytes = cl_iter_table_capacity_bytes();
  size_t stack_bytes = s_call_stack_capacity * sizeof(s_call_stack[0]);
  size_t compact_bytes = lists_bytes + handles_bytes + stack_bytes;
  size_t classic_bytes = view.classic_bytes
    + build.classic_bytes + buried.classic_bytes;
  long saved = (long) classic_bytes - (long) compact_bytes;
  int percent = classic_bytes == 0
    ? 0 : (int) (100 - 100 * compact_bytes / classic_bytes);
  long peak_saved = (long) s_compact_peak_classic_bytes
    - (long) s_compact_peak_total_bytes;
  int peak_percent = s_compact_peak_classic_bytes == 0
    ? 0 : (int) (
      100 - 100 * s_compact_peak_total_bytes / s_compact_peak_classic_bytes
    );

  fprintf(
    stderr,
    "Compact runtime storage stats:\n"
    "  elements             : view=%d build=%d buried=%d\n"
    "  macronodes           : view=%d build=%d buried=%d\n"
    "  compact lists        : %lu B\n"
    "  handle table         : %lu B (slot=%lu B, peak-live=%lu)\n"
    "  call stack capacity  : %lu B\n"
    "  compact total        : %lu B\n"
    "  classic equivalent   : %lu B\n"
    "  saved current        : %+ld B (%d%%)\n"
    "  peak elements        : %lu\n"
    "  peak compact lists   : %lu B\n"
    "  peak compact total   : %lu B\n"
    "  peak classic equiv.  : %lu B\n"
    "  saved at peak        : %+ld B (%d%%)\n",
    view.total_elements, build.total_elements, buried.total_elements,
    view.total_macronodes, build.total_macronodes, buried.total_macronodes,
    (unsigned long) lists_bytes,
    (unsigned long) handles_bytes,
    (unsigned long) cl_iter_table_slot_size(),
    (unsigned long) cl_iter_table_peak_slots(),
    (unsigned long) stack_bytes,
    (unsigned long) compact_bytes,
    (unsigned long) classic_bytes,
    saved, percent,
    (unsigned long) s_compact_peak_elements,
    (unsigned long) s_compact_peak_lists_bytes,
    (unsigned long) s_compact_peak_total_bytes,
    (unsigned long) s_compact_peak_classic_bytes,
    peak_saved, peak_percent
  );
}
#endif  /* R05_COMPACT_HANDLES */


R05_NORETURN void r05_exit(int retcode) {
  dump_buried();
  fflush(stderr);
  fflush(stdout);
  end_profiler();

#ifdef R05_SHOW_STAT
  fprintf(stderr, "Step count %lu\n", s_step_counter);
#ifdef R05_COMPACT_LIST
  r05_compact_view_field_stats();
#endif  /* R05_COMPACT_LIST */
#ifdef R05_COMPACT_HANDLES
  r05_compact_storage_stats();
#endif  /* R05_COMPACT_HANDLES */
#endif  /* R05_SHOW_STAT */
#ifdef R05_COMPACT_COUNTERS
  cl_iter_table_dump_counters();
#endif

  free_memory();
  fflush(stdout);

  exit(retcode);
}


R05_NORETURN void r05_recognition_impossible(void) {
  fprintf(stderr, "\nRECOGNITION IMPOSSIBLE\n\n");
  make_dump();
  r05_exit(EXIT_CODE_RECOGNITION_IMPOSSIBLE);
}


R05_NORETURN void r05_builtin_error(const char *message) {
  fprintf(stderr, "\nBUILTIN FUNCTION ERROR: %s\n\n", message);
  make_dump();
  r05_exit(EXIT_CODE_RECOGNITION_IMPOSSIBLE);
}


R05_NORETURN void r05_builtin_error_errno(const char *message) {
  fprintf(
    stderr, "\nBUILTIN FUNCTION ERROR: %s\n(errno = %d: %s)\n\n",
    message, errno, strerror(errno)
  );
  make_dump();
  r05_exit(EXIT_CODE_RECOGNITION_IMPOSSIBLE);
}


r05_number r05_step_count(void) {
  return s_step_counter;
}


static char **s_argv = 0;
static int s_argc = 0;

const char *r05_arg(int no) {
  if (no < s_argc) {
    return s_argv[no];
  } else {
    return "";
  }
}


R05_NORETURN void r05_switch_default_violation_impl(
  const char *expr, long value, const char *file, int line
) {
  fprintf(stderr, "%s:%d:SWITCH DEFAULT VIOLATION\n", file, line);
  fprintf(stderr, "    expression %s -> %ld is not handled\n", expr, value);
  abort();
}


/*==============================================================================
   Копилка
==============================================================================*/


#ifndef R05_COMPACT_HANDLES
static struct r05_node s_end_buried;

static struct r05_node s_begin_buried = {
  0, &s_end_buried, R05_DATATAG_ILLEGAL, { '\0' }
};
static struct r05_node s_end_buried = {
  &s_begin_buried, 0, R05_DATATAG_ILLEGAL, { '\0' }
};
#endif


struct buried_query {
  cl_iter_t left_bracket;
  cl_iter_t right_bracket;
  cl_iter_t value[2];
};

static int buried_query(struct buried_query *res, cl_iter_t key[]) {
#ifdef R05_COMPACT_HANDLES
  cl_iter_t buried_begin = crs_buried_begin(&s_compact_storage);
  cl_iter_t buried_end = crs_buried_end(&s_compact_storage);
#else
  cl_iter_t buried_begin = s_begin_buried.next;
  cl_iter_t buried_end = &s_end_buried;
#endif
  cl_iter_t left_bracket, right_bracket, eq;
  int found = 0;

  while (! cl_iter_eq(buried_begin, buried_end) && ! found) {
    cl_iter_t rep_key[2];

    left_bracket = buried_begin;
    right_bracket = cl_iter_link(left_bracket);

    found = r05_repeated_evar_left(rep_key, left_bracket, right_bracket, key)
      && r05_char_left(&eq, rep_key[1], right_bracket, '=');

    buried_begin = cl_iter_next(right_bracket);
  }

  if (found) {
    res->left_bracket = left_bracket;
    res->right_bracket = right_bracket;
    res->value[0] = cl_iter_next(eq);
    res->value[1] = cl_iter_prev(right_bracket);
  }

  return found;
}


enum brrp_behavior { BRRP_BR, BRRP_RP };

static void brrp_impl(
  cl_iter_t arg_begin, cl_iter_t arg_end,
  enum brrp_behavior behavior
) {
  cl_iter_t callee = cl_iter_next(arg_begin);
  cl_iter_t key[2], eq;

  key[0] = cl_iter_next(callee);
  key[1] = cl_iter_prev(key[0]);
  do {
    if (r05_char_left(&eq, key[1], arg_end, '=')) {
      struct buried_query query;

      if (BRRP_RP == behavior && buried_query(&query, key)) {
        cl_iter_t val[2];
        r05_close_evar(val, eq, arg_end);
        r05_correct_evar(val);
        r05_correct_evar(query.value);
        r05_splice_tevar(query.right_bracket, val);
        r05_splice_tevar(arg_end, query.value);
      } else {
        cl_iter_t left_bracket = callee;
        cl_iter_t right_bracket = arg_end;
        cl_iter_set_tag(left_bracket, R05_DATATAG_OPEN_BRACKET);
        cl_iter_set_tag(right_bracket, R05_DATATAG_CLOSE_BRACKET);
        r05_link_brackets(left_bracket, right_bracket);
#ifdef R05_COMPACT_HANDLES
        crs_splice_before(
          crs_buried_begin(&s_compact_storage), left_bracket, right_bracket
        );
#else
        list_splice(s_begin_buried.next, left_bracket, right_bracket);
#endif
        arg_end = arg_begin;
      }
      r05_splice_to_freelist(arg_begin, arg_end);
      return;
    }
  } while (r05_open_evar_advance(key, arg_end));

  r05_recognition_impossible();
}


enum dgcp_behavior { DGCP_DG, DGCP_CP };

static void dgcp_impl(
  cl_iter_t arg_begin, cl_iter_t arg_end,
  enum dgcp_behavior behavior
) {
  cl_iter_t key[2];
  struct buried_query query;
  int found;

  key[0] = cl_iter_next(cl_iter_next(arg_begin));
  key[1] = cl_iter_prev(arg_end);
  r05_reset_allocator();

  found = buried_query(&query, key);

  if (found) {
    if (behavior == DGCP_DG) {
      r05_correct_evar(query.value);
      r05_splice_tevar(arg_begin, query.value);
      r05_splice_to_freelist(query.left_bracket, query.right_bracket);
    } else {
      r05_alloc_tevar(query.value);
      r05_splice_from_freelist(arg_begin);
    }
  }

  r05_splice_to_freelist(arg_begin, arg_end);
}


void r05_br(cl_iter_t arg_begin, cl_iter_t arg_end) {
  brrp_impl(arg_begin, arg_end, BRRP_BR);
}

void r05_dg(cl_iter_t arg_begin, cl_iter_t arg_end) {
  dgcp_impl(arg_begin, arg_end, DGCP_DG);
}

void r05_cp(cl_iter_t arg_begin, cl_iter_t arg_end) {
  dgcp_impl(arg_begin, arg_end, DGCP_CP);
}

void r05_rp(cl_iter_t arg_begin, cl_iter_t arg_end) {
  brrp_impl(arg_begin, arg_end, BRRP_RP);
}


static void dump_buried(void) {
#ifdef R05_DUMP_BURIED
  fprintf(stderr, "\nBURIED:\n");
#ifdef R05_COMPACT_HANDLES
  {
    cl_iter_t begin = crs_buried_begin(&s_compact_storage);
    cl_iter_t tail = crs_buried_end(&s_compact_storage);
    if (! cl_iter_eq(begin, tail)) {
      print_seq(begin, cl_iter_prev(tail));
    }
  }
#else
  print_seq(&s_begin_buried, &s_end_buried);
#endif
#endif  /* ifdef R05_DUMP_BURIED */
}


int main(int argc, char **argv) {
  s_argc = argc;
  s_argv = argv;

  init_view_field();
  start_profiler();
  main_loop();
  r05_exit(0);

#ifndef R05_NORETURN_DEFINED
  return 0;
#endif
}
