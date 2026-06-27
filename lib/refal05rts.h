#ifndef Refal05RTS_H_
#define Refal05RTS_H_

#include <stddef.h>


#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */


#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#  define R05_NORETURN _Noreturn
#  define R05_NORETURN_DEFINED
#elif defined(__cplusplus) && __cplusplus >= 201103L
#  define R05_NORETURN [[noreturn]]
#  define R05_NORETURN_DEFINED
#elif defined(_MSC_VER) && _MSC_VER >= 1800
#  define R05_NORETURN __declspec(noreturn)
#  define R05_NORETURN_DEFINED
#elif defined(__GNUC__)
#  define R05_NORETURN __attribute__((noreturn))
#  define R05_NORETURN_DEFINED
#elif defined(__clang__)
#  define R05_NORETURN __attribute__((noreturn))
#  define R05_NORETURN_DEFINED
#else
#  define R05_NORETURN
#endif


enum r05_datatag {
  R05_DATATAG_ILLEGAL = 0,
  R05_DATATAG_CHAR,
  R05_DATATAG_FUNCTION,
  R05_DATATAG_NUMBER,

  R05_DATATAG_OPEN_BRACKET,
  R05_DATATAG_CLOSE_BRACKET,
  R05_DATATAG_OPEN_CALL,
  R05_DATATAG_CLOSE_CALL,
};

struct r05_node;
#ifdef R05_COMPACT_HANDLES
#include "cl_handle.h"
typedef cl_handle_t cl_iter_t;
#define CL_ITER_NULL ((cl_iter_t) { CL_HANDLE_NULL })
#else
typedef struct r05_node *cl_iter_t;
#define CL_ITER_NULL NULL
#endif

typedef void (*r05_function_ptr) (cl_iter_t begin, cl_iter_t end);

struct r05_function {
  r05_function_ptr ptr;
  const char *name;
  int entry;
  struct r05_metatable *metatable;
#ifdef R05_PROFILER
  double seconds;
  unsigned long calls;
  struct r05_function *next;
#endif
};

typedef unsigned int r05_number;

struct r05_node {
  struct r05_node *prev;
  struct r05_node *next;
  enum r05_datatag tag;
  union {
    char char_;
    struct r05_function *function;
    r05_number number;
    struct r05_node *link;
  } info;
};


#ifdef R05_COMPACT_HANDLES
cl_iter_t cl_iter_next(cl_iter_t it);
cl_iter_t cl_iter_prev(cl_iter_t it);
int cl_iter_eq(cl_iter_t a, cl_iter_t b);

enum r05_datatag cl_iter_tag(cl_iter_t it);
void cl_iter_set_tag(cl_iter_t it, enum r05_datatag tag);

char cl_iter_char(cl_iter_t it);
r05_number cl_iter_number(cl_iter_t it);
struct r05_function *cl_iter_function(cl_iter_t it);
cl_iter_t cl_iter_link(cl_iter_t it);

void cl_iter_set_char(cl_iter_t it, char ch);
void cl_iter_set_number(cl_iter_t it, r05_number number);
void cl_iter_set_function(cl_iter_t it, struct r05_function *function);
void cl_iter_set_link(cl_iter_t it, cl_iter_t link);
static inline int cl_iter_is_null(cl_iter_t it) {
  return it.handle == CL_HANDLE_NULL;
}
#else
static inline cl_iter_t cl_iter_next(cl_iter_t it) { return it->next; }
static inline cl_iter_t cl_iter_prev(cl_iter_t it) { return it->prev; }
static inline int cl_iter_eq(cl_iter_t a, cl_iter_t b) { return a == b; }

static inline enum r05_datatag cl_iter_tag(cl_iter_t it) { return it->tag; }
static inline void cl_iter_set_tag(cl_iter_t it, enum r05_datatag t) { it->tag = t; }

static inline char cl_iter_char(cl_iter_t it) { return it->info.char_; }
static inline r05_number cl_iter_number(cl_iter_t it) { return it->info.number; }
static inline struct r05_function *cl_iter_function(cl_iter_t it) { return it->info.function; }
static inline cl_iter_t cl_iter_link(cl_iter_t it) { return it->info.link; }

static inline void cl_iter_set_char(cl_iter_t it, char c) { it->info.char_ = c; }
static inline void cl_iter_set_number(cl_iter_t it, r05_number n) { it->info.number = n; }
static inline void cl_iter_set_function(cl_iter_t it, struct r05_function *f) { it->info.function = f; }
static inline void cl_iter_set_link(cl_iter_t it, cl_iter_t l) { it->info.link = l; }
static inline int cl_iter_is_null(cl_iter_t it) { return it == NULL; }
#endif

/* Операции сопоставления с образцом */

#define r05_empty_hole(left, right) (cl_iter_eq(cl_iter_next(left), (right)))

int r05_function_left(
  cl_iter_t *res, cl_iter_t left, cl_iter_t right,
  struct r05_function *function
);

