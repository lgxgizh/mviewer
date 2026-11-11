// Replay driver used when the targets are NOT built with -fsanitize=fuzzer:
// runs LLVMFuzzerTestOneInput once per file (or per file inside each directory)
// given on the command line. CTest uses it to replay the committed seed corpus
// on every platform, so a crash found by nightly fuzzing becomes a regression
// test once its reproducer is added under tests/fuzz/corpus/<target>/.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

namespace
{
int runFile(const std::filesystem::path &p)
{
    std::ifstream in(p, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
    LLVMFuzzerTestOneInput(bytes.empty() ? nullptr : bytes.data(), bytes.size());
    return 1;
}
} // namespace

int main(int argc, char **argv)
{
    int runs = 0;
    for (int i = 1; i < argc; ++i)
    {
        const std::filesystem::path arg(argv[i]);
        std::error_code ec;
        if (std::filesystem::is_directory(arg, ec))
        {
            for (const auto &entry : std::filesystem::directory_iterator(arg, ec))
            {
                if (entry.is_regular_file())
                    runs += runFile(entry.path());
            }
        }
        else if (std::filesystem::is_regular_file(arg, ec))
            runs += runFile(arg);
    }
    // A zero-input run on purpose: empty input must not crash either.
    LLVMFuzzerTestOneInput(nullptr, 0);
    std::printf("replayed %d corpus input(s)\n", runs);
    return runs > 0 ? 0 : 1;
}
