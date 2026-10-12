int main(void) {
    int zero;
    int x;
    zero = 0;
    x = 0;
    if (zero && 1 / zero) {
        x = 99;
    }
    if (zero || 1) { x = x + 1; }
    if (zero) { x = x + 1 / zero; } else { x = x + 4; }
    printf("short = %d %d\n", x, zero);
    return 0;
}
