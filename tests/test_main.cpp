extern int run_superglide_tests();
extern int run_utils_tests();
extern int run_config_validation_tests();
extern int run_input_state_tests();

int main() {
    run_superglide_tests();
    run_utils_tests();
    run_config_validation_tests();
    run_input_state_tests();
    return 0;
}
