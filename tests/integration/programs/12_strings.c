int main(void) {
    char buffer[5];
    char *p;
    scanf("%4s", buffer);
    p = buffer;
    p[0] = 'A';
    printf("string = %s %s\n", p, "world" + 1);
    return 0;
}
