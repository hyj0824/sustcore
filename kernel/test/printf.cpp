/**
 * @file printf.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief printf 测试
 * @version alpha-1.0.0
 * @date 2026-05-18
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#include <test/printf.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <utility>

namespace test::printf {
    namespace {
        struct SinkState {
            char buffer[128] = {};
            size_t offset    = 0;
            unsigned int calls = 0;
        };

        int collect_chunk(const char *data, size_t len, void *ctx) {
            auto *state = static_cast<SinkState *>(ctx);
            state->calls++;
            if (state->offset + len >= sizeof(state->buffer)) {
                return -1;
            }

            memcpy(state->buffer + state->offset, data, len);
            state->offset += len;
            state->buffer[state->offset] = '\0';
            return static_cast<int>(len);
        }

        int fail_chunk(const char *, size_t, void *) {
            return -1;
        }
    }  // namespace

    class CaseSnprintfBasic : public TestCase {
    public:
        CaseSnprintfBasic() : TestCase("snprintf 基础格式化") {}

        void _run(void *env [[maybe_unused]]) const noexcept override {
            char buffer[64] = {};

            int ret = snprintf(buffer, sizeof(buffer), "hello %s %d", "os", 42);
            ttest(ret == 11);
            ttest(strcmp(buffer, "hello os 42") == 0);

            ret = snprintf(buffer, sizeof(buffer), "hex=%#x pad=%05u", 0x2a, 7);
            ttest(ret == 18);
            ttest(strcmp(buffer, "hex=0x2a pad=00007") == 0);

            ret = snprintf(buffer, sizeof(buffer), "ptr=%p", (void *)0x1234);
            ttest(ret == 22);
            ttest(strcmp(buffer, "ptr=0x0000000000001234") == 0);
        }
    };

    class CaseSnprintfTruncate : public TestCase {
    public:
        CaseSnprintfTruncate() : TestCase("snprintf 截断行为") {}

        void _run(void *env [[maybe_unused]]) const noexcept override {
            char small[8] = {};

            int ret = snprintf(small, sizeof(small), "abcdefghi");
            ttest(ret == 9);
            ttest(strcmp(small, "abcdefg") == 0);
            ttest(small[7] == '\0');

            char untouched[] = "keep";
            ret = snprintf(untouched, 0, "long %u", 123u);
            ttest(ret == 8);
            ttest(strcmp(untouched, "keep") == 0);
        }
    };

    class CaseVcbprintfChunkedVa : public TestCase {
    private:
        static int call_vcbprintf(char *chunk, size_t chunk_size,
                                  SinkState *state, const char *fmt, ...) {
            va_list args;
            va_start(args, fmt);
            int ret = vcbprintf(chunk, chunk_size, collect_chunk, state, fmt, args);
            va_end(args);
            return ret;
        }

    public:
        CaseVcbprintfChunkedVa() : TestCase("vcbprintf 分块输出完整性") {}

        void _run(void *env [[maybe_unused]]) const noexcept override {
            char chunk[4] = {};
            SinkState state;

            int ret = call_vcbprintf(chunk, sizeof(chunk), &state,
                                     "prefix-%05u-%s", 7u, "tail");
            ttest(ret == 17);
            ttest(strcmp(state.buffer, "prefix-00007-tail") == 0);
            ttest(state.calls > 1);
        }
    };

    class CaseVcbprintfFailure : public TestCase {
    private:
        static int call_vcbprintf(char *chunk, size_t chunk_size,
                                  const char *fmt, ...) {
            va_list args;
            va_start(args, fmt);
            int ret = vcbprintf(chunk, chunk_size, fail_chunk, nullptr, fmt, args);
            va_end(args);
            return ret;
        }

    public:
        CaseVcbprintfFailure() : TestCase("vcbprintf sink 失败") {}

        void _run(void *env [[maybe_unused]]) const noexcept override {
            char chunk[4] = {};
            int ret = call_vcbprintf(chunk, sizeof(chunk), "abc");
            ttest(ret == -1);
        }
    };

    void collect_tests(TestFramework& framework) {
        auto cases = util::ArrayList<TestCase*>();
        cases.push_back(new CaseSnprintfBasic());
        cases.push_back(new CaseSnprintfTruncate());
        cases.push_back(new CaseVcbprintfChunkedVa());
        cases.push_back(new CaseVcbprintfFailure());

        framework.add_category(new TestCategory("printf", std::move(cases)));
    }
}  // namespace test::printf
