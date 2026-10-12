struct Pair {
    int x; /* 第一个整数。 */
    int y; /* 第二个整数。 */
};
int main(void) {
    struct Pair p = {1, 2};
    struct Pair q;
    q = p;
    q.x = 9;
    printf("pair = %d %d %d\n", p.x, q.x, q.y);
    return 0;
}
