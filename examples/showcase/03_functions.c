/* 普通局部变量在每次调用时重新初始化。 */
int next(void) {
    int count = 0;
    count=count+1;
    return count;
}

/* 参数乘以 2，返回计算结果。 */
int twice(int value) { return value * 2; }

int main(void) {
    int first = next();
    int second = next();
    printf("counter = %d %d\n", first, second);
    printf("twice = %d\n", twice(6));
    return 0;
}
