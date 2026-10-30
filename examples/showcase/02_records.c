/* 统计结构体数组中的及格人数，并输出各人成绩和平均分。 */


/* 每个学生保存一个整数成绩。 */
struct Student {
    int score;
};

/* 遍历学生数组，返回成绩不低于 60 分的人数。 */
int count_pass(struct Student p[], int count) {
    int i;
    int passed = 0;
    for (i = 0; i < count; i=i+1) {
        if (p[i].score >= 60) {
            passed=passed+1;
        }
    }
    return passed;
}

int main(void) {
    struct Student students[3];
    int i;
    int total = 0;
    students[0].score = 90;
    students[1].score = 55;
    students[2].score = 80;
    for (i = 0; i < 3; i=i+1) {
        total = total + students[i].score;
        if (i == 0) { printf("Alice: %d\n", students[i].score); }
        if (i == 1) { printf("Bob: %d\n", students[i].score); }
        if (i == 2) { printf("Chen: %d\n", students[i].score); }
    }
    printf("passed = %d\n", count_pass(students, 3));
    printf("average = %f\n", total / 3.0);
    return 0;
}
