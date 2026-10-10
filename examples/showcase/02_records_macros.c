/* 成绩达到这个分数表示及格，由演示程序在预处理时展开。 */
#define PASS_SCORE 60
#define STUDENT_COUNT 3


/* 每个学生保存名字和成绩，名字最多存 7 个字符及结束零。 */
struct Student {
    char name[8]; /* 学生名字。 */
    int score; /* 学生成绩。 */
};

/* 参数保存学生数组，按下标读取成绩。 */
int count_pass(struct Student p[], int count) {
    int i;
    int passed = 0;
    for (i = 0; i < count; i++) {
        if (p[i].score >= PASS_SCORE) {
            passed++;
        }
    }
    return passed;
}

int main(void) {
    struct Student students[STUDENT_COUNT] = {{"Alice", 90}, {"Bob", 55}, {"Chen", 80}};
    int i;
    int total = 0;
    for (i = 0; i < STUDENT_COUNT; i++) {
        total += students[i].score;
        printf("%s: %d\n", students[i].name, students[i].score);
    }
    printf("passed = %d\n", count_pass(students, STUDENT_COUNT));
    printf("average = %f\n", (float)total / STUDENT_COUNT);
    return 0;
}
