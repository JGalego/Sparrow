#include <stdio.h>
#include <string.h>

#include "sp_test.h"

static SpTestCase *first_case;
static SpTestCase *last_case;
static int current_failed;
static char failure_text[256];

void sp_test_register(SpTestCase *test_case)
{
    if (last_case != NULL) {
        last_case->next = test_case;
    } else {
        first_case = test_case;
    }
    last_case = test_case;
}

void sp_test_fail(const char *file, int line, const char *expression)
{
    if (current_failed) {
        return;
    }
    current_failed = 1;
    snprintf(failure_text, sizeof failure_text, "%s:%d: %s", file, line, expression);
}

static void write_xml_escaped(FILE *out, const char *text)
{
    for (; *text != '\0'; text++) {
        switch (*text) {
        case '<':
            fputs("&lt;", out);
            break;
        case '>':
            fputs("&gt;", out);
            break;
        case '&':
            fputs("&amp;", out);
            break;
        case '"':
            fputs("&quot;", out);
            break;
        default:
            fputc(*text, out);
        }
    }
}

static void write_junit_case(FILE *out, const SpTestCase *test_case, int failed, const char *text)
{
    fprintf(out, "  <testcase classname=\"%s\" name=\"%s\">\n", test_case->suite, test_case->name);
    if (test_case->requirements[0] != '\0') {
        fprintf(out, "    <properties><property name=\"requirement\" value=\"%s\"/></properties>\n",
                test_case->requirements);
    }
    if (failed) {
        fputs("    <failure message=\"", out);
        write_xml_escaped(out, text);
        fputs("\"/>\n", out);
    }
    fputs("  </testcase>\n", out);
}

int main(int argc, char **argv)
{
    const char *junit_path = NULL;
    int total = 0;
    int failures = 0;
    FILE *junit = NULL;

    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], "--junit") == 0) {
            junit_path = argv[i + 1];
        }
    }
    if (junit_path != NULL) {
        junit = fopen(junit_path, "w");
        if (junit == NULL) {
            perror(junit_path);
            return 2;
        }
        fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<testsuite name=\"c\">\n", junit);
    }

    for (SpTestCase *test_case = first_case; test_case != NULL; test_case = test_case->next) {
        current_failed = 0;
        failure_text[0] = '\0';
        test_case->run();
        total++;
        if (current_failed) {
            failures++;
            printf("FAIL %s.%s  %s\n", test_case->suite, test_case->name, failure_text);
        }
        if (junit != NULL) {
            write_junit_case(junit, test_case, current_failed, failure_text);
        }
    }

    if (junit != NULL) {
        fputs("</testsuite>\n", junit);
        fclose(junit);
    }
    printf("%d tests, %d failed\n", total, failures);
    return failures == 0 ? 0 : 1;
}
