#include <switch.h>
int main() {
    pdmqryInitialize();
    PdmPlayStatistics stats;
    pdmqryQueryPlayStatisticsByApplicationId(0, false, &stats);
    pdmqryExit();
    return 0;
}