int r05_function_right(
  cl_iter_t *res, cl_iter_t left, cl_iter_t right,
  struct r05_function *function
);

int r05_char_left(
  cl_iter_t *res, cl_iter_t left, cl_iter_t right, char ch
);

int r05_char_right(
  cl_iter_t *res, cl_iter_t left, cl_iter_t right, char ch
);

int r05_number_left(
  cl_iter_t *res, cl_iter_t left, cl_iter_t right,
  r05_number number
);

int r05_number_right(
  cl_iter_t *res, cl_iter_t left, cl_iter_t right,
  r05_number number
);

int r05_brackets_left(
  cl_iter_t *brackets, cl_iter_t left, cl_iter_t right
);

int r05_brackets_right(
  cl_iter_t *brackets, cl_iter_t left, cl_iter_t right
);

int r05_svar_left(
  cl_iter_t *svar, cl_iter_t left, cl_iter_t right
);

int r05_svar_right(
  cl_iter_t *svar, cl_iter_t left, cl_iter_t right
);

int r05_tvar_left(
  cl_iter_t *tvar, cl_iter_t left, cl_iter_t right
);

int r05_tvar_right(
  cl_iter_t *tvar, cl_iter_t left, cl_iter_t right
);

int r05_repeated_svar_left(
  cl_iter_t *svar, cl_iter_t left, cl_iter_t right,
  cl_iter_t *svar_sample
);

int r05_repeated_svar_right(
  cl_iter_t *svar, cl_iter_t left, cl_iter_t right,
  cl_iter_t *svar_sample
);

#define r05_repeated_tvar_left(v, l, r, s) \
  r05_repeated_tevar_left(v, l, r, s, 't')

#define r05_repeated_tvar_right(v, l, r, s) \
  r05_repeated_tevar_right(v, l, r, s, 't')

#define r05_repeated_evar_left(v, l, r, s) \
  r05_repeated_tevar_left(v, l, r, s, 'e')

#define r05_repeated_evar_right(v, l, r, s) \
  r05_repeated_tevar_right(v, l, r, s, 'e')

int r05_repeated_tevar_left(
  cl_iter_t *tevar, cl_iter_t left, cl_iter_t right,
  cl_iter_t *tevar_sample, char type
);

int r05_repeated_tevar_right(
  cl_iter_t *tevar, cl_iter_t left, cl_iter_t right,
  cl_iter_t *tevar_sample, char type
);

#define r05_close_evar(evar, left, right) \
  ((evar)[0] = cl_iter_next(left), (evar)[1] = cl_iter_prev(right))

int r05_open_evar_advance(cl_iter_t *evar, cl_iter_t right);

size_t r05_read_chars(
  cl_iter_t *char_interval, char buffer[], size_t buflen,
  cl_iter_t left, cl_iter_t right
);

/* Операции построения результата */

void r05_push_stack(cl_iter_t call_bracket);
void r05_link_brackets(cl_iter_t left, cl_iter_t right);

void r05_correct_evar(cl_iter_t *evar);

#define r05_splice_tvar r05_splice_tevar
#define r05_splice_evar r05_splice_tevar

void r05_splice_tevar(cl_iter_t res, cl_iter_t *tevar);

void r05_splice_to_freelist(cl_iter_t first, cl_iter_t last);
void r05_splice_from_freelist(cl_iter_t pos);

void r05_reset_allocator(void);

cl_iter_t r05_alloc_node(enum r05_datatag tag);

cl_iter_t r05_insert_pos(void);

#ifdef R05_COMPACT_HANDLES
void r05_alloc_char(char ch);
void r05_alloc_number(r05_number num);
void r05_alloc_function(struct r05_function *func);
#else
static inline void r05_alloc_char(char ch) {
  cl_iter_set_char(r05_alloc_node(R05_DATATAG_CHAR), ch);
}

static inline void r05_alloc_number(r05_number num) {
  cl_iter_set_number(r05_alloc_node(R05_DATATAG_NUMBER), num);
}

static inline void r05_alloc_function(struct r05_function *func) {
  cl_iter_set_function(r05_alloc_node(R05_DATATAG_FUNCTION), func);
}
#endif

void r05_alloc_chars(const char buffer[], size_t len);

#define r05_alloc_open_bracket(pos) \
  (*(pos) = r05_alloc_node(R05_DATATAG_OPEN_BRACKET))

#define r05_alloc_close_bracket(pos) \
  (*(pos) = r05_alloc_node(R05_DATATAG_CLOSE_BRACKET))

#define r05_alloc_open_call(pos) \
  (*(pos) = r05_alloc_node(R05_DATATAG_OPEN_CALL))

#define r05_alloc_close_call(pos) \
  (*(pos) = r05_alloc_node(R05_DATATAG_CLOSE_CALL))

#define r05_alloc_insert_pos(pos) (*(pos) = r05_insert_pos());

