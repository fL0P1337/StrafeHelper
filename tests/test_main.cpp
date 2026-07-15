extern int run_superglide_tests();
extern int run_utils_tests();
extern int run_config_validation_tests();

int main() {
    run_superglide_tests();
    run_utils_tests();
    run_config_validation_tests();
    return 0;
}
