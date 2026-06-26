#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "compact_list.h"

static void dummy_fn(struct r05_node *b, struct r05_node *e) { (void)b; (void)e; }
static struct r05_function g_dummy_func = { dummy_fn, "Dummy", 0, NULL };


static void print_item(const cm_item_t *it) {
  switch (it->tag) {
    case R05_DATATAG_CHAR:
      printf("'%c'", it->info.char_);
      break;
    case R05_DATATAG_NUMBER:
      printf("%u", it->info.number);
      break;
    case R05_DATATAG_FUNCTION:
      printf("<%s>", it->info.function ? it->info.function->name : "?");
      break;
    case R05_DATATAG_OPEN_BRACKET:
      printf("(");
      break;
    case R05_DATATAG_CLOSE_BRACKET:
      printf(")");
      break;
    default:
      printf("?");
      break;
  }
}

static void print_list(cm_list_t *list, const char *label) {
  cm_iter_t it  = cm_list_begin(list);
  cm_iter_t end = cm_list_end(list);

  printf("  %-30s [ ", label);
  while (!cm_iter_eq(it, end)) {
    print_item(cm_iter_get(it));
    printf(" ");
    it = cm_iter_next(it);
  }
  printf("]\n");
}

static void print_structure(cm_list_t *list) {
  cm_macronode_t *cur = list->head->next_macro;
  printf("  Макрозвенья: ");
  while (cur != list->tail) {
    printf("[");
    for (int i = 0; i < cur->count; i++) {
      print_item(&cur->items[i]);
      if (i + 1 < cur->count) printf(",");
    }
    printf("](cap=%d) ", cur->capacity);
    cur = cur->next_macro;
  }
  printf("\n");
}

static cm_iter_t find_char(cm_list_t *list, char c) {
  cm_iter_t it  = cm_list_begin(list);
  cm_iter_t end = cm_list_end(list);
  while (!cm_iter_eq(it, end)) {
    cm_item_t *item = cm_iter_get(it);
    if (item->tag == R05_DATATAG_CHAR && item->info.char_ == c) {
      return it;
    }
    it = cm_iter_next(it);
  }
  assert(0 && "символ не найден");
  return end;
}

static void print_stats(cm_list_t *list) {
  cm_stats_t s = cm_list_stats(list);
  printf("  Элементов: %d, Макрозвеньев: %d\n",
         s.total_elements, s.total_macronodes);
  printf("  Компактное представление: %zu байт\n", s.compact_bytes);
  printf("  Классическое (r05_node): %zu байт (sizeof r05_node=%zu)\n",
         s.classic_bytes, sizeof(struct r05_node));
  if (s.classic_bytes > 0) {
    int pct = (int)(100 - 100 * s.compact_bytes / s.classic_bytes);
    printf("  Экономия: ~%d%%\n", pct > 0 ? pct : 0);
  }
}

static void verify_reverse(cm_list_t *list) {
  cm_item_t fwd[256];
  int n = 0;
  cm_iter_t it  = cm_list_begin(list);
  cm_iter_t end = cm_list_end(list);
  while (!cm_iter_eq(it, end) && n < 256) {
    fwd[n++] = *cm_iter_get(it);
    it = cm_iter_next(it);
  }

  it = cm_iter_prev(end);
  cm_iter_t begin = cm_list_begin(list);
  int m = 0;
  cm_item_t bwd[256];
  while (1) {
    bwd[m++] = *cm_iter_get(it);
    if (cm_iter_eq(it, begin)) break;
    it = cm_iter_prev(it);
  }

  assert(n == m);
  for (int i = 0; i < n; i++) {
    assert(fwd[i].tag == bwd[n - 1 - i].tag);
    assert(memcmp(&fwd[i].info, &bwd[n-1-i].info, sizeof(fwd[i].info)) == 0);
  }
  printf("  [OK] Двусторонняя итерация корректна\n");
}


static void test1_bulk_and_iteration(void) {
  printf("\n\t\tТест 1: Создание [A,B,C,D,E] одним макрозвеном + итерация\t\t\n");

  cm_list_t list = cm_list_create();

  cm_item_t items[5] = {
    cm_item_char('A'),
    cm_item_char('B'),
    cm_item_char('C'),
    cm_item_char('D'),
    cm_item_char('E'),
  };
  cm_push_back_bulk(&list, items, 5);

  print_list(&list, "Список:");
  print_structure(&list);
  verify_reverse(&list);
  print_stats(&list);

  cm_list_destroy(&list);
}


static void test2_insert(void) {
  printf("\n\t\tТест 2: Вставить 'X' перед 'C' -> [A,B,X,C,D,E]\t\t\n");

  cm_list_t list = cm_list_create();
  cm_item_t items[5] = {
    cm_item_char('A'), cm_item_char('B'), cm_item_char('C'),
    cm_item_char('D'), cm_item_char('E'),
  };
  cm_push_back_bulk(&list, items, 5);
  print_list(&list, "До:");

  cm_iter_t pos_c = find_char(&list, 'C');
  cm_insert_before(pos_c, cm_item_char('X'));

  print_list(&list, "После:");
  print_structure(&list);
  verify_reverse(&list);

  cm_list_destroy(&list);
}


