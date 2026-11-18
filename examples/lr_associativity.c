/* LR 左递归规则直接生成左结合 AST；赋值仍为右结合。 */
int main(void) {
    int a, b;
    a = b = 7;
    printf("%d %d %d\n", 12 / 3 / 2, 8 - 3 - 1, a + b);
    return 0;
}
