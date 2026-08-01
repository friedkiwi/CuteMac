#include <iostream>
#include <vector>

#include "cutemac/core/MachineScheduler.h"

int main()
{
    cutemac::core::MachineScheduler scheduler;
    std::vector<int> order;
    scheduler.schedule(20, [&]() { order.push_back(2); });
    scheduler.schedule(10, [&]() { order.push_back(1); });
    scheduler.schedule(20, [&]() { order.push_back(3); });
    scheduler.advance(10);
    scheduler.advance(10);
    if (order != std::vector<int> { 1, 2, 3 } || scheduler.now() != 20) {
        std::cerr << "scheduler did not preserve cycle/sequence order\n";
        return 1;
    }
    scheduler.reset();
    scheduler.advance(5);
    return scheduler.now() == 5 ? 0 : 1;
}
