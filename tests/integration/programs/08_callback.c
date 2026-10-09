int twice(int x) {
    return x * 2;
}
int apply(int (*fn)(int), int x) {
    return fn(x);
}
int main(void) {
    int (*fn)(int);
    fn = twice;
    printf("callback = %d\n", apply(fn, 6));
    return 0;
}
