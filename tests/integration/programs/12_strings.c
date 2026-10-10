int main(void) {
    char buffer[5];
    scanf("%4s", buffer);
    buffer[0] = 'A';
    printf("string = %s %s\n", buffer, "orld");
    return 0;
}
