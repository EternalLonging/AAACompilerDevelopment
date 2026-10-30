int main(void) {
    int a[6];
    int i;
    int sum;
    a[0] = 1; a[1] = 2; a[2] = 3; a[3] = 4; a[4] = 5; a[5] = 6;
    sum = 0;
    for (i = 0; i < 6; i = i + 1) {
        sum = sum + a[i];
    }
    printf("array = %d\n", sum);
    return 0;
}
