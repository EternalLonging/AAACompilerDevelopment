/* 鎴愮哗杈惧埌杩欎釜鍒嗘暟琛ㄧず鍙婃牸锛岀敱婕旂ず绋嬪簭鍦ㄩ澶勭悊鏃跺睍寮€銆?*/


/* 姣忎釜瀛︾敓淇濆瓨鍚嶅瓧鍜屾垚缁╋紝鍚嶅瓧鏈€澶氬瓨 7 涓瓧绗﹀強缁撴潫闆躲€?*/
struct Student {
    int score;
};

/* 鍙傛暟淇濆瓨瀛︾敓鏁扮粍锛屾寜涓嬫爣璇诲彇鎴愮哗銆?*/
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
    students[0].score = 90;
    students[1].score = 55;
    students[2].score = 80;
    int i;
    int total = 0;
    for (i = 0; i < 3; i=i+1) {
        total = total + students[i].score;
        if (i == 0) printf("Alice: %d\n", students[i].score);
        if (i == 1) printf("Bob: %d\n", students[i].score);
        if (i == 2) printf("Chen: %d\n", students[i].score);
    }
    printf("passed = %d\n", count_pass(students, 3));
    printf("average = %f\n", total / 3.0);
    return 0;
}
