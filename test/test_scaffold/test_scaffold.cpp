// Phase 0 scaffold test (Matrix-TODO.md).
//
// This test proves that the host test build runs. It tests no firmware code.
// The real tests arrive with the pure units: xyToIndex and the 3x5 font in
// Phase 2, the load estimator in Phase 3, the card queue in Phase 4.

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
