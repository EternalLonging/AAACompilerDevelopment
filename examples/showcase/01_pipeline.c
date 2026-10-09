/* 展示变量、函数、循环、输入输出和完整编译链路。 */
int sum_to(int n) {
    int i;
    int sum = 0;
    for (i = 1; i <= n; i++) {
        sum += i;
    }
    return sum;
}

int main(void) {
    int n;
    scanf("%d", &n);
    printf("sum = %d\n", sum_to(n));
    return 0;
}
