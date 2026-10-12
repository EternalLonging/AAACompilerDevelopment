int main(void) {
    int i = 0;
    int sum = 0;
    while (i < 4) {
        i = i + 1;
        if (i == 2) { continue; }
        sum = sum + i;
    }
    while (sum < 9) { sum = sum + 1; }
    if (i == 4) { sum = sum + 3; } else { sum = 0; }
    printf("control = %d\n", sum);
    return 0;
}
