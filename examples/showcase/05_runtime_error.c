/* 运行错误：i 等于数组长度，超过合法下标 0 和 1。 */
int main(void) {
    int values[2] = {3, 7};
    int i = 2;
    printf("%d\n", values[i]);
    return 0;
}
