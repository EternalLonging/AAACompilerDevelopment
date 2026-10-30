struct Pair {
    int x;
    int y;
};
int main(void) {
    struct Pair p;
    struct Pair q;
    p.x = 1; p.y = 2;
    q = p;
    q.x = 9;
    printf("pair = %d %d %d\n", p.x, q.x, q.y);
    return 0;
}
