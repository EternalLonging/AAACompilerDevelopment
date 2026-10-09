/* 静态变量只初始化一次，多次调用时保留上次的值。 */
int next(void) {
    static int count = 0;
    count++;
    return count;
}

int twice(int value) {
    return value * 2;
}

/* fn 保存要调用的函数，value 保存传给该函数的参数。 */
int apply(int (*fn)(int), int value) {
    return fn(value);
}

int main(void) {
    int first = next();
    int second = next();
    printf("counter = %d %d\n", first, second);
    printf("callback = %d\n", apply(twice, 6));
    return 0;
}
