/* lib/kernel/list.c를 위한 테스트 프로그램.

   Pintos의 다른 곳에서 충분히 테스트되지 않은
   리스트 기능을 테스트하려고 시도합니다.

   이것은 여러분이 제출한 프로젝트에 대해 저희가 실행할 테스트가 아닙니다.
   완전성을 위해 여기에 있습니다.
*/

#undef NDEBUG
#include <debug.h>
#include <list.h>
#include <random.h>
#include <stdio.h>
#include "threads/test.h"

/* 저희가 테스트할 연결 리스트의 최대 요소 수. */
#define MAX_SIZE 64

/* 연결 리스트 요소. */
struct value
  {
    struct list_elem elem;      /* 리스트 요소. */
    int value;                  /* 아이템 값. */
  };

static void shuffle (struct value[], size_t);
static bool value_less (const struct list_elem *, const struct list_elem *,
                        void *);
static void verify_list_fwd (struct list *, int size);
static void verify_list_bkwd (struct list *, int size);

/* 연결 리스트 구현을 테스트합니다. */
void
test (void)
{
  int size;

  printf ("다양한 크기의 리스트 테스트 중:"); // testing various size lists:
  for (size = 0; size < MAX_SIZE; size++)
    {
      int repeat;

      printf (" %d", size);
      for (repeat = 0; repeat < 10; repeat++)
        {
          static struct value values[MAX_SIZE * 4];
          struct list list;
          struct list_elem *e;
          int i, ofs;

          /* 값 0...SIZE-1을 VALUES 배열에 무작위 순서로 넣습니다. */
          for (i = 0; i < size; i++)
            values[i].value = i;
          shuffle (values, size);

          /* 리스트를 조립합니다. */
          list_init (&list);
          for (i = 0; i < size; i++)
            list_push_back (&list, &values[i].elem);

          /* 정확한 최소 및 최대 요소를 확인합니다. */
          e = list_min (&list, value_less, NULL);
          ASSERT (size ? list_entry (e, struct value, elem)->value == 0
                  : e == list_begin (&list)); // size가 0이 아니면 e의 값이 0, 0이면 e는 리스트의 시작
          e = list_max (&list, value_less, NULL);
          ASSERT (size ? list_entry (e, struct value, elem)->value == size - 1
                  : e == list_begin (&list)); // size가 0이 아니면 e의 값이 size-1, 0이면 e는 리스트의 시작

          /* 리스트를 정렬하고 확인합니다. */
          list_sort (&list, value_less, NULL);
          verify_list_fwd (&list, size);

          /* 리스트를 뒤집고 확인합니다. */
          list_reverse (&list);
          verify_list_bkwd (&list, size);

          /* 배열을 섞고, list_insert_ordered()를 사용하여 삽입한 후,
             순서를 확인합니다. */
          shuffle (values, size);
          list_init (&list);
          for (i = 0; i < size; i++)
            list_insert_ordered (&list, &values[i].elem,
                                 value_less, NULL);
          verify_list_fwd (&list, size);

          /* 일부 아이템을 복제하고, list_unique()로 중복을 제거한 후, 확인합니다. */
          ofs = size;
          for (e = list_begin (&list); e != list_end (&list);
               e = list_next (e))
            {
              struct value *v = list_entry (e, struct value, elem);
              int copies = random_ulong () % 4; // 0~3개의 복사본 생성
              while (copies-- > 0)
                {
                  values[ofs].value = v->value;
                  list_insert (e, &values[ofs++].elem); // 현재 요소 e 앞에 복사본 삽입
                }
            }
          ASSERT ((size_t) ofs < sizeof values / sizeof *values); // values 배열 범위 초과 확인
          list_unique (&list, NULL, value_less, NULL); // 중복 제거
          verify_list_fwd (&list, size); // 중복 제거 후 원래 크기의 정렬된 리스트인지 확인
        }
    }

  printf (" 완료\n"); // done
  printf ("리스트: 통과\n"); // list: PASS
}

/* ARRAY 배열의 CNT 개 요소들을 무작위 순서로 섞습니다. */
static void
shuffle (struct value *array, size_t cnt)
{
  size_t i;

  for (i = 0; i < cnt; i++)
    {
      size_t j = i + random_ulong () % (cnt - i);
      struct value t = array[j];
      array[j] = array[i];
      array[i] = t;
    }
}

/* 값 A가 값 B보다 작으면 true를, 그렇지 않으면 false를 반환합니다. */
static bool
compare_threads(const struct list_elem *a_, const struct list_elem *b_,
            void *aux UNUSED)
{
  const struct value *a = list_entry (a_, struct value, elem);
  const struct value *b = list_entry (b_, struct value, elem);

  return a->value < b->value;
}

/* LIST가 순방향으로 순회될 때 값 0...SIZE-1을 포함하는지 확인합니다. */
static void
verify_list_fwd (struct list *list, int size)
{
  struct list_elem *e;
  int i;

  for (i = 0, e = list_begin (list);
       i < size && e != list_end (list);
       i++, e = list_next (e))
    {
      struct value *v = list_entry (e, struct value, elem);
      ASSERT (i == v->value); // 순서대로 0, 1, 2... 값이 나오는지 확인
    }
  ASSERT (i == size); // 모든 요소를 확인했는지 (개수)
  ASSERT (e == list_end (list)); // 반복이 리스트의 끝에서 올바르게 종료되었는지
}

/* LIST가 역방향으로 순회될 때 값 0...SIZE-1을 포함하는지 확인합니다.
   (주: 이 함수는 리스트가 [SIZE-1, SIZE-2, ..., 0] 순으로 정렬된 후 호출될 때,
    역방향 순회 시 0, 1, ..., SIZE-1 순으로 값이 나오는지 검증합니다.) */
static void
verify_list_bkwd (struct list *list, int size)
{
  struct list_elem *e;
  int i;

  for (i = 0, e = list_rbegin (list); // list_rbegin()은 리스트의 실제 마지막 요소(값이 가장 작은 요소)를 가리킴
       i < size && e != list_rend (list);
       i++, e = list_prev (e))
    {
      struct value *v = list_entry (e, struct value, elem);
      ASSERT (i == v->value); // 역방향으로 읽어도 값이 0, 1, 2... 순으로 나오는지 확인
    }
  ASSERT (i == size); // 모든 요소를 확인했는지 (개수)
  ASSERT (e == list_rend (list)); // 반복이 리스트의 (역방향) 끝에서 올바르게 종료되었는지
}