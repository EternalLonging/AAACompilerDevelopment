int main(void) {
    int a[6] = {1, 2, 3, 4, 5, 6};
    int i;
    int sum;
    sum = 0;
    for (i = 0; i < 6; i = i + 1) {
        sum = sum + a[i];
    }
    printf("array = %d\n", sum);
    return 0;
}
