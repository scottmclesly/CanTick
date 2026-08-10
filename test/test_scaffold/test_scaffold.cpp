// Host build scaffold test.
//
// This test proves that the host test build runs. It tests no firmware code.
// The pure units carry their own suites: matrix, bus_load, cards, strips and
// panel.

#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_host_build_runs(void) {
  TEST_ASSERT_EQUAL_INT(1, 1);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_host_build_runs);
  return UNITY_END();
}
