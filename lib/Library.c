#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef R05_POSIX
#include <sys/wait.h>
#endif  /* R05_POSIX */

#include "refal05rts.h"


enum {
  R05_NUMBER_BITS = CHAR_BIT * sizeof(r05_number),
};


#define STATIC_ASSERT(message, expr) \
  int message : ((expr) ? +1 : -1)

struct static_asserts {
  STATIC_ASSERT(char_bit_is_8, CHAR_BIT == 8);
  STATIC_ASSERT(
    platform_32_or_64, R05_NUMBER_BITS == 32 || R05_NUMBER_BITS == 64
  );
};


#define ALIAS_DESCRIPTOR(name, rep, origin) \
  struct r05_function r05f_ ## name = { \
    origin, rep, 1, NULL, R05_INIT_PROFILER \
  };


#define DEFINE_ALIAS(name, rep, origin) \
  static void r05c_ ## origin( \
    cl_iter_t arg_begin, cl_iter_t arg_end \
  ); \
  ALIAS_DESCRIPTOR(name, rep, r05c_ ## origin)


DEFINE_ALIAS(k25_, "%", Mod);
DEFINE_ALIAS(k2A_, "*", Mul);
DEFINE_ALIAS(k2B_, "+", Add);
DEFINE_ALIAS(m_, "-", Sub);
DEFINE_ALIAS(k2F_, "/", Div);


#define is_ident_tail(c) \
  (isalpha((int) (c)) || isdigit((int) (c)) || (c) == '_' || (c) == '-')


static struct r05_function *s_arithmetic_names[] = {
  &r05f_k25_, &r05f_k2A_, &r05f_k2B_, &r05f_m_, &r05f_k2F_, NULL
};


struct builtin_info {
  r05_number id;
  struct r05_function *function;
  struct r05_function *type;
};

// Fix tentative definition for MSVC, keep during merge
static struct builtin_info s_builtin_info[100];


static int chain_str_eq(
  cl_iter_t begin, cl_iter_t end, const char *name
);


/**
   1. <Mu s.Func e.Arg> == <s.Func e.Arg>
*/
R05_IMPLEMENT_METAFUNCTION(Mu, "Mu") {
  cl_iter_t mu = cl_iter_next(arg_begin);
  cl_iter_t callable = cl_iter_next(mu);
  struct r05_metatable *metatable = cl_iter_function(mu)->metatable;
  struct r05_function **cur, **end;
  cl_iter_t brackets[2];

  assert(metatable != NULL);
  cur = metatable->functions;
  end = metatable->functions + metatable->size;

  if (R05_DATATAG_FUNCTION == cl_iter_tag(callable)) {
    const char *name = cl_iter_function(callable)->name;
    while (cur < end && strcmp((*cur)->name, name) != 0) {
      ++cur;
    }
    if (cur < end) {
      cl_iter_set_function(callable, *cur);
    } else if (! cl_iter_function(callable)->entry) {
      r05_recognition_impossible();
    }
    r05_splice_to_freelist(mu, mu);
  } else if (R05_DATATAG_CHAR == cl_iter_tag(callable)) {
    char name = cl_iter_char(callable);
    struct r05_function **alias = s_arithmetic_names;
    while (*alias != NULL && (*alias)->name[0] != name) {
      ++alias;
    }

    if (*alias != NULL) {
      cl_iter_set_function(mu, *alias);
    } else {
      while (
        cur < end && ((*cur)->name[0] != name || (*cur)->name[1] != '\0')
      ) {
        ++cur;
      }
      if (cur < end) {
        cl_iter_set_function(mu, *cur);
      } else {
        r05_recognition_impossible();
      }
    }
    r05_splice_to_freelist(callable, callable);
  } else if (
    r05_brackets_left(brackets, mu, arg_end)
    && ! cl_iter_eq(cl_iter_next(brackets[0]), brackets[1])
  ) {
    cl_iter_t p = cl_iter_next(brackets[0]);
    cl_iter_t name_b = cl_iter_next(brackets[0]);
    cl_iter_t name_e = cl_iter_prev(brackets[1]);
    struct r05_function *callee = NULL;
    struct builtin_info *bi = s_builtin_info;
    struct r05_function **alias = s_arithmetic_names;

    while (! cl_iter_eq(p, brackets[1]) && R05_DATATAG_CHAR == cl_iter_tag(p)) {
      p = cl_iter_next(p);
    }
    if (! cl_iter_eq(p, brackets[1])) {
      r05_recognition_impossible();
    }

    while (cur < end && ! chain_str_eq(name_b, name_e, (*cur)->name)) {
      ++cur;
    }
    if (cur < end) {
      callee = *cur;
    }

    while (
      bi->function != NULL && ! chain_str_eq(name_b, name_e, bi->function->name)
    ) {
      ++bi;
    }
    if (bi->function != NULL) {
      assert(callee == NULL);
      callee = bi->function;
    }

    while (*alias != NULL && ! chain_str_eq(name_b, name_e, (*alias)->name)) {
      ++alias;
    }
    if (*alias != NULL) {
      assert(callee == NULL);
      callee = *alias;
    }

    if (callee) {
      cl_iter_set_function(mu, callee);
    } else {
      r05_recognition_impossible();
    }
    r05_splice_to_freelist(brackets[0], brackets[1]);
  } else {
    r05_recognition_impossible();
  }

  r05_push_stack(arg_end);
  r05_push_stack(arg_begin);
}


struct signed_number {
  signed sign;
  r05_number value;
};


static cl_iter_t parse_signed_number(
  struct signed_number *sn, cl_iter_t p
) {
  if (R05_DATATAG_CHAR == cl_iter_tag(p)) {
    if ('-' == cl_iter_char(p)) {
      sn->sign = -1;
    } else if ('+' == cl_iter_char(p)) {
      sn->sign = +1;
    } else {
      r05_recognition_impossible();
    }

    p = cl_iter_next(p);
  } else {
    sn->sign = +1;
  }

  if (R05_DATATAG_NUMBER != cl_iter_tag(p)) {
    r05_recognition_impossible();
  }

  sn->value = cl_iter_number(p);

  if (0 == sn->value) {
    sn->sign = +1;
  }

  return cl_iter_next(p);
}


struct arithm_arg {
  struct signed_number x, y;
};


static void parse_arithm_arg(
  struct arithm_arg *aa, cl_iter_t arg_begin, cl_iter_t arg_end
) {
  cl_iter_t func_name, p;

  func_name = cl_iter_next(arg_begin);
  p = cl_iter_next(func_name);

  if (R05_DATATAG_OPEN_BRACKET == cl_iter_tag(p)) {
    p = cl_iter_next(p);
    p = parse_signed_number(&aa->x, p);

    if (R05_DATATAG_CLOSE_BRACKET != cl_iter_tag(p)) {
      r05_recognition_impossible();
    }

    p = cl_iter_next(p);
  } else {
    p = parse_signed_number(&aa->x, p);
  }

  p = parse_signed_number(&aa->y, p);

  if (! cl_iter_eq(p, arg_end)) {
    r05_recognition_impossible();
  }
}


/**
   2. <Add e.ArithmArg> == '-'? 1? s.NUMBER

   e.ArithmArg ::=
       (s.Sign? s.NUMBER) s.Sign? s.NUMBER
     | s.Sign? s.NUMBER s.Sign? s.NUMBER
*/
static void add(
  const struct arithm_arg *aa,
  cl_iter_t arg_begin, cl_iter_t arg_end
);


R05_DEFINE_ENTRY_FUNCTION(Add, "Add") {
  struct arithm_arg arg;

  parse_arithm_arg(&arg, arg_begin, arg_end);
  add(&arg, arg_begin, arg_end);
}


R05_DEFINE_ENTRY_FUNCTION(Inc, "Inc") {
  struct arithm_arg arg;
  cl_iter_t func_name = cl_iter_next(arg_begin);
  cl_iter_t p = cl_iter_next(func_name);

  p = parse_signed_number(&arg.x, p);
  if (! cl_iter_eq(p, arg_end)) {
    r05_recognition_impossible();
  }

  arg.y.sign = +1;
  arg.y.value = 1;
  add(&arg, arg_begin, arg_end);
}


static cl_iter_t emplace_number(
  struct signed_number res, r05_number high, cl_iter_t p
) {
  if (res.sign < 0) {
    cl_iter_set_tag(p, R05_DATATAG_CHAR);
    cl_iter_set_char(p, '-');
    p = cl_iter_next(p);
  }

  if (high) {
    cl_iter_set_tag(p, R05_DATATAG_NUMBER);
    cl_iter_set_number(p, high);
    p = cl_iter_next(p);
  }

  cl_iter_set_tag(p, R05_DATATAG_NUMBER);
  cl_iter_set_number(p, res.value);
  return p;
}



static void add(
  const struct arithm_arg *aa,
  cl_iter_t arg_begin, cl_iter_t arg_end
) {
  struct signed_number res;
  r05_number carry = 0;
  cl_iter_t p = arg_begin;

  if (aa->x.sign == aa->y.sign) {
    res.sign = aa->x.sign;
    res.value = aa->x.value + aa->y.value;
    if (res.value < aa->x.value) {
      carry = 1;
    }
  } else if (aa->x.value > aa->y.value) {
    res.sign = aa->x.sign;
    res.value = aa->x.value - aa->y.value;
  } else if (aa->x.value < aa->y.value) {
    res.sign = aa->y.sign;
    res.value = aa->y.value - aa->x.value;
  } else {
    res.sign = 0;
    res.value = 0;
  }

  p = emplace_number(res, carry, p);
  r05_splice_to_freelist(cl_iter_next(p), arg_end);
}


/**
   3. <Arg s.ArgNo> == e.Argument

      s.ArgNo ::= s.NUMBER
      e.Argument ::= s.CHAR*
*/
R05_DEFINE_ENTRY_FUNCTION(Arg, "Arg") {
  cl_iter_t callable = cl_iter_next(arg_begin);
  cl_iter_t parg_no = cl_iter_next(callable);
  int arg_no;

  if (
    cl_iter_eq(parg_no, arg_end)
    || R05_DATATAG_NUMBER != cl_iter_tag(parg_no)
    || ! cl_iter_eq(cl_iter_next(parg_no), arg_end)
  ) {
    r05_recognition_impossible();
  }

  arg_no = (int) cl_iter_number(parg_no);

  r05_reset_allocator();
  r05_alloc_string(r05_arg(arg_no));
  r05_splice_from_freelist(arg_begin);
  r05_splice_to_freelist(arg_begin, arg_end);
}


/**
  4. <Br e.Key '=' e.Value> == empty
*/
ALIAS_DESCRIPTOR(Br, "Br", r05_br);


/**
   5. <Card> == s.CHAR* 0?
*/
static void read_from_stream(FILE *input);

R05_DEFINE_ENTRY_FUNCTION(Card, "Card") {
  cl_iter_t callee = cl_iter_next(arg_begin);

  if (! cl_iter_eq(cl_iter_next(callee), arg_end)) {
    r05_recognition_impossible();
  }

  r05_reset_allocator();
  read_from_stream(stdin);
  r05_splice_from_freelist(arg_begin);
  r05_splice_to_freelist(arg_begin, arg_end);
}


static void read_from_stream(FILE *input) {
  int cur_char;

  while (cur_char = fgetc(input), cur_char != EOF && cur_char != '\n') {
    r05_alloc_char((char) cur_char);
  }

  if (cur_char == EOF) {
    r05_alloc_number(0);
  }
}


/**
   6. <Chr e.Expr> == e.Expr’

   В e.Expr’ все числа заменены на литеры с соответствующими кодами
*/
R05_DEFINE_ENTRY_FUNCTION(Chr, "Chr") {
  cl_iter_t callee = cl_iter_next(arg_begin);
  cl_iter_t p = cl_iter_next(callee);

  while (! cl_iter_eq(p, arg_end)) {
    if (cl_iter_tag(p) == R05_DATATAG_NUMBER) {
      cl_iter_set_tag(p, R05_DATATAG_CHAR);
      cl_iter_set_char(p, (unsigned char) cl_iter_number(p));
    }
    p = cl_iter_next(p);
  }

  r05_splice_to_freelist(arg_begin, callee);
  r05_splice_to_freelist(arg_end, arg_end);
}


/**
  7. <Cp e.Key> == e.Value
*/
ALIAS_DESCRIPTOR(Cp, "Cp", r05_cp);


/**
  8. <Dg e.Key> == e.Value
*/
ALIAS_DESCRIPTOR(Dg, "Dg", r05_dg);


/**
  10. <Div e.ArithmArg> == '-'? s.NUMBER
*/
struct divmod {
  struct signed_number div, mod;
};


void divmod(const struct arithm_arg *aa, struct divmod *res) {
  if (0 == aa->y.value) {
    r05_builtin_error("divide by zero");
  }

  res->div.value = aa->x.value / aa->y.value;
  res->div.sign = aa->x.sign * aa->y.sign;
  res->mod.value = aa->x.value % aa->y.value;
  res->mod.sign = aa->x.sign;
}


R05_DEFINE_ENTRY_FUNCTION(Div, "Div") {
  struct arithm_arg arg;
  struct divmod res;
  cl_iter_t p = arg_begin;

  parse_arithm_arg(&arg, arg_begin, arg_end);
  divmod(&arg, &res);
  p = emplace_number(res.div, 0, p);
  r05_splice_to_freelist(cl_iter_next(p), arg_end);
}


/**
  11. <Divmod e.ArithmArg> == ('-'? s.NUMBER) '-'? s.NUMBER
*/
R05_DEFINE_ENTRY_FUNCTION(Divmod, "Divmod") {
  struct arithm_arg arg;
  struct divmod res;
  cl_iter_t open = arg_begin, close, p;

  parse_arithm_arg(&arg, arg_begin, arg_end);
  divmod(&arg, &res);
  cl_iter_set_tag(open, R05_DATATAG_OPEN_BRACKET);
  close = cl_iter_next(emplace_number(res.div, 0, cl_iter_next(open)));
  cl_iter_set_tag(close, R05_DATATAG_CLOSE_BRACKET);
  r05_link_brackets(open, close);
  p = emplace_number(res.mod, 0, cl_iter_next(close));

  if (! cl_iter_eq(p, arg_end)) {
    r05_splice_to_freelist(cl_iter_next(p), arg_end);
  }
}


/**
  12. <Explode s.FUNCTION> == s.CHAR+
*/
R05_DEFINE_ENTRY_FUNCTION(Explode, "Explode") {
  cl_iter_t callable = cl_iter_next(arg_begin);
  cl_iter_t ident = cl_iter_next(callable);

  if (
     cl_iter_eq(ident, arg_end)
     || R05_DATATAG_FUNCTION != cl_iter_tag(ident)
     || ! cl_iter_eq(cl_iter_next(ident), arg_end)
  ) {
    r05_recognition_impossible();
  }

  r05_reset_allocator();
  r05_alloc_string(cl_iter_function(ident)->name);
  r05_splice_from_freelist(arg_begin);
  r05_splice_to_freelist(arg_begin, arg_end);
}


/**
  13. <First s.Len e.Items> == (e.Prefix) e.Suffix

  e.Items : e.Prefix e.Suffix
  |e.Prefix| == s.Len || { |e.Prefix| < s.Len && |e.Suffix| == 0 }
*/
R05_DEFINE_ENTRY_FUNCTION(First, "First") {
  cl_iter_t sLen;
  r05_number counter;
  cl_iter_t ePrefix[2];
  cl_iter_t left_bracket, right_bracket, callee;

  callee = cl_iter_next(arg_begin);

  if (
    ! r05_svar_left(&sLen, callee, arg_end)
    || cl_iter_tag(sLen) != R05_DATATAG_NUMBER
  ) {
    r05_recognition_impossible();
  }

  counter = cl_iter_number(sLen);

  ePrefix[0] = cl_iter_next(sLen);
  ePrefix[1] = sLen;
  while (counter > 0 && r05_open_evar_advance(ePrefix, arg_end)) {
    -- counter;
  }

  left_bracket = callee;
  right_bracket = sLen;

  cl_iter_set_tag(left_bracket, R05_DATATAG_OPEN_BRACKET);
  cl_iter_set_tag(right_bracket, R05_DATATAG_CLOSE_BRACKET);
  r05_link_brackets(left_bracket, right_bracket);

  r05_correct_evar(ePrefix);
  r05_splice_evar(right_bracket, ePrefix);

  r05_splice_to_freelist(arg_begin, arg_begin);
  r05_splice_to_freelist(arg_end, arg_end);
}


/**
  14. <Get s.FileNo> == s.Char* 0?
      s.FileNo ::= s.NUMBER
*/
FILE *open_numbered(unsigned int no, const char mode);

R05_DEFINE_ENTRY_FUNCTION(Get, "Get") {
  cl_iter_t callable = cl_iter_next(arg_begin);
  cl_iter_t pfile_no = cl_iter_next(callable);
  FILE *stream;

  if (
    cl_iter_eq(pfile_no, arg_end)
    || R05_DATATAG_NUMBER != cl_iter_tag(pfile_no)
    || ! cl_iter_eq(cl_iter_next(pfile_no), arg_end)
  ) {
    r05_recognition_impossible();
  }

  stream = open_numbered((unsigned int) cl_iter_number(pfile_no), 'r');

  r05_reset_allocator();
  read_from_stream(stream);
  r05_splice_from_freelist(arg_begin);
  r05_splice_to_freelist(arg_begin, arg_end);
}


R05_DEFINE_ENTRY_FUNCTION(LoadFile, "LoadFile") {
  cl_iter_t eFileName[2];
  cl_iter_t open = CL_ITER_NULL;
  cl_iter_t close = CL_ITER_NULL;
  enum {
    FILENAME_LEN = FILENAME_MAX
  };
  char filename[FILENAME_LEN + 1] = { '\0' };
  char buffer[4096];
  size_t filename_len;
  size_t used = 0;
  int line_open = 0;
  int ch;
  FILE *input;

  filename_len = r05_read_chars(
    eFileName, filename, FILENAME_LEN, cl_iter_next(arg_begin), arg_end
  );
  if (! r05_empty_hole(eFileName[1], arg_end)) {
    static const char error_format[] =
      "Very long file name, maximum available is %u";
    char error[sizeof(error_format) + 32];

    sprintf(error, error_format, (unsigned int) FILENAME_MAX);
    r05_builtin_error(error);
  }
  filename[filename_len] = '\0';

  input = fopen(filename, "r");
  if (input == NULL) {
    r05_builtin_error_errno(filename);
  }

  r05_reset_allocator();
  while ((ch = fgetc(input)) != EOF) {
    if (! line_open) {
      r05_alloc_open_bracket(&open);
      line_open = 1;
      used = 0;
    }

    if (ch == '\n') {
      if (used != 0) {
        r05_alloc_chars(buffer, used);
        used = 0;
      }
      r05_alloc_close_bracket(&close);
      r05_link_brackets(open, close);
      line_open = 0;
      continue;
    }

    buffer[used++] = (char) ch;
    if (used == sizeof(buffer)) {
      r05_alloc_chars(buffer, used);
      used = 0;
    }
  }

  if (line_open) {
    if (used != 0) {
      r05_alloc_chars(buffer, used);
    }
    r05_alloc_close_bracket(&close);
    r05_link_brackets(open, close);
  }
  fclose(input);

  r05_splice_from_freelist(arg_begin);
  r05_splice_to_freelist(arg_begin, arg_end);
}


enum { FILE_LIMIT = 40 };

static FILE *s_streams[FILE_LIMIT] = { NULL };

enum { UINT_DIGITS = (sizeof(unsigned int) * 8 + 2) / 3 };

FILE *open_numbered(unsigned int file_no, const char mode) {
  char mode_str[2] = "*";

  mode_str[0] = mode;
  file_no %= FILE_LIMIT;
  if (file_no == 0 && mode == 'r') {
    return stdin;
  } else if (file_no == 0 && mode == 'w') {
    return stderr;
  }

  if (s_streams[file_no] == NULL) {
    static const char filename_format[] = "REFAL%u.DAT";
    char filename[sizeof(filename_format) + UINT_DIGITS];

    sprintf(filename, filename_format, file_no);
    s_streams[file_no] = fopen(filename, mode_str);

    if (s_streams[file_no] == NULL) {
      static const char error_format[] = "Can't open REFAL%u.DAT as \"%c\"";
      char error[sizeof(error_format) + UINT_DIGITS];

      sprintf(error, error_format, file_no, mode);
      r05_builtin_error_errno(error);
    }
  }

  return s_streams[file_no];
}


/**
  15. <Implode e.ValidPrefix e.Suffix> == s.FUNCTION e.Suffix
      <Implode e.Suffix> == 0 e.Suffix

  e.ValidPrefix ::= s.Lettern { s.Letter | s.Digit | '_' | '-' | '$' }
  s.Letter ::= 'A' | … | 'Z' | 'a' | … | 'z'
  s.Digit ::= '0' | … | '9'
*/
static struct r05_function *implode(
  cl_iter_t begin, cl_iter_t end
);


R05_DEFINE_ENTRY_FUNCTION(Implode, "Implode") {
  cl_iter_t sFunc, sBegin, sEnd;

  sFunc = cl_iter_next(arg_begin);
  sBegin = cl_iter_next(sFunc);

  if (
    cl_iter_tag(sBegin) != R05_DATATAG_CHAR
    || ! isalpha((int) cl_iter_char(sBegin))
  ) {
    cl_iter_set_tag(sFunc, R05_DATATAG_NUMBER);
    cl_iter_set_number(sFunc, 0);
  } else {
    sEnd = cl_iter_next(sBegin);

#define is_implode_tail(c) (is_ident_tail(c) || (c) == '$')
    while (
      cl_iter_tag(sEnd) == R05_DATATAG_CHAR
      && is_implode_tail(cl_iter_char(sEnd))
    ) {
      sEnd = cl_iter_next(sEnd);
    }
#undef is_implode_tail

    sEnd = cl_iter_prev(sEnd);
    cl_iter_set_function(sFunc, implode(sBegin, sEnd));
    r05_splice_to_freelist(sBegin, sEnd);
  }

  r05_splice_to_freelist(arg_begin, arg_begin);
  r05_splice_to_freelist(arg_end, arg_end);
}


static int chain_str_eq(
  cl_iter_t begin, cl_iter_t end, const char *name
) {
  cl_iter_t node = begin, limit = cl_iter_next(end);
  const char *pc = name;
  while (
    ! cl_iter_eq(node, limit)
    && *pc != '\0'
    && *pc == cl_iter_char(node)
  ) {
    node = cl_iter_next(node);
    pc++;
  }

  return cl_iter_eq(node, limit) && *pc == '\0';
}


struct imploded {
  struct r05_function function;
  size_t hash;
  struct imploded *next;
  char name[1];
};


static struct imploded **s_imploded = NULL;
static size_t s_imploded_size = 0;
static size_t s_imploded_count = 0;


static void cleanup_imploded_table(void) {
  size_t i;
  for (i = 0; i < s_imploded_size; ++i) {
    struct imploded *item = s_imploded[i];
    while (item != NULL) {
      struct imploded *next = item->next;
      free(item);
      item = next;
    }
  }
  free(s_imploded);
}


static struct r05_function *implode(
  cl_iter_t begin, cl_iter_t end
) {
  struct builtin_info *info;
  struct r05_function **alias;
  cl_iter_t node, limit = cl_iter_next(end);
  size_t len, hash, i;
  struct imploded **bucket, *known, *new;

  for (info = s_builtin_info; info->function != 0; ++info) {
    if (chain_str_eq(begin, end, info->function->name)) {
      return info->function;
    }
  }

  for (alias = s_arithmetic_names; *alias != NULL; ++alias) {
    if (chain_str_eq(begin, end, (*alias)->name)) {
      return *alias;
    }
  }

  len = 0;
  hash = 7369; /* просто число */
  for (node = begin; ! cl_iter_eq(node, limit); node = cl_iter_next(node)) {
    len++;
    hash *= 6007; /* простое число */
    hash += (unsigned char) cl_iter_char(node);
  }

  if (s_imploded == NULL) {
    s_imploded_size = 100;
    s_imploded = calloc(s_imploded_size, sizeof(s_imploded[0]));
    atexit(cleanup_imploded_table);
  }

  bucket = &s_imploded[hash % s_imploded_size];
  for (known = *bucket; known != NULL; known = known->next) {
    if (chain_str_eq(begin, end, known->function.name)) {
      return &known->function;
    }
  }

  new = malloc(sizeof(struct imploded) + len);
  new->function.ptr = r05_enum_function_code;
  new->function.name = new->name;
  new->hash = hash;
  new->next = *bucket;

  for (
    i = 0, node = begin;
    ! cl_iter_eq(node, limit);
    node = cl_iter_next(node), i++
  ) {
    new->name[i] = cl_iter_char(node);
  }
  new->name[i] = '\0';

  *bucket = new;
  s_imploded_count++;

  if (s_imploded_count > 3 * s_imploded_size) {
    size_t new_size = 3 * s_imploded_size / 2;
    struct imploded **new_table = calloc(new_size, sizeof(new_table[0]));

    for (i = 0; i < s_imploded_size; ++i) {
      known = s_imploded[i];
      while (known != NULL) {
        struct imploded *next = known->next;
        bucket = &new_table[known->hash % new_size];
        known->next = *bucket;
        *bucket = known;
        known = next;
      }
    }

    free(s_imploded);
    s_imploded = new_table;
    s_imploded_size = new_size;
  }

  return &new->function;
}


/**
  17. <Lenw e.Expr> == s.Len e.Expr
      s.Len ::= s.NUMBER, s.Len == |e.Expr|
*/
R05_DEFINE_ENTRY_FUNCTION(Lenw, "Lenw") {
  cl_iter_t sLen, tTerm[2];
  r05_number counter = 0;

  sLen = tTerm[1] = cl_iter_next(arg_begin);

  while (r05_tvar_left(tTerm, tTerm[1], arg_end)) {
    ++ counter;
  }

  cl_iter_set_tag(sLen, R05_DATATAG_NUMBER);
  cl_iter_set_number(sLen, counter);

  r05_splice_to_freelist(arg_begin, arg_begin);
  r05_splice_to_freelist(arg_end, arg_end);
}


/**
   18. <Lower e.Expr> == e.Expr’

   В e.Expr’ все литеры приведены к нижнему регистру
*/
R05_DEFINE_ENTRY_FUNCTION(Lower, "Lower") {
  cl_iter_t callee = cl_iter_next(arg_begin);
  cl_iter_t p = cl_iter_next(callee);

  while (! cl_iter_eq(p, arg_end)) {
    if (cl_iter_tag(p) == R05_DATATAG_CHAR) {
      cl_iter_set_char(p, (char) tolower(cl_iter_char(p)));
    }
    p = cl_iter_next(p);
  }

  r05_splice_to_freelist(arg_begin, callee);
  r05_splice_to_freelist(arg_end, arg_end);
}

/**
  19. <Mod e.ArithmArg> == '-'? s.NUMBER
*/
R05_DEFINE_ENTRY_FUNCTION(Mod, "Mod") {
  struct arithm_arg arg;
  struct divmod res;
  cl_iter_t p = arg_begin;

  parse_arithm_arg(&arg, arg_begin, arg_end);
  divmod(&arg, &res);
  p = emplace_number(res.mod, 0, p);
  r05_splice_to_freelist(cl_iter_next(p), arg_end);
}


/**
  20. <Mul e.ArithmArg> == '-'? s.NUMBER? s.NUMBER
*/
R05_DEFINE_ENTRY_FUNCTION(Mul, "Mul") {
  struct arithm_arg arg;
  struct signed_number res;
  r05_number high = 0;
  cl_iter_t p = arg_begin;

  parse_arithm_arg(&arg, arg_begin, arg_end);
  res.sign = arg.x.sign * arg.y.sign;
  res.value = arg.x.value * arg.y.value;

  if (arg.x.value != 0 && res.value / arg.x.value != arg.y.value) {
    r05_number x = arg.x.value, y = arg.y.value, y_low, y_high;

    if (x > y) {
      r05_number prev_x = x;
      x = y;
      y = prev_x;
    }

    y_low = y;
    y_high = 0;
    res.value = 0;

    while (x > 0) {
      if (x & 1) {
        res.value += y_low;
        high += y_high;
        if (res.value < y_low) {
          high += 1;
        }
      }

      y_high <<= 1;
      y_high |= (y_low >> 31);
      y_low <<= 1;
      x >>= 1;
    }
  }

  p = emplace_number(res, high, p);
  r05_splice_to_freelist(cl_iter_next(p), arg_end);
}


/**
  21. <Numb s.Space* s.Sign? s.Digit* e.Skipped> == '-'? s.NUMBER+
      s.Space ::= ' ' | '\t'
      s.Sign ::= '+' | '-'
      s.Digit ::= '0' | '1' | '2' | '3' | '4' | '5' | '6' | '7' | '8' | '9'

      Если аргумент не начинается с последовательности цифр,
      функция возвращает 0.
*/
R05_DEFINE_ENTRY_FUNCTION(Numb, "Numb") {
  cl_iter_t callee = cl_iter_next(arg_begin);
  cl_iter_t p = cl_iter_next(callee);
  signed sign = +1;

  while (
    R05_DATATAG_CHAR == cl_iter_tag(p)
    && (' ' == cl_iter_char(p) || '\t' == cl_iter_char(p))
  ) {
    p = cl_iter_next(p);
  }

  if (R05_DATATAG_CHAR == cl_iter_tag(p)) {
    if ('-' == cl_iter_char(p)) {
      sign = -1;
      p = cl_iter_next(p);
    } else if ('+' == cl_iter_char(p)) {
      p = cl_iter_next(p);
    }
  }

  while (R05_DATATAG_CHAR == cl_iter_tag(p) && '0' == cl_iter_char(p)) {
    p = cl_iter_next(p);
  }

  if (
    R05_DATATAG_CHAR != cl_iter_tag(p)
    || ! isdigit((int) cl_iter_char(p))
  ) {
    cl_iter_set_tag(arg_begin, R05_DATATAG_NUMBER);
    cl_iter_set_number(arg_begin, 0);
    r05_splice_to_freelist(callee, arg_end);
  } else {
    cl_iter_t first_digit, target;
    size_t ndigits;
    r05_number accum;
    cl_iter_t first_10p, last_10p;

    enum {
      BITS_PORTION = 2 * sizeof(r05_number),
      PORTION_MASK = (1 << BITS_PORTION) - 1,
    };

    first_digit = p;
    ndigits = 0;
    while (
      R05_DATATAG_CHAR == cl_iter_tag(p)
      && isdigit((int) cl_iter_char(p))
    ) {
      p = cl_iter_next(p);
      ndigits += 1;
    }

    accum = 0;
    p = first_digit;
    target = first_10p = cl_iter_next(arg_begin);
    do {
      accum = accum * 10 + cl_iter_char(p) - '0';
      p = cl_iter_next(p);
      ndigits -= 1;
      if (ndigits % BITS_PORTION == 0) {
        cl_iter_set_tag(target, R05_DATATAG_NUMBER);
        cl_iter_set_number(target, accum);
        target = cl_iter_next(target);
        accum = 0;
      }
    } while (ndigits > 0);
    last_10p = cl_iter_prev(target);

    if (cl_iter_eq(first_10p, last_10p)) {
      if (cl_iter_number(first_10p) > 0 && sign < 0) {
        cl_iter_set_tag(arg_begin, R05_DATATAG_CHAR);
        cl_iter_set_char(arg_begin, '-');
      } else {
        r05_splice_to_freelist(arg_begin, cl_iter_prev(first_10p));
      }
      r05_splice_to_freelist(cl_iter_next(last_10p), arg_end);
    } else {
      int offset, power;
      r05_number power5 = 1;

      for (power = 0; power < BITS_PORTION / 2; ++power) {
        power5 *= 25;
      }

      offset = 0;
      accum = 0;
      target = arg_end;
      do {
          accum |= (cl_iter_number(last_10p) & PORTION_MASK) << offset;

          if (offset < 3 * BITS_PORTION) {
            offset += BITS_PORTION;
          } else {
            cl_iter_set_tag(target, R05_DATATAG_NUMBER);
            cl_iter_set_number(target, accum);
            target = cl_iter_prev(target);
            accum = 0;
            offset = 0;
          }

          p = last_10p;
          while (! cl_iter_eq(p, first_10p)) {
            cl_iter_t prev = cl_iter_prev(p);
            cl_iter_set_number(
              p,
              (cl_iter_number(prev) & PORTION_MASK) * power5
              + (cl_iter_number(p) >> BITS_PORTION)
            );
            p = prev;
          }
          cl_iter_set_number(
            first_10p, cl_iter_number(first_10p) >> BITS_PORTION
          );

          if (cl_iter_number(first_10p) == 0) {
            first_10p = cl_iter_next(first_10p);
          }
      } while (! cl_iter_eq(cl_iter_prev(first_10p), last_10p));

      if (accum > 0) {
        cl_iter_set_tag(target, R05_DATATAG_NUMBER);
        cl_iter_set_number(target, accum);
        target = cl_iter_prev(target);
      }

      if (sign < 0) {
        cl_iter_set_tag(target, R05_DATATAG_CHAR);
        cl_iter_set_char(target, '-');
        target = cl_iter_prev(target);
      }

      r05_splice_to_freelist(arg_begin, target);
    }
  }
}


/**
  22. <Open s.Mode s.FileNo e.FileName> == []
      s.Mode ::=
          'r' | 'w' | 'a'
        |  r  |  w  |  a
        |  rb |  wb |  ab
*/

static void ensure_close_stream(unsigned int file_no);

R05_DEFINE_ENTRY_FUNCTION(Open, "Open") {
  cl_iter_t eFileName[2], sMode, sFileNo;
  unsigned int file_no;
  char mode_str[2] = { '.', '\0' };
  const char *mode = mode_str;
  static const char filename_format[] = "REFAL%u.DAT";
  enum {
    DEFAULT_LEN = sizeof(filename_format) + UINT_DIGITS,
    FILENAME_LEN = FILENAME_MAX > DEFAULT_LEN ? FILENAME_MAX : DEFAULT_LEN
  };
  char filename[FILENAME_LEN + 1] = { '\0' };
  size_t filename_len;

  if (
    ! r05_svar_left(&sMode, cl_iter_next(arg_begin), arg_end)
    || (
      R05_DATATAG_CHAR != cl_iter_tag(sMode)
      && R05_DATATAG_FUNCTION != cl_iter_tag(sMode)
    )
    || ! r05_svar_left(&sFileNo, sMode, arg_end)
    || R05_DATATAG_NUMBER != cl_iter_tag(sFileNo)
  ) {
    r05_recognition_impossible();
  }

  filename_len =
    r05_read_chars(eFileName, filename, FILENAME_LEN, sFileNo, arg_end);

  file_no = cl_iter_number(sFileNo) % FILE_LIMIT;

  if (filename_len != 0) {
    filename[filename_len] = '\0';
  } else {
    sprintf(filename, filename_format, file_no);
  }

  if (R05_DATATAG_CHAR == cl_iter_tag(sMode)) {
    char mode = cl_iter_char(sMode);
    if (mode != 'r' && mode != 'w' && mode != 'a') {
      r05_builtin_error("Bad file mode, expected 'r', 'w' or 'a'");
    }
    mode_str[0] = mode;
  } else {
    mode = cl_iter_function(sMode)->name;
  }

  if (! r05_empty_hole(eFileName[1], arg_end)) {
    static const char error_format[] =
      "Very long file name, maximum available is %u";
    char error[sizeof(error_format) + UINT_DIGITS];

    sprintf(error, error_format, (unsigned int) FILENAME_MAX);
    r05_builtin_error(error);
  }

  ensure_close_stream(file_no);

  s_streams[file_no] = fopen(filename, mode);
  if (s_streams[file_no] == NULL) {
    char mode_buffer[100] = { '\0' };
    static const char error_format[] = "Can't open %s for \"%s\"";
    char error[sizeof(error_format) + FILENAME_MAX + sizeof(mode_buffer)];

    strncpy(mode_buffer, mode, sizeof(mode_buffer) - 1);
    sprintf(error, error_format, filename, mode_buffer);
    r05_builtin_error_errno(error);
  }

  r05_splice_to_freelist(arg_begin, arg_end);
}

static void ensure_close_stream(unsigned int file_no) {
  if (s_streams[file_no] != NULL && fclose(s_streams[file_no]) == EOF) {
    r05_builtin_error_errno("Can't close stream");
  }

  s_streams[file_no] = NULL;
}


/**
  23. <Ord e.Expr> == e.Expr’

  В e.Expr’ все литеры заменены на их коды ASCII
*/
R05_DEFINE_ENTRY_FUNCTION(Ord, "Ord") {
  cl_iter_t callee = cl_iter_next(arg_begin);
  cl_iter_t p = cl_iter_next(callee);

  while (! cl_iter_eq(p, arg_end)) {
    if (cl_iter_tag(p) == R05_DATATAG_CHAR) {
      cl_iter_set_tag(p, R05_DATATAG_NUMBER);
      cl_iter_set_number(p, (unsigned char) cl_iter_char(p));
    }
    p = cl_iter_next(p);
  }

  r05_splice_to_freelist(arg_begin, callee);
  r05_splice_to_freelist(arg_end, arg_end);
}


enum output_func_type {
  PRINT, PROUT, PUT, PUTOUT, WRITE
};

static void output_func(
  cl_iter_t arg_begin, cl_iter_t arg_end,
  enum output_func_type type
) {
  cl_iter_t callee = cl_iter_next(arg_begin);
  cl_iter_t p, before_expr;
  FILE *output;

  if (type == PRINT || type == PROUT) {
    before_expr = callee;
    output = stdout;
  } else if (type == PUT || type == PUTOUT || type == WRITE) {
    cl_iter_t pfile_no = cl_iter_next(callee);

    if (R05_DATATAG_NUMBER != cl_iter_tag(pfile_no)) {
      r05_recognition_impossible();
    }

    before_expr = pfile_no;
    output = open_numbered((unsigned int) cl_iter_number(pfile_no), 'w');
  } else {
    r05_switch_default_violation(type);
  }

#define CHECK_PRINTF(printf_call) \
  ((printf_call) >= 0 ? (void) 0 \
  : r05_builtin_error_errno("Error in call " #printf_call))
#define CHECK_PUTC(putc_call) \
  ((putc_call) != EOF ? (void) 0 \
  : r05_builtin_error_errno("Error in call " #putc_call))

  for (
    p = cl_iter_next(before_expr);
    ! cl_iter_eq(p, arg_end);
    p = cl_iter_next(p)
  ) {
    switch (cl_iter_tag(p)) {
      case R05_DATATAG_CHAR:
        CHECK_PUTC(putc(cl_iter_char(p), output));
        break;

      case R05_DATATAG_FUNCTION:
        CHECK_PRINTF(fprintf(output, "%s ", cl_iter_function(p)->name));
        break;

      case R05_DATATAG_NUMBER:
        CHECK_PRINTF(
          fprintf(output, "%lu ", (long unsigned int) cl_iter_number(p))
        );
        break;

      case R05_DATATAG_OPEN_BRACKET:
        CHECK_PUTC(putc('(', output));
        break;

      case R05_DATATAG_CLOSE_BRACKET:
        CHECK_PUTC(putc(')', output));
        break;

      default:
        r05_switch_default_violation(cl_iter_tag(p));
    }
  }

  if (type != WRITE) {
    CHECK_PRINTF(fprintf(output, "\n"));
  }

#undef CHECK_PRINTF

  if (type == PRINT || type == PUT) {
    r05_splice_to_freelist(arg_begin, before_expr);
    r05_splice_to_freelist(arg_end, arg_end);
  } else if (type == PROUT || type == PUTOUT || type == WRITE) {
    r05_splice_to_freelist(arg_begin, arg_end);
  } else {
    r05_switch_default_violation(type);
  }
}


/**
  24. <Print e.Expr> == []
*/
R05_DEFINE_ENTRY_FUNCTION(Print, "Print") {
  output_func(arg_begin, arg_end, PRINT);
}


/**
  25. <Prout e.Expr> == []
*/
R05_DEFINE_ENTRY_FUNCTION(Prout, "Prout") {
  output_func(arg_begin, arg_end, PROUT);
}


/**
  26. <Put s.FileNo e.Expr> == []
*/
R05_DEFINE_ENTRY_FUNCTION(Put, "Put") {
  output_func(arg_begin, arg_end, PUT);
}


/**
  27. <Putout s.FileNo e.Expr> == []
*/
R05_DEFINE_ENTRY_FUNCTION(Putout, "Putout") {
  output_func(arg_begin, arg_end, PUTOUT);
}


/**
  28. <Rp e.Key '=' e.Value> == empty
*/
ALIAS_DESCRIPTOR(Rp, "Rp", r05_rp);


/**
  29. <Step> == s.NUMBER
*/
R05_DEFINE_ENTRY_FUNCTION(Step, "Step") {
  cl_iter_set_tag(arg_begin, R05_DATATAG_NUMBER);
  cl_iter_set_number(arg_begin, r05_step_count());
  r05_splice_to_freelist(cl_iter_next(arg_begin), arg_end);
}


/**
  30. <Sub s.NUMBER s.NUMBER> == '-' s.NUMBER
*/
R05_DEFINE_ENTRY_FUNCTION(Sub, "Sub") {
  struct arithm_arg arg;

  parse_arithm_arg(&arg, arg_begin, arg_end);
  arg.y.sign = -arg.y.sign;
  add(&arg, arg_begin, arg_end);
}


/**
  31. <Symb e.Sign s.NUMBER+> == e.Sign s.CHAR+
      e.Sign ::= '+' | '-' | пусто
*/
R05_DEFINE_ENTRY_FUNCTION(Symb, "Symb") {
  cl_iter_t callable = cl_iter_next(arg_begin);
  cl_iter_t number_start = cl_iter_next(callable);
  cl_iter_t p;


  if (R05_DATATAG_CHAR == cl_iter_tag(number_start)) {
    char sign = cl_iter_char(number_start);

    if (sign != '+' && sign != '-') {
      r05_recognition_impossible();
    }
    number_start = cl_iter_next(number_start);
  }

  if (R05_DATATAG_NUMBER != cl_iter_tag(number_start)) {
    r05_recognition_impossible();
  }
  p = number_start;

  while (R05_DATATAG_NUMBER == cl_iter_tag(p) && 0 == cl_iter_number(p)) {
    p = cl_iter_next(p);
  }

  if (cl_iter_eq(p, arg_end)) {
    cl_iter_set_tag(number_start, R05_DATATAG_CHAR);
    cl_iter_set_char(number_start, '0');
    r05_splice_to_freelist(arg_begin, callable);
    r05_splice_to_freelist(cl_iter_next(number_start), arg_end);
  } else if (R05_DATATAG_NUMBER == cl_iter_tag(p)) {
    size_t nmacrodigits;
    cl_iter_t pvalue_start = p;

    nmacrodigits = 0;
    while (R05_DATATAG_NUMBER == cl_iter_tag(p)) {
      nmacrodigits += 1;
      p = cl_iter_next(p);
    }

    if (cl_iter_eq(p, arg_end)) {
      size_t nbits = nmacrodigits * R05_NUMBER_BITS;
      size_t ndecdigits = (nbits * 28 + 92) / 93;
      size_t i;
      cl_iter_t insert_pos, last_dec_digit;
      r05_number rem;
      int last_loop;

      enum {
        DEC_CHUNK = R05_NUMBER_BITS == 32 ? 4 : 9,
        DEC_POWER = R05_NUMBER_BITS == 32 ? 10 * 1000 : 1000 * 1000 * 1000,
        HALF = R05_NUMBER_BITS / 2,
      };

      const r05_number HALF_MASK = ((r05_number) 1 << HALF) - 1;

      r05_reset_allocator();
      for (i = 0; i < ndecdigits; ++i) {
        r05_alloc_char('0');
      }
      insert_pos = cl_iter_next(arg_end);
      r05_splice_from_freelist(insert_pos);
      last_dec_digit = cl_iter_prev(insert_pos);

      do {
        rem = 0;
        p = pvalue_start;
        do {
          r05_number high, low;

          high = (rem << HALF) | (cl_iter_number(p) >> HALF);
          rem = high % DEC_POWER;
          high /= DEC_POWER;

          low = (rem << HALF) | (cl_iter_number(p) & HALF_MASK);
          rem = low % DEC_POWER;
          low /= DEC_POWER;

          cl_iter_set_number(p, (high << HALF) | low);
          p = cl_iter_next(p);
        } while (! cl_iter_eq(p, arg_end));

        if (0 == cl_iter_number(pvalue_start)) {
          pvalue_start = cl_iter_next(pvalue_start);
        }

        last_loop = cl_iter_eq(pvalue_start, arg_end);
        for (i = 0; last_loop ? rem > 0 : i < DEC_CHUNK; ++i) {
          cl_iter_set_char(
            last_dec_digit, cl_iter_char(last_dec_digit) + (char) (rem % 10)
          );
          rem /= 10;
          last_dec_digit = cl_iter_prev(last_dec_digit);
        }
      } while (! last_loop);

      r05_splice_to_freelist(arg_begin, callable);
      r05_splice_to_freelist(number_start, last_dec_digit);
    } else {
      r05_recognition_impossible();
    }
  } else {
    r05_recognition_impossible();
  }
}


/**
  32. <Time> == s.CHAR+
*/
R05_DEFINE_ENTRY_FUNCTION(Time, "Time") {
  char *time_str, *n;
  time_t now;

  if (! cl_iter_eq(cl_iter_next(cl_iter_next(arg_begin)), arg_end)) {
    r05_recognition_impossible();
  }

  time(&now);
  time_str = ctime(&now);
  n = strchr(time_str, '\n');
  assert(n != NULL);
  *n = '\0';

  r05_reset_allocator();
  r05_alloc_string(time_str);
  r05_splice_from_freelist(arg_begin);
  r05_splice_to_freelist(arg_begin, arg_end);
}


/**
  33. <Type e.Expr> == s.Type s.SubType e.Expr
      Type of first term of e.Expr

      s.Type s.SubType ::=
          'Lu' — uppercase latin letter
        | 'Ll' — lowercase latin letter
        | 'D0' — decimal digit
        | 'Wi' — identifier (function)
        | 'Wq' — quotted identifier
        | 'N0' — number
        | 'Pu' — isprint() && isupper()
        | 'Pl' — isprint() && ! isupper()
        | 'Ou' — other && isupper()
        | 'Ol' — other && ! isupper()
        | 'B0' — brackets
        | '*0' — empty expression
*/
R05_DEFINE_ENTRY_FUNCTION(Type, "Type") {
  cl_iter_t callee = cl_iter_next(arg_begin);
  cl_iter_t first_term = cl_iter_next(callee);
  char type, subtype;

  if (cl_iter_eq(first_term, arg_end)) {
    type = '*';
    subtype = '0';
  } else if (R05_DATATAG_CHAR == cl_iter_tag(first_term)) {
    char ch = cl_iter_char(first_term);

    if (isupper(ch)) {
      subtype = 'u';
    } else {
      subtype = 'l';
    }
    if (isalpha(ch)) {
      type = 'L';
    } else if (isdigit(ch)) {
      type = 'D';
      subtype = '0';
    } else if (isprint(ch)) {
      type = 'P';
    } else {
      type = 'O';
    }
  } else if (R05_DATATAG_FUNCTION == cl_iter_tag(first_term)) {
    type = 'W';
    subtype = 'q';

    if (isalpha((int) cl_iter_function(first_term)->name[0])) {
      const char *p = &cl_iter_function(first_term)->name[1];
      while (*p != '\0' && is_ident_tail(*p)) {
        p++;
      }

      if (*p == '\0') {
        subtype = 'i';
      }
    }
  } else if (R05_DATATAG_NUMBER == cl_iter_tag(first_term)) {
    type = 'N';
    subtype = '0';
  } else if (R05_DATATAG_OPEN_BRACKET == cl_iter_tag(first_term)) {
    type = 'B';
    subtype = '0';
  } else {
    r05_switch_default_violation(cl_iter_tag(first_term));
#ifndef R05_NORETURN_DEFINED
    return;
#endif
  }

  cl_iter_set_tag(arg_begin, R05_DATATAG_CHAR);
  cl_iter_set_char(arg_begin, type);
  cl_iter_set_tag(callee, R05_DATATAG_CHAR);
  cl_iter_set_char(callee, subtype);

  r05_splice_to_freelist(arg_end, arg_end);
}


/**
   34. <Upper e.Expr> == e.Expr’

   В e.Expr’ все литеры приведены к верхнему регистру
*/
R05_DEFINE_ENTRY_FUNCTION(Upper, "Upper") {
  cl_iter_t callee = cl_iter_next(arg_begin);
  cl_iter_t p = cl_iter_next(callee);

  while (! cl_iter_eq(p, arg_end)) {
    if (cl_iter_tag(p) == R05_DATATAG_CHAR) {
      cl_iter_set_char(p, (char) toupper(cl_iter_char(p)));
    }
    p = cl_iter_next(p);
  }

  r05_splice_to_freelist(arg_begin, callee);
  r05_splice_to_freelist(arg_end, arg_end);
}


/**
  44. Пустая функция с именем ""
*/
struct r05_function r05f_ = {
  r05_enum_function_code, "", 1, NULL, R05_INIT_PROFILER
};



/**
  48. <Up ↓e.Expr> == e.Expr′
*/
R05_IMPLEMENT_METAFUNCTION(Up, "Up") {
  (void) arg_begin, (void) arg_end;
  r05_builtin_error("Metafunction Up is not implemented yet");
}


/**
  49. <Ev-met ↓e.Expr> == { 0 | 1 | 2 } ↓e.Expr′
*/
R05_IMPLEMENT_METAFUNCTION(Evm_met, "Ev-met") {
  (void) arg_begin, (void) arg_end;
  r05_builtin_error("Metafunction Ev-met is not implemented yet");
}


/**
   50. <Residue s.Func e.Arg> == <s.Func e.Arg>
*/
R05_IMPLEMENT_METAFUNCTION(Residue, "Residue") {
  r05c_Mu(arg_begin, arg_end);
}

void r05c_k3F_(cl_iter_t arg_begin, cl_iter_t arg_end) {
  r05c_Residue(arg_begin, arg_end);
}


/**
  51. <GetEnv e.EnvName> == e.EnvValue
      e.EnvName, e.EnvValue ::= s.CHAR*
*/
R05_DEFINE_ENTRY_FUNCTION(GetEnv, "GetEnv") {
  cl_iter_t eEnvName[2];
  enum { MAX_ENV = 2000 };
  char env_name[MAX_ENV + 1];
  size_t env_name_len;
  const char *env_value;

  env_name_len =
    r05_read_chars(eEnvName, env_name, MAX_ENV, cl_iter_next(arg_begin), arg_end);

  if (! r05_empty_hole(eEnvName[1], arg_end)) {
    if (R05_DATATAG_CHAR == cl_iter_tag(cl_iter_next(eEnvName[1]))) {
      r05_builtin_error("very long environment variable name");
    } else {
      r05_recognition_impossible();
    }
  }

  env_name[env_name_len] = '\0';
  env_value = getenv(env_name);
  if (! env_value) {
    env_value = "";
  }

  r05_reset_allocator();
  r05_alloc_string(env_value);
  r05_splice_from_freelist(arg_begin);
  r05_splice_to_freelist(arg_begin, arg_end);
}


/**
  52. <System e.Command> == e.RetCode
      e.RetCode ::= '-'? s.NUMBER
*/
R05_DEFINE_ENTRY_FUNCTION(System, "System") {
  cl_iter_t eCommand[2];
  enum { MAX_COMMAND = 2000 };
  char command[MAX_COMMAND + 1];
  size_t command_len;
  int retcode;

  command_len =
    r05_read_chars(eCommand, command, MAX_COMMAND, cl_iter_next(arg_begin), arg_end);

  if (! r05_empty_hole(eCommand[1], arg_end)) {
    if (R05_DATATAG_CHAR == cl_iter_tag(cl_iter_next(eCommand[1]))) {
      r05_builtin_error("very long command line");
    } else {
      r05_recognition_impossible();
    }
  }

  command[command_len] = '\0';
  fflush(NULL);
  retcode = system(command);

#if defined(WIFEXITED) && defined(WEXITSTATUS)
  if (WIFEXITED(retcode)) {
    retcode = WEXITSTATUS(retcode);
  } else {
    retcode = -1;
  }
#endif  /* defined(WIFEXITED) && defined(WEXITSTATUS) */

  r05_reset_allocator();
  if (retcode < 0) {
    r05_alloc_char('-');
    retcode = -retcode;
  }
  r05_alloc_number((r05_number) retcode);
  r05_splice_from_freelist(arg_begin);
  r05_splice_to_freelist(arg_begin, arg_end);
}


/**
  53. <Exit e.RetCode>
*/
R05_DEFINE_ENTRY_FUNCTION(Exit, "Exit") {
  cl_iter_t callable = cl_iter_next(arg_begin);
  cl_iter_t pretcode = cl_iter_next(callable);
  int retcode;
  signed sign = +1;

  if (cl_iter_eq(pretcode, arg_end)) {
    r05_recognition_impossible();
  }

  if (R05_DATATAG_CHAR == cl_iter_tag(pretcode)) {
    if ('-' == cl_iter_char(pretcode)) {
      sign = -1;
      pretcode = cl_iter_next(pretcode);
    } else {
      r05_recognition_impossible();
    }
  }

  if (
    cl_iter_eq(pretcode, arg_end)
    || R05_DATATAG_NUMBER != cl_iter_tag(pretcode)
    || ! cl_iter_eq(cl_iter_next(pretcode), arg_end)
  ) {
    r05_recognition_impossible();
  }

  retcode = sign * (int) cl_iter_number(pretcode);
  r05_exit(retcode);
}


/**
  54. <Close s.FileNo> == []
*/
R05_DEFINE_ENTRY_FUNCTION(Close, "Close") {
  cl_iter_t callable = cl_iter_next(arg_begin);
  cl_iter_t pfile_no = cl_iter_next(callable);
  unsigned int file_no;

  if (
    cl_iter_eq(pfile_no, arg_end)
    || R05_DATATAG_NUMBER != cl_iter_tag(pfile_no)
    || ! cl_iter_eq(cl_iter_next(pfile_no), arg_end)
  ) {
    r05_recognition_impossible();
  }

  file_no = (unsigned int) cl_iter_number(pfile_no) % FILE_LIMIT;
  ensure_close_stream(file_no);
  r05_splice_to_freelist(arg_begin, arg_end);
}


/**
  55. <ExistFile e.FileName>
        == True
        == False
      e.FileName ::= s.CHAR+
*/
R05_DEFINE_LOCAL_ENUM(True, "True")
R05_DEFINE_LOCAL_ENUM(False, "False")

R05_DEFINE_ENTRY_FUNCTION(ExistFile, "ExistFile") {
  cl_iter_t callee = cl_iter_next(arg_begin);
  cl_iter_t eFileName[2];
  char filename[FILENAME_MAX + 1];
  size_t filename_len;
  FILE *file;

  filename_len =
    r05_read_chars(eFileName, filename, FILENAME_MAX, callee, arg_end);

  if (! r05_empty_hole(eFileName[1], arg_end)) {
    if (R05_DATATAG_CHAR == cl_iter_tag(cl_iter_next(eFileName[1]))) {
      r05_builtin_error("very long filename");
    } else {
      r05_recognition_impossible();
    }
  }

  filename[filename_len] = '\0';
  file = fopen(filename, "r");

  cl_iter_set_tag(arg_begin, R05_DATATAG_FUNCTION);
  if (file != NULL) {
    cl_iter_set_function(arg_begin, &r05f_True);
    if (fclose(file) == EOF) {
      r05_builtin_error_errno("fclose error");
    }
  } else {
    cl_iter_set_function(arg_begin, &r05f_False);
  }

  r05_splice_to_freelist(callee, arg_end);
}


/**
  57. <RemoveFile e.FileName>
        == True ()
        == False (e.Message)
      e.Message ::= s.CHAR*
*/
R05_DEFINE_ENTRY_FUNCTION(RemoveFile, "RemoveFile") {
  cl_iter_t eFileName[2], left_bracket, right_bracket;
  char filename[FILENAME_MAX + 1];
  size_t filename_len;
  int res;
  struct r05_function *sign;
  const char *message;

  filename_len =
    r05_read_chars(eFileName, filename, FILENAME_MAX, cl_iter_next(arg_begin), arg_end);

  if (! r05_empty_hole(eFileName[1], arg_end)) {
    if (R05_DATATAG_CHAR == cl_iter_tag(cl_iter_next(eFileName[1]))) {
      r05_builtin_error("very long filename");
    } else {
      r05_recognition_impossible();
    }
  }

  filename[filename_len] = '\0';
  errno = 0;
  res = remove(filename);

  if (res == 0) {
    sign = &r05f_True;
    message = "";
  } else {
    sign = &r05f_False;
    message = strerror(errno);
  }

  r05_reset_allocator();
  r05_alloc_function(sign);
  r05_alloc_open_bracket(&left_bracket);
  r05_alloc_string(message);
  r05_alloc_close_bracket(&right_bracket);
  r05_link_brackets(left_bracket, right_bracket);
  r05_splice_from_freelist(arg_begin);
  r05_splice_to_freelist(arg_begin, arg_end);
}


/**
  58. <Implode_Ext s.CHAR*> == s.FUNCTION
*/
R05_DEFINE_ENTRY_FUNCTION(Implodeu_Ext, "Implode_Ext") {
  cl_iter_t func_name = cl_iter_next(arg_begin);
  cl_iter_t current;

  current = cl_iter_next(func_name);
  while (R05_DATATAG_CHAR == cl_iter_tag(current)) {
    current = cl_iter_next(current);
  }

  if (! cl_iter_eq(current, arg_end)) {
    r05_recognition_impossible();
  }

  cl_iter_set_tag(arg_begin, R05_DATATAG_FUNCTION);
  cl_iter_set_function(
    arg_begin, implode(cl_iter_next(func_name), cl_iter_prev(arg_end))
  );
  r05_splice_to_freelist(func_name, arg_end);
}


/**
  59. <Explode_Ext s.FUNCTION> == s.CHAR+
*/
ALIAS_DESCRIPTOR(Explodeu_Ext, "Explode_Ext", r05c_Explode)


/**
  60. <TimeElapsed 0?> == s.CHAR+
*/
R05_DEFINE_ENTRY_FUNCTION(TimeElapsed, "TimeElapsed") {
  cl_iter_t func_name = cl_iter_next(arg_begin);
  cl_iter_t maybe_zero = cl_iter_next(func_name);
  double now = r05_time_elapsed();
  char buffer[100];
  static double last_cutoff = 0;

  sprintf(buffer, "%.3f", now - last_cutoff);

  if (
    R05_DATATAG_NUMBER == cl_iter_tag(maybe_zero)
    && 0 == cl_iter_number(maybe_zero)
    && cl_iter_eq(cl_iter_next(maybe_zero), arg_end)
  ) {
    last_cutoff = now;
  } else if (! cl_iter_eq(maybe_zero, arg_end)) {
    r05_recognition_impossible();
  }

  r05_reset_allocator();
  r05_alloc_string(buffer);
  r05_splice_from_freelist(arg_begin);
  r05_splice_to_freelist(arg_begin, arg_end);
}


/**
  61. <Compare e.ArithmArg> == '-' | '0' | '+'

  Функция возвращает знак разности между аргументами
*/
R05_DEFINE_ENTRY_FUNCTION(Compare, "Compare") {
  struct arithm_arg arg;
  signed result;

  parse_arithm_arg(&arg, arg_begin, arg_end);

  result =
    arg.x.sign < arg.y.sign ? -1 :
    arg.x.sign > arg.y.sign ? +1 : 0;

  if (result == 0) {
    result =
      arg.x.value < arg.y.value ? -1 :
      arg.x.value > arg.y.value ? +1 : 0;

    result *= arg.x.sign;
  }

  cl_iter_set_tag(arg_begin, R05_DATATAG_CHAR);
  /* Suppress false warning in BCC 5.5.1 */
  cl_iter_set_char(arg_begin, (char) (result < 0 ? '-' : result > 0 ? '+' : '0'));
  r05_splice_to_freelist(cl_iter_next(arg_begin), arg_end);
}


static r05_number random_digit_in_range(r05_number max);
static r05_number random_digit(void);


/**
  64. <Random s.Len> == e.RandomDigits
      e.RandomDigits ::= s.NUMBER+
      |e.RandomDigits| == ((s.Len != 0) ? s.Len : 1)
*/
R05_DEFINE_ENTRY_FUNCTION(Random, "Random") {
  cl_iter_t callable = cl_iter_next(arg_begin);
  cl_iter_t pcount = cl_iter_next(callable);
  r05_number count;

  if (
    R05_DATATAG_NUMBER != cl_iter_tag(pcount)
    || ! cl_iter_eq(cl_iter_next(pcount), arg_end)
  ) {
    r05_recognition_impossible();
  }

  count = cl_iter_number(pcount);
  count = count > 0 ? count - 1 : 1;
  count = random_digit_in_range(count) + 1;

  r05_reset_allocator();
  while (count > 0) {
    r05_alloc_number(random_digit());
    --count;
  }

  r05_splice_from_freelist(arg_begin);
  r05_splice_to_freelist(arg_begin, arg_end);
}


/**
  65. <RandomDigit s.Max> == s.RandomDigit
      s.RandomDigit, s.Max ::= s.NUMBER
      s.RandomDigit <= s.Max
*/
R05_DEFINE_ENTRY_FUNCTION(RandomDigit, "RandomDigit") {
  cl_iter_t callee = cl_iter_next(arg_begin);
  cl_iter_t pmax = cl_iter_next(callee);
  const r05_number MAX = ~0;
  r05_number max, res;

  if (
    R05_DATATAG_NUMBER != cl_iter_tag(pmax)
    || ! cl_iter_eq(cl_iter_next(pmax), arg_end)
  ) {
    r05_recognition_impossible();
  }

  max = cl_iter_number(pmax);
  if (max != MAX) {
    res = random_digit_in_range(max + 1);
  } else {
    res = random_digit();
  }

  cl_iter_set_tag(arg_begin, R05_DATATAG_NUMBER);
  cl_iter_set_number(arg_begin, res);
  r05_splice_to_freelist(callee, arg_end);
}


static r05_number random_digit_in_range(r05_number limit) {
  const r05_number MAX = ~0;
  r05_number max_valid;
  r05_number random;

  if (limit == 0) {
    return 0;
  }

  max_valid = MAX - MAX % limit;

  do {
    random = random_digit();
  } while (random >= max_valid);

  return random % limit;
}

/*
  Метод Фибоначчи с запаздываниями.
  См. D. E. Knuth, The Art of Computer Programming,
  Volume 2, chapter 3.2.2, program A
*/
static r05_number random_digit(void) {
  enum { cMinDelay = 24, cMaxDelay = 55 };

  static int init = 0;
  static size_t k = cMaxDelay - 1;
  static size_t j = cMinDelay - 1;
  static r05_number y[cMaxDelay];

  r05_number result;

  if (! init) {
    r05_number seed = (r05_number) time(NULL);
    size_t i;

    for (i = 0; i < cMaxDelay; ++i) {
      seed = seed * 1103515245 + 12345;
      y[i] = seed;
    }

    init = 1;
  }

  result = y[k] = y[k] + y[j];
  k = (k + cMaxDelay - 1) % cMaxDelay;
  j = (j + cMaxDelay - 1) % cMaxDelay;

  return result;
}


/**
  66. <Write s.FileNo e.Expr> == []
*/
R05_DEFINE_ENTRY_FUNCTION(Write, "Write") {
  output_func(arg_begin, arg_end, WRITE);
}


/**
  67. <ListOfBuiltin> == (s.No s.Name s.Type)*

      s.No ::= s.NUMBER
      s.Name ::= s.FUNCTION
      s.Type ::= special | regular
*/
R05_DEFINE_LOCAL_ENUM(special, "special")
R05_DEFINE_LOCAL_ENUM(regular, "regular")

R05_DECLARE_ENTRY_FUNCTION(ListOfBuiltin);

static struct builtin_info s_builtin_info[100] = {
#define ALLOC_BUILTIN(id, function, type) \
  { id, &r05f_ ## function, &r05f_ ## type },

  ALLOC_BUILTIN(1, Mu, special)
  ALLOC_BUILTIN(2, Add, regular)
  ALLOC_BUILTIN(3, Arg, regular)
  ALLOC_BUILTIN(4, Br, regular)
  ALLOC_BUILTIN(5, Card, regular)
  ALLOC_BUILTIN(6, Chr, regular)
  ALLOC_BUILTIN(7, Cp, regular)
  ALLOC_BUILTIN(8, Dg, regular)
  ALLOC_BUILTIN(10, Div, regular)
  ALLOC_BUILTIN(11, Divmod, regular)
  ALLOC_BUILTIN(12, Explode, regular)
  ALLOC_BUILTIN(13, First, regular)
  ALLOC_BUILTIN(14, Get, regular)
  ALLOC_BUILTIN(15, Implode, regular)
  ALLOC_BUILTIN(17, Lenw, regular)
  ALLOC_BUILTIN(18, Lower, regular)
  ALLOC_BUILTIN(19, Mod, regular)
  ALLOC_BUILTIN(20, Mul, regular)
  ALLOC_BUILTIN(21, Numb, regular)
  ALLOC_BUILTIN(22, Open, regular)
  ALLOC_BUILTIN(23, Ord, regular)
  ALLOC_BUILTIN(24, Print, regular)
  ALLOC_BUILTIN(25, Prout, regular)
  ALLOC_BUILTIN(26, Put, regular)
  ALLOC_BUILTIN(27, Putout, regular)
  ALLOC_BUILTIN(28, Rp, regular)
  ALLOC_BUILTIN(29, Step, regular)
  ALLOC_BUILTIN(30, Sub, regular)
  ALLOC_BUILTIN(31, Symb, regular)
  ALLOC_BUILTIN(32, Time, regular)
  ALLOC_BUILTIN(33, Type, regular)
  ALLOC_BUILTIN(34, Upper, regular)
  /* ALLOC_BUILTIN(35, Sysfun, regular) */
  /* ALLOC_BUILTIN(42, Impd_d_, regular) */
  /* ALLOC_BUILTIN(43, Stopd_d_, regular) */
  { 44, &r05f_, &r05f_regular },
  /* ALLOC_BUILTIN(45, Freeze, regular) */
  /* ALLOC_BUILTIN(46, Freezer, regular) */
  /* ALLOC_BUILTIN(47, Dn, regular) */
  ALLOC_BUILTIN(48, Up, special)
  ALLOC_BUILTIN(49, Evm_met, special)
  ALLOC_BUILTIN(50, Residue, special)
  ALLOC_BUILTIN(51, GetEnv, regular)
  ALLOC_BUILTIN(52, System, regular)
  ALLOC_BUILTIN(53, Exit, regular)
  ALLOC_BUILTIN(54, Close, regular)
  ALLOC_BUILTIN(55, ExistFile, regular)
  /* ALLOC_BUILTIN(56, GetCurrentDirectory, regular) */
  ALLOC_BUILTIN(57, RemoveFile, regular)
  ALLOC_BUILTIN(58, Implodeu_Ext, regular)
  ALLOC_BUILTIN(59, Explodeu_Ext, regular)
  ALLOC_BUILTIN(60, TimeElapsed, regular)
  ALLOC_BUILTIN(61, Compare, regular)
  /* ALLOC_BUILTIN(62, DeSysfun, regular) */
  /* ALLOC_BUILTIN(63, XMLParse, regular) */
  ALLOC_BUILTIN(64, Random, regular)
  ALLOC_BUILTIN(65, RandomDigit, regular)
  ALLOC_BUILTIN(66, Write, regular)
  ALLOC_BUILTIN(67, ListOfBuiltin, regular)
  /* ALLOC_BUILTIN(68, SizeOf, regular) */
  /* ALLOC_BUILTIN(69, GetPID, regular) */
  /* ALLOC_BUILTIN(70, int4fab_1, regular) */
  /* ALLOC_BUILTIN(71, GetPPID, regular) */

#undef ALLOC_BUILTIN

  { 0, NULL, NULL },
};


R05_DEFINE_ENTRY_FUNCTION(ListOfBuiltin, "ListOfBuiltin") {
  cl_iter_t callee = cl_iter_next(arg_begin);
  cl_iter_t left_bracket, right_bracket;
  struct builtin_info *info;

  if (! cl_iter_eq(cl_iter_next(callee), arg_end)) {
    r05_recognition_impossible();
  }

  r05_reset_allocator();
  for (info = s_builtin_info; info->function != NULL; ++info) {
    r05_alloc_open_bracket(&left_bracket);
    r05_alloc_number(info->id);
    r05_alloc_function(info->function);
    r05_alloc_function(info->type);
    r05_alloc_close_bracket(&right_bracket);
    r05_link_brackets(left_bracket, right_bracket);
  }

  r05_splice_from_freelist(arg_begin);
  r05_splice_to_freelist(arg_begin, arg_end);
};
