// Query output escapes quotes, backslashes and control characters for JSON.
static TEST_SIG(string_escape)
{
    char *test_name = "string_escape";
    bool result = true;

    if (!g_temp_storage.memory) TEST_CHECK(ts_init(KILOBYTES(64)), true);

    char input[] = "a\"b\\c\n\x01\x1f";
    char *escaped = ts_string_escape(input);
    TEST_CHECK(escaped != NULL, true);
    if (escaped) TEST_CHECK(strcmp(escaped, "a\\\"b\\\\c\\n\\u0001\\u001f"), 0);

    // The last control character's digits end exactly at the terminator.
    char last[] = "\x1f";
    escaped = ts_string_escape(last);
    TEST_CHECK(escaped != NULL, true);
    if (escaped) TEST_CHECK(strcmp(escaped, "\\u001f"), 0);

    char plain[] = "plain";
    TEST_CHECK(ts_string_escape(plain) == NULL, true);

    ts_reset();
    return result;
}
