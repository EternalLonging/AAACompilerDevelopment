struct Pair {
    int x; /* 第一个整数。 */
    int y; /* 第二个整数。 */
};
struct Pair change(struct Pair p) {
    p.x = 9;
    return p;
}
int main(void) {
    struct Pair p = {1, 2};
    struct Pair q;
    q = change(p);
    printf("pair = %d %d %d\n", p.x, q.x, q.y);
    return 0;
}
