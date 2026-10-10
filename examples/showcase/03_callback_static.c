/* 静态变量只初始化一次，多次调用时保留上次的值。 */
int next(void) {
    static int count = 0;
    count++;
    return count;
}

int twice(int value) {
    return value * 2;
}

int main(void) {
    int first = next();
    int second = next();
    printf("counter = %d %d\n", first, second);
    printf("twice = %d\n", twice(6));
    return 0;
}
