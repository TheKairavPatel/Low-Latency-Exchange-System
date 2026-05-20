#include "priceLevel.hpp"
#include <iostream>


int main()
{
    PriceLevel level;

    ClientOrder o1 = {10000, 100, 1, 0};
    ClientOrder o2 = {10000, 200, 2, 0};
    ClientOrder o3 = {10000, 300, 3, 0};
    ClientOrder o4 = {10000, 650, 4, 0};
    uint8_t s1 = level.insertOrder(o1);
    uint8_t s2 = level.insertOrder(o2);
    uint8_t s3 = level.insertOrder(o3);
    printf("%d ", level.fillOrder(o4)); // expect 50 left
    printf("head: %d tail: %d\n", level.getHead(), level.getTail()); // expect 0 2
    return 0;
}