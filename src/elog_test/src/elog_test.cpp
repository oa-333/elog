
#include <fstream>

#include "elog_test_common.h"

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    testing::AddGlobalTestEnvironment(new ELogEnvironment());
    return RUN_ALL_TESTS();
}