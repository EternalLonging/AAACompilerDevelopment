/* 鎴愮哗杈惧埌杩欎釜鍒嗘暟琛ㄧず鍙婃牸锛岀敱婕旂ず绋嬪簭鍦ㄩ澶勭悊鏃跺睍寮€銆?*/


/* 姣忎釜瀛︾敓淇濆瓨鍚嶅瓧鍜屾垚缁╋紝鍚嶅瓧鏈€澶氬瓨 7 涓瓧绗﹀強缁撴潫闆躲€?*/
struct Student {
    char name[8]; /* 瀛︾敓鍚嶅瓧銆?*/
    int score; /* 瀛︾敓鎴愮哗銆?*/
};

/* 鍙傛暟淇濆瓨瀛︾敓鏁扮粍锛屾寜涓嬫爣璇诲彇鎴愮哗銆?*/
int count_pass(struct Student p[], int count) {
    int i;
    int passed = 0;
    for (i = 0; i < count; i++) {
        if (p[i].score >= 60) {
            passed++;
        }
    }
    return passed;
}

int main(void) {
    struct Student students[3] = {{"Alice", 90}, {"Bob", 55}, {"Chen", 80}};
    int i;
    int total = 0;
    for (i = 0; i < 3; i++) {
        total += students[i].score;
        printf("%s: %d\n", students[i].name, students[i].score);
    }
    printf("passed = %d\n", count_pass(students, 3));
    printf("average = %f\n", (float)total / 3);
    return 0;
}
