#include <iostream>
#include <stdexcept>
#include <vector>

void mayThrow(int i) {
    if (i % 2 == 0) {
        throw std::runtime_error("Even number encountered");
    }
    std::cout << "Processing: " << i << std::endl;
}

int main() {
    std::vector<int> numbers = {1, 2, 3, 4, 5};

    std::cout << "Starting loop" << std::endl;
    for (int num : numbers) {
        try {
            mayThrow(num);
        } catch (const std::logic_error& e) {
            std::cout << "Caught logic_error: " << e.what() << std::endl;
        }
        std::cout << "End of iteration for " << num << std::endl;
    }
    std::cout << "Loop finished" << std::endl;

    return 0;
}