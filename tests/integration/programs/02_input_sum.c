int main(void) {
    int n;
    int i;
    int sum;
    scanf("%d", &n);
    sum = 0;
    for (i = 1; i <= n; i++) {
        sum += i;
    }
    printf("sum = %d\n", sum);
    return 0;
}
