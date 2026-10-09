extern int total;
int total = 3;
int next(void) {
    static int n = 0;
    n++;
    return n;
}
int main(void) {
    int a;
    int b;
    a = next();
    b = next();
    printf("storage = %d %d %d\n", total, a, b);
    return 0;
}
