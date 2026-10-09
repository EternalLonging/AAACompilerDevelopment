int main(void) {
    int zero;
    int x;
    zero = 0;
    x = 0;
    if (zero && 1 / zero) {
        x = 99;
    }
    x += 1 || ++zero;
    x += zero ? 1 / zero : 4;
    printf("short = %d %d\n", x, zero);
    return 0;
}
