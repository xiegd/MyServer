#include <iostream>
#include <unordered_map>
#include <stdexcept>
#include <vector>

using namespace std;

class Solution {
public:
    int lengthOfLIS(vector<int>& nums) {
        // 当前的最大长度和最大值
        vector<vector<int>> dp(nums.size(), vector<int>(2, -1));
        dp[0][0] = 1;
        dp[0][1] = nums[0];
        for (int i = 1; i < nums.size(); i++) {
            // 满足要求
            if (nums[i] < dp[i - 1][1]) {
                dp[i][0] = dp[i - 1][0] + 1;
                dp[i][1] = nums[i];
            }
            // 不满足要求, 选
            // 需要证明，不满足要求，选择了会不会更好
            else {
                dp[i][0] = dp[i - 1][0];
                dp[i][1] = dp[i - 1][1];
            }
        }
        for (auto val : dp) cout << val[0] << ", " << val[1] << "\n";
        return dp.back()[0];
        
        vector<int> mem(nums.size(), -1);

        auto dfs = [&] (auto&& dfs, int i, int cur_min) -> int {
            if (i < 0) {
                return 0;
            }
            if (mem[i] != -1) {
                return mem[i];
            }
                            cout << nums[i] << ", " << cur_min << "\n";  
            // 选择， 满足/不满足要求, 或者不选
            if (nums[i] < cur_min) {
              
                return mem[i] = max(dfs(dfs, i - 1, nums[i]) + 1, mem[i]);
            }
            else {
                return mem[i] = max(dfs(dfs, i - 1, nums[i]), mem[i]);
            }
            // 不选
            return mem[i] = dfs(dfs, i - 1, cur_min);
            // return mem[i] = max(max(dfs(dfs, i - 1, nums[i]) + 1, 
            //                 dfs(dfs, i - 1, nums[i])), 
            //             dfs(dfs, i - 1, cur_min));

        };
        dfs(dfs, nums.size() - 1, INT_MAX);
        for (auto val : mem) cout << val << ", ";
        cout << "\n";
        return mem.back();
    }
};

int main() {
    int a = 1;
    int b = 2;

    int res = add(&add_num, a, b);
    int res2 = add(add_num, a, b);
    cout << res << endl;
    cout << res2 << endl;
    return 0;
}