static inline void r05_alloc_svar(cl_iter_t *sample) {
  cl_iter_t node = r05_alloc_node(cl_iter_tag(*sample));

  switch (cl_iter_tag(*sample)) {
    case R05_DATATAG_CHAR:
      cl_iter_set_char(node, cl_iter_char(*sample));
      break;

    case R05_DATATAG_NUMBER:
      cl_iter_set_number(node, cl_iter_number(*sample));
      break;

    case R05_DATATAG_FUNCTION:
      cl_iter_set_function(node, cl_iter_function(*sample));
      break;

    case R05_DATATAG_OPEN_BRACKET:
    case R05_DATATAG_CLOSE_BRACKET:
    case R05_DATATAG_OPEN_CALL:
    case R05_DATATAG_CLOSE_CALL:
      cl_iter_set_link(node, cl_iter_link(*sample));
      break;

    default:
      break;
  }
}

#define r05_alloc_tvar r05_alloc_tevar
#define r05_alloc_evar r05_alloc_tevar

void r05_alloc_tevar(cl_iter_t *sample);
void r05_alloc_string(const char *string);


void r05_enum_function_code(cl_iter_t begin, cl_iter_t end);


/* Профилирование */

void r05_this_is_generated_function(void);
void r05_start_e_loop(void);
void r05_stop_e_loop(void);
double r05_time_elapsed(void);

/* Рефал-машина, операционная среда и диагностика */

R05_NORETURN void r05_recognition_impossible(void);
R05_NORETURN void r05_exit(int retcode);
R05_NORETURN void r05_builtin_error(const char *message);
R05_NORETURN void r05_builtin_error_errno(const char *message);

r05_number r05_step_count(void);
const char *r05_arg(int no);

#define r05_switch_default_violation(value) \
  r05_switch_default_violation_impl(#value, value, __FILE__, __LINE__)

R05_NORETURN void r05_switch_default_violation_impl(
  const char *expr, long value, const char *file, int line
);


#ifdef R05_PROFILER
#define R05_INIT_PROFILER 0, 0, NULL,
#else
#define R05_INIT_PROFILER
#endif


#define R05_DEFINE_ENTRY_ENUM(name, rep) \
  struct r05_function r05f_ ## name = { \
    r05_enum_function_code, rep, 1, NULL, \
    R05_INIT_PROFILER \
  };
#define R05_DEFINE_LOCAL_ENUM(name, rep) \
  static struct r05_function r05f_ ## name = { \
    r05_enum_function_code, rep, 0, NULL, \
    R05_INIT_PROFILER \
  };


#define R05_DECLARE_ENTRY_FUNCTION(name) \
  extern struct r05_function r05f_ ## name;
#define R05_DECLARE_LOCAL_FUNCTION(name) \
  static struct r05_function r05f_ ## name;


#define R05_DEFINE_ENTRY_FUNCTION(name, rep) \
  R05_DEFINE_FUNCTION_AUX(name, /* пусто */, rep, 1)
#define R05_DEFINE_LOCAL_FUNCTION(name, rep) \
  R05_DEFINE_FUNCTION_AUX(name, static, rep, 0)

#define R05_DEFINE_FUNCTION_AUX(name, scope, rep, entry) \
  static void r05c_ ## name( \
    cl_iter_t arg_begin, cl_iter_t arg_end \
  ); \
  scope struct r05_function r05f_ ## name = { \
    r05c_ ## name, rep, entry, NULL, R05_INIT_PROFILER \
  }; \
  static void r05c_ ## name( \
    cl_iter_t arg_begin, cl_iter_t arg_end \
  )


struct r05_metatable {
  size_t size;
  struct r05_function **functions;
};


#define R05_DEFINE_METAFUNCTION(name, rep) \
  extern void r05c_ ## name( \
    cl_iter_t arg_begin, cl_iter_t arg_end \
  ); \
  static struct r05_metatable metatable; \
  static struct r05_function r05f_ ## name = { \
    r05c_ ## name, rep, 0, &metatable, R05_INIT_PROFILER \
  };

#define R05_IMPLEMENT_METAFUNCTION(name, rep) \
  extern void r05c_ ## name( \
    cl_iter_t arg_begin, cl_iter_t arg_end \
  ); \
  static struct r05_function r05f_ ## name = { \
    r05c_ ## name, rep, 0, NULL, R05_INIT_PROFILER \
  }; \
  void r05c_ ## name( \
    cl_iter_t arg_begin, cl_iter_t arg_end \
  )


void r05_br(cl_iter_t arg_begin, cl_iter_t arg_end);
void r05_dg(cl_iter_t arg_begin, cl_iter_t arg_end);
void r05_cp(cl_iter_t arg_begin, cl_iter_t arg_end);
void r05_rp(cl_iter_t arg_begin, cl_iter_t arg_end);


#ifdef __cplusplus
}  /* extern "C" */
#endif /* __cplusplus */


#endif /* Refal05RTS_H_ */
