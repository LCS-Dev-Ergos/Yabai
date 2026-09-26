static TEST_SIG(navigation_numbers)
{
    char *test_name = "navigation_numbers";
    bool result = true;

    const char *valid[] = { "0", "1", "0.95", "0.1", "1.0" };
    const char *invalid[] = { "nan", "inf", "-0.1", "1.01", "1e999", "2147483648", "", "0.1junk" };
    float value;

    for (size_t i = 0; i < sizeof(valid)/sizeof(*valid); ++i) {
        struct token token = { .text = (char *) valid[i], .length = strlen(valid[i]) };
        TEST_CHECK(space_navigation_number(token, &value), true);
    }

    for (size_t i = 0; i < sizeof(invalid)/sizeof(*invalid); ++i) {
        struct token token = { .text = (char *) invalid[i], .length = strlen(invalid[i]) };
        TEST_CHECK(space_navigation_number(token, &value), false);
    }

    return result;
}
