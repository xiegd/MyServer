#include <iostream>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <iomanip>

#include <memory>
#include <iostream>
#include <vector>
using namespace std;

class Solution {
public:
    int leastInterval(vector<char>& tasks, int n) {
        vector<char> res(0);
        vector<int> mark(tasks.size(), -1);
        int count = 0;
        bool can_visit = false, need_idle = true;
        for (int i = 0; count < tasks.size(); ++i) {
            if (i >= tasks.size()) {
                i %= tasks.size();  // 完成了一次遍历, 如果这次遍历中有执行任务则need_idel为false
                if (need_idle) {
                    res.push_back('a');  // task都是大写字母
                }
                need_idle = true;
            }
            if (mark[i] == 0) {
                continue;  // 已执行则跳过
            }
            // 检查前n-1个没出现过当前元素
            cout << "res.size() - n: " << res.size() - n << endl;

            int gap = 0;
            for (int j = 0; j < res.size(); ++j) {
                if (res[j] != tasks[i]) {
                    gap++;
                }
                if (gap == n) {
                    if (gap < res.size() && res[gap] == tasks[i]) {
                        continue;
                    }
                    can_visit = true;
                    break;
                }
            }
            // 可以访问, 找到了位置或者res为空
            if (can_visit || res.size() == 0) {
                std::cout << "push_back: " << tasks[i] << std::endl;
                res.insert(res.begin() + gap, tasks[i]);
                // res.push_back(tasks[i]);
                mark[i] = 0;
                need_idle = false;
                count++;
            }
            std::cout << "------------" << std::endl;
            can_visit = false;
        }
        for (auto i : res) {
            std::cout << i << ", ";
        }
        std::cout << std::endl;

        return res.size();
    }
};
int main() {
    Solution s;
    vector<char> tasks = {'A', 'A', 'A', 'B', 'B', 'B'};
    int n = 2;
    std::cout << s.leastInterval(tasks, n) << std::endl;
}
