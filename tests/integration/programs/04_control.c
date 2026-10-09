int main(void) {
    int i;
    int sum;
    i = 0;
    sum = 0;
    while (i < 4) {
        i++;
        if (i == 2) {
            continue;
        }
        sum += i;
    }
    do {
        sum++;
    } while (sum < 9);
    switch (i) {
        case 4: sum += 1;
        case 5: sum += 2; break;
        default: sum = 0;
    }
    printf("control = %d\n", sum);
    return 0;
}
