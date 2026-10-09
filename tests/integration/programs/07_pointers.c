int main(void) {
    int a[3] = {2, 3, 4};
    int *p;
    p = a;
    *(p + 1) = 7;
    printf("pointer = %d %d\n", a[1], *(p + 2));
    return 0;
}
