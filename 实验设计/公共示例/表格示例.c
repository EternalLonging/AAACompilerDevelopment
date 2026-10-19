/* 五部分表格共用的源码示例，便于核对符号与四元式。 */
int seed = 2;
struct Pair { int left; char mark; };

int sum(int values[], int n) {
    int i = 0;
    int total = 0;
    for (i = 0; i < n; i = i + 1) {
        if (values[i] < 0) { continue; }
        total = total + values[i];
        if (total > 100) { break; }
    }
    return total;
}

int main() {
    int values[] = {1, 2, 3};
    struct Pair pair;
    int total;
    float scale = 1;
    char label[] = "ok";
    pair.left = seed + 1;
    pair.mark = 'A';
    while (seed < 3) { seed = seed + 1; }
    total = sum(values, 3);
    { int seed = 7; total = total + seed; }
    if (total > 0 && pair.left > 0) {
        total = total + pair.left;
    } else { total = 0; }
    printf("%d %c %s\n", total, pair.mark, label);
    return 0;
}