static void test3_remove(void) {
  printf("\n\t\tТест 3: Удалить 'B' из [A,B,X,C,D,E] -> [A,X,C,D,E] \t\t\n");

  cm_list_t list = cm_list_create();
  cm_item_t items[5] = {
    cm_item_char('A'), cm_item_char('B'), cm_item_char('C'),
    cm_item_char('D'), cm_item_char('E'),
  };
  cm_push_back_bulk(&list, items, 5);
  cm_iter_t pos_c = find_char(&list, 'C');
  cm_insert_before(pos_c, cm_item_char('X'));
  print_list(&list, "До:");

  cm_iter_t pos_b = find_char(&list, 'B');
  cm_remove(pos_b);

  print_list(&list, "После:");
  print_structure(&list);
  verify_reverse(&list);

  cm_list_destroy(&list);
}


static void test4_splice(void) {
  printf("\n\t\tТест 4: Перенести [C,D] в начало → [C,D,A,B,X,E]\t\t\n");

  cm_list_t list = cm_list_create();
  cm_item_t items[6] = {
    cm_item_char('A'), cm_item_char('B'), cm_item_char('X'),
    cm_item_char('C'), cm_item_char('D'), cm_item_char('E'),
  };
  cm_push_back_bulk(&list, items, 6);
  print_list(&list, "До:");
  print_structure(&list);

  cm_iter_t begin = find_char(&list, 'C');
  cm_iter_t end   = find_char(&list, 'D');
  cm_iter_t dest  = cm_list_begin(&list);

  cm_splice(dest, begin, end);

  print_list(&list, "После:");
  print_structure(&list);
  verify_reverse(&list);

  cm_list_destroy(&list);
}


static void test5_push_back_reuse(void) {
  printf("\n\t\tТест 5: push_back повторно использует свободное место\t\t\n");

  cm_list_t list = cm_list_create();

  cm_macronode_t *mn = cm_macronode_alloc(8);
  mn->count = 3;
  mn->items[0] = cm_item_char('P');
  mn->items[1] = cm_item_char('Q');
  mn->items[2] = cm_item_char('R');
  mn->prev_macro = list.head;
  mn->next_macro = list.tail;
  list.head->next_macro = mn;
  list.tail->prev_macro = mn;

  print_list(&list, "До добавления:");
  print_structure(&list);

  cm_push_back(&list, cm_item_char('S'));
  cm_push_back(&list, cm_item_char('T'));

  print_list(&list, "После добавления S, T:");
  print_structure(&list);
  verify_reverse(&list);
  print_stats(&list);

  cm_list_destroy(&list);
}


static void test6_numbers_and_brackets(void) {
  printf("\n=== Тест 6: Числа и скобки — ( 1 2 3 ) ===\n");

  cm_list_t list = cm_list_create();

  cm_push_back(&list, cm_item_open_bracket());
  cm_push_back(&list, cm_item_number(1));
  cm_push_back(&list, cm_item_number(2));
  cm_push_back(&list, cm_item_number(3));
  cm_push_back(&list, cm_item_close_bracket());

  print_list(&list, "Список:");
  print_structure(&list);
  verify_reverse(&list);
  print_stats(&list);

  cm_list_destroy(&list);
}


static void test7_function_node(void) {
  printf("\n=== Тест 7: Функциональный узел ===\n");

  cm_list_t list = cm_list_create();
  cm_push_back(&list, cm_item_function(&g_dummy_func));
  cm_push_back(&list, cm_item_char('x'));

  print_list(&list, "Список:");
  print_structure(&list);
  verify_reverse(&list);

  cm_list_destroy(&list);
}


static void test8_memory_comparison(void) {
  int N = 100;
  printf("\n=== Тест 8: Сравнение памяти для %d элементов ===\n", N);

  cm_list_t list = cm_list_create();
  cm_item_t *bulk = (cm_item_t *)malloc((size_t)N * sizeof(cm_item_t));
  for (int i = 0; i < N; i++) {
    bulk[i] = cm_item_number((r05_number)i);
  }
  cm_push_back_bulk(&list, bulk, N);
  free(bulk);

  print_stats(&list);

  printf("\n  Разбиваем на 10 макрозвеньев (10 сплитов)...\n");
  for (int step = 9; step < N; step += 10) {
    if (step >= N) break;
    cm_iter_t it = cm_list_begin(&list);
    cm_iter_t end = cm_list_end(&list);
    int cnt = 0;
    while (!cm_iter_eq(it, end)) {
      if (cnt == step) {
        cm_split(&it);
        break;
      }
      it = cm_iter_next(it);
      cnt++;
    }
  }

  print_structure(&list);
  print_stats(&list);

  cm_list_destroy(&list);
}

int main(void) {
  printf("=== Демонстрация уплотнённого спискового представления (Рефал-05) ===\n");
  printf("sizeof(cm_item_t)      = %zu байт\n", sizeof(cm_item_t));
  printf("sizeof(cm_macronode_t) = %zu байт (заголовок)\n", sizeof(cm_macronode_t));
  printf("sizeof(r05_node)       = %zu байт (классический узел)\n\n",
         sizeof(struct r05_node));

  test1_bulk_and_iteration();
  test2_insert();
  test3_remove();
  test4_splice();
  test5_push_back_reuse();
  test6_numbers_and_brackets();
  test7_function_node();
  test8_memory_comparison();

  printf("\n=== Все тесты пройдены успешно ===\n");
  return 0;
}
