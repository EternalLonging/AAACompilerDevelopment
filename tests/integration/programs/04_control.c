int main(void) {
    int i = 0;
    int sum = 0;
    while (i < 4) {
        i++;
        if (i == 2) { continue; }
        sum += i;
    }
    while (sum < 9) { sum++; }
    if (i == 4) { sum += 3; } else { sum = 0; }
    printf("control = %d\n", sum);
    return 0;
}
