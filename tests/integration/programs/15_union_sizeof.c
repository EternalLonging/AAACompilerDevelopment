union Data {
    int n; /* 按整数访问共享存储。 */
    float f; /* 按浮点数访问同一段存储。 */
};
int main(void) {
    union Data a = {7};
    union Data b;
    b = a;
    a.n = 9;
    printf("union = %d %d\n", b.n, (int)sizeof(int));
    return 0;
}
