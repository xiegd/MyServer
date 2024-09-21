#include <iostream>
#include <string>

#include "thrdpool.h"

using namespace std;

int main() {
  thrdpool_t* pool = thrdpool_create(4, 4);
  cout << sizeof(*pool) << endl;
}