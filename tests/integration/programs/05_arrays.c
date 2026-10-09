int main(void) {
    int a[2][3] = {{1, 2, 3}, {4, 5, 6}};
    int i;
    int j;
    int sum;
    sum = 0;
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 3; j++) {
            sum += a[i][j];
        }
    }
    printf("array = %d\n", sum);
    return 0;
}
